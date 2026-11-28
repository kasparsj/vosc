export const HYDRA_SOURCE_LIMIT = 128 * 1024;
export const HYDRA_EXCHANGE_LIMIT = 64 * 1024;
export const HYDRA_EMITTER_VERSION = 1;
export const HYDRA_DEPTH_LIMIT = 32;
export const HYDRA_OPERATION_LIMIT = 128;
export const HYDRA_PARAMETER_LIMIT = 16;

export type HydraLocation = { line: number; column: number };
export type HydraSpan = { firstLine: number; lastLine: number; location: HydraLocation | null };
type Amount = number | string;
export type HydraNode =
  | { op: "osc"; location: HydraLocation | null; frequency: number; sync: number; offset: number }
  | { op: "rotate"; location: HydraLocation | null; input: HydraNode; angle: number; speed: number }
  | { op: "blend"; location: HydraLocation | null; input: HydraNode; other: HydraNode; amount: Amount };
export type HydraGraph = { output: HydraNode; parameters: [string, number][] };

export function hydraNumber(value: unknown, label: string, minimum = -10000, maximum = 10000) {
  if (typeof value !== "number" || !Number.isFinite(value) || value < minimum || value > maximum ||
      (value !== 0 && Math.fround(value) === 0)) {
    throw new Error(`${label} requires a finite GLSL float between ${minimum} and ${maximum}.`);
  }
  return value;
}

export function hydraFrequency(value: unknown) {
  const frequency = hydraNumber(value, "osc frequency", 0);
  if (frequency === 0) throw new Error("osc frequency must be positive.");
  return frequency;
}

export function hydraParameterId(value: unknown) {
  if (typeof value !== "string" || !/^[a-z][a-z0-9_]{0,31}$/.test(value)) {
    throw new Error("param ID must start with a lowercase letter and use up to 32 lowercase letters, digits or underscores.");
  }
  return value;
}

function record(value: unknown): Record<string, unknown> {
  if (!value || typeof value !== "object" || Array.isArray(value)) throw new Error("Invalid Hydra graph.");
  return value as Record<string, unknown>;
}

export function validHydraLocation(value: unknown, source: readonly string[]): value is HydraLocation {
  if (!value || typeof value !== "object") return false;
  const { line, column } = value as HydraLocation;
  return Number.isInteger(line) && line >= 1 && line <= source.length &&
    Number.isInteger(column) && column >= 1 && column <= source[line - 1].length + 1;
}

function glsl(value: number) {
  const number = String(Math.fround(value));
  return /[.e]/.test(number) ? number : `${number}.0`;
}

export function compileHydra(graph: unknown, source: string) {
  const sourceLines = source.split(/\r\n|[\n\r\u2028\u2029]/);
  const { output, parameters } = record(graph);
  if (!Array.isArray(parameters) || parameters.length > HYDRA_PARAMETER_LIMIT) {
    throw new Error("Hydra supports at most 16 parameters.");
  }
  const defaults = new Map<string, number>();
  for (const entry of parameters) {
    if (!Array.isArray(entry) || entry.length !== 2) throw new Error("Invalid Hydra parameter.");
    const id = hydraParameterId(entry[0]);
    const value = hydraNumber(entry[1], "param default", 0, 1);
    if (defaults.has(id) && defaults.get(id) !== value) throw new Error(`Conflicting defaults for param "${id}".`);
    defaults.set(id, value);
  }

  const used = new Set<string>();
  const operations: { body: string[]; location: HydraLocation | null }[] = [];
  let visits = 0;

  function emit(value: unknown, depth: number): string {
    if (++visits > HYDRA_OPERATION_LIMIT || depth > HYDRA_DEPTH_LIMIT) {
      throw new Error("Hydra graph exceeds 128 operations or depth 32.");
    }
    const node = record(value);
    const { location } = node;
    if (location !== null && !validHydraLocation(location, sourceLines)) throw new Error("Invalid Hydra source location.");
    let body: string[];
    switch (node.op) {
      case "osc": {
        const frequency = glsl(hydraFrequency(node.frequency));
        const sync = glsl(hydraNumber(node.sync, "osc sync"));
        const offset = glsl(hydraNumber(node.offset, "osc offset"));
        body = [
          `float phase = (uv.x + time * ${sync}) * ${frequency};`,
          `return vec4(0.5 + 0.5 * sin(vec3(phase) + vec3(-(${offset}), 0.0, ${offset})), 1.0);`,
        ];
        break;
      }
      case "rotate": {
        const input = emit(node.input, depth + 1);
        const angle = glsl(hydraNumber(node.angle, "rotate angle"));
        const speed = glsl(hydraNumber(node.speed, "rotate speed"));
        body = [
          `float angle = radians(${angle}) + time * ${speed};`,
          "vec2 p = uv - vec2(0.5);",
          "float c = cos(angle), s = sin(angle);",
          `return ${input}(vec2(c * p.x + s * p.y, -s * p.x + c * p.y) + vec2(0.5));`,
        ];
        break;
      }
      case "blend": {
        const input = emit(node.input, depth + 1);
        const other = emit(node.other, depth + 1);
        let amount: string;
        if (typeof node.amount === "string") {
          const id = hydraParameterId(node.amount);
          if (!defaults.has(id)) throw new Error(`Missing Hydra parameter "${id}".`);
          used.add(id);
          amount = `clamp(u_hydra_${id}, 0.0, 1.0)`;
        } else {
          amount = glsl(hydraNumber(node.amount, "blend amount", 0, 1));
        }
        body = [`return mix(${input}(uv), ${other}(uv), ${amount});`];
        break;
      }
      default: throw new Error("Unsupported Hydra graph operation.");
    }

    operations.push({ body, location });
    return `hydra_${operations.length}`;
  }

  const root = emit(output, 1);
  const lines = ["#version 330", "uniform float time;", "uniform vec2 resolution;"];
  for (const id of [...used].sort()) lines.push(`uniform float u_hydra_${id} = ${glsl(defaults.get(id)!)};`);
  lines.push("out vec4 hydraColor;", "");
  const spans: HydraSpan[] = [];
  operations.forEach(({ body, location }, index) => {
    const firstLine = lines.length + 1;
    lines.push(`vec4 hydra_${index + 1}(vec2 uv) {`, ...body.map(line => `    ${line}`), "}");
    spans.push({ firstLine, lastLine: lines.length, location });
    lines.push("");
  });
  lines.push("void main() {", `    hydraColor = ${root}(gl_FragCoord.xy / resolution);`, "}", "");
  const fragment = lines.join("\n");
  const bytes = new TextEncoder();
  if (bytes.encode(fragment).length > HYDRA_SOURCE_LIMIT ||
      bytes.encode(JSON.stringify(spans)).length > HYDRA_EXCHANGE_LIMIT) {
    throw new Error("Generated Hydra shader exceeds its size limit.");
  }
  return { fragment, spans };
}
