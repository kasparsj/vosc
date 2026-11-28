import { createHash, randomUUID } from "node:crypto";
import { TextDecoder } from "node:util";
import { NativeCleanupError, NativeRenderer, type NativeObject as Row } from "./native-renderer";
import { requireFields, WireError, type WireRequest } from "./transport";

type Slot = { id: string; label: string; default: number };
type Component = { fixed: number } | { slot: string; low: number; high: number };
type Uniform = { name: string; owner: "layer"; writable: boolean; type: string; values: number[]; binding?: string };
type Attempt = { documentId: string; generation: number; editSequence: number; runSequence: number };
type Candidate = {
  lease: string;
  token: string;
  nativeRevision: number;
  deadline: number;
  sourceDigest: string;
  controlDigest: string;
  slots: Map<string, Slot>;
  bindings: Map<string, Component[]>;
  attempt: Attempt;
};
type ControlChange = { slot: Slot } | { name: string; components: Component[] };
type Mutation = { candidate: Candidate } | { lease: string; revision: number; change: ControlChange };
type Outcome = { ok: true; result: Row } | { ok: false; error: unknown };

const sourceLimit = 128 * 1024;
const wireVersion = 1;
const callbackUniform = "u_vosc_callback";
const decoder = new TextDecoder("utf-8", { fatal: true, ignoreBOM: true });
const scalarTypes = new Map([[0x1406, "float"], [0x8b50, "vec2"], [0x8b51, "vec3"], [0x8b52, "vec4"]]);
const uncertainCodes = new Set(["renderer_timeout", "renderer_transport", "renderer_protocol", "request_not_found"]);
const sha256 = (value: Buffer | string) => createHash("sha256").update(value).digest("hex");
const errorCode = (error: unknown) => error instanceof WireError ? error.code : undefined;
const isUncertain = (error: unknown) => uncertainCodes.has(errorCode(error) ?? "");

function object(value: unknown): Row | undefined {
  return value !== null && typeof value === "object" && !Array.isArray(value) ? value as Row : undefined;
}

function numeric(value: unknown): value is number {
  return typeof value === "number" && Number.isFinite(value);
}

function integer(value: unknown): value is number {
  return typeof value === "number" && Number.isSafeInteger(value);
}

function uuid(value: unknown): value is string {
  return typeof value === "string" && /^[a-f\d]{8}(?:-[a-f\d]{4}){3}-[a-f\d]{12}$/i.test(value);
}

function slotID(value: unknown): value is string {
  return typeof value === "string" && /^[a-zA-Z][a-zA-Z0-9_]{0,31}$/.test(value);
}

function slotDefinition(raw: unknown): Slot {
  const { id, label, default: initial } = object(raw) ?? {};
  if (!slotID(id) || typeof label !== "string" || !label.trim() || Buffer.byteLength(label) > 80 ||
      /[\p{Cc}\p{Cf}]/u.test(label) || !numeric(initial) || initial < 0 || initial > 1 ||
      Object.keys(raw as Row).length !== 3) {
    throw new WireError("invalid_slot", "Expected a bounded slot ID, display label, and normalized default");
  }
  return { id, label, default: initial };
}

function fragment(request: WireRequest) {
  const { source: encoded, sha256: claimed } = request.params;
  const invalid = () => new WireError("invalid_source", "Expected bounded UTF-8 fragment bytes with matching SHA-256");
  if (typeof encoded !== "string" || encoded.length > 4 * Math.ceil(sourceLimit / 3)) throw invalid();
  const bytes = Buffer.from(encoded, "base64");
  if (!bytes.length || bytes.length > sourceLimit || bytes.toString("base64") !== encoded ||
      bytes.includes(0) || sha256(bytes) !== claimed) throw invalid();
  let source: string;
  try { source = decoder.decode(bytes); } catch { throw invalid(); }
  const horizontalWhitespace = "[\\t\\p{Zs}]";
  const dependencies = new RegExp(`^${horizontalWhitespace}*#${horizontalWhitespace}*(?:include|pragma${horizontalWhitespace}+include)\\b`, "imu");
  if (dependencies.test(source) || /\\\r?\n/.test(source)) {
    throw new WireError("unsupported_dependency", "File includes and continued source directives are unsupported");
  }
  return { base64: encoded, digest: claimed as string };
}

