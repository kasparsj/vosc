import assert from "node:assert/strict";
import { createHash, randomUUID } from "node:crypto";
import { readFile, realpath, writeFile } from "node:fs/promises";
import { createConnection } from "node:net";
import { join, resolve } from "node:path";
import { setTimeout as delay } from "node:timers/promises";
import { Draft } from "../linux-host/draft";
import type { WireRequest } from "../linux-host/transport";
import { NativeRenderer, type NativeObject } from "../linux-host/native-renderer";

const [installation, output] = process.argv.slice(2, 4).map(path => resolve(path));
const recovery = process.argv.includes("--recovery");
if (!installation || !output) {
  throw new Error("Usage: node verify-webgpu-m2a.mjs <host install> <evidence.json> [--recovery]");
}

const fixtures = resolve("desktop/authoring/tests/fixtures/webgpu");
const quadrants = await readFile(join(fixtures, "m1-quadrants.wgsl"));
const uniforms = await readFile(join(fixtures, "m2a-uniforms.wgsl"));
const glsl = Buffer.from("#version 150\nout vec4 color; void main() { color = vec4(0.1, 0.2, 0.3, 1); }\n");
const invalid = Buffer.from("@fragment fn fs_main(@location(0) uv: vec2f) -> @location(0) vec4f {\n    return unknown;\n}\n");
const extraBinding = Buffer.from("@group(1) @binding(0) var<uniform> extra: vec4f;\n@fragment fn fs_main() -> @location(0) vec4f { return extra; }\n");
const renderer = await NativeRenderer.start({ installDirectory: installation, validation: false });
const lease = "wgsl-preview";
const controlDigest = "a".repeat(64);
const status = () => renderer.query("draft_m1_status");
const scene = () => renderer.query("get_scene");
const digest = (source: Buffer) => createHash("sha256").update(source).digest("hex");
function params(source: Buffer, token: string, revision: unknown, language?: string, owner = lease) {
  return { source_base64: source.toString("base64"), source_digest: digest(source), lease: owner, token,
    revision, slots: {}, bindings: {}, ...(language ? { language } : {}) };
}
function prepare(source: Buffer, token: string, revision: unknown, language = "wgsl", owner = lease) {
  return renderer.mutate("draft_prepare", params(source, token, revision, language, owner), token, "prepared");
}
function commit(token: string, revision: unknown, owner = lease) {
  return renderer.mutate("draft_commit", { lease: owner, token, revision, control_digest: controlDigest }, "commit-" + token);
}
async function supersedePending(pid: unknown, revision: unknown) {
  // One socket dispatches one request per update. Cold Dawn preparation
  // spans several updates, so the second request always displaces a pending one.
  const path = join(await realpath(`/proc/${pid}/cwd`), "control.sock");
  const first = "cold-superseded";
  const second = "invalid-before";
  const socket = createConnection(path);
  socket.setTimeout(4500, () => socket.destroy(new Error("Missing draft prepare admissions")));
  const candidates = [{ id: first, source: quadrants }, { id: second, source: invalid }];
  socket.write(candidates.map(({ id, source }) =>
    JSON.stringify({ id, method: "draft_prepare", params: params(source, id, revision, "wgsl") })
  ).join("\n") + "\n");
  const admissions: NativeObject[] = [];
  let pending = "";
  try {
    for await (const chunk of socket.setEncoding("utf8")) {
      pending += chunk;
      const lines = pending.split("\n");
      pending = lines.pop()!;
      admissions.push(...lines.map(line => JSON.parse(line)));
      if (admissions.length === 2) break;
    }
  } finally {
    socket.destroy();
  }
  assert.deepEqual(admissions.map(({ id, status }) => [id, status]), [[first, "queued"], [second, "queued"]]);
  await assert.rejects(renderer.recover(first, "prepared"), { code: "stale_candidate" });
  await assert.rejects(renderer.recover(second, "prepared"), { code: "shader_failed" });
  await assert.rejects(commit(first, revision), { code: "stale_candidate" });
  return admissions;
}
async function retained(revision: unknown, token: string, previousFrames: unknown) {
  const live = await scene();
  assert.equal(live.revision, revision);
  assert.equal((live.draft_activation as NativeObject).token, token);
  assert.equal(live.backend, "webgpu");
  assert.deepEqual(live.layers, []);
  assert.deepEqual(live.draft_slots, {});
  for (let i = 0; i < 20; ++i) {
    const state = await status();
    if (Number(state.webgpu_frames) > Number(previousFrames)) return state;
    await delay(35);
  }
  throw new Error("Retained WebGPU pipeline did not continue presenting");
}
function check(receipt: NativeObject, source: Buffer, token: string, previous: unknown, expected: number[][]) {
  const drawn = receipt.result as NativeObject;
  const live = drawn.scene as NativeObject;
  assert.equal(receipt.revision, Number(previous) + 1);
  assert.equal(drawn.first_frame, receipt.frame);
  assert.equal(live.frame, receipt.frame);
  assert.equal(live.rendered_revision, receipt.revision);
  assert.equal(drawn.backend, "webgpu");
  assert.equal(live.backend, "webgpu");
  assert.equal(drawn.token, token);
  assert.equal(drawn.lease, lease);
  assert.equal(drawn.source_digest, digest(source));
  assert.equal(drawn.control_digest, controlDigest);
  assert.equal(drawn.commit_id, receipt.id);
  const pixels = drawn.pixels as { x: number; y: number; rgba: number[] }[];
  assert.deepEqual(pixels.map(({ x, y }) => [x, y]), [[1, 1], [3, 1], [1, 3], [3, 3]].map(([x, y]) =>
    [Math.floor(x * Number(drawn.capture_width) / 4), Math.floor(y * Number(drawn.capture_height) / 4)]));
  assert.deepEqual(pixels.map(pixel => pixel.rgba.slice(0, expected[0].length)), expected);
  return pixels;
}

