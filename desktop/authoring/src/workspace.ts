import { TRIODE_COMPILER_ID } from "vosc-triode-identity";
import { HYDRA_EMITTER_VERSION } from "./hydra-compiler";
import type { HydraEvaluator } from "./hydra-evaluator";
import { randomBytes, randomUUID } from "node:crypto";
import { constants, type BigIntStats } from "node:fs";
import { lstat, open } from "node:fs/promises";
import { setTimeout as delay } from "node:timers/promises";
import { BINDING_LIMIT, CALLBACK_UNIFORM, SLOT_LIMIT, isCallbackValue, isDraftDigest, type AcceptedRendering, type Binding, type ComponentBinding, type ControlSignature, type DocumentEdit, type DocumentView,
  type DraftBackend, type DraftControls, type DraftDocument, type DraftLanguage, type HydraProvenance, type DraftProject, type DraftSource, type DraftUniform, type DrawReceipt,
  type MappingConflict, type Peer, type PendingMapping, type ReflectedDraft, type RunIdentity, type RunMode,
  type RunProblem, type RunView, type Slot, type TranslationIdentity, type WorkspaceView } from "./contracts";
import { editorText, MAX_PROJECT_BYTES, parseBinding, parseDocument, parseProject, source as checkedSource, sourceWithText, titleFor, validUniformName, writeReplay } from "./draft-project";
import { reportedError, type HostClient, type HostSession } from "./host-client";
import { HostError, writeAtomically } from "./local-state";
import { SampleRelay } from "./osc-samples";

const TYPE_ARITY = new Map([["float", 1], ["vec2", 2], ["vec3", 3], ["vec4", 4]]);
const CALLBACK_INTERVAL_MS = 100;
const evaluateSyntheticCallback = (elapsed: number) => 0.5 + 0.25 * Math.sin(elapsed * Math.PI / 1000);

function callbackReadback(state: ReflectedDraft) {
  if (state.callbackUniform !== CALLBACK_UNIFORM) return undefined;
  const uniform = state.uniforms.find(item => item.name === CALLBACK_UNIFORM);
  const value = uniform?.values[0];
  if (uniform?.type !== "float" || uniform.writable !== false || uniform.values.length !== 1 || !isCallbackValue(value)) {
    throw new HostError("readback_mismatch", "The render host did not confirm the callback uniform.");
  }
  return value;
}

export function isWritable(uniform: DraftUniform) {
  const arity = TYPE_ARITY.get(uniform.type);
  return uniform.writable && uniform.owner === "layer" && validUniformName(uniform.name) &&
    arity === uniform.values.length &&
    uniform.values.every(value => Number.isFinite(value) && Math.abs(value) <= BINDING_LIMIT);
}

const GLSL_TEMPLATE = `#version 330
out vec4 fragColor;

void main() {
    fragColor = vec4(0.2, 0.45, 0.6, 1.0);
}
`;

const WGSL_TEMPLATE = `@fragment fn fs_main(@location(0) uv: vec2f) -> @location(0) vec4f {
    return vec4f(uv.x, uv.y, 0.6, 1.0);
}
`;

