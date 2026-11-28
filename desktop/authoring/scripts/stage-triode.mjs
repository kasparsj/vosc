// Build the source-backed compiler artifact before Editor's offline build.
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdir, mkdtemp, readFile, realpath, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { build, version } from "esbuild";
import {
  adapterSource, compilerInputs, digest, disposition, editor, entry, metadataFiles, pinPath, readPin,
  readStagedCompiler, reviewedProofSha256, reviewedRevision, stagePath, triodeInputs,
} from "./triode-compiler.mjs";

async function buildCompiler(source) {
  const result = await build({
    absWorkingDir: source,
    entryPoints: [entry],
    platform: "neutral",
    format: "esm",
    target: "es2023",
    bundle: true,
    write: false,
    metafile: true,
    loader: { ".glsl": "text", ".frag": "text", ".vert": "text" },
  });
  assert.deepEqual(Object.keys(result.metafile.inputs).sort(), compilerInputs,
    "Compiler inputs differ from the staged Triode modules and Editor adapter");
  assert.ok(Object.values(result.metafile.outputs).every((output) => output.imports.length === 0),
    "Triode compiler must be self-contained");
  return result.outputFiles[0].contents;
}

async function writeFiles(root, files) {
  for (const [relative, bytes] of files) {
    const path = resolve(root, relative);
    await mkdir(dirname(path), { recursive: true });
    await writeFile(path, bytes);
  }
}

const [, , checkout, option, ...extra] = process.argv;
if (!checkout || (option && option !== "--record-pin") || extra.length) {
  throw new Error("Usage: node scripts/stage-triode.mjs <triode-checkout> [--record-pin] | --verify-staged");
}
if (checkout === "--verify-staged") {
  assert.ok(!option, "--verify-staged takes no extra arguments");
  const { pin } = await readStagedCompiler();
  const rebuilt = await buildCompiler(fileURLToPath(new URL("source/", stagePath)));
  assert.equal(digest(rebuilt), pin.bundleSha256, "Staged compiler rebuild differs from the pin");
  console.log(`Rebuilt pinned Triode compiler ${pin.bundleSha256} from staged sources`);
  process.exit(0);
}

const source = await realpath(checkout);
const revision = execFileSync("git", ["rev-parse", "HEAD"], { cwd: source, encoding: "utf8" }).trim();
const expectedPin = option === "--record-pin" ? null : await readPin();
if (expectedPin) {
  assert.equal(revision, expectedPin.compilerRevision, "Triode checkout does not match the pinned compiler commit");
}
execFileSync("git", ["merge-base", "--is-ancestor", reviewedRevision, revision], { cwd: source });

const copies = [...metadataFiles, ...triodeInputs].map(relative => [
  relative, execFileSync("git", ["show", `${revision}:${relative}`], { cwd: source }),
]);
// Keep the qualified bundle's source paths stable while combining both repositories.
copies.push([entry, await readFile(new URL(adapterSource, editor))]);
copies.sort(([a], [b]) => a < b ? -1 : a > b ? 1 : 0);
const sourceSha256 = Object.fromEntries(copies.map(([path, bytes]) => [path, digest(bytes)]));
const temporary = await mkdtemp(resolve(tmpdir(), "vosc-triode-"));
let bundle;
try {
  await writeFiles(temporary, copies);
  bundle = await buildCompiler(temporary);
} finally {
  await rm(temporary, { recursive: true, force: true });
}

const pin = {
  schemaVersion: 2,
  baseRevision: reviewedRevision,
  compilerRevision: revision,
  proofSha256: reviewedProofSha256,
  entry,
  adapterSource,
  esbuild: version,
  distribution: disposition,
  externalDistribution: "pending-disposition",
  bundleSha256: digest(bundle),
  compilerInputs,
  sourceSha256,
};
if (option === "--record-pin") {
  await writeFile(pinPath, JSON.stringify(pin, null, 2) + "\n", { flag: "wx" });
} else {
  assert.deepEqual(pin, expectedPin, "Triode compiler source, dependency, or bundle differs from the checked-in pin");
}

await writeFiles(fileURLToPath(stagePath),
  [["compiler.mjs", bundle], ...copies.map(([path, bytes]) => [`source/${path}`, bytes])]);
console.log(`Staged Triode compiler ${pin.bundleSha256} in build/triode/`);
