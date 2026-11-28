import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { test } from "node:test";
import { setTimeout as delay } from "node:timers/promises";
import { Draft } from "../draft";
import { NativeCleanupError, NativeRenderer, type NativeObject } from "../native-renderer";
import { WireError, type WireRequest } from "../transport";

const target = "test-renderer";
const lease = "d5ee0aaa-5a50-4fdc-9dc9-e5d15fed7044";
const documentId = "ef0a9cc5-5cd9-4725-9c96-682f370b74b3";
const slot = { id: "sTest", label: "Test", default: 0.25 };
const source = Buffer.from("#version 330\nout vec4 color; void main(){ color = vec4(1); }\n");
const intent = {
  source: source.toString("base64"),
  sha256: createHash("sha256").update(source).digest("hex"),
  lease, documentId, generation: 0, editSequence: 0, runSequence: 1,
  slots: [slot], bindings: {},
};

function request(method: string, state: NativeObject = {}, params: NativeObject = {}, id = method.replace(".", "-")): WireRequest {
  return { version: 1, target, method, id, params, epoch: state.epoch as string,
    revision: state.revision as number };
}

function deferred<T>() {
  let resolve!: (value: T) => void;
  const promise = new Promise<T>(done => { resolve = done; });
  return { promise, resolve };
}

function nativeScene(revision: number, frame: number, gain: number, sample = slot.default) {
  return {
    revision, rendered_revision: revision, external_epoch: 0, frame,
    draft_slots: { [slot.id]: sample }, draft_activation: { lease },
    layers: [{ shader: { loaded: true, program: 1, uniforms: {
      gain: { gl_type: 0x1406, writable: true, rendered_value: JSON.stringify(gain) },
    } } }],
  };
}

for (const validation of [false, true]) {
  test(`unconfirmed ${validation ? "validator" : "preview"} startup cleanup blocks both starts and reset`, async t => {
    const draft = new Draft(target, "/unused");
    const cold = await draft.execute(request("health"));
    const prepare = () => draft.execute(request("run.prepare", cold, intent));
    const validate = () => draft.execute(request("validate", {}, { source: intent.source, sha256: intent.sha256 }));
    const start = validation ? validate : prepare;
    const cleanupFailure = new NativeCleanupError();
    let starts = 0;
    const startup = t.mock.method(NativeRenderer, "start", async (options: Parameters<typeof NativeRenderer.start>[0]) => {
      assert.equal(options.validation, validation);
      if (++starts === 1) throw new WireError("renderer_capabilities", "Unsupported renderer");
      throw cleanupFailure;
    });

    await assert.rejects(start(), { code: "renderer_capabilities" });
    assert.equal((await draft.execute(request("health"))).epoch, cold.epoch);
    const sameCleanupFailure = (error: unknown) => error === cleanupFailure;
    await assert.rejects(start(), sameCleanupFailure);
    await assert.rejects(prepare(), sameCleanupFailure);
    await assert.rejects(validate(), sameCleanupFailure);
    await assert.rejects(draft.fence(request("run.fence", cold, { lease })), sameCleanupFailure);
    await assert.rejects(draft.reset(), sameCleanupFailure);
    assert.equal(startup.mock.callCount(), 2);
  });
}

test("reset waits for a starting child and refuses to confirm its failed cleanup", async t => {
  const draft = new Draft(target, "/unused");
  const started = deferred<NativeRenderer>();
  const entered = deferred<void>();
  const cleanupFailure = new NativeCleanupError();
  t.mock.method(NativeRenderer, "start", () => {
    entered.resolve();
    return started.promise;
  });
  const validation = draft.execute(request("validate", {}, { source: intent.source, sha256: intent.sha256 }));
  await entered.promise;
  const reset = draft.reset();
  let resetSettled = false;
  void reset.finally(() => { resetSettled = true; }).catch(() => {});
  await Promise.resolve();
  assert.equal(resetSettled, false);
  const stop = t.mock.fn(async () => { throw cleanupFailure; });
  started.resolve({ running: true, stop } as unknown as NativeRenderer);
  const sameCleanupFailure = (error: unknown) => error === cleanupFailure;
  await Promise.all([assert.rejects(validation, sameCleanupFailure), assert.rejects(reset, sameCleanupFailure)]);
  assert.equal(stop.mock.callCount(), 1);
});