const HYDRA_TEMPLATE = `// Run builds one native shader. Use Inspector to map mix to OSC.
// rotate() changes the coordinates passed to earlier operations.
osc(11, 0.18, 0.7)
  .blend(osc(7, -0.12, 1.1).rotate(-31), param("mix", 0.5))
  .rotate(17)
  .out();
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

function documentFromSource(source: DraftSource, language: DraftLanguage = "glsl"): DraftDocument {
  return { format: "vosc.authoring.draft", version: 2, id: randomUUID(), title: titleFor(source),
    language, source: checkedSource(source), destination: null,
    controls: { slots: [], bindings: [], signatures: [] }, accepted: null };
}

function controlSignatures(uniforms: DraftUniform[], bindings: ComponentBinding[]): ControlSignature[] {
  const bound = new Set(bindings.map(item => item.name));
  return uniforms.filter(uniform => bound.has(uniform.name) && isWritable(uniform)).map(({ name, type, owner, values }) => ({ name, type, owner, arity: values.length }));
}

const EMPTY_RUN: RunView = {
  phase: "idle", mode: null, message: null, attempt: null, problems: [], review: null,
  manualReady: false, paused: false,
};

type Version = { epoch: string; revision: number };
type Lease = {
  id: string;
  host: HostSession;
  version: Version | null;
  sampleSequence: number;
};
type Attempt = {
  identity: Omit<RunIdentity, "lease">;
  source: DraftSource;
  controls: DraftControls;
  title: string;
  mode: RunMode;
  restoring: boolean;
  rendering?: AcceptedRendering;
  language: DraftLanguage;
  hydra?: HydraProvenance;
  context: number;
  peer: Peer;
};
type WireComponent = { fixed: number } | { slot: string; low: number; high: number };
type WireBindings = Record<string, { type?: string; components: WireComponent[] }>;
type Prepared = RunIdentity & Version & {
  target: string;
  token: string;
  controlDigest: string;
  bindings: WireBindings;
  conflicts: MappingConflict[];
  backend?: DraftBackend;
} & Partial<TranslationIdentity>;
type Commit = { attempt: Attempt; lease: Lease; prepared: Prepared; id: string; terminal: boolean };
type ControlWrite = {
  id: string;
  lease: Lease;
  prior: ReflectedDraft;
  context: number;
  previousRun: RunView;
  apply: (state: ReflectedDraft) => void;
  terminal: boolean;
};
type Retirement = {
  lease: Lease;
  commit: Commit | null;
  control: ControlWrite | null;
  initialFence: Promise<boolean>;
  fenced: boolean;
  recovering: Promise<boolean> | null;
};
type ReceiptStatus = {
  state: string;
  response?: { id: string; ok: boolean; result?: unknown;
    error?: { code: string; message: string; diagnostics?: unknown } };
};

function samePeer(left: Peer | null, right: Peer | null) {
  return left !== null && right !== null && left.target === right.target && left.fingerprint === right.fingerprint;
}

function hostKey(peer: Peer) { return `${peer.target}:${peer.fingerprint}`; }

function signatureOf(controls: DraftControls, name: string) {
  return controls.signatures.find(signature => signature.name === name);
}

function encodeBindings(controls: DraftControls): WireBindings {
  const bindings: WireBindings = Object.create(null);
  for (const { name, component, binding } of controls.bindings) {
    const configured = bindings[name] ??= { type: signatureOf(controls, name)?.type, components: [] };
    configured.components[component] = binding.mode === "fixed" ? { fixed: binding.value } :
      { slot: binding.slot, low: binding.low, high: binding.high };
  }
  return bindings;
}

function decodeBindings(bindings: WireBindings, slots: Slot[]): ComponentBinding[] {
  return Object.entries(bindings).flatMap(([name, { components }]) =>
    components.map((component, index) => {
      const binding = parseBinding("fixed" in component ?
        { mode: "fixed", value: component.fixed } : { mode: "range", ...component }, slots);
      return { name, component: index, binding };
    }));
}

function identityMatches(expected: RunIdentity, actual: Partial<RunIdentity>) {
  return actual.lease === expected.lease && actual.documentId === expected.documentId &&
    actual.generation === expected.generation && actual.editSequence === expected.editSequence &&
    actual.runSequence === expected.runSequence && actual.sourceDigest === expected.sourceDigest;
}

function problemEntries(error: HostError, source: string): RunProblem[] {
  const lines = source.split("\n");
  const hasLineDirective = /^\s*#\s*line\b/m.test(source);
  const raw = error.diagnostics.length ? error.diagnostics : [error.message];
  return raw.flatMap((message, index) => message.split("\n").filter(Boolean).map(text => {
    const match = hasLineDirective ? null :
      text.match(/^\s*(?:ERROR|WARNING):\s*0:(\d+)(?::(\d+))?:/i) ??
      text.match(/^\s*0\((\d+)\)\s*:/) ??
      text.match(/^\s*(?:Error while parsing WGSL:\s*)?:?(\d+):(\d+)\s+error:/i);
    const structured = hasLineDirective ? undefined : error.diagnosticLocations[index];
    const line = structured?.line ?? Number(match?.[1]);
    const column = structured?.column ?? Number(match?.[2]);
    const location = line >= 1 && line <= lines.length ? { line,
      ...(column >= 1 && column <= lines[line - 1].length + 1 ? { column } : {}) } : {};
    return { code: error.code, message: text, ...location };
  }));
}

function describeConflict(issue: unknown): MappingConflict {
  const item = issue as { name?: string; reason?: MappingConflict["reason"]; actual_type?: string; default_values?: unknown };
  if (!item || typeof item.name !== "string" || !validUniformName(item.name)) {
    throw new HostError("state_mismatch", "The host returned an invalid mapping report.");
  }
  if (item.reason === "inactive") return { name: item.name, reason: item.reason, message: "Unavailable in candidate. No binding will be applied." };
  if (item.reason === "renderer_owned") return { name: item.name, reason: item.reason, message: "The renderer owns this uniform and will supply its value." };
  if (item.reason === "retyped") {
    const type = typeof item.actual_type === "string" && TYPE_ARITY.has(item.actual_type) ? item.actual_type : null;
    const values = Array.isArray(item.default_values) && item.default_values.every(Number.isFinite) ?
      item.default_values.join(", ") : null;
    return { name: item.name, reason: item.reason,
      message: type && values ? `Changed to ${type}. The candidate will use native defaults (${values}).` :
        "The uniform type changed. No binding will be applied." };
  }
  throw new HostError("state_mismatch", "The host returned an unknown mapping conflict.");
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
  private pendingReply: ControlWrite | null = null;
  private notice: string | null = null;
  private runView: RunView = EMPTY_RUN;
  private lastObservation: WorkspaceView["lastObservation"] = null;
  private context = 0;
  private runSequence = 0;
  private lease: Lease | null = null;
  private flight: Promise<void> | null = null;
  private queue: Attempt | null = null;
  private prepared: { attempt: Attempt; lease: Lease; candidate: Prepared } | null = null;
  private activeCommit: Commit | null = null;
  private unknownCommit: Commit | null = null;
  private retired = new Map<string, Retirement>();
  private retiringAttempt: Attempt | null = null;
  private sampleTask: Promise<void> | null = null;
  private samples = new SampleRelay((slot, value) => this.relaySample(slot, value));
  private callbackTimer: ReturnType<typeof setInterval> | null = null;
  private callbackError: string | null = null;

  private evaluation: AbortController | null = null;
  private hydraPendingEdit = 0;
  private cancelledRun = 0;

  constructor(readonly host: HostClient, private readonly evaluateHydra?: HydraEvaluator,
              private readonly evaluateCallback = evaluateSyntheticCallback) {}

  private cancelEvaluation() {
    if (this.evaluation || this.runView.phase === "evaluating") {
      this.evaluation?.abort();
      this.runView = EMPTY_RUN;
    }
  }

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
      destination: document.destination,
      pendingMappings: document.controls.pendingMappings ?? [],
      acceptedArtifact: accepted && {
        ...(accepted.rendering ? { backend: accepted.rendering.backend } : accepted.language === "wgsl" ?
          { backend: "webgpu" as const } : {}),
        id: accepted.id, sourceDigest: accepted.project.source.digest, revision: accepted.revision,
        saved: this.savedArtifact === accepted.id,
        ...(accepted.hydra ? { generated: { source: Buffer.from(accepted.project.source.base64, "base64").toString("utf8"),
          digest: accepted.project.source.digest } } : {}),
        stale: source.digest !== (accepted.hydra?.patch.digest ?? accepted.project.source.digest) ||
          !samePeer(document.destination, accepted.project.destination) || !!document.controls.pendingMappings?.length ||
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
    this.dropRun();
    this.lastObservation = null;
    this.document = document;
    this.documentGeneration += 1;
    this.editSequence = 0;
    this.hydraPendingEdit = 0;
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

  invalidateHydraEdit(value: unknown) {
    const edit = value as Partial<DocumentEdit> | null;
    if (!edit || this.document?.language !== "hydra-js" || edit.id !== this.document.id ||
        edit.generation !== this.documentGeneration || typeof edit.editSequence !== "number" ||
        !Number.isSafeInteger(edit.editSequence) || edit.editSequence <= this.editSequence) return false;
    this.hydraPendingEdit = Math.max(this.hydraPendingEdit, edit.editSequence);
    this.cancelEvaluation();
    return true;
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
    this.cancelEvaluation();
    return this.documentView()!;
  }

  create(language: DraftLanguage = "glsl") {
    return this.perform(async () => {
      const hydra = language === "hydra-js";
      const wgsl = language === "wgsl";
      this.setDocument(documentFromSource(sourceWithText({
        name: hydra ? "Untitled.js" : wgsl ? "Untitled.wgsl" : "Untitled.frag",
        origin: hydra ? "New Hydra patch" : wgsl ? "New WGSL fragment" : "New GLSL fragment", digest: "", base64: "",
      }, hydra ? HYDRA_TEMPLATE : wgsl ? WGSL_TEMPLATE : GLSL_TEMPLATE), language));
      return this.view();
    });
  }

  openSource(source: DraftSource, language: DraftLanguage = "glsl") {
    return this.perform(async () => {
      this.setDocument(documentFromSource(source, language));
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
    const callbackValue = this.document?.accepted?.callbackValue;
    return {
      run: this.runView, lastObservation: this.lastObservation, document: this.documentView(), documentGeneration: this.documentGeneration, peer: this.host.peer, reflected: this.state, project, previewReady: this.previewMatches(),
      drawnReceipt: this.previewMatches() ? this.acknowledged?.receipt ?? null : null,
      samplePort: this.samples.port, notice: this.notice,
      callback: callbackValue === undefined ? null : { value: callbackValue,
        active: Boolean(this.callbackTimer && this.previewMatches()), error: this.callbackError },
    };
  }

  private previewMatches() {
    const peer = this.host.peer;
    const ack = this.acknowledged;
    const accepted = this.document?.accepted;
    const backend = accepted?.rendering?.backend ?? (accepted?.language === "wgsl" ? "webgpu" : "opengl");
    return Boolean(peer && this.project && this.state && !this.state.needsRestore && ack &&
      this.project.destination.target === peer.target && this.project.destination.fingerprint === peer.fingerprint &&
      this.state.target === peer.target &&
      (backend === "webgpu" ? this.state.backend === "webgpu" : this.state.backend !== "webgpu") &&
      this.state.artifactDigest === (accepted?.rendering?.backend === "webgpu" ? accepted.rendering.artifactDigest : undefined) &&
      this.state.compilerIdentity === (accepted?.rendering?.backend === "webgpu" ? accepted.rendering.compilerIdentity : undefined) &&
      this.state.sourceDigest === this.project.source.digest &&
      this.state.epoch === ack.epoch && this.state.revision === ack.revision && this.state.lease === this.lease?.id &&
      !this.pendingReply && !this.unknownCommit);
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
    if (this.previewMatches() && Number.isSafeInteger(state.frame ?? state.receipt?.frame)) {
      this.lastObservation = { revision: state.revision, time: Date.now() };
    }
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
      const before = this.host.peer;
      const result = await this.host.checkPair();
      if (result.peer && !samePeer(before, result.peer)) this.dropRun();
      return { state: result.state, ...(result.peer ? { workspace: await this.linked() } : {}) };
    });
  }

  connectRemembered(fingerprint: unknown) {
    return this.perform(async () => {
      this.dropRun();
      await this.host.connectRemembered(fingerprint);
      return this.linked();
    });
  }

  disconnect() {
    return this.perform(async () => {
      this.dropRun();
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
    if (this.busy || this.flight || (this.callbackTimer && this.sampleTask)) return this.view();
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
    if (this.unknownCommit) {
      await this.resolveUnknownCommit(this.unknownCommit);
      this.drainRun();
      return;
    }
    if (this.pendingReply) {
      await this.resolveControlWrite(this.pendingReply);
      this.drainRun();
      return;
    }
    try {
      const blocked = this.retiringAttempt;
      if (blocked && this.current(blocked)) {
        const prior = this.retired.get(hostKey(blocked.peer));
        if (prior && !await this.settleRetirement(prior)) return;
        if (!accept() || !this.current(blocked)) return;
        if (prior) this.clearRetirement(blocked.peer, prior);
        this.retiringAttempt = null;
        this.setPhase(blocked, "idle", "The previous commit and lease are confirmed. Run this draft when ready.");
      }
      const raw = await this.host.rpc("reflect", {});
      if (!accept()) return;
      const next = this.receive(raw);
      const lease = this.lease;
      if (lease?.version && next.epoch !== lease.version.epoch) {
        const canRun = this.canRunAfterRestart(next, lease);
        this.requireRestoreAfterRestart();
        if (canRun) this.offerRunAfterRestart();
      }
      if (this.project && !this.previewMatches()) {
        this.notice ??= next.needsRestore ? "The host needs this draft to be restored." :
          "Preview differs from this workspace. Restore the draft to continue.";
      }
    } catch (error) {
      if (!accept()) return;
      if (error instanceof HostError && error.code === "busy") return;
      if (this.callbackTimer) this.pauseCallback("The render host could not be reached. Restore the preview to restart the callback.");
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
      this.drainRun();
    }
  }

  private checkedPreview() {
    if (!this.previewMatches() || !this.project || !this.state) {
      throw new HostError("preview_unavailable", "Restore this draft to its paired host before making changes.");
    }
    return { project: this.project, state: this.state };
  }

  private adopt(state: ReflectedDraft, project: DraftProject, pendingMappings?: PendingMapping[],
                workingOverride?: DraftControls,
                run?: { language: DraftLanguage; hydra?: HydraProvenance; rendering?: AcceptedRendering }) {
    const callbackValue = callbackReadback(state);
    if (!state.receipt) throw new HostError("missing_drawn_receipt", "The host did not acknowledge a drawn frame.");
    this.acknowledged = { epoch: state.epoch, revision: state.revision, receipt: state.receipt };
    this.lastObservation = { revision: state.revision, time: Date.now() };
    if (this.lease) this.lease.version = state;
    if (!this.document) throw new HostError("no_draft", "Open or create a VOSC draft first.");
    const signatures = controlSignatures(state.uniforms, project.bindings);
    const { name, origin, digest, base64 } = project.source;
    const acceptedProject = { ...project, source: { name, origin, digest, base64 } };
    const accepted = this.document.accepted;
    const { language, hydra, rendering } = run ?? accepted ?? {};
    this.document = { ...this.document, version: rendering ? 3 : 2,
      destination: this.document.destination ?? project.destination,
      controls: workingOverride ?? { slots: project.slots, bindings: project.bindings, signatures,
        ...(pendingMappings?.length || this.document.controls.pendingMappings ?
          { pendingMappings: pendingMappings ?? this.document.controls.pendingMappings } : {}) },
      accepted: { id: randomUUID(), revision: state.revision, project: acceptedProject, signatures,
        ...(language === "wgsl" ? { language } : {}), ...(hydra ? { hydra } : {}),
        ...(rendering ? { rendering } : {}),
        ...(callbackValue === undefined ? {} : { callbackValue }) } };
    this.notice = null;
    return this.view();
  }

  private async change(method: "slot.configure" | "uniform.set", params: Record<string, unknown>,
                       apply: ControlWrite["apply"]) {
    const prior = this.state;
    const lease = this.lease;
    if (!prior || !lease) throw new HostError("host_unavailable", "Connect to a paired render host.");
    const write = { id: randomUUID(), lease, prior, context: this.context, previousRun: this.runView, apply, terminal: false };
    this.pendingReply = write;
    let response: unknown;
    try {
      response = await lease.host.rpc(method, params, prior, write.id);
    } catch (error) {
      if (!this.uncertainWrite(error)) {
        this.rejectControl(write, error);
        throw error;
      }
      await this.resolveControlWrite(write);
      return this.view();
    }
    try {
      this.confirmControl(write, response);
    } catch {
      await this.resolveControlWrite(write);
    }
    return this.view();
  }

  private uncertainWrite(error: unknown) {
    return !(error instanceof HostError) || !!error.uncertainId || error.code === "renderer_uncertain";
  }

  private currentControl(write: ControlWrite) {
    return this.context === write.context && this.lease === write.lease && samePeer(this.host.peer, write.lease.host.peer);
  }

  private finishControl(write: ControlWrite) {
    write.terminal = true;
    if (this.pendingReply === write && this.currentControl(write)) {
      this.pendingReply = null;
      if (this.runView.phase === "uncertain" || this.runView.phase === "reconciling") this.runView = write.previousRun;
    }
  }

  private rejectControl(write: ControlWrite, error: unknown) {
    this.finishControl(write);
    if (!this.currentControl(write) || !(error instanceof HostError)) return;
    if (error.code === "stale_epoch" || error.code === "draft_state_lost") {
      this.requireRestoreAfterRestart();
    }
    this.notice = error.message;
  }

  private confirmControl(write: ControlWrite, raw: unknown) {
    const state = raw as ReflectedDraft | null;
    const receipt = state?.receipt;
    if (!state || state.target !== write.prior.target || state.lease !== write.lease.id || state.epoch !== write.prior.epoch ||
        state.sourceDigest !== write.prior.sourceDigest || state.commitID !== write.prior.commitID || state.needsRestore ||
        !Number.isSafeInteger(state.revision) || state.revision <= write.prior.revision || receipt?.id !== write.id ||
        receipt.status !== "rendered" || receipt.revision !== state.revision || !Number.isSafeInteger(receipt.frame) ||
        !Number.isSafeInteger(state.frame) || state.frame! < receipt.frame) {
      throw new HostError("missing_drawn_receipt", "A matching native control receipt was not received.");
    }
    if (!this.currentControl(write)) { write.terminal = true; return; }
    const observed = this.receive(state, true);
    write.apply(observed);
    this.finishControl(write);
  }

  private async resolveControlWrite(write: ControlWrite) {
    if (write.terminal) return;
    if (this.currentControl(write)) {
      this.runView = { ...EMPTY_RUN, phase: "reconciling", paused: true, message: "Checking the native control receipt." };
    }
    try {
      const terminal = await this.lookup(write.lease.host, write.id);
      if (terminal?.error && !this.uncertainWrite(terminal.error)) {
        this.rejectControl(write, terminal.error);
        return;
      }
      if (terminal && "result" in terminal) {
        this.confirmControl(write, terminal.result);
        return;
      }
    } catch { /* A matching native readback can still resolve the write. */ }
    try {
      const reflected = await write.lease.host.recover("reflect", {}) as ReflectedDraft | null;
      if (this.changedEpoch(reflected, write.prior)) {
        this.finishControl(write);
        if (this.currentControl(write)) this.requireRestoreAfterRestart();
        return;
      }
      this.confirmControl(write, reflected);
      return;
    } catch { /* Keep the accepted configuration until its successor is proven. */ }
    if (this.currentControl(write)) {
      this.runView = { ...EMPTY_RUN, phase: "uncertain", paused: true, message: "The native control result is unconfirmed. Check this host." };
    }
  }

  private current(attempt: Attempt) {
    return attempt.context === this.context && attempt.identity.generation === this.documentGeneration &&
      this.document?.id === attempt.identity.documentId && samePeer(attempt.peer, this.host.peer);
  }

  private dropRun() {
    this.cancelEvaluation();
    this.stopCallback();
    this.callbackError = null;
    this.context += 1;
    this.queue = null;
    this.prepared = null;
    const commit = this.unknownCommit ?? this.activeCommit;
    const control = this.pendingReply;
    this.pendingReply = null;
    this.activeCommit = null;
    this.unknownCommit = null;
    this.retiringAttempt = null;
    const lease = this.lease;
    this.lease = null;
    this.acknowledged = null;
    this.samples.stop();
    this.runView = EMPTY_RUN;
    const flight = this.flight;
    this.flight = null;
    if (lease) {
      // Fence immediately and retry after the in-flight operation returns.
      // Either order still requires proving that commit's separate outcome.
      const initialFence = this.fence(lease).then(async fenced => {
        if (!flight) return fenced;
        await flight;
        return this.fence(lease);
      });
      const prior = { lease, commit, control, initialFence, fenced: false, recovering: null };
      this.retired.set(hostKey(lease.host.peer), prior);
      void this.reconcileRetirement(prior).then(settled => {
        if (settled) this.clearRetirement(lease.host.peer, prior);
      });
    }
  }

  private async fence(lease: Lease) {
    if (!lease.version) return true;
    try {
      const result = await lease.host.recover("run.fence", { lease: lease.id }, lease.version) as
        { target?: string; epoch?: string; lease?: string };
      return result.target === lease.host.peer.target && result.epoch === lease.version.epoch && result.lease === lease.id;
    } catch (error) {
      return error instanceof HostError && ["stale_epoch", "stale_lease"].includes(error.code);
    }
  }

  private clearRetirement(peer: Peer, prior: Retirement) {
    const key = hostKey(peer);
    if (this.retired.get(key) === prior) this.retired.delete(key);
  }

  private reconcileRetirement(prior: Retirement) {
    if (prior.recovering) return prior.recovering;
    const recovering = (async () => {
      if (!prior.fenced && (await prior.initialFence || await this.fence(prior.lease))) prior.fenced = true;
      if (prior.commit && !prior.commit.terminal) await this.resolveUnknownCommit(prior.commit);
      if (prior.control && !prior.control.terminal) await this.resolveControlWrite(prior.control);
      return prior.fenced && (!prior.commit || prior.commit.terminal) && (!prior.control || prior.control.terminal);
    })().finally(() => { prior.recovering = null; });
    prior.recovering = recovering;
    return recovering;
  }

  private async settleRetirement(prior: Retirement) {
    const version = prior.lease.version;
    const peer = prior.lease.host.peer;
    if (version && samePeer(peer, this.host.peer)) {
      try {
        // A restarted service may use a new port. Only a currently selected
        // authenticated session for the same target and certificate can
        // prove that this retired epoch and all its writes are gone.
        const selected = this.host.session();
        if (samePeer(peer, selected.peer)) {
          const observed = await selected.rpc("reflect", {}) as ReflectedDraft | null;
          const current = this.host.peer;
          if (samePeer(peer, current) && current?.endpoint.host === selected.peer.endpoint.host &&
              current.endpoint.port === selected.peer.endpoint.port &&
              this.changedEpoch(observed, { target: peer.target, epoch: version.epoch })) {
            prior.fenced = true;
            if (prior.commit) prior.commit.terminal = true;
            if (prior.control) prior.control.terminal = true;
            return true;
          }
        }
      } catch { /* Recover against the original pinned session when fresh proof is unavailable. */ }
    }
    return this.reconcileRetirement(prior);
  }

  private async settleFence(attempt: Attempt) {
    const prior = this.retired.get(hostKey(attempt.peer));
    if (!prior) return true;
    if (!await this.settleRetirement(prior)) {
      if (this.current(attempt)) {
        this.retiringAttempt = attempt;
        this.setPhase(attempt, "uncertain", "Checking the previous draft’s commit and lease on this host.");
      }
      return false;
    }
    this.clearRetirement(attempt.peer, prior);
    return true;
  }

  private setPhase(attempt: Attempt, phase: RunView["phase"], message: string | null,
                   problems: RunProblem[] = [], review: RunView["review"] = null) {
    if (!this.current(attempt)) return;
    this.runView = { ...this.runView, phase, message, mode: attempt.mode, problems, review,
      attempt: { source: editorText(attempt.hydra?.patch ?? attempt.source),
        sourceDigest: attempt.hydra?.patch.digest ?? attempt.source.digest,
        editSequence: attempt.identity.editSequence, runSequence: attempt.identity.runSequence,
        ...(attempt.hydra ? { generated: { source: Buffer.from(attempt.source.base64, "base64").toString("utf8"), digest: attempt.source.digest } } : {}) },
      paused: ["mapping_review", "reconciling", "uncertain", "restore_required"].includes(phase),
    };
  }

  private requireCurrent(attempt: Attempt) {
    if (!this.current(attempt)) throw new HostError("stale_document", "The preview context changed.");
  }

  private freshHydraRun(attempt: Attempt) {
    return this.current(attempt) && attempt.identity.editSequence === this.editSequence &&
      attempt.identity.editSequence >= this.hydraPendingEdit && attempt.identity.runSequence === this.runSequence &&
      attempt.peer.endpoint.host === this.host.peer?.endpoint.host &&
      attempt.peer.endpoint.port === this.host.peer?.endpoint.port;
  }

  private requirePreAdmission(attempt: Attempt) {
    this.requireCurrent(attempt);
    if (this.cancelledBeforeCommit(attempt)) {
      throw new HostError("run_cancelled", "Run cancelled. Previous preview retained.");
    }
    if (attempt.language === "hydra-js" && !attempt.restoring && !this.freshHydraRun(attempt)) {
      throw new HostError("hydra_stale", "The Hydra patch changed. Run the current source explicitly.");
    }
  }

  private cancelledBeforeCommit(attempt: Attempt, prepared?: Prepared) {
    return attempt.identity.runSequence <= this.cancelledRun ||
      ((attempt.language === "wgsl" || prepared?.backend === "webgpu") &&
        !!this.queue && this.queue.identity.runSequence > attempt.identity.runSequence);
  }

  private captureRun(mode: RunMode, restoring = false): Attempt {
    const document = this.document;
    const peer = this.host.peer;
    if (!document || !peer) throw new HostError("host_unavailable", "Open a draft and connect to a paired render host.");
    if (document.destination && !samePeer(document.destination, peer)) {
      throw new HostError("wrong_host", "Choose Use this host before running on a new destination.");
    }
    const artifact = restoring ? document.accepted : null;
    if (restoring && (!artifact || !samePeer(artifact.project.destination, peer))) {
      throw new HostError("wrong_host", "Connect to the accepted draft’s paired host before restoring.");
    }
    const source = artifact?.project.source ?? document.source;
    const controls = artifact ? { slots: artifact.project.slots, bindings: artifact.project.bindings,
      signatures: artifact.signatures } : document.controls;
    return { identity: { documentId: document.id, generation: this.documentGeneration,
      editSequence: this.editSequence, runSequence: ++this.runSequence, sourceDigest: source.digest },
      source, controls, title: artifact?.project.title ?? document.title, mode, restoring,
      language: artifact?.language ?? document.language, hydra: artifact?.hydra, rendering: artifact?.rendering,
      context: this.context, peer };
  }

  run(mode: unknown = "manual") {
    if (mode !== "manual" && mode !== "automatic") throw new HostError("invalid_request", "Choose a Run mode.");
    if (mode === "automatic" && (this.document?.language !== "glsl" || !this.runView.manualReady || this.runView.paused)) return Promise.resolve(this.view());
    if (this.runView.phase === "restore_required" && this.project && samePeer(this.host.peer, this.project.destination)) {
      throw new HostError("restore_required", "Restore the last accepted artifact before running here again.");
    }
    const attempt = this.captureRun(mode);
    if (attempt.language === "hydra-js") this.cancelEvaluation();
    return this.submit(attempt);
  }

  private submit(attempt: Attempt) {
    if (this.unknownCommit || this.pendingReply) {
      throw new HostError("restore_required", "Check the previous native request before running again.");
    }
    if (this.flight || this.busy) {
      if (this.queue?.mode !== "manual" || attempt.mode === "manual") this.queue = attempt;
      return Promise.resolve(this.view());
    }
    return this.launch(() => this.prepareAndMaybeCommit(attempt));
  }

  private async launch(operation: () => Promise<void>): Promise<WorkspaceView> {
    this.generation += 1;
    const flight = Promise.resolve().then(operation);
    this.flight = flight;
    try { await flight; }
    finally {
      if (this.flight === flight) {
        this.flight = null;
        this.drainRun();
      }
    }
    return this.view();
  }

  private drainRun() {
    if (this.flight || this.busy || this.unknownCommit || !this.queue || (this.prepared && this.queue.mode === "automatic")) return;
    const next = this.queue;
    this.queue = null;
    if (this.current(next) && (next.mode === "manual" || !this.runView.paused)) {
      void this.launch(() => this.prepareAndMaybeCommit(next));
    }
  }

  private async lookup(host: HostSession, id: string) {
    const deadline = Date.now() + 2400;
    let value: ReceiptStatus;
    do {
      value = await host.recover("receipt.get", { id }) as ReceiptStatus;
      if (value?.state !== "pending" || Date.now() >= deadline) break;
      await delay(180);
    } while (true);
    if (value?.state !== "completed" || value.response?.id !== id) return null;
    if (value.response.ok === true) return { result: value.response.result };
    if (value.response.ok === false && value.response.error) return { error: reportedError(value.response.error) };
    return null;
  }

  private async prepareAndMaybeCommit(attempt: Attempt) {
    try {
      this.requirePreAdmission(attempt);
      if (!attempt.source.base64) throw new HostError("empty_source", "Enter source before running.");
      if (attempt.language === "hydra-js" && !attempt.restoring) {
        if (!this.evaluateHydra) throw new HostError("hydra_unavailable", "The isolated Hydra evaluator is unavailable.");
        const job = new AbortController();
        this.evaluation = job;
        this.setPhase(attempt, "evaluating", "Evaluating the Hydra patch.");
        try {
          const patch = attempt.source;
          const patchText = new TextDecoder().decode(Buffer.from(patch.base64, "base64"));
          const { fragment, spans } = await this.evaluateHydra(patchText, job.signal);
          if (job.signal.aborted || !this.freshHydraRun(attempt)) return;
          const source = sourceWithText({ name: "Generated Hydra GLSL", origin: patch.name, digest: "", base64: "" }, fragment);
          attempt = { ...attempt, source, identity: { ...attempt.identity, sourceDigest: source.digest },
            hydra: { version: 2, emitterVersion: HYDRA_EMITTER_VERSION, compilerId: TRIODE_COMPILER_ID,
              patch, generatedDigest: source.digest, spans } };
        } catch (error) {
          if (job.signal.aborted || !this.freshHydraRun(attempt)) return;
          throw error;
        } finally {
          if (this.evaluation === job) this.evaluation = null;
        }
      }
      const compiling = `Compiling ${attempt.language === "wgsl" ? "WGSL" : "GLSL"} on the render host.`;
      this.setPhase(attempt, "preparing", compiling);
      await this.observation;
      await this.sampleTask;
      this.requirePreAdmission(attempt);
      await this.cancelPrepared();
      this.requirePreAdmission(attempt);
      if (!await this.settleFence(attempt)) return;
      if (this.current(attempt)) this.retiringAttempt = null;
      this.requirePreAdmission(attempt);
      let lease = this.lease ?? { id: randomUUID(), host: this.host.session(), version: null, sampleSequence: 0 };
      const raw = await lease.host.rpc("reflect", {});
      this.requirePreAdmission(attempt);
      const state = this.receive(raw);
      if (lease.version && lease.version.epoch !== state.epoch) {
        this.lease = null;
        this.acknowledged = null;
        this.samples.stop();
        if (!this.canRunAfterRestart(state, lease)) {
          throw new HostError("stale_epoch", "The renderer restarted. Restore the accepted artifact first.");
        }
        this.stopCallback();
        if (this.queue?.mode === "automatic") this.queue = null;
        this.offerRunAfterRestart();
        if (attempt.mode === "automatic") return;
        this.setPhase(attempt, "preparing", compiling);
        lease = { id: randomUUID(), host: lease.host, version: null, sampleSequence: 0 };
      }
      if (!lease.version && attempt.mode === "manual" && state.lease && state.lease !== lease.id) {
        const fenced = await this.fence({ id: state.lease, host: lease.host, version: state, sampleSequence: 0 });
        this.requirePreAdmission(attempt);
        if (!fenced) throw new HostError("lease_owned", "Another controller may own the prior preview lease.");
      }
      lease.version = state;
      this.lease = lease;
      const id = randomUUID();
      const identity = { ...attempt.identity, lease: lease.id };
      const intent = encodeBindings(attempt.controls);
      const { sourceDigest, ...wireAttempt } = identity;
      const wgsl = attempt.language === "wgsl";
      const rendering = attempt.rendering;
      const translation = rendering?.backend === "webgpu" && !wgsl ? rendering : undefined;
      if (translation && state.webgpuAuto !== true) {
        throw new HostError("translation_unavailable", "This Render Host cannot restore the accepted translation. Run the source again.");
      }
      const backend = state.webgpuAuto !== true ? undefined : attempt.restoring ?
        rendering?.backend ?? (wgsl ? "webgpu" : "opengl") : wgsl ? undefined : "auto";
      let result: unknown;
      try {
        result = await lease.host.rpc("run.prepare", { ...wireAttempt, source: attempt.source.base64,
          sha256: sourceDigest, slots: attempt.controls.slots, bindings: intent,
          ...(wgsl ? { language: "wgsl" } : {}), ...(backend ? { backend } : {}),
          ...(translation ? { artifactDigest: translation.artifactDigest, compilerIdentity: translation.compilerIdentity } : {}),
        }, state, id);
      } catch (error) {
        if (!(error instanceof HostError) || !error.uncertainId) throw error;
        const receipt = await this.lookup(lease.host, id);
        if (receipt?.error) throw receipt.error;
        if (!receipt) throw new HostError("prepare_unconfirmed", "The preparation was not confirmed. Run again after the host responds.");
        result = receipt.result;
      }
      this.requireCurrent(attempt);
      const candidate = this.preparedResult(result, identity, state, id, intent, attempt, backend);
      if (this.cancelledBeforeCommit(attempt, candidate) ||
          (attempt.language === "hydra-js" && !attempt.restoring && !this.freshHydraRun(attempt))) {
        await lease.host.rpc("run.cancel", { lease: lease.id, token: id }, candidate);
        if (this.cancelledBeforeCommit(attempt, candidate)) return;
        this.requirePreAdmission(attempt);
      }
      if (candidate.conflicts.length) {
        if (attempt.restoring) {
          await lease.host.rpc("run.cancel", { lease: lease.id, token: id }, candidate);
          throw new HostError("restore_required", "The accepted artifact’s controls no longer match native reflection.");
        }
        this.prepared = { attempt, lease, candidate };
        this.setPhase(attempt, "mapping_review", "Review the candidate’s control changes before applying it.", [],
          { token: id, conflicts: candidate.conflicts });
        return;
      }
      await this.commit(attempt, lease, candidate);
    } catch (error) {
      if ((error instanceof HostError && error.code === "run_cancelled") ||
          this.cancelledBeforeCommit(attempt)) return;
      this.failAttempt(attempt, error);
    }
  }

  private preparedResult(raw: unknown, identity: RunIdentity, state: ReflectedDraft, token: string,
                         intent: WireBindings, attempt: Attempt, preference?: "auto" | "opengl" | "webgpu"): Prepared {
    const result = raw as Prepared | null;
    if (!result || !identityMatches(identity, result) || result.target !== state.target || result.epoch !== state.epoch ||
        result.revision !== state.revision || result.token !== token || !/^[a-f0-9]{64}$/.test(result.controlDigest) ||
        !result.bindings || typeof result.bindings !== "object" || Array.isArray(result.bindings) || !Array.isArray(result.conflicts)) {
      throw new HostError("state_mismatch", "The prepared candidate does not match this Run.");
    }
    const wgsl = attempt.language === "wgsl";
    const translated = result.backend === "webgpu" && !wgsl;
    const requested = attempt.rendering;
    if ((result.backend !== undefined && (result.backend !== "opengl" && result.backend !== "webgpu" ||
          (wgsl && result.backend !== "webgpu") || (!preference && translated) ||
          (preference && preference !== "auto" && preference !== result.backend))) ||
        (translated ? !isDraftDigest(result.artifactDigest) || !isDraftDigest(result.compilerIdentity) :
          result.artifactDigest !== undefined || result.compilerIdentity !== undefined) ||
        (requested?.backend === "webgpu" && !wgsl && (!translated || requested.artifactDigest !== result.artifactDigest ||
          requested.compilerIdentity !== result.compilerIdentity))) {
      throw new HostError("state_mismatch", "The prepared backend or translation does not match this Run.");
    }
    const conflicts = (result.conflicts as unknown[]).map(describeConflict);
    const names = new Set(conflicts.map(item => item.name));
    if (names.size !== conflicts.length || Object.keys(result.bindings).length + names.size !== Object.keys(intent).length ||
        Object.keys(intent).some(name => {
          const returned = Object.hasOwn(result.bindings, name) ? result.bindings[name] : undefined;
          if (names.has(name)) return Boolean(returned);
          if (!returned) return true;
          const requested = intent[name].components;
          const expectedType = ["float", "vec2", "vec3", "vec4"][requested.length - 1];
          if (returned.type !== expectedType || !Array.isArray(returned.components)) return true;
          const effective = returned.components.map(component => "fixed" in component ?
            { fixed: component.fixed } : { slot: component.slot, low: component.low, high: component.high });
          return JSON.stringify(effective) !== JSON.stringify(requested);
        })) {
      throw new HostError("state_mismatch", "The effective candidate bindings do not match the requested controls.");
    }
    return { ...result, conflicts };
  }

  private async commit(attempt: Attempt, lease: Lease, prepared: Prepared) {
    this.requireCurrent(attempt);
    const commit = { attempt, lease, prepared, id: randomUUID(), terminal: false };
    this.activeCommit = commit;
    this.setPhase(attempt, "committing", "Waiting for the candidate’s first native draw.");
    try {
      let raw: unknown;
      try {
        raw = await lease.host.rpc("run.commit", { lease: lease.id, token: prepared.token }, prepared, commit.id);
      } catch (error) {
        if (!this.uncertainWrite(error)) {
          commit.terminal = true;
          throw error;
        }
        if (this.current(attempt)) {
          this.unknownCommit = commit;
          this.setPhase(attempt, "reconciling", "Checking the same host for the native draw receipt.");
        }
        await this.resolveUnknownCommit(commit);
        return;
      }
      try {
        this.confirmCommit(commit, raw);
      } catch {
        if (this.current(attempt)) this.unknownCommit = commit;
        await this.resolveUnknownCommit(commit);
      }
    } finally {
      if (this.activeCommit === commit) this.activeCommit = null;
    }
  }

  private confirmCommit(commit: Commit, raw: unknown) {
    const { attempt, prepared, id } = commit;
    const state = raw as ReflectedDraft | null;
    const receipt = state?.receipt;
    const scene = receipt?.result?.scene;
    const layers = scene?.layers as { shader?: { loaded?: boolean; program?: number } }[] | undefined;
    const shader = Array.isArray(layers) ? layers[0]?.shader : undefined;
    const webgpu = prepared.backend === "webgpu" || attempt.language === "wgsl";
    const activation = scene?.draft_activation as Record<string, unknown> | undefined;
    const matchingBackend = webgpu ?
      state?.backend === "webgpu" && scene?.backend === "webgpu" && receipt?.result?.backend === "webgpu" &&
        receipt.result.first_frame === receipt.frame && scene.draft_device_lost !== true :
      state?.backend !== "webgpu" && shader?.loaded === true && Number.isSafeInteger(shader.program) && shader.program! > 0;
    const matchesRendering = (row?: Record<string, unknown>) => row?.backend === prepared.backend &&
      row?.artifact_digest === prepared.artifactDigest && row?.compiler_identity === prepared.compilerIdentity &&
      row?.first_frame === receipt?.frame;
    const matchingNegotiated = !prepared.backend || (state?.backend === prepared.backend && scene?.backend === prepared.backend &&
      state.artifactDigest === prepared.artifactDigest && state.compilerIdentity === prepared.compilerIdentity &&
      matchesRendering(receipt?.result) && matchesRendering(activation) && activation?.lease === prepared.lease &&
      activation.token === prepared.token && activation.commit_id === id && activation.source_digest === prepared.sourceDigest &&
      activation.control_digest === prepared.controlDigest);
    if (!matchingBackend || !matchingNegotiated || !state || state.target !== prepared.target || state.epoch !== prepared.epoch ||
        !identityMatches(prepared, state) || state.token !== prepared.token || state.controlDigest !== prepared.controlDigest ||
        state.commitID !== id || receipt?.id !== id || receipt.status !== "rendered" || !scene || state.needsRestore ||
        !Number.isSafeInteger(receipt.frame) || !Number.isSafeInteger(state.revision) || state.revision <= prepared.revision ||
        receipt.revision !== state.revision || scene.rendered_revision !== state.revision ||
        scene.frame !== receipt.frame || state.frame !== receipt.frame ||
        receipt.result?.lease !== prepared.lease || receipt.result?.token !== prepared.token ||
        receipt.result?.commit_id !== id || receipt.result?.source_digest !== prepared.sourceDigest ||
        receipt.result?.control_digest !== prepared.controlDigest) {
      throw new HostError("missing_drawn_receipt", "A matching native first-frame receipt was not received.");
    }
    if (!this.current(attempt)) { commit.terminal = true; return; }
    const bindings = decodeBindings(prepared.bindings, attempt.controls.slots);
    const project = parseProject({ format: "vosc.authoring.draft", version: 1, title: attempt.title,
      destination: attempt.peer, source: attempt.source, slots: state.slots, bindings });
    const pending = [...attempt.controls.pendingMappings ?? []];
    for (const { name, reason } of prepared.conflicts) {
      if (pending.some(item => item.name === name)) continue;
      pending.push({ name, reason, bindings: attempt.controls.bindings.filter(item => item.name === name),
        signature: signatureOf(attempt.controls, name) });
    }
    const observed = this.receive(state, true);
    this.unknownCommit = null;
    const rendering: AcceptedRendering | undefined = prepared.backend === "opengl" ? { backend: "opengl" } :
      prepared.backend === "webgpu" ? prepared.artifactDigest && prepared.compilerIdentity ?
        { backend: "webgpu", artifactDigest: prepared.artifactDigest, compilerIdentity: prepared.compilerIdentity } :
        { backend: "webgpu" } : attempt.rendering;
    this.adopt(observed, project, pending, attempt.restoring ? this.document?.controls : undefined,
      { language: attempt.language, hydra: attempt.hydra, rendering });
    this.startCallback(observed);
    commit.terminal = true;
    if (attempt.mode === "manual" && !attempt.restoring && attempt.language === "glsl") this.runView = { ...this.runView, manualReady: true };
    this.setPhase(attempt, "drawn", `${state.backend === "webgpu" ? "WebGPU" : "Native"} revision ${state.revision} drew frame ${receipt.frame}.`);
  }

  private async resolveUnknownCommit(commit: Commit) {
    if (commit.terminal) return;
    let terminal: Awaited<ReturnType<Workspace["lookup"]>> = null;
    try { terminal = await this.lookup(commit.lease.host, commit.id); }
    catch { /* A successful readback can still prove the same commit. */ }
    if (terminal?.error && !this.uncertainWrite(terminal.error)) {
      commit.terminal = true;
      if (this.current(commit.attempt)) {
        this.unknownCommit = null;
        this.failAttempt(commit.attempt, terminal.error);
      }
      return;
    }
    if (terminal && "result" in terminal) {
      try { this.confirmCommit(commit, terminal.result); return; }
      catch { /* Verify a fresh matching draw before trusting a malformed receipt. */ }
    }
    try {
      const reflected = await commit.lease.host.recover("reflect", {}) as ReflectedDraft | null;
      if (this.changedEpoch(reflected, commit.prepared)) {
        commit.terminal = true;
        if (this.current(commit.attempt)) {
          this.unknownCommit = null;
          this.lease = null;
          this.failAttempt(commit.attempt, new HostError("stale_epoch", "The renderer restarted."));
        }
        return;
      }
      this.confirmCommit(commit, reflected);
      return;
    } catch { /* Keep the transaction fenced from further writes until it is proven. */ }
    this.setPhase(commit.attempt, "uncertain", "The native outcome is unconfirmed. Check this host before retrying.");
  }

  private changedEpoch(state: ReflectedDraft | null, prior: { target: string; epoch: string }) {
    return state?.target === prior.target && typeof state.epoch === "string" && state.epoch.length > 0 &&
      state.epoch !== prior.epoch && Number.isSafeInteger(state.revision) && state.revision >= 0 &&
      typeof state.needsRestore === "boolean" && Array.isArray(state.uniforms) && Array.isArray(state.slots);
  }

  private canRunAfterRestart(state: ReflectedDraft, lease: Lease) {
    const accepted = this.document?.accepted;
    const peer = lease.host.peer;
    return !this.unknownCommit && !this.pendingReply && state.needsRestore && !state.lease && !!accepted &&
      (accepted.rendering?.backend === "webgpu" || accepted.language === "wgsl") &&
      samePeer(this.host.peer, peer) && samePeer(accepted.project.destination, peer) &&
      !!lease.version && this.changedEpoch(state, { target: peer.target, epoch: lease.version.epoch });
  }

  private offerRunAfterRestart() {
    const message = "The renderer restarted. Run the working draft or restore the last accepted preview.";
    this.runView = { ...EMPTY_RUN, paused: true, message };
    this.notice = message;
  }

  private requireRestoreAfterRestart() {
    this.lease = null;
    this.dropRun();
    this.runView = { ...EMPTY_RUN, phase: "restore_required", paused: true,
      message: "The private renderer restarted. Restore the accepted artifact explicitly." };
  }

  private failAttempt(attempt: Attempt, error: unknown) {
    if (!this.current(attempt)) return;
    const failure = error instanceof HostError ? error : new HostError("run_failed", "The native run failed.");
    const phase = ["stale_epoch", "restore_required", "stale_lease", "stale_revision", "draft_state_lost"].includes(failure.code) ?
      "restore_required" : "failed";
    if (phase === "restore_required") {
      this.acknowledged = null;
      this.samples.stop();
      this.runView = { ...this.runView, manualReady: false };
    }
    let problems = failure.sourceLocation ? [{ code: failure.code, message: failure.message, ...failure.sourceLocation }] :
      problemEntries(failure, editorText(attempt.source));
    if (attempt.hydra) {
      problems = problems.map(({ line, column: _column, ...problem }) => {
        const span = line === undefined ? undefined :
          attempt.hydra?.spans.find(entry => line >= entry.firstLine && line <= entry.lastLine);
        return { ...problem, ...(line === undefined ? {} : { generatedLine: line }), ...span?.location };
      });
    }
    this.setPhase(attempt, phase, failure.message.split("\n")[0], problems);
  }

  private async cancelPrepared() {
    const prepared = this.prepared;
    if (!prepared) return;
    this.prepared = null;
    await prepared.lease.host.rpc("run.cancel", { lease: prepared.lease.id, token: prepared.candidate.token },
      prepared.candidate);
  }

  approveRun(token: unknown) {
    const staged = this.prepared;
    if (!staged || staged.candidate.token !== token || this.busy || this.flight || this.pendingReply || this.unknownCommit) {
      throw new HostError("stale_candidate", "The prepared mapping review is no longer available.");
    }
    return this.launch(async () => {
      try {
        await this.sampleTask;
        if (this.prepared !== staged) return;
        this.prepared = null;
        this.requireCurrent(staged.attempt);
        if (staged.attempt.identity.editSequence !== this.editSequence ||
            (staged.attempt.hydra && !this.freshHydraRun(staged.attempt))) {
          await staged.lease.host.rpc("run.cancel", { lease: staged.lease.id, token }, staged.candidate);
          if (staged.attempt.hydra) {
            this.setPhase(staged.attempt, "idle", "The Hydra patch changed. Run the current source explicitly.");
          } else await this.prepareAndMaybeCommit(this.captureRun("manual"));
          return;
        }
        await this.commit(staged.attempt, staged.lease, staged.candidate);
      } catch (error) { this.failAttempt(staged.attempt, error); }
    });
  }

  cancelRun(token?: unknown, automaticOnly = false) {
    if (this.queue?.mode === "automatic") this.queue = null;
    if (token === undefined) {
      if ((!automaticOnly || this.runView.mode === "automatic") &&
          this.document && (this.document.language !== "glsl" || this.state?.webgpuAuto === true)) {
        if (this.queue?.language === this.document.language) this.queue = null;
        if (this.evaluation || this.runView.phase === "evaluating" || this.runView.phase === "preparing") {
          this.cancelledRun = this.runSequence;
          this.cancelEvaluation();
          this.runView = { ...EMPTY_RUN, manualReady: this.runView.manualReady,
            message: "Run cancelled. Previous preview retained." };
        }
      }
      return Promise.resolve(this.view());
    }
    const staged = this.prepared;
    if (staged?.candidate.token !== token || this.flight || this.busy || this.pendingReply || this.unknownCommit) {
      throw new HostError("stale_candidate", "The prepared mapping review is no longer available.");
    }
    return this.launch(async () => {
      try { await this.sampleTask; await this.cancelPrepared(); }
      catch (error) { if (staged) this.failAttempt(staged.attempt, error); return; }
      if (staged) this.setPhase(staged.attempt, "idle", "Candidate cancelled. Previous preview retained.");
    });
  }

  useSelectedHost() {
    const document = this.document;
    const peer = this.host.peer;
    if (!document || !peer) throw new HostError("host_unavailable", "Connect to a paired host first.");
    if (!samePeer(document.destination, peer)) {
      this.dropRun();
      this.document = { ...document, destination: peer };
      this.notice = "Destination selected. Run when ready.";
    }
    return this.view();
  }

  discardMapping(name: unknown) {
    return this.resolveMapping(name);
  }

  remapMapping(name: unknown, target: unknown) {
    if (typeof target !== "string") throw new HostError("invalid_uniform", "Choose a target uniform.");
    return this.resolveMapping(name, target);
  }

  private resolveMapping(name: unknown, target?: string) {
    const document = this.document;
    const pending = document?.controls.pendingMappings ?? [];
    const mapping = pending.find(item => item.name === name);
    if (!document || !mapping || this.flight || this.busy || this.prepared || this.unknownCommit || this.pendingReply) {
      throw new HostError("invalid_mapping", "Choose an inactive mapping after the current Run.");
    }
    let bindings = document.controls.bindings;
    if (target !== undefined) {
      const uniform = this.state?.uniforms.find(item => item.name === target);
      if (!this.previewMatches() || !uniform || !isWritable(uniform) || uniform.values.length !== mapping.bindings.length ||
          (mapping.signature && mapping.signature.type !== uniform.type)) {
        throw new HostError("invalid_uniform", "Choose an active uniform of the same type and size.");
      }
      bindings = [...bindings.filter(binding => binding.name !== target),
        ...mapping.bindings.map(binding => ({ ...binding, name: target }))];
    }
    const signatures = target === undefined ? document.controls.signatures :
      [...document.controls.signatures.filter(item => item.name !== target),
        ...controlSignatures(this.state?.uniforms ?? [], bindings.filter(item => item.name === target))];
    this.document = { ...document, controls: { ...document.controls, bindings, signatures,
      pendingMappings: pending.filter(item => item !== mapping) } };
    if (target !== undefined) this.notice = "Mapping assigned. Run to apply it to the preview.";
    return this.view();
  }

  async activate(readSource: () => Promise<DraftSource>) {
    const initial = this.documentContext();
    const source = await readSource();
    const current = this.documentContext();
    if (initial.generation !== current.generation || initial.editSequence !== current.editSequence) {
      throw new HostError("stale_document", "Edits were kept. Preview the library source again.");
    }
    if (!this.host.peer) throw new HostError("host_unavailable", "Connect to a paired render host first.");
    await this.openSource(source);
    return this.run();
  }

  configureSlot(label: unknown) {
    return this.perform(async () => {
      this.requireControlIdle();
      await this.sampleTask;
      const { project } = this.checkedPreview();
      if (typeof label !== "string" || !label.trim() || label.includes("\u0000") || Buffer.byteLength(label.trim()) > 80 ||
          project.slots.length >= SLOT_LIMIT) {
        throw new HostError("invalid_slot", `Enter a slot name up to 80 bytes. A draft can have ${SLOT_LIMIT} slots.`);
      }
      const slot: Slot = { id: "s" + randomBytes(6).toString("hex"), label: label.trim(), default: 0 };
      return this.change("slot.configure", { ...slot }, result => {
        if (!result.slots.some(item => item.id === slot.id && item.label === slot.label)) {
          throw new HostError("missing_slot", "The slot did not appear in the native draft.");
        }
        this.invalidateReview();
        const controls = this.document!.controls;
        this.adopt(result, { ...project, slots: result.slots }, undefined, { ...controls, slots: result.slots });
      });
    });
  }

  bindComponent(name: unknown, component: unknown, bindingValue: unknown) {
    return this.perform(async () => {
      this.requireControlIdle();
      await this.sampleTask;
      const { project, state } = this.checkedPreview();
      const uniform = state.uniforms.find(item => item.name === name);
      if (!uniform || !isWritable(uniform) || typeof component !== "number" ||
          !Number.isInteger(component) || component < 0 || component >= uniform.values.length) {
        throw new HostError("invalid_uniform", "Choose a writable shader uniform component.");
      }
      const binding = parseBinding(bindingValue, project.slots);
      return this.change("uniform.set", { name: uniform.name, component, ...binding }, updated => {
        const applied = updated.uniforms.find(item => item.name === name);
        if (!applied || applied.values.length !== uniform.values.length ||
            (binding.mode === "fixed" && Math.abs(applied.values[component] - Math.fround(binding.value)) > 0.00001)) {
          throw new HostError("readback_mismatch", "The rendered uniform does not match the control.");
        }
        const current = project.bindings.filter(item => item.name === name);
        const siblings: ComponentBinding[] = uniform.values.map((value, index) =>
          ({ name: uniform.name, component: index, binding: current[index]?.binding ?? { mode: "fixed", value } }));
        siblings[component] = { name: uniform.name, component, binding };
        const bindings = [...project.bindings.filter(item => item.name !== name), ...siblings];
        this.invalidateReview();
        const controls = this.document!.controls;
        const working = { ...controls, bindings: [...controls.bindings.filter(item => item.name !== name), ...siblings],
          signatures: [...controls.signatures.filter(item => item.name !== name), ...controlSignatures(updated.uniforms, siblings)] };
        this.adopt(updated, { ...project, bindings }, undefined, working);
      });
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

  private requireControlIdle() {
    if (this.flight || this.unknownCommit || this.pendingReply) {
      throw new HostError("busy", "Wait for the current native request before editing controls.");
    }
  }

  private invalidateReview() {
    const staged = this.prepared;
    if (!staged) return;
    this.prepared = null;
    this.setPhase(staged.attempt, "idle", "Controls changed. Run again to review the candidate.");
  }

  private relaySample(slot: string, value: number) {
    const { lease, state } = this;
    if (this.busy || this.flight || this.sampleTask || !lease || !state || !this.previewMatches() ||
        !state.slots.some(item => item.id === slot)) return Promise.resolve();
    const context = this.context;
    const sequence = ++lease.sampleSequence;
    const delivery = lease.host.rpc("sample", { lease: lease.id, sequence, slot, value }, state)
      .then(raw => {
        if (context !== this.context || state.revision !== this.state?.revision || this.unknownCommit) return;
        const drawn = raw as ReflectedDraft | null;
        if (!drawn || drawn.lease !== lease.id || drawn.epoch !== state.epoch || drawn.revision !== state.revision) {
          throw new HostError("sample_unobserved", "The render host did not confirm the OSC sample.");
        }
        this.receive(drawn);
      }).catch(error => {
        if (context !== this.context || state.revision !== this.state?.revision) return;
        if (error instanceof HostError && ["busy", "sample_rate", "stale_revision"].includes(error.code)) return;
        this.acknowledged = null;
        this.samples.stop();
        const detail = error instanceof HostError ? `${error.message} ` : "";
        this.notice = `OSC input stopped. ${detail}Restore the draft and enable OSC input again.`;
      }).finally(() => { if (this.sampleTask === delivery) this.sampleTask = null; });
    this.sampleTask = delivery;
    return delivery;
  }

  private stopCallback() {
    if (this.callbackTimer) clearInterval(this.callbackTimer);
    this.callbackTimer = null;
  }

  private startCallback(state: ReflectedDraft) {
    this.stopCallback();
    this.callbackError = null;
    if (state.callbackUniform !== CALLBACK_UNIFORM) return;
    const started = performance.now();
    let lastDispatch = -Infinity;
    this.callbackTimer = setInterval(() => {
      const now = performance.now();
      if (this.busy || this.flight || this.observation || this.sampleTask || !this.previewMatches() ||
          now - lastDispatch < CALLBACK_INTERVAL_MS) return;
      lastDispatch = now;
      this.relayCallback(now - started);
    }, CALLBACK_INTERVAL_MS);
    this.callbackTimer.unref();
  }

  private pauseCallback(message: string) {
    this.stopCallback();
    this.callbackError = message;
  }

  private relayCallback(elapsed: number) {
    const { lease, state, context } = this;
    if (!lease || !state || state.callbackUniform !== CALLBACK_UNIFORM) return;
    let value: number;
    try {
      const evaluated = this.evaluateCallback(elapsed);
      if (!isCallbackValue(evaluated)) throw new Error("Invalid normalized callback value");
      value = Math.fround(evaluated);
    } catch {
      this.pauseCallback("The Editor callback did not produce a finite value from 0 to 1. Run or Restore to restart it.");
      return;
    }
    const id = randomUUID();
    const sequence = ++lease.sampleSequence;
    const current = () => context === this.context && lease === this.lease &&
      state.epoch === this.state?.epoch && state.revision === this.state?.revision &&
      state.commitID === this.state?.commitID && this.previewMatches();
    const delivery = lease.host.rpc("callback.sample", { lease: lease.id, sequence, value }, state, id)
      .then(raw => {
        if (!current() || !this.document?.accepted) return;
        const drawn = raw as ReflectedDraft | null;
        const receipt = drawn?.receipt;
        const scene = receipt?.result?.scene;
        if (!drawn || drawn.target !== state.target || drawn.epoch !== state.epoch || drawn.revision !== state.revision ||
            drawn.lease !== lease.id || drawn.commitID !== state.commitID || drawn.sourceDigest !== state.sourceDigest ||
            drawn.needsRestore || receipt?.id !== id || receipt.status !== "rendered" || receipt.revision !== state.revision ||
            !Number.isSafeInteger(receipt.frame) || drawn.frame !== receipt.frame || scene?.frame !== receipt.frame ||
            scene.rendered_revision !== state.revision || receipt.result?.lease !== lease.id || receipt.result.sequence !== sequence) {
          throw new HostError("sample_unobserved", "The render host did not confirm the callback draw.");
        }
        const observed = callbackReadback(drawn);
        if (observed === undefined || Math.fround(observed) !== value) {
          throw new HostError("readback_mismatch", "The drawn callback value did not match the Editor sample.");
        }
        this.receive(drawn);
        this.document = { ...this.document, accepted: { ...this.document.accepted, callbackValue: observed } };
      }).catch(error => {
        if (!current() || (error instanceof HostError && ["busy", "sample_rate"].includes(error.code))) return;
        const detail = error instanceof HostError ? error.message + " " : "";
        this.pauseCallback(detail + "Run or Restore to restart the callback.");
      }).finally(() => { if (this.sampleTask === delivery) this.sampleTask = null; });
    this.sampleTask = delivery;
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
    const attempt = this.captureRun("manual", true);
    this.cancelEvaluation();
    return this.submit(attempt);
  }

  captureExport() {
    const accepted = this.document?.accepted;
    if (!accepted) throw new HostError("no_draft", "There is no accepted native artifact to export.");
    if (accepted.language === "wgsl" || accepted.rendering?.backend === "webgpu") {
      throw new HostError("export_unsupported", "WebGPU previews can be saved as VOSC drafts. Standalone replay export is unavailable.");
    }
    const { callbackValue } = accepted;
    return {
      label: accepted.revision === null ? `Artifact ${accepted.id.slice(0, 8)}` : `Revision ${accepted.revision}`,
      callbackValue,
      write: (directory: string) => writeReplay(directory, accepted.project, callbackValue),
    };
  }

  export(parent: string) {
    return this.captureExport().write(parent);
  }
}