async function runHost(wgsl: boolean) {
  const target = "verify-webgpu-m2a";
  const draft = new Draft(target, installation);
  const hostLease = randomUUID();
  const documentId = randomUUID();
  function request(method: string, state: NativeObject = {}, params: NativeObject = {}) {
    const value: WireRequest = { id: randomUUID(), method, params, version: 1, target,
      epoch: state.epoch as string | undefined, revision: state.revision as number | undefined };
    return draft.execute(value);
  }
  function prepareHost(source: Buffer, cold: NativeObject, language?: string) {
    return request("run.prepare", cold, { source: source.toString("base64"), sha256: digest(source),
      lease: hostLease, documentId, generation: 0, editSequence: 0, runSequence: 1,
      slots: [], bindings: {}, ...(language ? { language } : {}) });
  }
  try {
    const cold = await request("health");
    if (!wgsl) await assert.rejects(prepareHost(quadrants, cold, "wgsl"), { code: "wgsl_unavailable" });
    const source = wgsl ? quadrants : glsl;
    const staged = await prepareHost(source, cold, wgsl ? "wgsl" : undefined);
    const state = await request("run.commit", staged, { lease: hostLease, token: staged.token });
    assert.equal(state.sourceDigest, digest(source));
    const receipt = state.receipt as NativeObject;
    assert.equal(receipt.status, "rendered");
    assert.equal(state.frame, receipt.frame);
    if (wgsl) assert.equal(state.backend, "webgpu");
    else assert.notEqual(state.backend, "webgpu"); // Older GL-only renderers omit the backend field.
    return state;
  } finally {
    await draft.reset();
  }
}