test("real v1 request fields correlate the original draw and recover a lost reply without committing again", async t => {
  const draft = new Draft(target, "/unused");
  let scene = nativeScene(6, 100, 0);
  let commitReceipt: NativeObject | undefined;
  let recoveries = 0;
  let commits = 0;
  const native = {
    running: true,
    stop: async () => {},
    query: async () => scene,
    async mutate(method: string, params: NativeObject, id: string) {
      if (method === "draft_prepare") {
        assert.equal(params.revision, 6);
        assert.deepEqual(params.slots, { [slot.id]: slot.default });
        return { id, result: { token: params.token, bindings: {}, conflicts: [] } };
      }
      if (method === "draft_commit") {
        commits++;
        scene = nativeScene(7, 101, 0);
        commitReceipt = { id, revision: 7, frame: 101, result: {
          scene, lease, token: params.token, source_digest: intent.sha256,
          control_digest: params.control_digest, commit_id: id,
        } };
        scene = nativeScene(7, 150, 0);
        throw new WireError("renderer_timeout", "Lost terminal reply");
      }
      if (method === "draft_bind_uniform") {
        assert.deepEqual(params.components, [{ fixed: 0.625 }]);
        scene = nativeScene(8, 151, 0.625);
        return { id, revision: 8, frame: 151, result: { scene, lease } };
      }
      if (method === "draft_sample") {
        scene = nativeScene(8, 152, 0.625, 0.75);
        return { id, revision: 8, frame: 152, result: { scene, lease, sequence: params.sequence } };
      }
      if (method === "draft_callback") {
        assert.deepEqual(params, { lease, revision: 8, sequence: id === "callback" ? 2 : 3, value: 0.5 });
        const base = nativeScene(8, id === "callback" ? 153 : 154, 0.625, 0.75);
        const callbackScene = {
          ...base, draft_activation: { lease, callback_uniform: "u_vosc_callback" },
          layers: [{ shader: { uniforms: {
            u_vosc_callback: { gl_type: 0x1406, writable: true, rendered_value: id === "callback" ? "0.5" : "0.75" },
          } } }],
        };
        return { id, revision: 8, frame: callbackScene.frame, status: "rendered",
          result: { scene: callbackScene, lease, sequence: params.sequence } };
      }
      throw new Error("Unexpected native method " + method);
    },
    async recover(id: string, terminal: string) {
      assert.equal(id, "commit-1");
      assert.equal(terminal, "rendered");
      recoveries++;
      if (recoveries === 1) throw new WireError("request_not_found", "Draw not yet published");
      return commitReceipt!;
    },
  };
  t.mock.method(NativeRenderer, "start", async () => native as unknown as NativeRenderer);
  try {
    const cold = await draft.execute(request("health"));
    assert.equal(cold.needsRestore, true);
    const prepared = await draft.execute(request("run.prepare", cold, intent, "prepare-1"));
    assert.equal(prepared.revision, 0);
    assert.match(prepared.controlDigest as string, /^[a-f0-9]{64}$/);
    await assert.rejects(draft.execute(request("run.commit", cold, { lease, token: "prepare-1" }, "commit-1")),
      { code: "renderer_uncertain" });
    assert.equal(draft.completedMutation("commit-1"), undefined);
    const recovered = await draft.recoverMutation("commit-1");
    assert.equal(recovered?.receipt, commitReceipt);
    assert.equal(recovered?.frame, 101);
    assert.equal(recovered?.revision, 7);
    assert.equal(recovered?.commitID, "commit-1");
    assert.equal((await draft.execute(request("reflect"))).frame, 150);
    assert.equal(commits, 1);

    await assert.rejects(draft.execute(request("uniform.set", cold,
      { mode: "fixed", name: "gain", component: 0, value: 0.625 })), { code: "stale_revision" });
    const bound = await draft.execute(request("uniform.set", recovered,
      { mode: "fixed", name: "gain", component: 0, value: 0.625 }));
    assert.equal(bound.frame, 151);
    assert.deepEqual(bound.uniforms, [{ name: "gain", owner: "layer", type: "float", writable: true, values: [0.625] }]);
    const sampled = await draft.execute(request("sample", bound, { lease, sequence: 1, slot: slot.id, value: 0.75 }));
    assert.deepEqual(sampled.samples, { [slot.id]: 0.75 });
    assert.equal(sampled.receipt, undefined);
    assert.equal(sampled.revision, 8);
    await delay(20);
    await assert.rejects(draft.execute(request("callback.sample", { ...sampled, epoch: cold.epoch + "-stale" },
      { lease, sequence: 2, value: 0.5 })), { code: "stale_epoch" });
    const callback = await draft.execute(request("callback.sample", sampled, { lease, sequence: 2, value: 0.5 }, "callback"));
    assert.equal(callback.callbackUniform, "u_vosc_callback");
    assert.equal(callback.revision, 8);
    assert.deepEqual(callback.uniforms, [{ name: "u_vosc_callback", owner: "layer", writable: false, type: "float", values: [0.5] }]);
    assert.equal((callback.receipt as NativeObject).id, "callback");
    await delay(20);
    await assert.rejects(draft.execute(request("callback.sample", sampled, { lease, sequence: 3, value: 0.5 }, "wrong-value")),
      { code: "sample_unobserved" });
    await draft.reset();
    const restarted = await draft.execute(request("health"));
    assert.notEqual(restarted.epoch, cold.epoch);
    assert.equal(restarted.needsRestore, true);
    assert.equal(draft.completedMutation("commit-1"), undefined);
  } finally {
    await draft.reset();
  }
});

