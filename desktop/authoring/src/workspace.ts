import { randomBytes, randomUUID } from "node:crypto";
import { open } from "node:fs/promises";
import { setTimeout as delay } from "node:timers/promises";
import { BINDING_LIMIT, SLOT_LIMIT, type DraftProject, type DraftSource, type DraftUniform, type DrawReceipt,
  type ReflectedDraft, type Slot, type WorkspaceView } from "./contracts";
import { MAX_PROJECT_BYTES, parseBinding, parseProject, titleFor, validUniformName, writeReplay } from "./draft-project";
import type { HostClient } from "./host-client";
import { HostError, writeAtomically } from "./local-state";
import { SampleRelay } from "./osc-samples";

const TYPE_ARITY = new Map([["float", 1], ["vec2", 2], ["vec3", 3], ["vec4", 4]]);

export function isWritable(uniform: DraftUniform) {
  const arity = TYPE_ARITY.get(uniform.type);
  return uniform.writable && uniform.owner === "layer" && validUniformName(uniform.name) &&
    arity === uniform.values.length &&
    uniform.values.every(value => Number.isFinite(value) && Math.abs(value) <= BINDING_LIMIT);
}

function defaultBindings(uniforms: DraftUniform[]) {
  return uniforms.filter(isWritable).flatMap(uniform => uniform.values.map((value, component) =>
    ({ name: uniform.name, component, binding: { mode: "fixed" as const, value } })));
}

export class Workspace {
  private project: DraftProject | null = null;
  private state: ReflectedDraft | null = null;
  private acknowledged: { epoch: string; revision: number; receipt: DrawReceipt } | null = null;
  private busy = false;
  private generation = 0;
  private observation: Promise<void> | null = null;
  private pendingReply: { id: string; target: string; epoch: string } | null = null;
  private notice: string | null = null;
  private samples = new SampleRelay((slot, value) => this.relaySample(slot, value));

  constructor(readonly host: HostClient) {}

  view(): WorkspaceView {
    const project = this.project && {
      ...this.project,
      source: { name: this.project.source.name, digest: this.project.source.digest, origin: this.project.source.origin },
    };
    return {
      peer: this.host.peer, reflected: this.state, project, previewReady: this.previewMatches(),
      drawnReceipt: this.previewMatches() ? this.acknowledged?.receipt ?? null : null,
      samplePort: this.samples.port, notice: this.notice,
    };
  }

  private previewMatches() {
    const peer = this.host.peer;
    const ack = this.acknowledged;
    return Boolean(peer && this.project && this.state && !this.state.needsRestore && ack &&
      this.project.destination.target === peer.target && this.project.destination.fingerprint === peer.fingerprint &&
      this.state.target === peer.target &&
      this.state.sourceDigest === this.project.source.digest &&
      this.state.epoch === ack.epoch && this.state.revision === ack.revision && !this.pendingReply);
  }

  private receive(raw: unknown, drawn = false) {
    const state = raw as ReflectedDraft;
    if (!state || state.target !== this.host.peer?.target || typeof state.epoch !== "string" ||
        !Number.isSafeInteger(state.revision) || state.revision < 0 ||
        !Array.isArray(state.uniforms) || !Array.isArray(state.slots) ||
        typeof state.needsRestore !== "boolean") {
      throw new HostError("state_mismatch", "The host returned an invalid draft state.");
    }
    if (drawn && (state.receipt?.status !== "rendered" || !Number.isSafeInteger(state.receipt.frame))) {
      throw new HostError("missing_drawn_receipt", "The host did not acknowledge a drawn frame.");
    }
    this.state = state;
    if (!drawn && !this.previewMatches()) this.samples.stop();
    return state;
  }

  private async linked() {
    this.state = null;
    this.acknowledged = null;
    this.pendingReply = null;
    this.samples.stop();
    this.notice = null;
    await this.refresh();
    return this.view();
  }

