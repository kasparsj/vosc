import { randomBytes, randomUUID } from "node:crypto";
import { constants, type BigIntStats } from "node:fs";
import { lstat, open } from "node:fs/promises";
import { setTimeout as delay } from "node:timers/promises";
import { BINDING_LIMIT, SLOT_LIMIT, type ControlSignature, type DocumentEdit, type DocumentView, type DraftDocument, type DraftProject, type DraftSource, type DraftUniform, type DrawReceipt,
  type ReflectedDraft, type Slot, type WorkspaceView } from "./contracts";
import { editorText, MAX_PROJECT_BYTES, parseBinding, parseDocument, parseProject, source as checkedSource, sourceWithText, titleFor, validUniformName, writeReplay } from "./draft-project";
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

const GLSL_TEMPLATE = `#version 330
out vec4 fragColor;

void main() {
    fragColor = vec4(0.2, 0.45, 0.6, 1.0);
}
`;

function fileToken(stat: BigIntStats): string {
  return [stat.dev, stat.ino, stat.size, stat.mtimeNs, stat.ctimeNs].join(":");
}

export async function draftFileToken(path: string) {
  try {
    const stat = await lstat(path, { bigint: true });
    if (!stat.isFile()) throw new HostError("invalid_project", "Choose a regular VOSC draft file.");
    return fileToken(stat);
  } catch (error) {
    if ((error as NodeJS.ErrnoException).code === "ENOENT") return null;
    throw error;
  }
}

async function readDraftFile(path: string) {
  const file = await open(path, constants.O_RDONLY | constants.O_NOFOLLOW | constants.O_NONBLOCK);
  try {
    const stat = await file.stat({ bigint: true });
    if (!stat.isFile() || stat.size > BigInt(MAX_PROJECT_BYTES)) {
      throw new HostError("invalid_project", "Choose a regular VOSC draft file up to 1 MiB.");
    }
    const buffer = Buffer.alloc(Number(stat.size) + 1);
    const { bytesRead } = await file.read(buffer, 0, buffer.length, 0);
    const token = fileToken(stat);
    if (bytesRead !== Number(stat.size) || token !== fileToken(await file.stat({ bigint: true }))) {
      throw new HostError("file_changed", "The saved draft changed while reading.");
    }
    try {
      const contents = new TextDecoder("utf-8", { fatal: true }).decode(buffer.subarray(0, bytesRead));
      return { document: parseDocument(JSON.parse(contents)), token };
    } catch (error) {
      if (error instanceof HostError) throw error;
      throw new HostError("invalid_project", "Choose a valid UTF-8 VOSC draft.");
    }
  } finally {
    await file.close();
  }
}

function workingIdentity(document: DraftDocument) {
  return JSON.stringify([document.title, document.source.digest, document.destination, document.controls]);
}

function documentFromSource(source: DraftSource): DraftDocument {
  return { format: "vosc.authoring.draft", version: 2, id: randomUUID(), title: titleFor(source),
    language: "glsl", source: checkedSource(source), destination: null,
    controls: { slots: [], bindings: [], signatures: [] }, accepted: null };
}

function controlSignatures(uniforms: DraftUniform[]): ControlSignature[] {
  return uniforms.filter(isWritable).map(({ name, type, owner, values }) => ({ name, type, owner, arity: values.length }));
}

export class Workspace {
  private document: DraftDocument | null = null;
  private documentGeneration = 0;
  private editSequence = 0;
  private originalSource: DraftSource | null = null;
  private savedSource: DraftSource | null = null;
  private savedWorking: string | null = null;
  private savedArtifact: string | null = null;
  private savedPath: string | null = null;
  private savedToken: string | null = null;
  private saving = false;
  private state: ReflectedDraft | null = null;
  private acknowledged: { epoch: string; revision: number; receipt: DrawReceipt } | null = null;
  private busy = false;
  private generation = 0;
  private observation: Promise<void> | null = null;
  private pendingReply: { id: string; target: string; epoch: string } | null = null;
  private notice: string | null = null;
  private samples = new SampleRelay((slot, value) => this.relaySample(slot, value));

  constructor(readonly host: HostClient) {}