function sortedJSON(value: unknown): string {
  if (Array.isArray(value)) return "[" + value.map(sortedJSON).join(",") + "]";
  const row = object(value);
  if (row) return "{" + Object.keys(row).sort().map(key => JSON.stringify(key) + ":" + sortedJSON(row[key])).join(",") + "}";
  return JSON.stringify(value);
}

function shader(scene: Row) {
  return Array.isArray(scene.layers) ? object(object(scene.layers[0])?.shader) : undefined;
}

function uniforms(scene: Row): Uniform[] {
  const declarations = object(shader(scene)?.uniforms) ?? {};
  const result: Uniform[] = [];
  for (const name of Object.keys(declarations).sort()) {
    const declaration = object(declarations[name]);
    if (!declaration || !integer(declaration.gl_type)) continue;
    const type = scalarTypes.get(declaration.gl_type);
    let observation: unknown = declaration.rendered_value;
    if (typeof observation === "string") {
      try { observation = JSON.parse(observation); } catch { observation = undefined; }
    }
    result.push({
      name, owner: "layer", writable: type !== undefined && declaration.writable === true &&
        object(scene.draft_activation)?.callback_uniform !== name,
      type: type ?? "0x" + declaration.gl_type.toString(16),
      values: (Array.isArray(observation) ? observation : [observation]).filter(numeric),
      ...(typeof declaration.binding === "string" ? { binding: declaration.binding } : {}),
    });
  }
  return result;
}

function drawnScene(receipt: Row, previous: number, message: string) {
  const result = object(receipt.result);
  const scene = object(result?.scene);
  if (!result || !scene || !integer(receipt.revision) || receipt.revision <= previous ||
      !integer(receipt.frame) || scene.frame !== receipt.frame || scene.rendered_revision !== receipt.revision) {
    throw new WireError("renderer_protocol", message);
  }
  return { result, scene, revision: receipt.revision };
}

/** The host serializes ordinary requests. Fences may enter during a native exchange. */
export class Draft {
  private renderer?: NativeRenderer;
  private validator?: NativeRenderer;
  private starting?: Promise<NativeRenderer>;
  private startupCleanupFailure?: NativeCleanupError;
  private validatorStop?: Promise<void>;
  private validationIdle?: ReturnType<typeof setTimeout>;
  private epoch = randomUUID();
  private revision = 0;
  private activation?: Attempt & { lease: string; token: string; controlDigest: string; commitID: string; sourceDigest: string };
  private slots = new Map<string, Slot>();
  private bindings = new Map<string, Component[]>();
  private fenced = new Set<string>();
  private lastSampleTime = -Infinity;
  private candidate?: Candidate;
  private uncertain?: { id: string; mutation: Mutation };
  private recovered?: { id: string; outcome: Outcome };
  private resetting?: Promise<void>;
  onReset?: () => void;

  constructor(private readonly target: string, private readonly installDirectory: string) {}

  reset(): Promise<void> {
    if (this.resetting) return this.resetting;
    const renderer = this.renderer;
    this.renderer = undefined;
    this.epoch = randomUUID();
    this.revision = 0;
    this.activation = undefined;
    this.slots.clear();
    this.bindings.clear();
    this.fenced.clear();
    this.lastSampleTime = -Infinity;
    this.candidate = undefined;
    this.uncertain = undefined;
    this.recovered = undefined;
    const starting = this.starting?.catch(() => {
      // The startup path latches any unconfirmed cleanup before it settles.
    });
    const stopped = Promise.allSettled([renderer?.stop(), this.stopValidator(), starting]).then(results => {
      if (this.startupCleanupFailure) throw this.startupCleanupFailure;
      const failed = results.find(result => result.status === "rejected");
      if (failed) throw failed.reason;
      this.resetting = undefined;
    });
    this.resetting = stopped;
    this.onReset?.();
    return stopped;
  }

