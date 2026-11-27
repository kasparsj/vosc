import { build } from "esbuild";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const cwd = fileURLToPath(new URL("../", import.meta.url));
const out = "build/workspace.test.cjs";
await build({
  absWorkingDir: cwd,
  entryPoints: ["tests/workspace.test.ts"],
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
  outfile: out,
  target: "node24",
});
const result = spawnSync(process.execPath, ["--test", out], { cwd, stdio: "inherit" });
process.exitCode = result.status ?? 1;