  private get project() { return this.document?.accepted?.project ?? null; }

  documentContext() {
    return { generation: this.documentGeneration, editSequence: this.editSequence };
  }

  private documentView(): DocumentView | null {
    const document = this.document;
    if (!document) return null;
    const { source, accepted } = document;
    return {
      id: document.id, ...this.documentContext(), title: document.title, language: document.language,
      source: { name: source.name, origin: source.origin, digest: source.digest, text: editorText(source) },
      dirty: workingIdentity(document) !== this.savedWorking,
      acceptedArtifact: accepted && {
        id: accepted.id, sourceDigest: accepted.project.source.digest, revision: accepted.revision,
        saved: this.savedArtifact === accepted.id,
        stale: source.digest !== accepted.project.source.digest ||
          JSON.stringify(document.controls.bindings) !== JSON.stringify(accepted.project.bindings) ||
          JSON.stringify(document.controls.slots) !== JSON.stringify(accepted.project.slots),
      },
    };
  }

  needsSave() {
    return this.document !== null && (workingIdentity(this.document) !== this.savedWorking ||
      (this.document.accepted?.id ?? null) !== this.savedArtifact);
  }

  destinationForSave() {
    return { path: this.savedPath, token: this.savedToken, title: this.document?.title };
  }

  private setDocument(document: DraftDocument | null, path: string | null = null, token: string | null = null) {
    this.document = document;
    this.documentGeneration += 1;
    this.editSequence = 0;
    this.originalSource = document?.source ?? null;
    this.savedSource = path ? this.originalSource : null;
    this.savedWorking = document && workingIdentity(document);
    this.savedArtifact = path ? document?.accepted?.id ?? null : null;
    this.savedPath = path;
    this.savedToken = token;
    this.acknowledged = null;
    this.pendingReply = null;
    this.samples.stop();
    this.notice = document?.accepted ? "Draft opened. Restore it on its paired host for native preview." : null;
  }

  edit(value: unknown): DocumentView {
    const edit = value as Partial<DocumentEdit> | null;
    const current = this.document;
    if (!current || !edit || edit.id !== current.id || edit.generation !== this.documentGeneration ||
        typeof edit.editSequence !== "number" || !Number.isSafeInteger(edit.editSequence) ||
        edit.editSequence <= this.editSequence || typeof edit.text !== "string") {
      throw new HostError("stale_document", "This edit no longer matches the active draft.");
    }
    const text = edit.text.replace(/\r\n?/g, "\n");
    const retained = [this.savedSource, this.originalSource, current.source]
      .find(source => source && editorText(source) === text);
    this.document = { ...current, source: retained ?? sourceWithText(current.source, text) };
    this.editSequence = edit.editSequence;
    return this.documentView()!;
  }

  create() {
    return this.perform(async () => {
      this.setDocument(documentFromSource(sourceWithText({
        name: "Untitled.frag", origin: "New GLSL fragment", digest: "", base64: "",
      }, GLSL_TEMPLATE)));
      return this.view();
    });
  }

  openSource(source: DraftSource) {
    return this.perform(async () => {
      this.setDocument(documentFromSource(source));
      return this.view();
    });
  }

  clear() {
    this.setDocument(null);
  }