  async execute(request: WireRequest): Promise<Row> {
    await this.resetting;
    if (this.startupCleanupFailure) throw this.startupCleanupFailure;
    if (this.renderer && !this.renderer.running) await this.reset();
    switch (request.method) {
      case "validate":
        requireFields(request, ["source", "sha256"]);
        return this.validate(fragment(request));
      case "health":
      case "reflect": {
        requireFields(request, []);
        await this.settleUnknown();
        const child = this.renderer;
        if (!child) return this.snapshot({});
        const scene = await child.query("get_scene");
        this.assertRenderer(child);
        return this.snapshot(scene);
      }
      case "run.prepare":
        await this.settleUnknown();
        this.expected(request);
        return this.prepare(request);
      case "run.commit":
        await this.settleUnknown();
        this.expected(request);
        return this.commit(request);
      case "run.cancel": {
        await this.settleUnknown();
        requireFields(request, ["lease", "token"]);
        this.expected(request);
        const staged = this.candidate;
        if (staged && staged.lease === request.params.lease && staged.token === request.params.token) {
          await this.discard(staged);
        }
        return { target: this.target, epoch: this.epoch, revision: this.revision };
      }
      case "slot.configure":
      case "uniform.set":
      case "sample":
      case "callback.sample":
        await this.settleUnknown();
        this.expected(request);
        if (!this.activation || this.fenced.has(this.activation.lease)) {
          throw new WireError("restore_required", "Run or restore this draft before changing its controls");
        }
        if (request.method === "slot.configure") return this.configureSlot(request);
        if (request.method === "uniform.set") return this.bindUniform(request);
        return this.relaySample(request);
      default:
        throw new WireError("unsupported_method", "This draft operation is unavailable");
    }
  }

  async fence(request: WireRequest): Promise<Row> {
    requireFields(request, ["lease"]);
    const { lease } = request.params;
    if (typeof lease !== "string") throw new WireError("invalid_request", "A preview lease is required");
    if (request.epoch !== this.epoch) {
      throw new WireError("stale_epoch", "Renderer process changed before the lease was fenced");
    }
    await this.revoke(lease, request.id);
    return { target: this.target, epoch: request.epoch, lease };
  }

  async revoke(lease: string, id: string = randomUUID()): Promise<void> {
    if (this.startupCleanupFailure) throw this.startupCleanupFailure;
    this.fenced.add(lease);
    const child = this.renderer;
    if (!child) return;
    const receipt = await child.mutate("draft_fence", { lease }, id, "ok");
    if (object(receipt.result)?.lease !== lease) {
      throw new WireError("renderer_protocol", "Native fence returned a different lease");
    }
  }

  async recoverMutation(id: string): Promise<Row | undefined> {
    if (this.startupCleanupFailure) throw this.startupCleanupFailure;
    if (this.uncertain?.id === id) await this.settleUnknown();
    const outcome = this.completedMutation(id);
    if (!outcome) return undefined;
    if (!outcome.ok) throw outcome.error;
    return outcome.result;
  }

  completedMutation(id: string): Outcome | undefined {
    return this.recovered?.id === id ? this.recovered.outcome : undefined;
  }

  private expected(request: WireRequest) {
    if (request.epoch !== this.epoch) {
      throw new WireError("stale_epoch", "Renderer process changed. Refresh the selected draft before restoring it");
    }
    if (request.revision !== this.revision) {
      throw new WireError("stale_revision", "Draft revision changed. Read back current controls before applying edits");
    }
  }

  private assertRenderer(child: NativeRenderer) {
    if (this.renderer !== child || !child.running) {
      throw new WireError("draft_state_lost", "The draft renderer changed before confirming the operation");
    }
  }

  private currentRenderer(): NativeRenderer {
    if (!this.renderer) throw new WireError("restore_required", "Run or restore a saved shader first");
    this.assertRenderer(this.renderer);
    return this.renderer;
  }

  private async startNative(epoch: string, validation: boolean): Promise<NativeRenderer> {
    const starting = NativeRenderer.start({
      installDirectory: this.installDirectory, validation,
      onExit: stopped => {
        if (this.renderer === stopped) void this.reset().catch(() => {});
      },
    }).then(async child => {
      if (this.epoch !== epoch || !child.running) {
        await child.stop();
        if (validation && this.epoch !== epoch) {
          throw new WireError("stale_epoch", "Renderer process changed during validation");
        }
        throw new WireError("draft_state_lost", "The draft renderer changed during startup");
      }
      if (validation) this.validator = child;
      else this.renderer = child;
      return child;
    }).catch(error => {
      if (error instanceof NativeCleanupError) this.startupCleanupFailure = error;
      throw error;
    });
    this.starting = starting;
    try {
      return await starting;
    } finally {
      this.starting = undefined;
    }
  }

