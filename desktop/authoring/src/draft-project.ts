import { basename, join } from "node:path";
import { mkdtemp, writeFile, rm } from "node:fs/promises";
import { BINDING_LIMIT, SLOT_LIMIT, SOURCE_LIMIT, type Binding, type DraftProject,
  type DraftSource, type Slot } from "./contracts";
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

function source(value: unknown): DraftSource {
  if (!record(value) || !text(value.name, 256) || !text(value.origin, 8192) ||
      typeof value.digest !== "string" || !/^[a-f0-9]{64}$/.test(value.digest) ||
      typeof value.base64 !== "string" || value.base64.length > Math.ceil(SOURCE_LIMIT / 3) * 4) {
    throw new HostError("invalid_project", "The saved shader is missing or exceeds its size limit.");
  }
  const bytes = Buffer.from(value.base64, "base64");
  if (!bytes.length || bytes.length > SOURCE_LIMIT || bytes.toString("base64") !== value.base64 || hash(bytes) !== value.digest) {
    throw new HostError("invalid_project", "The saved shader does not match its digest.");
  }
  try {
    new TextDecoder("utf-8", { fatal: true }).decode(bytes);
  } catch {
    throw new HostError("invalid_project", "The saved shader must be UTF-8.");
  }
  return { name: value.name, origin: value.origin, digest: value.digest, base64: value.base64 };
}

export function parseProject(value: unknown): DraftProject {
  if (!record(value) || value.format !== "vosc.authoring.draft" || value.version !== 1 ||
      !record(value.destination) ||
      typeof value.destination.target !== "string" || !/^[A-Fa-f0-9-]{36}$/.test(value.destination.target) ||
      typeof value.destination.fingerprint !== "string" || !/^[a-f0-9]{64}$/.test(value.destination.fingerprint) ||
      !record(value.destination.endpoint) || !text(value.destination.endpoint.host, 253) ||
      typeof value.destination.endpoint.port !== "number" || !Number.isInteger(value.destination.endpoint.port) ||
      value.destination.endpoint.port < 1 || value.destination.endpoint.port > 65535 ||
      !Array.isArray(value.slots) || value.slots.length > SLOT_LIMIT || !value.slots.every(slot) ||
      !Array.isArray(value.bindings)) {
    throw new HostError("invalid_project", "This file does not contain a supported VOSC draft.");
  }
  if (!validTitle(value.title)) {
    throw new HostError("invalid_project", "The draft title must be a nonempty single-line string up to 2048 bytes.");
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
  return {
    format: value.format, version: value.version, title: value.title,
    destination: {
      target: value.destination.target, fingerprint: value.destination.fingerprint,
      endpoint: { host: value.destination.endpoint.host, port: value.destination.endpoint.port },
    },
    source: source(value.source), slots, bindings,
  };
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
  const title = source.name.replace(/\.(frag|fs|fsh|glsl)$/i, "").replace(/[\r\n]+/g, " ").trim();
  return Array.from(title).slice(0, 60).join("").trimEnd() || "VOSC Draft";
}

export function sourceName(path?: string) {
  const leaf = path && basename(path);
  if (!leaf) return "Fragment shader";
  if (Buffer.byteLength(leaf) <= 256) return leaf;
  // Trim whole code points; source provenance separately retains the full path.
  return Array.from(leaf).slice(0, 63).join("") + "…";
}
