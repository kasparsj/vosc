import { compileSinglePass, type Chain, type ColorTransform } from "vosc-triode-compiler";
import {
  HYDRA_DEPTH_LIMIT, HYDRA_OPERATION_LIMIT, HYDRA_PARAMETER_LIMIT, HYDRA_EXCHANGE_LIMIT, HYDRA_COLOR_BODY_LIMIT,
  hydraFrequency, hydraNumber, hydraParameterId, record, validCustomName, validateColorBody, validHydraLocation,
  type HydraLocation,
} from "./hydra-compiler";

export class UnsupportedHydraOperation extends Error {}

type PatchNode = {
  name: string;
  args: (number | string)[];
  input?: PatchNode;
  other?: PatchNode;
  location: HydraLocation | null;
  depth: number;
  expanded: number;
};

const sourceURL = "vosc-hydra-patch.js";
export function patchErrorLocation(error: Error, source: string) {
  // The Function constructor contributes two lines, plus our strict directive.
  const match = error.stack?.match(/\bvosc-hydra-patch\.js:(\d+):(\d+)/);
  if (!match) return null;
  const location = { line: Number(match[1]) - 3, column: Number(match[2]) };
  return validHydraLocation(location, source.split(/\r\n|[\n\r\u2028\u2029]/)) ? location : null;
}

