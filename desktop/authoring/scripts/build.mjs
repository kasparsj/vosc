import { cp, mkdir, readFile, rm, writeFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { build } from "esbuild";
import metadata from "../package.json" with { type: "json" };
import { compilerPlugin, digest, readStagedCompiler } from "./triode-compiler.mjs";

const cwd = fileURLToPath(new URL("../", import.meta.url));
const output = new URL("../build/app/", import.meta.url);
const auditPath = new URL("triode-compiler-audit.json", output);
await rm(auditPath, { force: true });
const compiler = await readStagedCompiler();
await mkdir(output, { recursive: true });
await build({
  absWorkingDir: cwd,
  entryPoints: { main: "src/main.ts", preload: "src/preload.ts" },
  bundle: true,
  external: ["electron"],
  outdir: fileURLToPath(output),
  outExtension: { ".js": ".cjs" },
  platform: "node",
  format: "cjs",
  target: "es2023",
  plugins: [compilerPlugin(compiler, { includeCompiler: false })],
});
const worker = await build({
  absWorkingDir: cwd,
  entryPoints: { renderer: "src/renderer.tsx", "hydra-worker": "src/hydra-worker.ts" },
  bundle: true,
  outdir: fileURLToPath(output),
  platform: "browser",
  target: "es2023",
  define: { "process.env.NODE_ENV": '"production"' },
  jsx: "automatic",
  minify: true,
  metafile: true,
  plugins: [compilerPlugin(compiler)],
});
const compilerOutputs = Object.entries(worker.metafile.outputs)
  .filter(([, item]) => item.inputs["vosc-triode:vosc-triode-compiler"]?.bytesInOutput > 0);
if (compilerOutputs.length !== 1 || compilerOutputs[0][0] !== "build/app/hydra-worker.js") {
  throw new Error("The staged Triode compiler must be bundled only into the isolated Editor worker.");
}
for (const file of ["index.html", "styles.css", "hydra-bootstrap.js"]) {
  await cp(new URL(`../src/${file}`, import.meta.url), new URL(file, output));
}
await cp(new URL("../src/icons/", import.meta.url), new URL("icons/", output), { recursive: true });
if (process.platform === "linux") {
  await cp(new URL("../discovery/avahi_browse.py", import.meta.url), new URL("avahi_browse.py", output));
}
await writeFile(new URL("package.json", output), JSON.stringify({
  name: metadata.name,
  productName: metadata.productName,
  version: metadata.version,
  main: metadata.main,
}) + "\n");
await writeFile(auditPath, JSON.stringify({
  bundleSha256: compiler.pin.bundleSha256,
  workerSha256: digest(await readFile(new URL("hydra-worker.js", output))),
  workerCompilerInputs: compiler.pin.compilerInputs,
  distribution: compiler.pin.distribution,
  externalDistribution: compiler.pin.externalDistribution,
}, null, 2) + "\n");
