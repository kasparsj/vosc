import { build } from "esbuild";
import { readFile, writeFile } from "node:fs/promises";
import { pathToFileURL, fileURLToPath } from "node:url";
import { resolve } from "node:path";
import { compilerPlugin, readStagedCompiler } from "../scripts/triode-compiler.mjs";

const output = process.argv[2];
if (!output) throw new Error("Supply an output file for the generated GLSL fixture");
const editor = fileURLToPath(new URL("../", import.meta.url));
const result = await build({
  absWorkingDir: editor,
  stdin: { contents: `
    import { evaluateHydraPatch } from "./src/hydra-patch";
    import { compileHydra } from "./src/hydra-compiler";
    export const emit = source => compileHydra(evaluateHydraPatch(source), source);
  `, resolveDir: editor, loader: "ts" },
  bundle: true, platform: "node", format: "esm", write: false,
  plugins: [compilerPlugin(await readStagedCompiler())],
});
const emit = (await import(`data:text/javascript;base64,${Buffer.from(result.outputFiles[0].contents).toString("base64")}`)).emit;
const source = await readFile(new URL("./fixtures/hydra/animated.js", import.meta.url), "utf8");
const shader = emit(source);
await writeFile(resolve(output), shader.fragment, { flag: "wx" });