  checkPair() {
    return this.perform(async () => {
      const result = await this.host.checkPair();
      return { state: result.state, ...(result.peer ? { workspace: await this.linked() } : {}) };
    });
  }

  connectRemembered(fingerprint: unknown) {
    return this.perform(async () => {
      await this.host.connectRemembered(fingerprint);
      return this.linked();
    });
  }

  disconnect() {
    return this.perform(async () => {
      this.host.disconnect();
      this.state = null;
      this.acknowledged = null;
      this.pendingReply = null;
      this.samples.stop();
      this.notice = this.project ? "Draft retained in this workspace. Connect to its paired host to restore preview." : null;
      return this.view();
    });
  }

  async reflect() {
    if (this.busy) return this.view();
    if (!this.observation) {
      const generation = this.generation;
      const current = this.refresh(() => this.generation === generation)
        .finally(() => { if (this.observation === current) this.observation = null; });
      this.observation = current;
    }
    await this.observation;
    return this.view();
  }

  private async refresh(accept = () => true) {
    if (!this.host.peer) return;
    try {
      const raw = await this.host.rpc("reflect", {});
      if (!accept()) return;
      const next = this.receive(raw);
      if (this.pendingReply && (this.pendingReply.target !== next.target || this.pendingReply.epoch !== next.epoch)) {
        this.pendingReply = null;
        this.acknowledged = null;
      }
      if (this.project && !this.previewMatches()) {
        this.notice ??= next.needsRestore ? "The host needs this draft to be restored." :
          "Preview differs from this workspace. Restore the draft to continue.";
      }
    } catch (error) {
      if (!accept()) return;
      if (error instanceof HostError && error.code === "busy") return;
      this.acknowledged = null;
      this.state = null;
      this.samples.stop();
      throw error;
    }
  }

  private async perform<T>(action: () => Promise<T>) {
    if (this.busy) throw new HostError("busy", "Wait for the current draft operation.");
    this.busy = true;
    this.generation += 1;
    this.observation = null;
    try {
      return await action();
    } finally {
      this.busy = false;
    }
  }

  private checkedPreview() {
    if (!this.previewMatches() || !this.project || !this.state) {
      throw new HostError("preview_unavailable", "Restore this draft to its paired host before making changes.");
    }
    return { project: this.project, state: this.state };
  }

  private adopt(state: ReflectedDraft, project: DraftProject) {
    if (!state.receipt) throw new HostError("missing_drawn_receipt", "The host did not acknowledge a drawn frame.");
    this.acknowledged = { epoch: state.epoch, revision: state.revision, receipt: state.receipt };
    this.project = project;
    this.notice = null;
    return this.view();
  }

  private async change(method: string, params: Record<string, unknown>): Promise<ReflectedDraft> {
    const prior = this.state;
    if (!prior) throw new HostError("host_unavailable", "Connect to a paired render host.");
    if (this.pendingReply) {
      const pending = this.pendingReply;
      await this.reconcile(pending.id);
      this.pendingReply = null;
      this.acknowledged = null;
      await this.refresh();
      return this.change(method, params);
    }

    const id = randomUUID();
    let result: unknown;
    try {
      result = await this.host.rpc(method, params, prior, id);
    } catch (error) {
      if (!(error instanceof HostError) || !error.uncertainId) throw error;
      this.pendingReply = { id, target: prior.target, epoch: prior.epoch };
      try {
        result = await this.reconcile(id);
      } finally {
        this.samples.stop();
      }
    }
    this.pendingReply = null;
    const next = this.receive(result, true);
    if (next.epoch !== prior.epoch || next.revision <= prior.revision || next.needsRestore) {
      this.acknowledged = null;
      throw new HostError("stale_revision", "The preview epoch or revision changed. Restore the draft.");
    }
    return next;
  }