test("the native fence enters during prepare and refuses the prepared token once the reply arrives", async t => {
  const draft = new Draft(target, "/unused");
  const entered = deferred<void>();
  const finish = deferred<NativeObject>();
  const methods: string[] = [];
  const native = {
    running: true,
    stop: async () => {},
    query: async () => nativeScene(1, 10, 0),
    async mutate(method: string, params: NativeObject) {
      methods.push(method);
      if (method === "draft_prepare") {
        entered.resolve();
        return finish.promise;
      }
      assert.equal(params.lease, lease);
      return { result: { lease } };
    },
  };
  t.mock.method(NativeRenderer, "start", async () => native as unknown as NativeRenderer);
  try {
    const cold = await draft.execute(request("health"));
    const prepare = draft.execute(request("run.prepare", cold, intent, "prepare-1"));
    await entered.promise;
    const fence = await draft.fence(request("run.fence", cold, { lease }));
    assert.equal(fence.lease, lease);
    finish.resolve({ result: { token: "prepare-1", bindings: {}, conflicts: [] } });
    await assert.rejects(prepare, { code: "stale_lease" });
    await assert.rejects(draft.execute(request("run.commit", cold, { lease, token: "prepare-1" })),
      { code: "stale_candidate" });
    assert.deepEqual(methods, ["draft_prepare", "draft_fence", "draft_cancel"]);
  } finally {
    await draft.reset();
  }
});