  private async discard(staged: Candidate) {
    this.candidate = undefined;
    const child = this.renderer;
    if (!child) return;
    await child.mutate("draft_cancel", { lease: staged.lease, token: staged.token }, undefined, "ok");
    this.assertRenderer(child);
  }

  private async prepare(request: WireRequest): Promise<Row> {
    requireFields(request, ["source", "sha256", "lease", "documentId", "generation", "editSequence",
      "runSequence", "slots", "bindings"]);
    const { lease, documentId, generation, editSequence, runSequence, slots: requestedSlots, bindings } = request.params;
    const intent = object(bindings);
    if (!uuid(lease) || this.fenced.has(lease) || !uuid(documentId) ||
        !integer(generation) || generation < 0 || !integer(editSequence) || editSequence < 0 ||
        !integer(runSequence) || runSequence <= 0 || !Array.isArray(requestedSlots) || requestedSlots.length > 16 ||
        !intent || Object.keys(intent).length > 128) {
      throw new WireError("invalid_request", "Expected bounded draft source and control intent");
    }
    const slots = new Map<string, Slot>();
    for (const raw of requestedSlots) {
      const definition = slotDefinition(raw);
      if (slots.has(definition.id)) throw new WireError("invalid_slot", "Duplicate slot identity");
      slots.set(definition.id, definition);
    }
    const source = fragment(request);
    if (this.candidate) await this.discard(this.candidate);
    await this.stopValidator();
    this.expected(request);
    const child = this.renderer ? this.currentRenderer() : await this.startNative(request.epoch!, false);
    const baseline = await child.query("get_scene");
    this.assertRenderer(child);
    if (this.fenced.has(lease)) throw new WireError("stale_lease", "The draft lease was fenced");
    if (!integer(baseline.revision)) {
      throw new WireError("renderer_protocol", "Native configuration revision is missing");
    }
    const receipt = await child.mutate("draft_prepare", {
      source_base64: source.base64, source_digest: source.digest, lease, token: request.id,
      revision: baseline.revision, slots: Object.fromEntries([...slots].map(([id, slot]) => [id, slot.default])),
      bindings: intent,
    }, request.id, "prepared");
    this.assertRenderer(child);
    const result = object(receipt.result);
    const effective = object(result?.bindings);
    if (result?.token !== request.id || !effective || !Array.isArray(result.conflicts) ||
        !result.conflicts.every(object)) {
      throw new WireError("renderer_protocol", "Native preparation returned an unmatched token");
    }
    const applied = new Map<string, Component[]>();
    for (const [name, binding] of Object.entries(effective)) {
      const components = object(binding)?.components;
      if (!Array.isArray(components) || !components.every(part => {
        const item = object(part);
        return item && (numeric(item.fixed) || (slotID(item.slot) && numeric(item.low) && numeric(item.high)));
      })) {
        throw new WireError("renderer_protocol", "Native preparation returned malformed bindings");
      }
      applied.set(name, components as Component[]);
    }
    const sortedSlots = [...slots.keys()].sort().map(id => slots.get(id)!);
    const controlDigest = sha256(sortedJSON({ slots: sortedSlots, bindings: effective }));
    const attempt = { documentId, generation, editSequence, runSequence };
    const staged = { lease, token: request.id, nativeRevision: baseline.revision, deadline: performance.now() + 20_000,
      sourceDigest: source.digest, controlDigest, slots, bindings: applied, attempt };
    if (this.fenced.has(lease)) {
      await this.discard(staged);
      throw new WireError("stale_lease", "The draft lease was fenced");
    }
    this.candidate = staged;
    return { ...attempt, target: this.target, epoch: this.epoch, revision: this.revision, lease, token: request.id,
      sourceDigest: source.digest, controlDigest, bindings: effective, conflicts: result.conflicts };
  }

