import assert from "node:assert/strict";
import { execFileSync, spawn, spawnSync } from "node:child_process";
import { createHash } from "node:crypto";
import { once } from "node:events";
import { chmod, copyFile, mkdir, mkdtemp, readdir, readFile, realpath, rm, stat, unlink, writeFile } from "node:fs/promises";
import { createServer } from "node:net";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";

const stage = fileURLToPath(new URL("./stage_linux_host.mjs", import.meta.url));
const stockAssets = [
  "shaders/passthru.vert",
  "shaders/include/of_default_uniforms.glsl",
  "shaders/include/of_default_vertex_in_attributes.glsl",
];

test("stage an internal host bundle without modifying the renderer inputs", { timeout: 15000 }, async () => {
  const root = await mkdtemp(join(await realpath(tmpdir()), "vosc-runtime-"));
  try {
    const input = join(root, "bin");
    await mkdir(join(input, "data/shaders/include"), { recursive: true });
    for (const path of stockAssets) {
      await copyFile(new URL("../../../assets/" + path, import.meta.url), join(input, "data", path));
    }
    const binary = join(input, "vosc");
    if (process.platform === "linux" && process.arch === "x64") await copyFile(process.execPath, binary);
    else {
      const header = Buffer.alloc(20);
      Buffer.from([127, 69, 76, 70, 2, 1]).copy(header);
      header.writeUInt16LE(62, 18);
      await writeFile(binary, header);
    }
    await chmod(binary, 0o755);
    const output = join(root, "bundle");
    const args = [stage, "--output", output, "--renderer-bin", binary,
      "--source-revision", "a".repeat(40), "--sdk-revision", "b".repeat(40)];
    execFileSync(process.execPath, args, { timeout: 5000 });
    const manifest = JSON.parse(await readFile(join(output, "manifest.json"), "utf8"));
    assert.equal(manifest.platform, "linux-x64");
    const digest = createHash("sha256").update(await readFile(binary)).digest("hex");
    assert.equal(manifest.files.find(item => item.path === "renderer/vosc").sha256, digest);
    assert.equal((await stat(output)).mode & 0o777, 0o700);
    for (const path of stockAssets) {
      const stagedPath = "renderer/data/" + path;
      const original = await readFile(join(input, "data", path));
      assert.deepEqual(await readFile(join(output, stagedPath)), original);
      assert.equal(manifest.files.find(item => item.path === stagedPath).sha256,
        createHash("sha256").update(original).digest("hex"));
    }
    const missingAsset = stockAssets.at(-1);
    await unlink(join(input, "data", missingAsset));
    const missingOutput = output + "-missing";
    const missing = spawnSync(process.execPath, [stage, "--output", missingOutput, ...args.slice(3)], {
      encoding: "utf8", timeout: 5000,
    });
    assert.equal(missing.status, 1);
    assert.ok(missing.stderr.includes("Missing stock renderer asset") && missing.stderr.includes(missingAsset));
    await assert.rejects(stat(missingOutput), { code: "ENOENT" });
    await copyFile(new URL("../../../assets/" + missingAsset, import.meta.url), join(input, "data", missingAsset));
    assert.notEqual(spawnSync(process.execPath, args, { timeout: 5000 }).status, 0, "staging refuses an existing bundle");
    assert.match(execFileSync(process.execPath, [join(output, "host.cjs"), "--help"], { encoding: "utf8" }),
      /vosc-render-host pair approve/);

    if (process.platform !== "linux" || process.arch !== "x64") return;
    const reservePort = async () => {
      const server = createServer().listen(0, "127.0.0.1");
      await once(server, "listening");
      return server;
    };
    const reserved = await Promise.all([reservePort(), reservePort()]);
    const [bootstrap, rpc] = reserved.map(server => String(server.address().port));

    const state = join(root, "state");
    const runtime = join(root, "runtime");
    const registry = join(runtime, "vosc-hosts");
    await mkdir(runtime, { mode: 0o700 });
    const entry = join(output, "host.cjs");
    const serve = [entry, "serve", "--state-dir", state, "--bind", "127.0.0.1",
      "--bootstrap-port", bootstrap, "--rpc-port", rpc];
    const release = server => new Promise(resolve => server.close(resolve));
    try {
      await release(reserved[0]);
      const occupied = spawnSync(process.execPath, serve, { encoding: "utf8", timeout: 5000 });
      assert.equal(occupied.status, 1);
      assert.equal(occupied.stdout, "");
      assert.match(occupied.stderr, new RegExp(`Authenticated RPC port ${rpc} is already in use.*--rpc-port`));

      const bootstrapProbe = createServer();
      bootstrapProbe.listen(Number(bootstrap), "127.0.0.1");
      await once(bootstrapProbe, "listening");
      await release(bootstrapProbe);
    } finally {
      await release(reserved[1]);
    }

    const start = async (...flags) => {
      const child = spawn(process.execPath, [...serve, ...flags], {
        env: { ...process.env, XDG_RUNTIME_DIR: runtime }, stdio: ["ignore", "pipe", "pipe"],
      });
      let output = "";
      let diagnostics = "";
      child.stderr.setEncoding("utf8").on("data", text => { diagnostics += text; });
      const exited = once(child, "exit");
      const ready = await Promise.race([
        new Promise(resolve => child.stdout.setEncoding("utf8").on("data", text => {
          output += text;
          if (output.includes("\n")) resolve(JSON.parse(output.split("\n")[0]));
        })),
        exited.then(([code]) => { throw new Error(`Service exited before readiness (${code}) ${diagnostics}`); }),
      ]);
      return { child, ready, exited };
    };

    const first = await start("--no-discovery");
    try {
      assert.equal(first.ready.event, "ready");
      assert.equal(first.ready.discovery, false);
      await assert.rejects(stat(registry), { code: "ENOENT" });
      assert.equal((await stat(join(state, "admin.sock"))).mode & 0o777, 0o600);
      assert.deepEqual(JSON.parse(execFileSync(process.execPath, [entry, "pair", "list", "--state-dir", state], { timeout: 5000 })),
        []);
      const duplicate = spawnSync(process.execPath, serve, { encoding: "utf8", timeout: 3000 });
      assert.equal(duplicate.status, 1);
      assert.match(duplicate.stderr, /profile is already in use/);
    } finally {
      first.child.kill("SIGTERM");
      await first.exited;
    }
    const restarted = await start();
    try {
      assert.equal(restarted.ready.target, first.ready.target);
      assert.equal(restarted.ready.fingerprint, first.ready.fingerprint);
      const advertisements = await readdir(registry, { withFileTypes: true });
      assert.equal(advertisements.length, 1);
      assert.ok(advertisements[0].isSocket());
    } finally {
      restarted.child.kill("SIGTERM");
      await restarted.exited;
    }
    assert.deepEqual(await readdir(registry), []);
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});
