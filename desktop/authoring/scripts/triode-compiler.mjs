// The runtime consumes exact staged bytes. It never resolves a Triode checkout.
import { createHash } from "node:crypto";
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { version } from "esbuild";

export const editor = new URL("../", import.meta.url);
export const pinPath = new URL("triode-compiler-pin.json", editor);
export const stagePath = new URL("build/triode/", editor);
export const entry = "src/vosc-single-pass.js";
export const adapterSource = "compiler/vosc-single-pass.js";
export const triodeInputs = ["src/generate-glsl.js", "src/glsl/single-pass-transforms.js", "src/types.js"];
export const compilerInputs = [...triodeInputs, entry];
export const metadataFiles = ["LICENSE", "NOTICE", "package.json", "package-lock.json"];
export const disposition = "private-qualification-only";
export const reviewedRevision = "fdd9421dab4b119131c8bec54fe88965949b179f";
export const reviewedProofSha256 = "4de6fb6eb84fe472515a1d70bcd8d85aeb2d5acfd90d84c32c48d3d557795260";
export const digest = (bytes) => createHash("sha256").update(bytes).digest("hex");

const digestPattern = /^[0-9a-f]{64}$/;
const revisionPattern = /^[0-9a-f]{40}$/;
export async function readPin() {
  let pin;
  try {
    pin = JSON.parse(await readFile(pinPath, "utf8"));
  } catch (error) {
    throw new Error("Cannot read the Triode compiler pin (triode-compiler-pin.json).", { cause: error });
  }
  if (!pin || pin.schemaVersion !== 2 || pin.entry !== entry || pin.adapterSource !== adapterSource ||
      pin.esbuild !== version ||
      pin.baseRevision !== reviewedRevision || pin.distribution !== disposition ||
      !revisionPattern.test(pin.compilerRevision) || pin.proofSha256 !== reviewedProofSha256 ||
      pin.externalDistribution !== "pending-disposition" || !digestPattern.test(pin.bundleSha256) ||
      JSON.stringify(pin.compilerInputs) !== JSON.stringify(compilerInputs) ||
      !pin.sourceSha256 || typeof pin.sourceSha256 !== "object" ||
      JSON.stringify(Object.keys(pin.sourceSha256).sort()) !==
        JSON.stringify([...metadataFiles, ...compilerInputs].sort()) ||
      !Object.values(pin.sourceSha256).every((hash) => typeof hash === "string" && digestPattern.test(hash))) {
    throw new Error("Invalid Triode compiler pin or mismatched esbuild version.");
  }
  return pin;
}

export async function readStagedCompiler() {
  const pin = await readPin();
  async function verified(relative, sha256) {
    let bytes;
    try {
      bytes = await readFile(new URL(relative, stagePath));
    } catch (error) {
      throw new Error(`Missing Triode compiler input build/triode/${relative}. Run node scripts/stage-triode.mjs <triode-checkout> or stage the verified compiler artifact.`, { cause: error });
    }
    if (digest(bytes) !== sha256) throw new Error(`Triode compiler input SHA-256 mismatch: build/triode/${relative}.`);
    return bytes;
  }
  const bundle = await verified("compiler.mjs", pin.bundleSha256);
  await Promise.all(Object.entries(pin.sourceSha256).map(([path, sha256]) => verified(`source/${path}`, sha256)));
  return { pin, bundle: bundle.toString("utf8") };
}

export function compilerPlugin({ pin, bundle }, { includeCompiler = true } = {}) {
  return {
    name: "vosc-triode",
    setup(build) {
      build.onResolve({ filter: /^vosc-triode-(compiler|identity)$/ }, ({ path }) => {
        if (!includeCompiler && path === "vosc-triode-compiler") {
          return { errors: [{ text: "Triode compilation belongs in the isolated Editor worker." }] };
        }
        return { path, namespace: "vosc-triode" };
      });
      build.onLoad({ filter: /.*/, namespace: "vosc-triode" }, ({ path }) => ({
        contents: path === "vosc-triode-compiler"
          ? bundle
          : `export const TRIODE_COMPILER_ID = ${JSON.stringify(pin.bundleSha256)};\n`,
        resolveDir: fileURLToPath(stagePath),
      }));
    },
  };
}