  private async reconcile(id: string): Promise<unknown> {
    type Receipt = {
      state: string; response?: { id: string; ok: boolean; result?: unknown; error?: { code: string; message: string } };
    };
    let reply: Receipt;
    const deadline = Date.now() + 2400;
    do {
      reply = await this.host.rpc("receipt.get", { id }) as Receipt;
      if (reply?.state !== "pending" || Date.now() >= deadline) break;
      await delay(180);
    } while (true);
    if (reply?.state !== "completed" || reply.response?.id !== id) {
      throw new HostError("ambiguous_request", "The earlier request has no final receipt. Refresh before retrying.");
    }
    if (!reply.response.ok) {
      this.pendingReply = null;
      throw new HostError(reply.response.error?.code ?? "host_failed", reply.response.error?.message ?? "Host rejected the request.");
    }
    return reply.response.result;
  }

  private async validateSource(source: DraftSource) {
    const peer = this.host.peer;
    const result = await this.host.rpc("validate", { source: source.base64, sha256: source.digest }) as
      { target?: string; sourceDigest?: string; receipt?: DrawReceipt; shader?: { uniforms: DraftUniform[] } };
    if (!result || result.target !== peer?.target || result.sourceDigest !== source.digest ||
        result.receipt?.status !== "rendered" || !Array.isArray(result.shader?.uniforms)) {
      throw new HostError("missing_native_receipt", "The host returned an unmatched shader receipt.");
    }
    return defaultBindings(result.shader.uniforms);
  }

  activate(readSource: () => Promise<DraftSource>) {
    return this.perform(async () => {
      const peer = this.host.peer;
      if (!peer) throw new HostError("host_unavailable", "Connect to a paired render host.");
      const source = await readSource();
      const project = parseProject({ format: "vosc.authoring.draft", version: 1, title: titleFor(source),
        destination: peer, source, slots: [], bindings: [] });
      const bindings = await this.validateSource(project.source);
      parseProject({ ...project, bindings });
      await this.refresh();
      const state = await this.change("activate", { source: source.base64, sha256: source.digest });
      if (state.sourceDigest !== source.digest) {
        throw new HostError("source_mismatch", "The native preview did not acknowledge the selected shader.");
      }
      return this.adopt(state, parseProject({ ...project, slots: state.slots, bindings: defaultBindings(state.uniforms) }));
    });
  }

  configureSlot(label: unknown) {
    return this.perform(async () => {
      const { project } = this.checkedPreview();
      if (typeof label !== "string" || !label.trim() || label.includes("\u0000") || Buffer.byteLength(label.trim()) > 80 ||
          project.slots.length >= SLOT_LIMIT) {
        throw new HostError("invalid_slot", `Enter a slot name up to 80 bytes. A draft can have ${SLOT_LIMIT} slots.`);
      }
      const slot: Slot = { id: "s" + randomBytes(6).toString("hex"), label: label.trim(), default: 0 };
      const result = await this.change("slot.configure", { ...slot });
      if (!result.slots.some(item => item.id === slot.id && item.label === slot.label)) {
        throw new HostError("missing_slot", "The slot did not appear in the native draft.");
      }
      return this.adopt(result, { ...project, slots: result.slots });
    });
  }

  bindComponent(name: unknown, component: unknown, bindingValue: unknown) {
    return this.perform(async () => {
      const { project, state } = this.checkedPreview();
      const uniform = state.uniforms.find(item => item.name === name);
      if (!uniform || !isWritable(uniform) || typeof component !== "number" ||
          !Number.isInteger(component) || component < 0 || component >= uniform.values.length) {
        throw new HostError("invalid_uniform", "Choose a writable shader uniform component.");
      }
      const binding = parseBinding(bindingValue, project.slots);
      const updated = await this.change("uniform.set", { name: uniform.name, component, ...binding });
      const applied = updated.uniforms.find(item => item.name === name);
      if (!applied || applied.values.length !== uniform.values.length ||
          (binding.mode === "fixed" && Math.abs(applied.values[component] - Math.fround(binding.value)) > 0.00001)) {
        throw new HostError("readback_mismatch", "The rendered uniform does not match the control.");
      }
      const bindings = project.bindings.map(item =>
        item.name === name && item.component === component ? { ...item, binding } : item);
      return this.adopt(updated, { ...project, bindings });
    });
  }