test("validate uses a separate child, checks exact source digest, and requires correlated shader readback", async t => {
  const draft = new Draft(target, "/unused");
  let allocated = 0;
  let stops = 0;
  let shaderFails = false;
  let frame = 20;
  const native = {
    running: true,
    stop: async () => { stops++; },
    query: async () => nativeScene(2, frame, 0.5),
    async mutate(method: string, params: NativeObject) {
      assert.equal(method, "publish_shader");
      assert.equal(params.source_base64, intent.source);
      if (shaderFails) throw new WireError("shader_failed", "Compiler rejected the source");
      return { status: "rendered", revision: 2, frame: 20, external_epoch: 0 };
    },
  };
  t.mock.method(NativeRenderer, "start", async ({ validation }: Parameters<typeof NativeRenderer.start>[0]) => {
    assert.equal(validation, true);
    allocated++;
    return native as unknown as NativeRenderer;
  });
  try {
    await assert.rejects(draft.execute(request("validate", {}, { source: intent.source, sha256: "wrong" })),
      { code: "invalid_source" });
    const dependency = Buffer.from("#pragma include \"external.glsl\"\n");
    await assert.rejects(draft.execute(request("validate", {}, { source: dependency.toString("base64"),
      sha256: createHash("sha256").update(dependency).digest("hex") })), { code: "unsupported_dependency" });
    assert.equal(allocated, 0);
    const params = { source: intent.source, sha256: intent.sha256 };
    const valid = await draft.execute(request("validate", {}, params));
    assert.equal(valid.sourceDigest, intent.sha256);
    assert.equal((await draft.execute(request("health"))).needsRestore, true);
    assert.equal(allocated, 1);
    shaderFails = true;
    await assert.rejects(draft.execute(request("validate", {}, params)), { code: "shader_failed" });
    assert.equal(stops, 0);
    shaderFails = false;
    frame = 19;
    await assert.rejects(draft.execute(request("validate", {}, params)), { code: "renderer_protocol" });
    assert.equal(stops, 1);
  } finally {
    await draft.reset();
  }
});

for (const stopFails of [false, true]) {
  test(`idle validator teardown ${stopFails ? "blocks replacement after failure" : "finishes before replacement"}`, async t => {
    t.mock.timers.enable({ apis: ["setTimeout"] });
    const draft = new Draft(target, "/unused");
    const stopEntered = deferred<void>();
    const stopFinished = deferred<void>();
    const stoppingError = new WireError("renderer_stop", "The owned child did not exit");
    let starts = 0;
    let stops = 0;
    const validate = () => draft.execute(request("validate", {}, { source: intent.source, sha256: intent.sha256 }));
    t.mock.method(NativeRenderer, "start", async () => {
      const first = ++starts === 1;
      return {
        running: true,
        stop: async () => {
          stops++;
          if (!first) return;
          stopEntered.resolve();
          await stopFinished.promise;
          if (stopFails) throw stoppingError;
        },
        query: async () => nativeScene(2, 20, 0.5),
        mutate: async () => ({ status: "rendered", revision: 2, frame: 20, external_epoch: 0 }),
      } as unknown as NativeRenderer;
    });

    await validate();
    t.mock.timers.tick(2000);
    await stopEntered.promise;
    const second = validate();
    let settled = false;
    void second.finally(() => { settled = true; }).catch(() => {});
    await Promise.resolve();
    assert.equal(starts, 1);
    assert.equal(settled, false);
    stopFinished.resolve();

    if (stopFails) {
      await assert.rejects(second, { code: "renderer_stop" });
      const cold = await draft.execute(request("health"));
      await assert.rejects(draft.execute(request("run.prepare", cold, intent)), { code: "renderer_stop" });
      await assert.rejects(draft.reset(), { code: "renderer_stop" });
      assert.equal(starts, 1);
      assert.equal(stops, 1);
    } else {
      await second;
      assert.equal(starts, 2);
      assert.equal(stops, 1);
      await draft.reset();
      assert.equal(stops, 2);
    }
  });
}

const wgslBytes = Buffer.from("// shader bytes\r\n@fragment fn fs_main() -> @location(0) vec4f { return vec4f(1.0); }\r\n");
const wgslIntent = { ...intent, language: "wgsl", slots: [], source: wgslBytes.toString("base64"),
  sha256: createHash("sha256").update(wgslBytes).digest("hex") };