  private async commit(request: WireRequest): Promise<Row> {
    requireFields(request, ["lease", "token"]);
    const staged = this.candidate;
    if (!staged || staged.lease !== request.params.lease || staged.token !== request.params.token ||
        this.fenced.has(staged.lease)) {
      throw new WireError("stale_candidate", "Prepare the current source again before applying it");
    }
    if (performance.now() >= staged.deadline) {
      await this.discard(staged);
      throw new WireError("stale_candidate", "The prepared draft expired");
    }
    this.candidate = undefined;
    return this.applyMutation({ candidate: staged }, this.currentRenderer(), "draft_commit", {
      lease: staged.lease, token: staged.token, revision: staged.nativeRevision, control_digest: staged.controlDigest,
    }, request.id);
  }

  private async applyMutation(mutation: Mutation, child: NativeRenderer, method: string, params: Row, id: string): Promise<Row> {
    try {
      const receipt = await child.mutate(method, params, id);
      this.assertRenderer(child);
      return this.confirm(mutation, id, receipt);
    } catch (error) {
      if (this.renderer !== child) throw new WireError("draft_state_lost", "The draft renderer changed during mutation");
      if (errorCode(error) === "draft_state_lost") await this.reset();
      if (!isUncertain(error)) throw error;
      this.uncertain = { id, mutation };
      await this.settleUnknown();
      const outcome = this.completedMutation(id);
      if (!outcome) throw error;
      if (!outcome.ok) throw outcome.error;
      return outcome.result;
    }
  }

  private async settleUnknown() {
    const unknown = this.uncertain;
    if (!unknown) return;
    const child = this.currentRenderer();
    try {
      const receipt = await child.recover(unknown.id, "rendered");
      this.assertRenderer(child);
      const result = this.confirm(unknown.mutation, unknown.id, receipt);
      this.uncertain = undefined;
      this.recovered = { id: unknown.id, outcome: { ok: true, result } };
    } catch (error) {
      if (this.renderer !== child) throw new WireError("draft_state_lost", "The draft renderer changed during recovery");
      if (isUncertain(error)) {
        throw new WireError("renderer_uncertain", "The native draw is unconfirmed. Recheck the same host or restart the draft service");
      }
      if (errorCode(error) === "draft_state_lost") await this.reset();
      this.uncertain = undefined;
      this.recovered = { id: unknown.id, outcome: { ok: false, error } };
    }
  }

  private confirm(mutation: Mutation, id: string, receipt: Row): Row {
    if ("candidate" in mutation) {
      const staged = mutation.candidate;
      const message = "The native draw did not confirm the prepared draft";
      const { result, scene, revision } = drawnScene(receipt, staged.nativeRevision, message);
      if (result.lease !== staged.lease || result.token !== staged.token || result.source_digest !== staged.sourceDigest ||
          result.control_digest !== staged.controlDigest || result.commit_id !== id) {
        throw new WireError("renderer_protocol", message);
      }
      this.revision = revision;
      this.slots = staged.slots;
      this.bindings = staged.bindings;
      this.activation = { ...staged.attempt, lease: staged.lease, token: staged.token,
        controlDigest: staged.controlDigest, commitID: id, sourceDigest: staged.sourceDigest };
      this.lastSampleTime = -Infinity;
      return this.snapshot(scene, receipt);
    }
    const { lease, revision: previous, change } = mutation;
    const message = "Native draw did not confirm the requested control change";
    const { result, scene, revision } = drawnScene(receipt, previous, message);
    if (result.lease !== lease || object(scene.draft_activation)?.lease !== lease) {
      throw new WireError("renderer_protocol", message);
    }
    const samples = object(scene.draft_slots) ?? {};
    if ("slot" in change) {
      const observed = samples[change.slot.id];
      if (!numeric(observed) || Math.abs(observed - Math.fround(change.slot.default)) >= 1e-6) {
        throw new WireError("renderer_protocol", "Native renderer did not configure the requested slot");
      }
      this.slots.set(change.slot.id, change.slot);
    } else {
      const returned = uniforms(scene).find(uniform => uniform.name === change.name)?.values;
      if (!returned || returned.length !== change.components.length) {
        throw new WireError("renderer_protocol", "Native readback did not return the edited uniform");
      }
      for (const [index, part] of change.components.entries()) {
        if ("fixed" in part) {
          const expected = Math.fround(part.fixed);
          if (Math.abs(returned[index] - expected) > Math.max(1e-6, Math.abs(expected) * 1e-6)) {
            throw new WireError("renderer_protocol", "Drawn fixed uniform does not match its binding");
          }
        } else if (!numeric(samples[part.slot])) {
          throw new WireError("renderer_protocol", "Mapped native slot was not reflected");
        }
      }
      this.bindings.set(change.name, change.components);
    }
    this.candidate = undefined;
    this.revision = revision;
    return this.snapshot(scene, receipt);
  }