  view(): WorkspaceView {
    const project = this.project && {
      ...this.project,
      source: { name: this.project.source.name, digest: this.project.source.digest, origin: this.project.source.origin },
    };
    return {
      document: this.documentView(), documentGeneration: this.documentGeneration, peer: this.host.peer, reflected: this.state, project, previewReady: this.previewMatches(),
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
    if (!this.document) throw new HostError("no_draft", "Open or create a VOSC draft first.");
    const signatures = controlSignatures(state.uniforms);
    const { name, origin, digest, base64 } = project.source;
    const acceptedProject = { ...project, source: { name, origin, digest, base64 } };
    this.document = { ...this.document, destination: project.destination,
      controls: { slots: project.slots, bindings: project.bindings, signatures },
      accepted: { id: randomUUID(), revision: state.revision, project: acceptedProject, signatures } };
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
      const initiatingDocument = this.documentContext();
      const source = await readSource();
      const project = parseProject({ format: "vosc.authoring.draft", version: 1, title: titleFor(source),
        destination: peer, source, slots: [], bindings: [] });
      const bindings = await this.validateSource(project.source);
      parseProject({ ...project, bindings });
      await this.refresh();
      if (this.editSequence !== initiatingDocument.editSequence || this.documentGeneration !== initiatingDocument.generation) {
        throw new HostError("stale_document", "The draft changed while the source was being prepared.");
      }
      const state = await this.change("activate", { source: source.base64, sha256: source.digest });
      if (this.editSequence !== initiatingDocument.editSequence || this.documentGeneration !== initiatingDocument.generation) {
        this.acknowledged = null;
        this.samples.stop();
        throw new HostError("stale_document", "Edits were kept. Preview the library source again to open it.");
      }
      if (state.sourceDigest !== source.digest) {
        throw new HostError("source_mismatch", "The native preview did not acknowledge the selected shader.");
      }
      this.setDocument(documentFromSource(source));
      const view = this.adopt(state, parseProject({ ...project, slots: state.slots, bindings: defaultBindings(state.uniforms) }));
      this.savedWorking = workingIdentity(this.document!);
      return { ...view, document: this.documentView() };
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

  captureSave() {
    return { snapshot: this.document, generation: this.documentGeneration };
  }

  async save(path: string, expectedToken?: string | null, capture = this.captureSave()) {
    const { snapshot, generation } = capture;
    if (!snapshot) throw new HostError("no_draft", "Open or create a VOSC draft first.");
    if (this.saving) throw new HostError("busy", "Wait for the current save.");
    this.saving = true;
    try {
      const token = expectedToken === undefined
        ? path === this.savedPath ? this.savedToken : await draftFileToken(path)
        : expectedToken;
      const bytes = Buffer.from(JSON.stringify(parseDocument(snapshot), null, 2) + "\n", "utf8");
      if (bytes.length > MAX_PROJECT_BYTES) throw new HostError("invalid_project", "The draft exceeds 1 MiB.");
      const check = async () => {
        if (await draftFileToken(path) !== token) {
          throw new HostError("file_changed", "The draft file changed before Save. Review it or choose Save As.");
        }
        if (this.documentGeneration !== generation) {
          throw new HostError("stale_document", "The active draft changed before Save.");
        }
      };
      const promoted = await writeAtomically(path, bytes, 0o600, { check, exclusive: token === null });
      const savedToken = fileToken(promoted);
      if (this.documentGeneration === generation) {
        this.savedPath = path;
        this.savedToken = savedToken;
        this.savedWorking = workingIdentity(snapshot);
        this.savedSource = snapshot.source;
        this.savedArtifact = snapshot.accepted?.id ?? null;
      }
      return path;
    } finally {
      this.saving = false;
    }
  }

  open(path: string, beforeReplace: () => Promise<boolean> = async () => true) {
    return this.perform(async () => {
      const generation = this.documentGeneration;
      let selected = await readDraftFile(path);
      if (!await beforeReplace()) return this.view();
      const editSequence = this.editSequence;
      if (await draftFileToken(path) !== selected.token) {
        // Save in the dirty-document prompt may have replaced the very file
        // being reopened. Read its current contents instead of the old copy.
        selected = await readDraftFile(path);
      }
      if (generation !== this.documentGeneration || editSequence !== this.editSequence) {
        throw new HostError("stale_document", "The active draft changed before Open.");
      }
      this.setDocument(selected.document, path, selected.token);
      return this.view();
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
      const working = this.document!;
      const view = this.adopt(state, { ...project, destination: peer });
      this.document = { ...this.document!, destination: working.destination, controls: working.controls };
      return { ...view, document: this.documentView() };
    });
  }

  captureExport() {
    const accepted = this.document?.accepted;
    if (!accepted) throw new HostError("no_draft", "There is no accepted native artifact to export.");
    return {
      label: accepted.revision === null ? `Artifact ${accepted.id.slice(0, 8)}` : `Revision ${accepted.revision}`,
      write: (directory: string) => writeReplay(directory, accepted.project),
    };
  }

  export(parent: string) {
    return this.captureExport().write(parent);
  }
}
