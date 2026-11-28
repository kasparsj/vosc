import { execFileSync } from "node:child_process";
import { createHash } from "node:crypto";
import { constants, createReadStream } from "node:fs";
import { access, chmod, copyFile, cp, mkdir, open, readdir, realpath, rm, stat, writeFile } from "node:fs/promises";
import { basename, dirname, isAbsolute, join, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { parseArgs } from "node:util";
import { build } from "esbuild";

const root = fileURLToPath(new URL("../", import.meta.url));

async function digest(path) {
  const hash = createHash("sha256");
  for await (const chunk of createReadStream(path)) hash.update(chunk);
  return hash.digest("hex");
}

async function inventory(directory, root) {
  const files = [];
  for (const entry of (await readdir(directory, { withFileTypes: true })).sort((a, b) => a.name.localeCompare(b.name, "en"))) {
    const path = join(directory, entry.name);
    if (entry.isDirectory()) files.push(...await inventory(path, root));
    else if (entry.isFile()) files.push({ path: relative(root, path), sha256: await digest(path) });
    else throw new Error("Unexpected artifact in staged bundle " + path);
  }
  return files;
}

const { values } = parseArgs({ options: {
  output: { type: "string" }, "renderer-bin": { type: "string" },
  "source-revision": { type: "string" }, "sdk-revision": { type: "string" },
  "runtime-libs": { type: "string" }, "webgpu-tools": { type: "string" },
} });
const output = values.output;
const binary = values["renderer-bin"];
if (!output || !binary || !isAbsolute(output) || !isAbsolute(binary) ||
    !/^[a-f0-9]{40}$/.test(values["source-revision"] ?? "") ||
    !/^[a-f0-9]{40}$/.test(values["sdk-revision"] ?? "")) {
  throw new Error("Supply absolute --output and --renderer-bin and full --source-revision and --sdk-revision Git hashes");
}
const sourceBin = dirname(binary);
const runtimeLibraries = values["runtime-libs"];
const webgpuTools = values["webgpu-tools"];
if (runtimeLibraries && !isAbsolute(runtimeLibraries)) throw new Error("--runtime-libs must be absolute");
if (webgpuTools !== undefined && !isAbsolute(webgpuTools)) throw new Error("--webgpu-tools must be absolute");
const info = await stat(binary);
if (!info.isFile()) throw new Error("Renderer must be a regular file");
await access(binary, constants.X_OK);
const elf = await open(binary, "r");
try {
  const bytes = Buffer.alloc(20);
  if ((await elf.read(bytes)).bytesRead !== bytes.length ||
      !bytes.subarray(0, 4).equals(Buffer.from([127, 69, 76, 70])) ||
      bytes[4] !== 2 || bytes[5] !== 1 || bytes.readUInt16LE(18) !== 62) {
    throw new Error("Supply a staged Linux x86-64 ELF renderer");
  }
} finally { await elf.close(); }
// Match the stock files copied by DraftRenderer into preview and validator roots.
for (const asset of [
  "shaders/passthru.vert",
  "shaders/include/of_default_uniforms.glsl",
  "shaders/include/of_default_vertex_in_attributes.glsl",
]) {
  const path = join(sourceBin, "data", asset);
  const info = await stat(path).catch(error => {
    if (error.code === "ENOENT") throw new Error("Missing stock renderer asset " + path);
    throw error;
  });
  if (!info.isFile()) throw new Error("Stock renderer asset must be a file " + path);
  await access(path, constants.R_OK);
}

const outputPath = resolve(output);
const destination = join(await realpath(dirname(outputPath)), basename(outputPath));
const inputs = await Promise.all([sourceBin, runtimeLibraries, webgpuTools].filter(Boolean).map(path => realpath(path)));
if (inputs.some(input => destination === input || destination.startsWith(input + "/"))) {
  throw new Error("Choose an output directory outside the renderer, library, and WebGPU tool inputs");
}
await mkdir(destination, { mode: 0o700 });
try {
  await build({
    absWorkingDir: root, entryPoints: ["linux-host/main.ts"], bundle: true,
    outfile: join(destination, "host.cjs"), platform: "node", target: "node22", format: "cjs",
  });
  for (const helper of ["avahi_publish.py", "rpc_tls.py"]) {
    await copyFile(join(root, "linux-host", helper), join(destination, helper));
  }
  const launcher = join(destination, "vosc-render-host");
  await writeFile(launcher, '#!/bin/sh\nexec node "$(dirname -- "$0")/host.cjs" "$@"\n', { mode: 0o755 });
  const renderer = join(destination, "renderer");
  await mkdir(renderer);
  if (webgpuTools) {
    execFileSync("/usr/bin/python3", [fileURLToPath(new URL("./stage_webgpu_tools.py", import.meta.url)),
      "stage", webgpuTools, join(renderer, "webgpu-tools")], { stdio: "inherit" });
  }
  const staged = join(renderer, "vosc");
  await copyFile(binary, staged);
  await chmod(staged, 0o755);
  await cp(join(sourceBin, "data"), join(renderer, "data"), { recursive: true, dereference: true });
  const hasBundledLibraries = await stat(join(sourceBin, "libs")).then(
    info => info.isDirectory(), error => { if (error.code === "ENOENT") return false; throw error; });
  if (hasBundledLibraries) await cp(join(sourceBin, "libs"), join(renderer, "libs"), { recursive: true, dereference: true });
  if (runtimeLibraries) {
    await cp(runtimeLibraries, join(destination, "runtime-libs"), { recursive: true, dereference: true });
  }
  const manifest = {
    version: 1,
    platform: "linux-x64",
    sourceRevision: values["source-revision"],
    sdkRevision: values["sdk-revision"],
    renderer: "renderer/vosc",
    data: "renderer/data",
    ...(runtimeLibraries ? { runtimeLibraries: "runtime-libs" } : {}),
    files: await inventory(destination, destination),
  };
  await writeFile(join(destination, "manifest.json"), JSON.stringify(manifest, null, 2) + "\n");
  console.log(destination);
} catch (error) {
  await rm(destination, { recursive: true, force: true });
  throw error;
}
