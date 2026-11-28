import assert from "node:assert/strict";
import { chmod, mkdir, mkdtemp, realpath, rm, stat, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { test } from "node:test";
import { NativeRenderer } from "../native-renderer";

const fixture = `
const fs = require("node:fs");
const net = require("node:net");
const receipts = new Map();
const calls = [];
const operations = ["configure_layers", "publish_shader", "get_scene", "get_request",
  "draft_prepare", "draft_commit", "draft_cancel", "draft_fence",
  "draft_sample", "draft_callback", "draft_configure_slot", "draft_bind_uniform"];
const preview = process.argv.includes("--draft-preview");
const socket = process.env.VOSC_CONTROL_SOCKET;
if (process.env.DISPLAY === "ignore-sigterm") process.on("SIGTERM", () => {});
const server = net.createServer(connection => {
  connection.on("error", () => {});
  let buffered = "";
  connection.on("data", chunk => {
    buffered += chunk;
    const end = buffered.indexOf("\\n");
    if (end === -1) return;
    const { id, method, params } = JSON.parse(buffered.slice(0, end));
    calls.push({ id, method, params });
    const reply = body => connection.end(JSON.stringify({ id, ok: true, ...body }) + "\\n");
    const drawn = value => ({ id: value, ok: true, status: "rendered", revision: 2, frame: 6 });
    switch (method) {
      case "get_capabilities":
        reply({ result: { version: 1, transport: "unix-ndjson", operations:
          process.env.DISPLAY === "missing-callback" ? operations.filter(operation => operation !== "draft_callback") : operations } });
        break;
      case "configure_layers":
      case "draft_commit":
        receipts.set(id, { ...drawn(id), status: "queued" });
        reply({ status: "queued" });
        break;
      case "draft_lost_reply":
        receipts.set(id, drawn(id));
        connection.destroy();
        break;
      case "get_request": {
        const receipt = receipts.get(params.request_id);
        if (receipt?.status === "queued") {
          receipt.status = "rolling_back";
          reply({ result: receipt });
        } else if (receipt) {
          reply({ result: drawn(receipt.id) });
        } else {
          reply({ ok: false, error: { code: "request_not_found", message: "receipt unknown or evicted" } });
        }
        break;
      }
      case "describe":
        reply({ result: {
          preview, pid: process.pid, root: process.cwd(), args: process.argv.slice(2),
          osc: process.env.VOSC_OSC_PORT ?? null, preload: process.env.LD_PRELOAD ?? null,
          library: process.env.LD_LIBRARY_PATH, shaderTools: process.env.VOSC_DRAFT_SHADER_TOOLS, calls,
        } });
        break;
      case "wrong-id":
        reply({ id: "wrong", result: {} });
        break;
      case "native-error":
        reply({ ok: false, error: { code: "draft_rolled_back", message: "retained",
          diagnostics: Array.from({ length: 20 }, () => ({ line: 1 })) } });
        break;
      case "too-big":
        connection.end(" ".repeat(1024 * 1024 + 1) + "\\n");
        break;
      case "stall":
        break;
      case "quit":
        process.exit(0);
        break;
    }
  });
});
server.listen(socket, () => fs.chmodSync(socket, 0o600));
`;

async function installation() {
  const root = await mkdtemp(join(tmpdir(), "vosc-native-fixture-"));
  const renderer = join(root, "renderer");
  await mkdir(renderer);
  const executable = join(renderer, "vosc");
  await writeFile(executable, `#!${process.execPath}\n${fixture}`);
  await chmod(executable, 0o700);
  for (const asset of [
    "shaders/passthru.vert",
    "shaders/include/of_default_uniforms.glsl",
    "shaders/include/of_default_vertex_in_attributes.glsl",
  ]) {
    const path = join(renderer, "data", asset);
    await mkdir(dirname(path), { recursive: true });
    await writeFile(path, asset);
  }
  return root;
}

test("private native startup, exact receipt recovery and owned cleanup", async () => {
  const installDirectory = await installation();
  const priorPort = process.env.VOSC_OSC_PORT;
  process.env.VOSC_OSC_PORT = "1337";
  const priorTools = process.env.VOSC_DRAFT_SHADER_TOOLS;
  process.env.VOSC_DRAFT_SHADER_TOOLS = "/untrusted/tools";
  let renderer: NativeRenderer | undefined;
  let runtime: string | undefined;
  try {
    renderer = await NativeRenderer.start({ installDirectory, validation: false });
    const state = await renderer.query("describe");
    runtime = String(state.root);
    assert.equal((await stat(runtime)).mode & 0o777, 0o700);
    assert.equal((await stat(join(runtime, "control.sock"))).mode & 0o777, 0o600);
    assert.equal((await stat(join(runtime, "renderer.log"))).mode & 0o777, 0o600);
    assert.ok(Array.isArray(state.args));
    assert.equal(await realpath(state.args[1]), join(runtime, "data"));
    assert.deepEqual(state.args, ["--draft-data-root", state.args[1], "--size", "640x480",
      "--fps", "30", "--draft-preview"]);
    assert.equal(state.preload, null);
    assert.equal(state.osc, null);
    assert.equal(state.library, join(installDirectory, "runtime-libs"));
    assert.equal(state.shaderTools, join(installDirectory, "renderer", "webgpu-tools"));
    const receipt = await renderer.mutate("draft_commit", {}, "exact");
    assert.equal(receipt.id, "exact");
    assert.equal(receipt.status, "rendered");
    await assert.rejects(renderer.mutate("draft_lost_reply", {}, "lost"), { code: "renderer_transport" });
    const recovered = await renderer.recover("lost", "rendered");
    assert.equal(recovered.id, "lost");
    const after = await renderer.query("describe");
    assert.ok(Array.isArray(after.calls));
    assert.equal(after.calls.filter(call => call.method === "draft_lost_reply").length, 1);
    assert.equal(after.calls.filter(call => call.method === "get_request" && call.params.request_id === "lost").length, 1);
    await Promise.all([renderer.stop(), renderer.stop()]);
    assert.equal(renderer.running, false);
    await assert.rejects(stat(runtime), { code: "ENOENT" });
  } finally {
    if (priorPort === undefined) delete process.env.VOSC_OSC_PORT;
    else process.env.VOSC_OSC_PORT = priorPort;
    if (priorTools === undefined) delete process.env.VOSC_DRAFT_SHADER_TOOLS;
    else process.env.VOSC_DRAFT_SHADER_TOOLS = priorTools;
    await renderer?.stop();
    await rm(installDirectory, { recursive: true, force: true });
  }
});

test("validator mode, response budget, exact correlation and child exit notification", async () => {
  const installDirectory = await installation();
  let renderer: NativeRenderer | undefined;
  try {
    let notified: (value: NativeRenderer) => void = () => {};
    const dead = new Promise<NativeRenderer>(resolve => { notified = resolve; });
    renderer = await NativeRenderer.start({ installDirectory, validation: true, onExit: notified });
    const state = await renderer.query("describe");
    assert.equal(state.preview, false);
    assert.ok(Array.isArray(state.args) && state.args.includes("160x120"));
    await assert.rejects(renderer.query("wrong-id"), { code: "renderer_protocol" });
    await assert.rejects(renderer.query("native-error"), error => {
      assert.ok(error instanceof Error && "code" in error && error.code === "draft_rolled_back");
      assert.ok("diagnostics" in error && Array.isArray(error.diagnostics) && error.diagnostics.length === 12);
      return true;
    });
    await assert.rejects(renderer.query("too-big"), { code: "renderer_transport" });
    await assert.rejects(renderer.query("stall", {}, performance.now() + 40), { code: "renderer_timeout" });
    await assert.rejects(renderer.query("quit"), { code: "renderer_transport" });
    assert.equal(await dead, renderer);
    assert.equal(renderer.running, false);
  } finally {
    await renderer?.stop();
    await rm(installDirectory, { recursive: true, force: true });
  }
});

test("startup rejects incompatible capabilities and a confirmed cleanup permits retry", async () => {
  const installDirectory = await installation();
  const priorDisplay = process.env.DISPLAY;
  process.env.DISPLAY = "missing-callback";
  let renderer: NativeRenderer | undefined;
  try {
    await assert.rejects(NativeRenderer.start({ installDirectory, validation: false }), { code: "renderer_capabilities" });
    process.env.DISPLAY = ":retry";
    renderer = await NativeRenderer.start({ installDirectory, validation: false });
    assert.equal((await renderer.query("describe")).preview, true);
  } finally {
    if (priorDisplay === undefined) delete process.env.DISPLAY;
    else process.env.DISPLAY = priorDisplay;
    await renderer?.stop();
    await rm(installDirectory, { recursive: true, force: true });
  }
});

test("cleanup reaps a renderer that ignores SIGTERM", async () => {
  const installDirectory = await installation();
  const priorDisplay = process.env.DISPLAY;
  process.env.DISPLAY = "ignore-sigterm";
  let renderer: NativeRenderer | undefined;
  try {
    renderer = await NativeRenderer.start({ installDirectory, validation: true });
    const { root, pid } = await renderer.query("describe");
    await renderer.stop();
    assert.throws(() => process.kill(Number(pid), 0), { code: "ESRCH" });
    await assert.rejects(stat(String(root)), { code: "ENOENT" });
  } finally {
    if (priorDisplay === undefined) delete process.env.DISPLAY;
    else process.env.DISPLAY = priorDisplay;
    await renderer?.stop();
    await rm(installDirectory, { recursive: true, force: true });
  }
});