  private async stopValidator() {
    clearTimeout(this.validationIdle);
    this.validationIdle = undefined;
    if (this.validatorStop) return this.validatorStop;
    const validator = this.validator;
    if (!validator) return;

    // Keep ownership until the native child exits. A failed stop stays latched
    // so this service cannot start another child or report a clean reset.
    const stopped = Promise.resolve().then(() => validator.stop()).then(() => {
      this.validator = undefined;
      this.validatorStop = undefined;
    });
    this.validatorStop = stopped;
    return stopped;
  }

  private async validate(source: ReturnType<typeof fragment>): Promise<Row> {
    clearTimeout(this.validationIdle);
    const epoch = this.epoch;
    let child = this.validator;
    if (this.validatorStop || !child?.running) {
      await this.stopValidator();
      if (this.epoch !== epoch) {
        throw new WireError("stale_epoch", "Renderer process changed during validation");
      }
      child = await this.startNative(epoch, true);
    }
    try {
      const receipt = await child.mutate("publish_shader", { layer: 0, source_base64: source.base64 });
      const scene = await child.query("get_scene");
      const live = shader(scene);
      if (this.validator !== child || this.epoch !== epoch || !integer(receipt.revision) ||
          scene.rendered_revision !== receipt.revision || !integer(scene.external_epoch) ||
          scene.external_epoch !== receipt.external_epoch || !integer(scene.frame) || !integer(receipt.frame) ||
          scene.frame < receipt.frame || live?.loaded !== true || !integer(live.program) || live.program <= 0) {
        throw new WireError("renderer_protocol", "Drawn receipt does not match the active native shader");
      }
      return { target: this.target, epoch, sourceDigest: source.digest, receipt, shader: { uniforms: uniforms(scene) } };
    } catch (error) {
      if (this.validator === child && errorCode(error) !== "shader_failed") await this.stopValidator();
      throw error;
    } finally {
      if (this.validator === child && !this.validatorStop) {
        this.validationIdle = setTimeout(() => { void this.stopValidator().catch(() => {}); }, 2000);
        this.validationIdle.unref();
      }
    }
  }

  private configureSlot(request: WireRequest) {
    requireFields(request, ["id", "label", "default"]);
    const slot = slotDefinition(request.params);
    if (this.slots.size >= 16 && !this.slots.has(slot.id)) throw new WireError("slot_capacity", "Draft has 16 slots");
    return this.applyControls({ slot }, request, "draft_configure_slot", { slot: slot.id, initial: slot.default });
  }

  private bindingComponent(request: WireRequest): Component {
    const { mode, value, slot, low, high } = request.params;
    if (mode === "fixed") {
      requireFields(request, ["name", "component", "mode", "value"]);
      if (!numeric(value) || Math.abs(value) > 1_000_000) {
        throw new WireError("invalid_uniform", "A finite bounded uniform value is required");
      }
      return { fixed: value };
    }
    if (mode === "range") {
      requireFields(request, ["name", "component", "mode", "slot", "low", "high"]);
      if (!slotID(slot) || !this.slots.has(slot)) {
        throw new WireError("missing_slot", "Configure the mapped slot before applying a range");
      }
      if (!numeric(low) || !numeric(high) || Math.abs(low) > 1_000_000 || Math.abs(high) > 1_000_000) {
        throw new WireError("invalid_uniform", "Finite bounded range endpoints are required");
      }
      return { slot, low, high };
    }
    throw new WireError("invalid_uniform", "Choose fixed or ranged binding");
  }