  startSamples() {
    return this.perform(async () => {
      this.checkedPreview();
      await this.samples.start();
      return this.view();
    });
  }

  stopSamples() {
    this.samples.stop();
    return this.view();
  }

  private async relaySample(slot: string, value: number) {
    if (this.busy || !this.previewMatches() || !this.state?.slots.some(item => item.id === slot)) return;
    const generation = this.generation;
    try {
      await this.host.rpc("sample", { slot, value }, this.state);
    } catch (error) {
      if (generation !== this.generation) return;
      if (error instanceof HostError && error.code === "busy") return;
      this.acknowledged = null;
      this.samples.stop();
      const detail = error instanceof HostError ? `${error.message} ` : "";
      this.notice = `OSC input stopped. ${detail}Restore the draft and enable OSC input again.`;
    }
  }

  save(path: string) {
    return this.perform(async () => {
      const prior = this.checkedPreview();
      const observed = this.receive(await this.host.rpc("reflect", {}));
      if (!this.previewMatches() || prior.state.epoch !== observed.epoch || prior.state.revision !== observed.revision) {
        throw new HostError("stale_revision", "Preview changed before save. Restore the draft and retry.");
      }
      const document = parseProject(prior.project);
      await writeAtomically(path, JSON.stringify(document, null, 2) + "\n");
      return path;
    });
  }

  open(path: string) {
    return this.perform(async () => {
      const file = await open(path, "r");
      try {
        const stat = await file.stat();
        if (!stat.isFile() || stat.size > MAX_PROJECT_BYTES) {
          throw new HostError("invalid_project", "Choose a VOSC draft file up to 1 MiB.");
        }
        const buffer = Buffer.alloc(stat.size + 1);
        const { bytesRead } = await file.read(buffer, 0, buffer.length, 0);
        if (bytesRead !== stat.size) throw new HostError("invalid_project", "The saved draft changed while reading.");
        const loaded = parseProject(JSON.parse(buffer.subarray(0, bytesRead).toString("utf8")));
        this.project = loaded;
        this.acknowledged = null;
        this.samples.stop();
        this.notice = "Draft opened. Restore it to its paired host for a native preview.";
        return this.view();
      } finally {
        await file.close();
      }
    });
  }

  restore() {
    return this.perform(async () => {
      const project = this.project;
      const peer = this.host.peer;
      if (!project || !peer || project.destination.target !== peer.target ||
          project.destination.fingerprint !== peer.fingerprint) {
        throw new HostError("wrong_host", "Connect to the saved draft’s paired host before restoring.");
      }
      const reflected = await this.validateSource(project.source);
      const count = (bindings: DraftProject["bindings"]) => {
        const counts = new Map<string, number>();
        for (const { name } of bindings) counts.set(name, (counts.get(name) ?? 0) + 1);
        return counts;
      };
      const native = count(reflected);
      if ([...count(project.bindings)].some(([name, size]) => native.get(name) !== size)) {
        throw new HostError("invalid_project", "The saved controls do not match the shader’s reflected uniforms.");
      }
      this.acknowledged = null;
      this.samples.stop();
      await this.refresh();
      let state = await this.change("activate", { source: project.source.base64, sha256: project.source.digest });
      for (const slot of project.slots) state = await this.change("slot.configure", { ...slot });
      for (const { name, component, binding } of project.bindings) {
        state = await this.change("uniform.set", { name, component, ...binding });
      }
      return this.adopt(state, { ...project, destination: peer });
    });
  }

  export(parent: string) {
    return this.perform(async () => {
      if (!this.project) throw new HostError("no_draft", "Open or activate a draft before exporting.");
      return writeReplay(parent, this.project);
    });
  }
}
