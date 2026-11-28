import { build } from "esbuild";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const cwd = fileURLToPath(new URL("../", import.meta.url));
const entryPoints = ["workspace", "documents", "editor-imports"];
if (process.platform === "linux") entryPoints.push("local-discovery");
const outputDirectory = "build/tests";
await build({
  absWorkingDir: cwd,
  entryPoints: entryPoints.map(name => `tests/${name}.test.ts`),
  bundle: true,
  platform: "node",
  plugins: [{
    name: "test-electron-storage",
    setup(build) {
      build.onResolve({ filter: /^electron$/ }, () => ({ path: "electron", namespace: "test-electron" }));
      build.onLoad({ filter: /.*/, namespace: "test-electron" }, () => ({
        // Tests inject a disposable TLS identity and never access macOS Keychain.
        contents: "export const safeStorage = undefined;",
      }));
    },
  }],
  format: "cjs",
  outdir: outputDirectory,
  outExtension: { ".js": ".cjs" },
  target: "node24",
});
const result = spawnSync(process.execPath, ["--test", ...entryPoints.map(name => `${outputDirectory}/${name}.test.cjs`)], { cwd, stdio: "inherit" });
process.exitCode = result.status ?? 1;