test("Dawn-free host rejects WGSL before draft_prepare", async t => {
  const draft = new Draft(target, "/unused");
  const mutate = t.mock.fn();
  const stop = async () => {};
  t.mock.method(NativeRenderer, "start", async () =>
    ({ running: true, supportsDraftWgsl: false, mutate, stop } as unknown as NativeRenderer));
  try {
    const cold = await draft.execute(request("health"));
    await assert.rejects(draft.execute(request("run.prepare", cold, wgslIntent)), { code: "wgsl_unavailable" });
    assert.equal(mutate.mock.callCount(), 0);
  } finally {
    await draft.reset();
  }
});

test("Linux host recovers only a correlated WebGPU first-frame receipt and marks device loss", async t => {
  const draft = new Draft(target, "/unused");
  let scene: NativeObject = { revision: 7 };
  let receipt: NativeObject;
  let valid = false;
  const native = {
    running: true, supportsDraftWgsl: true, stop: async () => {},
    query: async () => scene,
    async mutate(method: string, params: NativeObject, id: string) {
      if (method === "draft_prepare") {
        assert.deepEqual(params, { source_base64: wgslIntent.source, source_digest: wgslIntent.sha256, language: "wgsl",
          lease, token: id, revision: 7, slots: {}, bindings: {} });
        return { id, result: { token: id, bindings: {}, conflicts: [] } };
      }
      assert.equal(method, "draft_commit");
      scene = { backend: "webgpu", revision: 8, rendered_revision: 8, frame: 101, layers: [],
        draft_activation: { lease, backend: "webgpu" } };
      receipt = { id, revision: 8, frame: 101, status: "rendered", result: {
        scene, backend: "opengl", first_frame: 102, lease, token: params.token,
        source_digest: wgslIntent.sha256, control_digest: params.control_digest, commit_id: id,
      } };
      return receipt;
    },
    async recover() {
      if (valid) Object.assign(receipt.result as NativeObject, { backend: "webgpu", first_frame: 101 });
      return receipt;
    },
  };
  t.mock.method(NativeRenderer, "start", async () => native as unknown as NativeRenderer);
  try {
    const cold = await draft.execute(request("reflect"));
    const prepared = await draft.execute(request("run.prepare", cold, wgslIntent, "wgsl-prepare"));
    await assert.rejects(draft.execute(request("run.commit", prepared, { lease, token: prepared.token }, "wgsl-commit")),
      { code: "renderer_uncertain" });
    valid = true;
    const recovered = await draft.recoverMutation("wgsl-commit");
    assert.equal(recovered?.backend, "webgpu");
    assert.equal(recovered?.sourceDigest, wgslIntent.sha256);
    assert.equal(recovered?.frame, 101);
    assert.equal(recovered?.revision, 8);
    assert.deepEqual(recovered?.uniforms, []);
    assert.equal(recovered?.needsRestore, false);
    scene = { ...scene, draft_device_lost: true };
    const lost = await draft.execute(request("reflect"));
    assert.equal(lost.needsRestore, true);
  } finally {
    await draft.reset();
  }
});

