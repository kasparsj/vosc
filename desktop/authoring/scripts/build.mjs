import { cp, mkdir, writeFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { build } from "esbuild";
import metadata from "../package.json" with { type: "json" };

const cwd = fileURLToPath(new URL("../", import.meta.url));
const output = new URL("../build/app/", import.meta.url);
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
});
await build({
  absWorkingDir: cwd,
  entryPoints: ["src/renderer.tsx"],
  bundle: true,
  outfile: fileURLToPath(new URL("renderer.js", output)),
  platform: "browser",
  target: "es2023",
  define: { "process.env.NODE_ENV": '"production"' },
  jsx: "automatic",
  minify: true,
});
for (const file of ["index.html", "styles.css"]) {
  await cp(new URL(`../src/${file}`, import.meta.url), new URL(file, output));
}
await writeFile(new URL("package.json", output), JSON.stringify({
  name: metadata.name,
  productName: metadata.productName,
  version: metadata.version,
  main: metadata.main,
}) + "\n");
