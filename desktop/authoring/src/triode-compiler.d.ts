declare module "vosc-triode-compiler" {
  export interface Chain {
    [operation: string]: (...args: unknown[]) => Chain;
  }
  export interface ColorTransform {
    name: string;
    type: "color";
    inputs: { name: string; type: "float"; default: number }[];
    glsl: string;
  }
  export function compileSinglePass(build: (api: {
    osc(...args: number[]): Chain;
    previousFrame(): Chain;
    setFunction(definition: ColorTransform): void;
    parameter(id: string, value: number): object;
  }) => void): {
    colorExpression: string;
    readsPreviousFrame: boolean;
    functions: { name: string; source: string }[];
    uniforms: { name: string; type: "float"; value: number }[];
  };
}

declare module "vosc-triode-identity" {
  export const TRIODE_COMPILER_ID: string;
}