  private async bindUniform(request: WireRequest) {
    const component = this.bindingComponent(request);
    const { name, component: index } = request.params;
    if (typeof name !== "string" || !name || Buffer.byteLength(name) > 128 || !integer(index) || index < 0) {
      throw new WireError("invalid_uniform", "Expected a writable uniform and component index");
    }
    const child = this.currentRenderer();
    const live = await child.query("get_scene");
    this.assertRenderer(child);
    const reflection = uniforms(live).find(uniform => uniform.name === name);
    if (!reflection?.writable || index >= reflection.values.length) {
      throw new WireError("invalid_uniform", "Uniform is unavailable, renderer-owned, or component is out of range");
    }
    const components = (this.bindings.get(name) ?? reflection.values.map(fixed => ({ fixed }))).slice();
    if (components.length !== reflection.values.length) {
      throw new WireError("renderer_protocol", "Native reflection changed the uniform arity");
    }
    components[index] = component;
    return this.applyControls({ name, components }, request, "draft_bind_uniform", { layer: 0, name, components });
  }

  private applyControls(change: ControlChange, request: WireRequest, method: string, params: Row) {
    const lease = this.activation?.lease;
    if (!lease || this.fenced.has(lease)) {
      throw new WireError("stale_lease", "The draft lease was fenced before applying the controls");
    }
    const { revision } = this;
    return this.applyMutation({ lease, revision, change }, this.currentRenderer(), method,
      { ...params, lease, revision }, request.id);
  }

  private async relaySample(request: WireRequest): Promise<Row> {
    const callback = request.method === "callback.sample";
    requireFields(request, callback ? ["lease", "sequence", "value"] : ["lease", "sequence", "slot", "value"]);
    const { lease, sequence, slot, value } = request.params;
    if (typeof lease !== "string" || lease !== this.activation?.lease || this.fenced.has(lease) ||
        !integer(sequence) || sequence <= 0 || (!callback && (!slotID(slot) || !this.slots.has(slot))) ||
        !numeric(value) || value < 0 || value > 1) {
      throw new WireError("invalid_sample", "Expected a normalized sample for the current draft lease");
    }
    const now = performance.now();
    if (now - this.lastSampleTime < 15) throw new WireError("sample_rate", "Sample relay rate exceeded");
    this.lastSampleTime = now;
    const child = this.currentRenderer();
    const receipt = await child.mutate(callback ? "draft_callback" : "draft_sample",
      { lease, revision: this.revision, sequence, value, ...(callback ? {} : { slot }) }, request.id);
    this.assertRenderer(child);
    const result = object(receipt.result);
    const scene = object(result?.scene);
    if (result?.lease !== lease || result.sequence !== sequence || !scene || !integer(receipt.frame) ||
        scene.frame !== receipt.frame || receipt.revision !== this.revision) {
      throw new WireError("sample_unobserved", "The native renderer did not draw the tagged sample");
    }
    if (callback) {
      const readback = uniforms(scene).find(item => item.name === callbackUniform)?.values;
      if (object(scene.draft_activation)?.callback_uniform !== callbackUniform ||
          scene.rendered_revision !== receipt.revision || readback?.length !== 1 ||
          Math.abs(readback[0] - Math.fround(value)) > 1e-6) {
        throw new WireError("sample_unobserved", "The native callback draw did not confirm the uniform value");
      }
    }
    return this.snapshot(scene, callback ? receipt : undefined);
  }

  private snapshot(scene: Row, receipt?: Row): Row {
    const samples = object(scene.draft_slots);
    return {
      role: "draft", version: wireVersion, target: this.target, epoch: this.epoch, revision: this.revision,
      uniforms: uniforms(scene), slots: [...this.slots.keys()].sort().map(id => this.slots.get(id)!),
      needsRestore: !this.activation,
      ...(object(scene.draft_activation)?.callback_uniform === callbackUniform ? { callbackUniform } : {}),
      ...(integer(scene.frame) ? { frame: scene.frame } : {}),
      ...(samples ? { samples } : {}),
      ...(receipt ? { receipt } : {}),
      ...this.activation,
    };
  }
}