export function evaluateHydraPatch(source: string) {
  const graphs = new WeakMap<object, PatchNode>();
  const handles = new WeakMap<object, string>();
  const parameters = new Map<string, number>();
  const custom = new Map<string, { definition: ColorTransform; location: HydraLocation | null }>();
  let output: PatchNode | undefined;
  let outputError = false;
  let operations = 0;
  let customBytes = 0;
  const bytes = new TextEncoder();
  const location = () => patchErrorLocation(new Error(), source);

  function arity(name: string, actual: number, min: number, max = min) {
    if (actual < min || actual > max) {
      throw new Error(`${name} expects ${min === max ? min : `${min}–${max}`} arguments.`);
    }
  }

  function scalar(value: unknown, label: string, minimum?: number, maximum?: number) {
    if (typeof value === "function" || Array.isArray(value)) {
      throw new UnsupportedHydraOperation(`${label} requires a constant scalar. Frame callbacks and array sequences are unsupported.`);
    }
    return hydraNumber(value, label, minimum, maximum);
  }

  function wrap(name: string, args: (number | string)[], input?: PatchNode, other?: PatchNode) {
    const depth = 1 + Math.max(input?.depth ?? 0, other?.depth ?? 0);
    const expanded = 1 + (input?.expanded ?? 0) + (other?.expanded ?? 0);
    if (++operations > HYDRA_OPERATION_LIMIT || expanded > HYDRA_OPERATION_LIMIT || depth > HYDRA_DEPTH_LIMIT) {
      throw new Error("Hydra graph exceeds 128 operations or depth 32.");
    }
    const node = { name, args, input, other, location: location(), depth, expanded };
    const surface = Object.freeze({
      out(...args: unknown[]) {
        if (args.length || output) {
          outputError = true;
          throw new UnsupportedHydraOperation("Hydra supports exactly one argument-free out().");
        }
        output = node;
      },
    });
    const graph = new Proxy(surface, {
      get(target, key) {
        if (key === "out") return target.out;
        if (key === "rotate" || key === "rotateDeg" || key === "rotateRad") {
          return (...args: unknown[]) => {
            arity(key, args.length, 0, 2);
            const [angle = key === "rotateRad" ? 0.174533 : 10, speed = 0] = args;
            return wrap(key, [scalar(angle, `${key} angle`), scalar(speed, `${key} speed`)], node);
          };
        }
        if (key === "blend" || key === "modulate") {
          return (...args: unknown[]) => {
            arity(key, args.length, 1, 2);
            const [other, amount = key === "blend" ? 0.5 : 0.1] = args;
            const branch = other && typeof other === "object" ? graphs.get(other) : undefined;
            if (!branch) throw new UnsupportedHydraOperation(`${key} requires another single-pass chain.`);
            const id = key === "blend" && amount && typeof amount === "object" ? handles.get(amount) : undefined;
            const value = id ?? (key === "blend" ? scalar(amount, "blend amount", 0, 1) : scalar(amount, "modulate amount"));
            return wrap(key, [value], node, branch);
          };
        }
        const entry = typeof key === "string" ? custom.get(key) : undefined;
        if (entry) {
          return (...args: unknown[]) => {
            const { name, inputs } = entry.definition;
            arity(name, args.length, 0, inputs.length);
            return wrap(name, inputs.map((input, index) =>
              scalar(args[index] === undefined ? input.default : args[index], `${name} ${input.name}`)), node);
          };
        }
        throw new UnsupportedHydraOperation(`Unsupported Hydra operation "${String(key)}".`);
      },
    });
    graphs.set(graph, node);
    return graph;
  }

  function osc(...args: unknown[]) {
    arity("osc", args.length, 0, 3);
    const [frequency = 60, sync = 0.1, offset = 0] = args;
    return wrap("osc", [hydraFrequency(scalar(frequency, "osc frequency", 0)), scalar(sync, "osc sync"), scalar(offset, "osc offset")]);
  }

  function param(...args: unknown[]) {
    arity("param", args.length, 2);
    const id = hydraParameterId(args[0]);
    const value = scalar(args[1], "param default", 0, 1);
    if (parameters.has(id) && parameters.get(id) !== value) throw new Error(`Conflicting defaults for param "${id}".`);
    parameters.set(id, value);
    if (parameters.size > HYDRA_PARAMETER_LIMIT) throw new Error("Hydra supports at most 16 parameters.");
    const handle = Object.freeze(Object.create(null));
    handles.set(handle, id);
    return handle;
  }

  function setFunction(...args: unknown[]) {
    arity("setFunction", args.length, 1);
    const definition = args[0];
    if (!record(definition) || definition.type !== "color" || !validCustomName(definition.name) ||
        custom.has(definition.name) || !Array.isArray(definition.inputs) || definition.inputs.length > 16 ||
        typeof definition.glsl !== "string" || Object.keys(definition).some(key =>
          !["name", "type", "inputs", "glsl"].includes(key))) {
      throw new UnsupportedHydraOperation("Expected one uniquely named single-pass color transform with float inputs.");
    }
    const bodyBytes = bytes.encode(definition.glsl).length;
    if (bodyBytes > HYDRA_COLOR_BODY_LIMIT || customBytes + bodyBytes > HYDRA_EXCHANGE_LIMIT) {
      throw new UnsupportedHydraOperation("Custom color transforms are limited to 16 KiB each and 64 KiB total.");
    }
    const names = new Set<string>();
    const inputs = definition.inputs.map((input: unknown) => {
      if (!record(input) || !validCustomName(input.name) || names.has(input.name) || input.type !== "float" ||
          Object.keys(input).some(key => !["name", "type", "default"].includes(key))) {
        throw new UnsupportedHydraOperation("Expected unique named float inputs for the custom color transform.");
      }
      names.add(input.name);
      return { name: input.name, type: input.type, default: scalar(input.default, `${input.name} default`) } as const;
    });
    try {
      validateColorBody(definition.glsl, [...names]);
    } catch (error) {
      throw new UnsupportedHydraOperation(error instanceof Error ? error.message : "Unsupported custom color body.");
    }
    if (++operations > HYDRA_OPERATION_LIMIT) throw new Error("Hydra supports at most 128 operations.");
    customBytes += bodyBytes;
    custom.set(definition.name,
      { definition: { name: definition.name, type: "color", inputs, glsl: definition.glsl }, location: location() });
  }

  const patch = new Function("osc", "param", "setFunction", `"use strict";\n${source}\n//# sourceURL=${sourceURL}`);
  if (patch(osc, param, setFunction) !== undefined) throw new Error("Hydra patch must finish synchronously with out().");
  const root = output;
  if (!root || outputError) throw new UnsupportedHydraOperation("Hydra requires exactly one argument-free out().");

  const locations = new Map<string, HydraLocation | null>();
  const program = compileSinglePass(api => {
    for (const { definition } of custom.values()) api.setFunction(definition);
    function replay(node: PatchNode): Chain {
      const prior = locations.get(node.name);
      if (!locations.has(node.name)) locations.set(node.name, node.location);
      else if (prior?.line !== node.location?.line || prior?.column !== node.location?.column) locations.set(node.name, null);
      if (!node.input) return api.osc(...node.args as number[]);
      const chain = replay(node.input);
      const args = node.args.map(value => typeof value === "string" ? api.parameter(value, parameters.get(value)!) : value);
      return node.other ? chain[node.name](replay(node.other), ...args) : chain[node.name](...args);
    }
    replay(root).out();
  });
  return { ...program, functions: program.functions.map(fn => ({
    ...fn, location: custom.get(fn.name)?.location ?? locations.get(fn.name) ?? null,
  })), location: root.expanded === 1 ? root.location : null };
}