try {
  const initial = await scene();
  await renderer.mutate("draft_prepare", params(glsl, "glsl", initial.revision), "glsl", "prepared");
  const gl = await commit("glsl", initial.revision);
  const evidence: NativeObject = { gl };
  if (!renderer.supportsDraftWgsl) {
    await renderer.stop();
    evidence.linuxHost = await runHost(false);
    await writeFile(output, JSON.stringify(evidence, null, 2) + "\n");
    console.log("PASS Dawn-free renderer rejects WGSL and accepts the existing GLSL transaction");
  } else {
    const glStatus = await status();
    assert.equal(await realpath(`/proc/${glStatus.pid}/exe`), await realpath(join(installation, "renderer/vosc")));
    const pendingSupersession = await supersedePending(glStatus.pid, gl.revision);
    assert.equal((await status()).opengl_visible, true);
    assert.equal((await scene()).revision, gl.revision);

    await prepare(quadrants, "quadrants", gl.revision);
    const first = await commit("quadrants", gl.revision);
    check(first, quadrants, "quadrants", gl.revision, [[255, 64, 0, 255], [0, 128, 255, 255], [191, 0, 128, 255], [0, 255, 64, 255]]);
    const firstStatus = await status();
    assert.equal(firstStatus.pid, glStatus.pid);
    assert.equal(firstStatus.backend, "Vulkan");
    assert.equal(firstStatus.opengl_visible, false);
    assert.equal(firstStatus.webgpu_visible, true);

    await assert.rejects(prepare(invalid, "invalid-active", first.revision), { code: "shader_failed" });
    await assert.rejects(prepare(extraBinding, "extra-binding", first.revision), { code: "shader_failed" });
    await assert.rejects(prepare(glsl, "glsl-after", first.revision, "glsl"), { code: "backend_switch_required" });
    await assert.rejects(renderer.query("export_scene"), { code: "export_unsupported" });
    await assert.rejects(renderer.mutate("capture_preview", {}), { code: "wgsl_operation_unsupported" });
    const afterFailure = await retained(first.revision, "quadrants", firstStatus.webgpu_frames);

    await prepare(uniforms, "cancelled", first.revision);
    await renderer.mutate("draft_cancel", { lease, token: "cancelled" }, undefined, "ok");
    await assert.rejects(commit("cancelled", first.revision), { code: "stale_candidate" });

    const fenced = "revoked-lease";
    await prepare(uniforms, "fenced", first.revision, "wgsl", fenced);
    await renderer.mutate("draft_fence", { lease: fenced }, undefined, "ok");
    await assert.rejects(commit("fenced", first.revision, fenced), { code: "stale_lease" });

    // Race two preparations from the same base revision. Whether the first
    // compiled already or was still pending, it can no longer be committed.
    const older = prepare(quadrants, "superseded", first.revision).catch(error => {
      assert.equal(error.code, "stale_candidate");
    });
    await delay(40);
    await prepare(uniforms, "uniforms", first.revision);
    await older;
    await assert.rejects(commit("superseded", first.revision), { code: "stale_candidate" });
    const beforeReplacement = await retained(first.revision, "quadrants", afterFailure.webgpu_frames);
    const replacement = await commit("uniforms", first.revision);
    const rgb = [[0, 0, 255], [255, 0, 255], [0, 255, 255], [255, 255, 255]];
    const pixels = check(replacement, uniforms, "uniforms", first.revision, rgb);
    const replacementStatus = await status();
    assert.equal(replacementStatus.pid, firstStatus.pid);
    assert.equal(replacementStatus.adapter, firstStatus.adapter);
    assert.ok(Number(firstStatus.webgpu_context) > 0);
    assert.equal(replacementStatus.webgpu_context, firstStatus.webgpu_context);
    assert.ok(Number(replacementStatus.webgpu_frames) > Number(beforeReplacement.webgpu_frames));

    // Use a slower time scale so the captured byte stays below saturation.
    const clock = Buffer.from(uniforms.toString().replace("renderer.time, 0.0, 1.0", "renderer.time / 100.0, 0.0, 1.0"));
    await prepare(clock, "clock-before", replacement.revision);
    const initialClock = await commit("clock-before", replacement.revision);
    const beforeTime = check(initialClock, clock, "clock-before", replacement.revision, rgb)[0].rgba[3];
    await delay(450);
    await prepare(clock, "clock-after", initialClock.revision);
    const laterClock = await commit("clock-after", initialClock.revision);
    const afterTime = check(laterClock, clock, "clock-after", initialClock.revision, rgb)[0].rgba[3];
    assert.ok(afterTime > beforeTime, `Expected GPU elapsed time to advance (${beforeTime} -> ${afterTime})`);

    Object.assign(evidence, { glStatus, pendingSupersession, first, firstStatus, afterFailure, replacement, replacementStatus, laterClock,
      clockSamples: { beforeTime, afterTime }, replacementPixels: pixels });
    if (recovery) {
      const failToken = "test-fail-first-draft-draw";
      await prepare(quadrants, failToken, laterClock.revision);
      await assert.rejects(commit(failToken, laterClock.revision), { code: "draft_rolled_back" });
      const rejectedPresent = await renderer.query("get_request", { request_id: "commit-" + failToken });
      assert.equal((rejectedPresent.result as NativeObject).restored_previous, true);
      evidence.rejectedPresent = rejectedPresent;
      evidence.afterRejectedPresent = await retained(laterClock.revision, "clock-after", replacementStatus.webgpu_frames);
    }
    await renderer.stop();
    evidence.linuxHost = await runHost(true);
    await writeFile(output, JSON.stringify(evidence, null, 2) + "\n");
    console.log(`PASS packaged renderer ${firstStatus.pid} presented WGSL on Vulkan and retained prior output on failure`);
  }
} finally {
  await renderer.stop();
}