for (const selected of ["webgpu", "opengl"] as const) {
  test(`Linux host correlates negotiated ${selected} prepare, commit, and activation`, async t => {
    const draft = new Draft(target, "/unused");
    const artifactDigest = "a".repeat(64);
    const compilerIdentity = "b".repeat(64);
    const translated = selected === "webgpu";
    const publicArtifact = translated ? { artifactDigest, compilerIdentity } : {};
    const nativeArtifact = translated ? { artifact_digest: artifactDigest, compiler_identity: compilerIdentity } : {};
    let valid = false;
    let scene: NativeObject = { backend: selected, revision: 7 };
    let receipt: NativeObject;
    let activation: NativeObject;
    const native = {
      running: true, supportsDraftWebgpuAuto: true, stop: async () => {},
      query: async () => scene,
      async mutate(method: string, params: NativeObject, id: string) {
        if (method === "draft_prepare") {
          if (id === "restore") {
            assert.equal(params.backend, selected);
            assert.equal(params.artifact_digest, translated ? artifactDigest : undefined);
            assert.equal(params.compiler_identity, translated ? compilerIdentity : undefined);
          } else {
            assert.equal(params.backend, "auto");
            assert.equal(params.artifact_digest, undefined);
          }
          assert.equal(params.language, undefined);
          return { id, result: { token: id, bindings: {}, conflicts: [], backend: selected, ...nativeArtifact } };
        }
        assert.equal(method, "draft_commit");
        const identity = { backend: selected, first_frame: 101, lease, token: params.token, source_digest: intent.sha256,
          control_digest: params.control_digest, commit_id: id, ...nativeArtifact };
        activation = { ...identity, first_frame: 102 };
        scene = { backend: selected, revision: 8, rendered_revision: 8, frame: 101, layers: [], draft_activation: activation };
        receipt = { id, revision: 8, frame: 101, status: "rendered", result: { scene, ...identity } };
        return receipt;
      },
      async recover() {
        if (valid) activation.first_frame = 101;
        return receipt;
      },
    };
    t.mock.method(NativeRenderer, "start", async () => native as unknown as NativeRenderer);
    try {
      const cold = await draft.execute(request("health"));
      assert.equal(cold.webgpuAuto, true);
      const prepared = await draft.execute(request("run.prepare", cold, { ...intent, slots: [], backend: "auto" }, "auto"));
      assert.equal(prepared.backend, selected);
      assert.equal(prepared.artifactDigest, (publicArtifact as NativeObject).artifactDigest);
      assert.equal(prepared.compilerIdentity, (publicArtifact as NativeObject).compilerIdentity);
      await assert.rejects(draft.execute(request("run.commit", prepared, { lease, token: prepared.token }, "commit")),
        { code: "renderer_uncertain" });
      valid = true;
      const drawn = await draft.recoverMutation("commit");
      assert.equal(drawn?.backend, selected);
      assert.equal(drawn?.artifactDigest, (publicArtifact as NativeObject).artifactDigest);
      assert.equal(drawn?.compilerIdentity, (publicArtifact as NativeObject).compilerIdentity);
      assert.equal(drawn?.frame, 101);
      const reflected = await draft.execute(request("reflect"));
      assert.equal(reflected.webgpuAuto, true);
      assert.equal(reflected.artifactDigest, (publicArtifact as NativeObject).artifactDigest);
      if (translated) {
        await assert.rejects(draft.execute(request("run.prepare", reflected, { ...intent, slots: [], backend: "webgpu",
          artifactDigest }, "missing-pin")), { code: "invalid_request" });
      }
      await draft.execute(request("run.prepare", reflected, { ...intent, slots: [], backend: selected, ...publicArtifact }, "restore"));
    } finally {
      await draft.reset();
    }
  });
}

test("A new Linux host uses legacy GLSL on an older renderer and refuses unpinned fallback", async t => {
  const draft = new Draft(target, "/unused");
  const mutate = t.mock.fn(async (_method: string, params: NativeObject) => {
    assert.equal(Object.hasOwn(params, "backend"), false);
    return { result: { token: params.token, bindings: {}, conflicts: [] } };
  });
  t.mock.method(NativeRenderer, "start", async () => ({
    running: true, supportsDraftWebgpuAuto: false, mutate, query: async () => ({ revision: 6, backend: "opengl" }),
    stop: async () => {},
  } as unknown as NativeRenderer));
  try {
    const cold = await draft.execute(request("health"));
    assert.equal(cold.webgpuAuto, true);
    await assert.rejects(draft.execute(request("run.prepare", cold, { ...intent, backend: "webgpu",
      artifactDigest: "a".repeat(64), compilerIdentity: "b".repeat(64) }, "restore")), { code: "translation_unavailable" });
    assert.equal(mutate.mock.callCount(), 0);
    const result = await draft.execute(request("run.prepare", cold, { ...intent, backend: "auto" }, "auto"));
    assert.equal(result.backend, undefined);
    assert.equal(mutate.mock.callCount(), 1);
  } finally {
    await draft.reset();
  }
});
