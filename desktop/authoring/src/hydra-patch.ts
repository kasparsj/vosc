import {
  HYDRA_DEPTH_LIMIT, HYDRA_OPERATION_LIMIT, HYDRA_PARAMETER_LIMIT,
  hydraFrequency, hydraNumber, hydraParameterId,
  type HydraGraph, type HydraNode,
} from "./hydra-compiler";

export class UnsupportedHydraOperation extends Error {}

export function evaluateHydraPatch(source: string): HydraGraph {
  const graphs = new WeakMap<object, { node: HydraNode; depth: number; operations: number }>();
  const handles = new WeakMap<object, string>();
  const parameters = new Map<string, number>();
  let output: HydraNode | undefined;
  let operations = 0;

  function location() {
    // Function constructor contributes two lines, and the strict directive contributes one.
    const match = new Error().stack?.match(/vosc-hydra-patch\.js:(\d+):(\d+)/);
    return match ? { line: Number(match[1]) - 3, column: Number(match[2]) } : null;
  }

  function arity(name: string, actual: number, min: number, max = min) {
    if (actual < min || actual > max) {
      throw new Error(`${name} expects ${min === max ? min : `${min}–${max}`} arguments.`);
    }
  }

  function requireGraph(value: unknown) {
    const graph = value && typeof value === "object" ? graphs.get(value) : undefined;
    if (!graph) throw new Error("blend requires another Hydra graph.");
    return graph;
  }

  function wrap(node: HydraNode, depth: number, expanded: number): object {
    if (++operations > HYDRA_OPERATION_LIMIT || expanded > HYDRA_OPERATION_LIMIT || depth > HYDRA_DEPTH_LIMIT) {
      throw new Error("Hydra graph exceeds 128 operations or depth 32.");
    }

    const surface = Object.freeze({
      rotate(...args: unknown[]) {
        arity("rotate", args.length, 0, 2);
        const [angle = 10, speed = 0] = args;
        return wrap({
          op: "rotate", input: node, location: location(),
          angle: hydraNumber(angle, "rotate angle"), speed: hydraNumber(speed, "rotate speed"),
        }, depth + 1, expanded + 1);
      },
      blend(...args: unknown[]) {
        arity("blend", args.length, 1, 2);
        const [other, amount = 0.5] = args;
        const branch = requireGraph(other);
        const id = amount && typeof amount === "object" ? handles.get(amount) : undefined;
        return wrap({
          op: "blend", input: node, other: branch.node, location: location(),
          amount: id ?? hydraNumber(amount, "blend amount", 0, 1),
        }, Math.max(depth, branch.depth) + 1, expanded + branch.operations + 1);
      },
      out(...args: unknown[]) {
        arity("out", args.length, 0);
        if (output) throw new Error("Hydra supports exactly one out().");
        output = node;
      },
    });
    const graph = new Proxy(surface, {
      get(target, key) {
        if (Object.hasOwn(target, key)) return Reflect.get(target, key);
        throw new UnsupportedHydraOperation(`Unsupported Hydra operation "${String(key)}".`);
      },
    });
    graphs.set(graph, { node, depth, operations: expanded });
    return graph;
  }

  function osc(...args: unknown[]) {
    arity("osc", args.length, 0, 3);
    const [frequency = 60, sync = 0.1, offset = 0] = args;
    return wrap({
      op: "osc", location: location(), frequency: hydraFrequency(frequency),
      sync: hydraNumber(sync, "osc sync"), offset: hydraNumber(offset, "osc offset"),
    }, 1, 1);
  }

  function param(...args: unknown[]) {
    arity("param", args.length, 2);
    const id = hydraParameterId(args[0]);
    const value = hydraNumber(args[1], "param default", 0, 1);
    if (parameters.has(id) && parameters.get(id) !== value) throw new Error(`Conflicting defaults for param "${id}".`);
    parameters.set(id, value);
    if (parameters.size > HYDRA_PARAMETER_LIMIT) throw new Error("Hydra supports at most 16 parameters.");
    const handle = Object.freeze(Object.create(null));
    handles.set(handle, id);
    return handle;
  }

  const patch = new Function("osc", "param", `"use strict";\n${source}\n//# sourceURL=vosc-hydra-patch.js`);
  if (patch(osc, param) !== undefined) throw new Error("Hydra patch must finish synchronously with out().");
  if (!output) throw new Error("Hydra patch requires exactly one out().");
  return { output, parameters: [...parameters] };
}
