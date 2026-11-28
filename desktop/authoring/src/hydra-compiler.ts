export const HYDRA_SOURCE_LIMIT = 128 * 1024;
export const HYDRA_EXCHANGE_LIMIT = 64 * 1024;
export const HYDRA_EMITTER_VERSION = 2;
export const HYDRA_DEPTH_LIMIT = 32;
export const HYDRA_OPERATION_LIMIT = 128;
export const HYDRA_PARAMETER_LIMIT = 16;
export const HYDRA_COLOR_BODY_LIMIT = 16 * 1024;

export type HydraLocation = { line: number; column: number };
export type HydraSpan = { firstLine: number; lastLine: number; location: HydraLocation | null };
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

export function record(value: unknown): value is Record<string, unknown> {
  return value !== null && typeof value === "object" && !Array.isArray(value);
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

export const TRI_BUILTINS = ["osc", "rotate", "rotateDeg", "rotateRad", "blend", "modulate"] as const;
const builtinSignatures: Record<string, string> = Object.assign(Object.create(null), {
  osc: "vec2 _st, float frequency, float sync, float offset",
  rotate: "vec2 _st, float angle, float speed",
  rotateDeg: "vec2 _st, float angle, float speed",
  rotateRad: "vec2 _st, float angle, float speed",
  blend: "vec4 _c0, vec4 _c1, float amount",
  modulate: "vec2 _st, vec4 _c0, float amount",
});
const coordinateFunctions = new Set<string>(["rotate", "rotateDeg", "rotateRad", "modulate"]);
const previousFrameName = "voscPrevious";
const previousFrameSampler = "u_vosc_previous";
const customName = /^[A-Za-z][A-Za-z_0-9]{0,63}$/;
const glslTypes = new Set(["bool", "int", "uint", "float", ...["b", "i", "u", ""].flatMap(prefix =>
  [2, 3, 4].map(size => `${prefix}vec${size}`)), "mat2", "mat3", "mat4"]);
const intrinsics = new Set([
  "radians", "degrees", "sin", "cos", "tan", "asin", "acos", "atan", "pow", "exp", "log", "exp2", "log2", "sqrt",
  "inversesqrt", "abs", "sign", "floor", "trunc", "round", "ceil", "fract", "mod", "min", "max", "clamp", "mix",
  "step", "smoothstep", "length", "distance", "dot", "cross", "normalize", "reflect", "refract",
  "lessThan", "lessThanEqual", "greaterThan", "greaterThanEqual", "equal", "notEqual", "any", "all", "not",
]);
// Desktop GLSL 3.30, section 3.6, including words reserved for future use.
const glslKeywords = `
  attribute const uniform varying layout centroid flat smooth noperspective
  break continue do for while switch case default if else in out inout
  float int void bool true false invariant discard return
  mat2 mat3 mat4 mat2x2 mat2x3 mat2x4 mat3x2 mat3x3 mat3x4 mat4x2 mat4x3 mat4x4
  vec2 vec3 vec4 ivec2 ivec3 ivec4 bvec2 bvec3 bvec4 uint uvec2 uvec3 uvec4
  lowp mediump highp precision
  sampler1D sampler2D sampler3D samplerCube sampler1DShadow sampler2DShadow samplerCubeShadow
  sampler1DArray sampler2DArray sampler1DArrayShadow sampler2DArrayShadow
  isampler1D isampler2D isampler3D isamplerCube isampler1DArray isampler2DArray
  usampler1D usampler2D usampler3D usamplerCube usampler1DArray usampler2DArray
  sampler2DRect sampler2DRectShadow isampler2DRect usampler2DRect
  samplerBuffer isamplerBuffer usamplerBuffer
  sampler2DMS isampler2DMS usampler2DMS sampler2DMSArray isampler2DMSArray usampler2DMSArray
  struct
  common partition active asm class union enum typedef template this packed goto
  inline noinline volatile public static extern external interface
  long short double half fixed unsigned superp input output
  hvec2 hvec3 hvec4 dvec2 dvec3 dvec4 fvec2 fvec3 fvec4 sampler3DRect filter
  image1D image2D image3D imageCube iimage1D iimage2D iimage3D iimageCube
  uimage1D uimage2D uimage3D uimageCube image1DArray image2DArray
  iimage1DArray iimage2DArray uimage1DArray uimage2DArray
  image1DShadow image2DShadow image1DArrayShadow image2DArrayShadow
  imageBuffer iimageBuffer uimageBuffer sizeof cast namespace using row_major
`.trim().split(/\s+/);
const reserved = new Set([
  ...TRI_BUILTINS, ...glslKeywords, ...intrinsics, ...Object.getOwnPropertyNames(Object.prototype),
  "setFunction", "param", "parameter", "then", "main", "time", "resolution", "st", "hydraColor",
  "src", "prev", "noise", "shape", "gradient", "solid", "render", "camera", "s0", "o0",
  previousFrameName, previousFrameSampler,
]);

export function validCustomName(name: unknown): name is string {
  return typeof name === "string" && customName.test(name) && !reserved.has(name) &&
    !name.startsWith("u_hydra_") && !name.startsWith("gl_") && !name.includes("__");
}

function tokens(source: string) {
  const scanner = /\s+|\/\/[^\n\r]*|\/\*[\s\S]*?\*\/|(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?[uU]?|[A-Za-z_]\w*|[{}(),;.+*\/%!?:<>=&|\-]/gy;
  const result: string[] = [];
  for (let index = 0; index < source.length;) {
    scanner.lastIndex = index;
    const match = scanner.exec(source);
    if (!match) throw new Error("Custom color transforms require renderer-independent GLSL.");
    index = scanner.lastIndex;
    if (!/^\s|^\/[/\*]/.test(match[0])) result.push(match[0]);
  }
  return result;
}

export function validateColorBody(source: string, inputs: readonly string[]) {
  if (new TextEncoder().encode(source).length > HYDRA_COLOR_BODY_LIMIT) {
    throw new Error("Custom color transform body exceeds 16 KiB.");
  }
  const parts = tokens(source);
  const locals = new Set(inputs);
  const protectedNames = new Set(inputs);
  for (let index = 0; index + 1 < parts.length; index++) {
    if (glslTypes.has(parts[index]) && validCustomName(parts[index + 1]) && !protectedNames.has(parts[index + 1])) {
      locals.add(parts[index + 1]);
    }
  }
  const delimiters: string[] = [];
  parts.forEach((part, index) => {
    if (part === "(" || part === "{") delimiters.push(part);
    if ((part === ")" && delimiters.pop() !== "(") || (part === "}" && delimiters.pop() !== "{")) {
      throw new Error("Custom color transform has unbalanced delimiters.");
    }
    if (!/^[A-Za-z_]/.test(part)) return;
    const isSwizzle = parts[index - 1] === "." && /^(?:[rgba]{1,4}|[xyzw]{1,4}|[stpq]{1,4})$/.test(part);
    if (isSwizzle || locals.has(part) || glslTypes.has(part) || intrinsics.has(part) ||
        ["_c0", "time", "resolution", "const", "return", "if", "else", "true", "false"].includes(part)) return;
    throw new Error(`Unsupported custom color dependency "${part}".`);
  });
  if (delimiters.length) throw new Error("Custom color transform has unbalanced delimiters.");
}

export function compileHydra(program: unknown, source: string) {
  const sourceLines = source.split(/\r\n|[\n\r\u2028\u2029]/);
  if (!record(program) || !Array.isArray(program.functions) ||
      !Array.isArray(program.uniforms) ||
      program.uniforms.length > HYDRA_PARAMETER_LIMIT || typeof program.colorExpression !== "string") {
    throw new Error("Invalid Triode single-pass compiler result.");
  }
  const feedback = program.feedback;
  if (feedback !== null && (!record(feedback) || Object.keys(feedback).length !== 1 ||
      (feedback.location !== null && !validHydraLocation(feedback.location, sourceLines)))) {
    throw new Error("Invalid Triode previous-frame source.");
  }
  const functionCount = program.functions.length + Number(feedback !== null);
  if (!functionCount || functionCount > HYDRA_OPERATION_LIMIT) {
    throw new Error("Invalid Triode single-pass compiler result.");
  }
  const lines = ["#version 330"];
  if (feedback) {
    lines.push("#ifndef VOSC_SELF_FEEDBACK", "#error VOSC self-feedback requires a matching renderer",
      "#endif", `uniform sampler2D ${previousFrameSampler};`);
  }
  lines.push("uniform float time;", "uniform vec2 resolution;");
  const allowed = new Set(["st", "clamp"]);
  let previous = "";
  for (const uniform of program.uniforms) {
    if (!record(uniform) || typeof uniform.name !== "string" || !/^u_hydra_[a-z][a-z0-9_]{0,31}$/.test(uniform.name) ||
        uniform.name <= previous || uniform.type !== "float") throw new Error("Invalid Triode scalar parameter.");
    previous = uniform.name;
    allowed.add(uniform.name);
    lines.push(`uniform float ${uniform.name} = ${glsl(hydraNumber(uniform.value, "param default", 0, 1))};`);
  }
  lines.push("out vec4 hydraColor;", "");
  const spans: HydraSpan[] = [];
  if (feedback) {
    allowed.add(previousFrameName);
    const firstLine = lines.length + 1;
    lines.push(`vec4 ${previousFrameName}(vec2 st) {`, `    return texture(${previousFrameSampler}, st);`, "}");
    spans.push({ firstLine, lastLine: lines.length, location: feedback.location as HydraLocation | null });
    lines.push("");
  }
  for (const fn of program.functions) {
    if (!record(fn) || typeof fn.name !== "string" || allowed.has(fn.name) ||
        (!TRI_BUILTINS.some(name => name === fn.name) && !validCustomName(fn.name)) || typeof fn.source !== "string" ||
        (fn.location !== null && !validHydraLocation(fn.location, sourceLines))) {
      throw new Error("Invalid Triode shader function or location.");
    }
    const signature = /^\s*(vec[24]) ([A-Za-z]\w*)\(([^()]*)\)\s*\{([\s\S]*)\}\s*$/.exec(fn.source);
    if (!signature || signature[2] !== fn.name || signature[1] !== (coordinateFunctions.has(fn.name) ? "vec2" : "vec4")) {
      throw new Error("Invalid Triode shader function declaration.");
    }
    const args = signature[3].split(", ");
    const builtin = builtinSignatures[fn.name];
    const inputNames = args.map(arg => /^(?:float|vec[24]) ([A-Za-z_]\w*)$/.exec(arg)?.[1]);
    if (inputNames.some(name => !name) || (builtin ? signature[3] !== builtin :
      args[0] !== "vec4 _c0" || args.length > 17 || args.slice(1).some((arg, index) =>
        !arg.startsWith("float ") || !validCustomName(inputNames[index + 1])) ||
      new Set(inputNames).size !== inputNames.length)) {
      throw new Error("Invalid Triode shader function inputs.");
    }
    // Triode adds whitespace while wrapping the body. Keep generated lines intact.
    validateColorBody(signature[4].trim(), inputNames as string[]);
    allowed.add(fn.name);
    const firstLine = lines.length + 1;
    lines.push(...fn.source.trim().split(/\r\n|[\n\r]/));
    spans.push({ firstLine, lastLine: lines.length, location: fn.location });
    lines.push("");
  }
  const expression = program.colorExpression;
  if (!expression || !/^[\w.(),+\- ]+$/.test(expression)) throw new Error("Invalid Triode shader expression.");
  const expressionTokens = tokens(expression);
  if (expressionTokens.some(token => /^[A-Za-z_]/.test(token) && !allowed.has(token)) ||
      expressionTokens.includes(previousFrameName) !== (feedback !== null)) {
    throw new Error("Invalid Triode shader expression.");
  }
  if (program.location !== null && !validHydraLocation(program.location, sourceLines)) {
    throw new Error("Invalid Triode expression location.");
  }
  lines.push("void main() {", "    vec2 st = gl_FragCoord.xy / resolution;");
  const expressionLine = lines.length + 1;
  lines.push(`    hydraColor = ${expression};`, "}", "");
  if (program.location) spans.push({ firstLine: expressionLine, lastLine: expressionLine, location: program.location });
  const fragment = lines.join("\n");
  const bytes = new TextEncoder();
  if (bytes.encode(fragment).length > HYDRA_SOURCE_LIMIT ||
      bytes.encode(JSON.stringify(spans)).length > HYDRA_EXCHANGE_LIMIT) {
    throw new Error("Generated Hydra shader exceeds its size limit.");
  }
  return { fragment, spans };
}
