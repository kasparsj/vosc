import assert from "node:assert/strict";
import { readFile, realpath, writeFile } from "node:fs/promises";
import { join, resolve } from "node:path";
import { setTimeout as delay } from "node:timers/promises";
import { NativeRenderer, type NativeObject } from "../linux-host/native-renderer";

const [installation, shader, output] = process.argv.slice(2).map(path => resolve(path));
if (!installation || !shader || !output) {
  throw new Error("Usage: node verify-webgpu-m1.mjs <host install> <WGSL fixture> <evidence.json>");
}
const source = await readFile(shader, "utf8");
const renderer = await NativeRenderer.start({ installDirectory: installation, validation: false });

async function status() {
  return renderer.query("draft_m1_status");
}

async function later(field: string, value: unknown) {
  assert.equal(typeof value, "number");
  const deadline = performance.now() + 5000;
  do {
    await delay(40);
    const next = await status();
    if (Number(next[field]) > Number(value)) return next;
  } while (performance.now() < deadline);
  throw new Error(`No subsequent frame in ${field}`);
}

function expectBackend(state: NativeObject, pid: unknown, webgpu: boolean) {
  assert.equal(state.pid, pid);
  assert.equal(state.active, webgpu ? "webgpu" : "opengl");
  assert.equal(state.backend, webgpu ? "Vulkan" : "OpenGL");
  assert.equal(typeof state.adapter, "string");
  assert.notEqual(state.adapter, "");
  assert.equal(state.opengl_visible, !webgpu);
  assert.equal(state.webgpu_visible, webgpu);
}

async function captureGl() {
  const receipt = await renderer.mutate("capture_preview", { max_width: 64 }, undefined, "captured");
  const result = receipt.result as NativeObject;
  assert.equal(result.mime_type, "image/png");
  const png = Buffer.from(String(result.data_base64), "base64");
  assert.equal(png.subarray(0, 8).toString("hex"), "89504e470d0a1a0a");
  return { frame: receipt.frame, png };
}

try {
  await renderer.mutate("publish_shader", {
    layer: 0,
    source: "#version 150\nout vec4 fragColor;\nvoid main() { fragColor = vec4(0.125, 0.25, 0.75, 1.0); }\n",
  });
  const baseline = await status();
  assert.ok(Number.isSafeInteger(baseline.pid));
  const pid = baseline.pid;
  expectBackend(baseline, pid, false);
  assert.equal(await realpath(`/proc/${pid}/exe`), await realpath(join(installation, "renderer/vosc")));
  const before = await captureGl();

  const badId = "m1-invalid-wgsl";
  const rejectedAt = performance.now();
  await assert.rejects(renderer.mutate("draft_m1_switch", {
    backend: "webgpu", source: "@fragment fn fs_main() -> @location(0) vec4f { unavailable(); }",
  }, badId), /WGSL pipeline creation failed/);
  const rejectedRoundTripMs = Math.round(performance.now() - rejectedAt);
  const rejected = await renderer.query("get_request", { request_id: badId });
  assert.equal(rejected.status, "failed");
  assert.equal((rejected.error as NativeObject).code, "m1_switch_failed");
  expectBackend(rejected.result as NativeObject, pid, false);
  const afterFailure = await later("opengl_frame", (rejected.result as NativeObject).opengl_frame);
  expectBackend(afterFailure, pid, false);
  const after = await captureGl();
  assert.ok(Number(after.frame) > Number(before.frame));
  assert.deepEqual(after.png, before.png);

  const activated = await renderer.mutate("draft_m1_switch", { backend: "webgpu", source });
  const capture = activated.result as NativeObject;
  expectBackend(capture, pid, true);
  assert.equal(capture.webgpu_frames, 1);
  assert.ok(Number(capture.capture_width) >= 64 && Number(capture.capture_height) >= 64);
  const width = Number(capture.capture_width);
  const height = Number(capture.capture_height);
  assert.deepEqual(capture.pixels, [
    { x: Math.floor(width / 4), y: Math.floor(height / 4), rgba: [255, 64, 0, 255] },
    { x: Math.floor(3 * width / 4), y: Math.floor(height / 4), rgba: [0, 128, 255, 255] },
    { x: Math.floor(width / 4), y: Math.floor(3 * height / 4), rgba: [191, 0, 128, 255] },
    { x: Math.floor(3 * width / 4), y: Math.floor(3 * height / 4), rgba: [0, 255, 64, 255] },
  ]);
  const continued = await later("webgpu_frames", capture.webgpu_frames);
  expectBackend(continued, pid, true);

  const restored = await renderer.mutate("draft_m1_switch", { backend: "opengl" });
  const returned = restored.result as NativeObject;
  expectBackend(returned, pid, false);
  assert.ok(Number(returned.opengl_frame) > Number(continued.opengl_frame));
  const final = await captureGl();
  assert.deepEqual(final.png, before.png);

  const evidence = {
    baseline, rejected, rejectedRoundTripMs, afterFailure, openGlCaptureFrames: [before.frame, after.frame, final.frame],
    activated, continued, restored,
  };
  await writeFile(output, JSON.stringify(evidence, null, 2) + "\n");
  console.log(`PASS packaged renderer ${pid} OpenGL → Vulkan → OpenGL with four exact RGBA samples`);
} finally {
  await renderer.stop();
}
