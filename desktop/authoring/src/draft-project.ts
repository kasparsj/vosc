import { HYDRA_EMITTER_VERSION, HYDRA_EXCHANGE_LIMIT, HYDRA_OPERATION_LIMIT, validHydraLocation } from "./hydra-compiler";
import { randomUUID } from "node:crypto";
import { basename, join } from "node:path";
import { mkdtemp, writeFile, rm } from "node:fs/promises";
import { BINDING_LIMIT, SLOT_LIMIT, SOURCE_LIMIT, type Binding, type DraftProject,
  type ControlSignature, type HydraProvenance, type DraftDocument, type DraftSource, type PendingMapping, type Peer, type Slot, type SourceProvenance } from "./contracts";
import { hash, HostError } from "./local-state";

const SLOT_ID = /^s[a-f0-9]{12}$/;
const UNIFORM = /^[A-Za-z_]\w{0,63}$/;
const RESERVED_UNIFORMS = new Set(["camPos", "camLook", "camOrbit"]);
const MAX_UNIFORM_BINDINGS = 128;
export const MAX_PROJECT_BYTES = 1024 * 1024;

export function validUniformName(name: string) {
  return UNIFORM.test(name) && !RESERVED_UNIFORMS.has(name);
}

function record(value: unknown): value is Record<string, unknown> {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

function text(value: unknown, size: number): value is string {
  return typeof value === "string" && value.length > 0 && Buffer.byteLength(value) <= size && !value.includes("\u0000");
}

function validTitle(value: unknown): value is string {
  return text(value, 2048) && !/[\r\n]/.test(value) && /[^ \t]/.test(value);
}

function finiteFloat(value: unknown): value is number {
  return typeof value === "number" && Number.isFinite(value) && Math.abs(value) <= BINDING_LIMIT;
}

function slot(value: unknown): value is Slot {
  return record(value) && typeof value.id === "string" && SLOT_ID.test(value.id) &&
    text(value.label, 80) && value.default === 0;
}

export function parseBinding(value: unknown, slots: Slot[]): Binding {
  if (record(value) && value.mode === "fixed" && finiteFloat(value.value) &&
      (value.value === 0 || Math.fround(value.value) !== 0)) {
    return { mode: "fixed", value: value.value };
  }
  if (record(value) && value.mode === "range" && typeof value.slot === "string" &&
      slots.some(slot => slot.id === value.slot) && finiteFloat(value.low) && finiteFloat(value.high)) {
    return { mode: "range", slot: value.slot, low: value.low, high: value.high };
  }
  throw new HostError("invalid_binding", "Use values between −1,000,000 and 1,000,000 and choose a configured slot for a range.");
}

const digestPattern = /^[a-f0-9]{64}$/;
const uuidPattern = /^[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12}$/i;

function provenance(value: unknown): SourceProvenance | undefined {
  if (value === undefined) return undefined;
  if (!record(value) || !text(value.provider, 80)) {
    throw new HostError("invalid_project", "The draft contains invalid source provenance.");
  }
  const provenance: SourceProvenance = { provider: value.provider };
  for (const key of ["path", "url", "repository", "requestedRef", "commit"] as const) {
    const property = value[key];
    if (property !== undefined) {
      if (!text(property, 8192)) throw new HostError("invalid_project", "The draft contains invalid source provenance.");
      provenance[key] = property;
    }
  }
  for (const key of ["originalDigest", "convertedDigest"] as const) {
    const property = value[key];
    if (property !== undefined) {
      if (typeof property !== "string" || !digestPattern.test(property)) {
        throw new HostError("invalid_project", "The draft contains an invalid source provenance digest.");
      }
      provenance[key] = property;
    }
  }
  if (value.convertedBase64 !== undefined) {
    provenance.convertedBase64 = source({
      name: "Converted source", origin: "Import", digest: provenance.convertedDigest, base64: value.convertedBase64,
    }).base64;
  }
  return provenance;
}

export function source(value: unknown, allowEmpty = false): DraftSource {
  if (!record(value) || !text(value.name, 256) || !text(value.origin, 8192) ||
      typeof value.digest !== "string" || !/^[a-f0-9]{64}$/.test(value.digest) ||
      typeof value.base64 !== "string" || value.base64.length > Math.ceil(SOURCE_LIMIT / 3) * 4) {
    throw new HostError("invalid_project", "The saved shader is missing or exceeds its size limit.");
  }
  const bytes = Buffer.from(value.base64, "base64");
  if ((!allowEmpty && !bytes.length) || bytes.length > SOURCE_LIMIT || bytes.toString("base64") !== value.base64 || hash(bytes) !== value.digest) {
    throw new HostError("invalid_project", "The saved shader does not match its digest.");
  }
  try {
    if (bytes.includes(0)) throw new Error("NUL");
    new TextDecoder("utf-8", { fatal: true }).decode(bytes);
  } catch {
    throw new HostError("invalid_project", "The saved shader must be UTF-8 without NUL characters.");
  }
  const original = provenance(value.provenance);
  return { name: value.name, origin: value.origin, digest: value.digest, base64: value.base64,
    ...(original ? { provenance: original } : {}) };
}

function parseDestination(value: unknown): Peer {
  if (!record(value) ||
      typeof value.target !== "string" || !/^[A-Fa-f0-9-]{36}$/.test(value.target) ||
      typeof value.fingerprint !== "string" || !digestPattern.test(value.fingerprint) ||
      !record(value.endpoint) || !text(value.endpoint.host, 253) ||
      typeof value.endpoint.port !== "number" || !Number.isInteger(value.endpoint.port) ||
      value.endpoint.port < 1 || value.endpoint.port > 65535) {
    throw new HostError("invalid_project", "The draft contains an invalid render destination.");
  }
  return { target: value.target, fingerprint: value.fingerprint,
    endpoint: { host: value.endpoint.host, port: value.endpoint.port } };
}

function parseControls(value: unknown) {
  if (!record(value) || !Array.isArray(value.slots) || value.slots.length > SLOT_LIMIT ||
      !value.slots.every(slot) || !Array.isArray(value.bindings)) {
    throw new HostError("invalid_project", "The draft contains invalid controls.");
  }
  const slots = value.slots.map(item => ({ id: item.id, label: item.label, default: item.default }));
  if (new Set(slots.map(item => item.id)).size !== slots.length) {
    throw new HostError("invalid_project", "The draft contains duplicate OSC slot identities.");
  }
  if (value.bindings.length > MAX_UNIFORM_BINDINGS * 4) {
    throw new HostError("binding_limit", `Native replay supports up to ${MAX_UNIFORM_BINDINGS} shader uniforms.`);
  }
  const components = new Map<string, number>();
  const bindings = value.bindings.map((item: unknown) => {
    if (!record(item) || typeof item.name !== "string" || !validUniformName(item.name) ||
        typeof item.component !== "number" || !Number.isInteger(item.component) ||
        item.component < 0 || item.component > 3) {
      throw new HostError("invalid_project", "The draft contains an invalid uniform binding.");
    }
    const mask = components.get(item.name) ?? 0;
    const bit = 1 << item.component;
    if (mask & bit) throw new HostError("invalid_project", "The draft contains duplicate uniform bindings.");
    components.set(item.name, mask | bit);
    return { name: item.name, component: item.component, binding: parseBinding(item.binding, slots) };
  });
  if (components.size > MAX_UNIFORM_BINDINGS) {
    throw new HostError("binding_limit", `Native replay supports up to ${MAX_UNIFORM_BINDINGS} shader uniforms.`);
  }
  for (const mask of components.values()) {
    if ((mask & (mask + 1)) !== 0) throw new HostError("invalid_project", "Uniform components are incomplete.");
  }
  return { slots, bindings };
}

function parseSignatures(value: unknown): ControlSignature[] {
  if (!Array.isArray(value) || value.length > MAX_UNIFORM_BINDINGS) {
    throw new HostError("invalid_project", "The draft contains invalid reflected signatures.");
  }
  const names = new Set<string>();
  return value.map((item: unknown) => {
    if (!record(item) || typeof item.name !== "string" || !validUniformName(item.name) || names.has(item.name) ||
        item.owner !== "layer" || typeof item.type !== "string" || typeof item.arity !== "number" ||
        ["float", "vec2", "vec3", "vec4"][item.arity - 1] !== item.type) {
      throw new HostError("invalid_project", "The draft contains invalid reflected signatures.");
    }
    names.add(item.name);
    return { name: item.name, type: item.type, arity: item.arity, owner: item.owner };
  });
}

function parsePendingMappings(value: unknown, slots: Slot[]): PendingMapping[] {
  if (value === undefined) return [];
  if (!Array.isArray(value) || value.length > MAX_UNIFORM_BINDINGS) {
    throw new HostError("invalid_project", "The draft contains invalid inactive mappings.");
  }
  const names = new Set<string>();
  return value.map((item: unknown) => {
    if (!record(item) || typeof item.name !== "string" || !validUniformName(item.name) ||
        names.has(item.name) || (item.reason !== "inactive" && item.reason !== "renderer_owned" && item.reason !== "retyped")) {
      throw new HostError("invalid_project", "The draft contains an invalid inactive mapping.");
    }
    const { bindings } = parseControls({ slots, bindings: item.bindings });
    const signature = item.signature === undefined ? undefined : parseSignatures([item.signature])[0];
    if (!bindings.length || bindings.some(binding => binding.name !== item.name) ||
        (signature && (signature.name !== item.name || signature.arity !== bindings.length))) {
      throw new HostError("invalid_project", "The inactive mapping has inconsistent uniform bindings.");
    }
    names.add(item.name);
    return { name: item.name, reason: item.reason, bindings, ...(signature ? { signature } : {}) };
  });
}

export function parseProject(value: unknown): DraftProject {
  if (!record(value) || value.format !== "vosc.authoring.draft" || value.version !== 1) {
    throw new HostError("invalid_project", "This file does not contain a supported VOSC draft.");
  }
  if (!validTitle(value.title)) {
    throw new HostError("invalid_project", "The draft title must be a nonempty single-line string up to 2048 bytes.");
  }
  return { format: value.format, version: value.version, title: value.title,
    destination: parseDestination(value.destination), source: source(value.source), ...parseControls(value) };
}

function parseHydra(value: unknown, generated: DraftSource): HydraProvenance {
  const invalid = () => new HostError("invalid_project", "The accepted Hydra provenance is invalid.");
  if (!record(value) || !((value.version === 1 && value.emitterVersion === 1) ||
      (value.version === 2 && value.emitterVersion === HYDRA_EMITTER_VERSION &&
        typeof value.compilerId === "string" && digestPattern.test(value.compilerId))) ||
      value.generatedDigest !== generated.digest || !Array.isArray(value.spans) ||
      value.spans.length === 0 || value.spans.length > HYDRA_OPERATION_LIMIT ||
      Buffer.byteLength(JSON.stringify(value.spans)) > HYDRA_EXCHANGE_LIMIT) throw invalid();

  const patch = source(value.patch);
  const lines = editorText(patch).split(/[\n\u2028\u2029]/);
  const glslLines = editorText(generated).split("\n").length;
  let previous = 0;
  const spans = value.spans.map((span: unknown) => {
    if (!record(span) || typeof span.firstLine !== "number" || !Number.isInteger(span.firstLine) ||
        typeof span.lastLine !== "number" || !Number.isInteger(span.lastLine) ||
        span.firstLine <= previous || span.lastLine < span.firstLine || span.lastLine > glslLines ||
        (span.location !== null && !validHydraLocation(span.location, lines))) throw invalid();
    previous = span.lastLine;
    return { firstLine: span.firstLine, lastLine: span.lastLine,
      location: span.location && { line: span.location.line, column: span.location.column } };
  });
  const artifact = { patch, generatedDigest: generated.digest, spans };
  return value.version === 1 ? { version: 1, emitterVersion: 1, ...artifact } :
    { version: 2, emitterVersion: HYDRA_EMITTER_VERSION, compilerId: value.compilerId as string, ...artifact };
}

export function parseDocument(value: unknown): DraftDocument {
  if (record(value) && value.version === 1) {
    const project = parseProject(value);
    return {
      format: project.format, version: 2, id: randomUUID(), title: project.title, language: "glsl",
      source: project.source, destination: project.destination,
      controls: { slots: project.slots, bindings: project.bindings, signatures: [] },
      accepted: { id: randomUUID(), revision: null, project, signatures: [] },
    };
  }
  if (!record(value) || value.format !== "vosc.authoring.draft" || value.version !== 2 ||
      (value.language !== "glsl" && value.language !== "hydra-js") ||
      typeof value.id !== "string" || !uuidPattern.test(value.id) || !validTitle(value.title) || !record(value.controls)) {
    throw new HostError("invalid_project", "This file does not contain a supported VOSC draft.");
  }
  let accepted: DraftDocument["accepted"] = null;
  if (value.accepted !== null) {
    const artifact = value.accepted;
    if (!record(artifact) || typeof artifact.id !== "string" || !uuidPattern.test(artifact.id) ||
        (artifact.revision !== null && (typeof artifact.revision !== "number" ||
          !Number.isSafeInteger(artifact.revision) || artifact.revision < 0))) {
      throw new HostError("invalid_project", "The last accepted artifact is invalid.");
    }
    const project = parseProject(artifact.project);
    accepted = {
      id: artifact.id, revision: artifact.revision, project, signatures: parseSignatures(artifact.signatures),
      ...(value.language === "hydra-js" ? { hydra: parseHydra(artifact.hydra, project.source) } : {}),
    };
  }
  const controls = parseControls(value.controls);
  return {
    format: value.format, version: value.version, id: value.id, title: value.title, language: value.language,
    source: source(value.source, true), destination: value.destination === null ? null : parseDestination(value.destination),
    controls: { ...controls, signatures: parseSignatures(value.controls.signatures),
      ...(value.controls.pendingMappings === undefined ? {} :
        { pendingMappings: parsePendingMappings(value.controls.pendingMappings, controls.slots) }) }, accepted,
  };
}

export function editorText(source: DraftSource) {
  return new TextDecoder().decode(Buffer.from(source.base64, "base64")).replace(/\r\n?/g, "\n");
}

export function sourceWithText(previous: DraftSource, text: string) {
  const bytes = Buffer.from(text, "utf8");
  if (bytes.toString("utf8") !== text || bytes.includes(0) || bytes.length > SOURCE_LIMIT) {
    throw new HostError("invalid_source", "Use UTF-8 text without NUL characters up to 128 KiB.");
  }
  return { ...previous, base64: bytes.toString("base64"), digest: hash(bytes) };
}

type Arg = { type: "i" | "f" | "s" | "asset"; value: string | number };
function arg(type: Arg["type"], value: Arg["value"]): Arg {
  return { type, value };
}

export function replay(document: DraftProject) {
  const checked = parseProject(document);
  const commands: { address: string; args: Arg[] }[] = [];
  const add = (address: string, ...args: Arg[]) => commands.push({ address, args });
  add("/layers/reset", arg("i", 1), arg("s", "stack"));
  for (const slot of checked.slots) {
    add("/input", arg("s", slot.id), arg("s", ""), arg("i", 0), arg("i", 256));
    add("/input/set", arg("s", slot.id), arg("s", "maxAmp"), arg("f", 1));
  }
  add("/shader", arg("i", 0), arg("asset", "draft.frag"));
  const uniformNames = new Set(checked.bindings.map(item => item.name));
  for (const name of uniformNames) {
    const members = checked.bindings.filter(item => item.name === name).sort((a, b) => a.component - b.component);
    const settings = members.map(item => {
      const binding = item.binding;
      return binding.mode === "fixed" ? arg("f", binding.value) :
        arg("s", `(${binding.low})+(${binding.high - binding.low})*in_${binding.slot}`);
    });
    add("/shader/var", arg("i", 0), arg("s", name), ...settings);
  }
  return { version: 1, title: checked.title, size: [640, 480], onset_gate: 0, commands };
}

export async function writeReplay(parent: string, document: DraftProject) {
  const manifest = replay(document);
  // Native v1 uses the JSON numeric token to distinguish OSC ints from OSC floats.
  const { rawJSON } = JSON as JSON & { rawJSON(value: string): unknown };
  const recipeBytes = JSON.stringify(manifest, (_key, value: unknown) => {
    if (record(value) && value.type === "f" && typeof value.value === "number" && Number.isInteger(value.value)) {
      return { type: value.type, value: rawJSON(`${value.value}.0`) };
    }
    return value;
  }, 2) + "\n";
  const directory = await mkdtemp(join(parent, "VOSC Replay "));
  try {
    await writeFile(join(directory, "draft.frag"), Buffer.from(document.source.base64, "base64"), { flag: "wx" });
    const recipe = join(directory, "scene.vosc.json");
    await writeFile(recipe, recipeBytes, { flag: "wx" });
    return recipe;
  } catch (error) {
    await rm(directory, { recursive: true, force: true });
    throw error;
  }
}

export function titleFor(source: DraftSource) {
  const title = source.name.replace(/\.(frag|fs|fsh|glsl|js)$/i, "").replace(/[\r\n]+/g, " ").trim();
  return Array.from(title).slice(0, 60).join("").trimEnd() || "VOSC Draft";
}

export function sourceName(path?: string) {
  const leaf = path && basename(path);
  if (!leaf) return "Fragment shader";
  if (Buffer.byteLength(leaf) <= 256) return leaf;
  // Trim whole code points; source provenance separately retains the full path.
  return Array.from(leaf).slice(0, 63).join("") + "…";
}
