import assert from "node:assert/strict";
import { test } from "node:test";
import { DraftService } from "../draft-service";
import { WireError, type WireRequest } from "../transport";

type DraftState = ConstructorParameters<typeof DraftService>[1];
type Row = Record<string, unknown>;
const target = "render-host";
const peer = "first-leaf";
const other = "other-leaf";
const lease = "d5ee0aaa-5a50-4fdc-9dc9-e5d15fed7044";
const replacement = "ef0a9cc5-5cd9-4725-9c96-682f370b74b3";

function wire(method: string, id: string, params: Row = {}): WireRequest {
  return { method, id, params, target, version: 1, epoch: "first-epoch", revision: 3 };
}

function deferred<T>() {
  let resolve!: (value: T) => void;
  let reject!: (cause: unknown) => void;
  const promise = new Promise<T>((ok, fail) => { resolve = ok; reject = fail; });
  return { promise, resolve, reject };
}

function fixture(overrides: Partial<DraftState> = {}) {
  const calls: WireRequest[] = [];
  const draft: DraftState = {
    async execute(request) { return { id: request.id }; },
    async fence(request) { return { lease: request.params.lease }; },
    async revoke() {},
    async recoverMutation() { return undefined; },
    completedMutation() { return undefined; },
    async reset() { this.onReset?.(); },
    ...overrides,
  };
  const execute = draft.execute.bind(draft);
  draft.execute = async request => { calls.push(request); return execute(request); };
  const service = new DraftService(target, draft);
  const ask = (id: string, requester = peer) => service.dispatch(wire("receipt.get", "read", { id }), requester);
  return { service, calls, ask };
}

test("peer receipts use canonical envelope equality, evict oldest entries, and pin only the latest successful draws", async () => {
  const { service, calls, ask } = fixture({
    async execute(request) {
      if (request.id === "failed") throw new WireError("shader_failed", "Compile failed", [{ text: "error" }]);
      return { id: request.id };
    },
  });
  const prepare = wire("run.prepare", "prepare", { lease, bindings: { b: { x: 2 }, a: { x: 1 } } });
  assert.deepEqual(await service.dispatch(prepare, peer), { id: "prepare" });
  assert.deepEqual(await service.dispatch({ ...prepare, params: { bindings: { a: { x: 1 }, b: { x: 2 } }, lease } }, peer),
    { id: "prepare" });
  assert.equal(calls.length, 1);
  await assert.rejects(service.dispatch({ ...prepare, revision: 4 }, peer), { code: "id_conflict" });
  await assert.rejects(service.dispatch(prepare, other), { code: "lease_owned" });
  await assert.rejects(service.dispatch(wire("sample", "sample", { lease }), other), { code: "stale_lease" });
  await assert.rejects(service.dispatch(wire("callback.sample", "callback", { lease }), other), { code: "stale_lease" });
  await assert.rejects(service.dispatch(wire("callback.sample", "callback", { lease: replacement }), peer), { code: "stale_lease" });
  assert.deepEqual(await ask("prepare", other), { state: "unknown" });
  await service.dispatch(wire("run.commit", "commit", { lease }), peer);
  await service.dispatch(wire("slot.configure", "slot", {}), peer);
  await assert.rejects(service.dispatch(wire("uniform.set", "failed", {}), peer), { code: "shader_failed" });
  assert.deepEqual(await ask("failed"), { state: "completed", response: { id: "failed", ok: false,
    error: { code: "shader_failed", message: "Compile failed", diagnostics: [{ text: "error" }] } } });
  for (let index = 0; index < 64; index++) {
    await service.dispatch(wire("run.cancel", "cancel-" + index, { lease }), peer);
  }
  assert.deepEqual(await ask("prepare"), { state: "unknown" });
  assert.deepEqual(await ask("failed"), { state: "unknown" });
  assert.deepEqual(await ask("commit"), { state: "completed", response: { id: "commit", ok: true, result: { id: "commit" } } });
  assert.equal((await ask("slot")).state, "completed");
  assert.equal((await ask("cancel-0")).state, "completed");
  await service.dispatch(wire("run.cancel", "cancel-extra", { lease }), peer);
  assert.deepEqual(await ask("cancel-0"), { state: "unknown" });
  await service.reset();
  assert.deepEqual(await ask("commit"), { state: "unknown" });
  await service.dispatch(prepare, other);
});

test("an admitted draw is pending, then blocks resubmission until exact receipt reconciliation", async () => {
  const draw = deferred<Row>();
  let recoveries = 0;
  const { service, calls, ask } = fixture({
    async execute(request) {
      return request.method === "run.commit" ? draw.promise : {};
    },
    async recoverMutation(id) {
      assert.equal(id, "commit");
      recoveries++;
      if (recoveries === 1) throw new WireError("renderer_uncertain", "Still pending");
      return { frame: 29, commitID: id };
    },
  });
  const commit = wire("run.commit", "commit", { lease, token: "prepare" });
  await service.dispatch(wire("run.prepare", "prepare", { lease }), peer);
  const writing = service.dispatch(commit, peer);
  assert.deepEqual(await ask("commit"), { state: "pending" });
  assert.deepEqual(await ask("commit", other), { state: "unknown" });
  await assert.rejects(service.dispatch(commit, peer), { code: "busy" });
  await assert.rejects(service.dispatch({ ...commit, params: { lease, token: "other" } }, peer), { code: "id_conflict" });
  draw.reject(new WireError("renderer_uncertain", "Lost draw reply"));
  await assert.rejects(writing, { code: "renderer_uncertain" });
  await assert.rejects(service.dispatch(commit, peer), { code: "renderer_uncertain" });
  await assert.rejects(service.dispatch(wire("sample", "sample", { lease }), peer), { code: "prior_draw_unconfirmed" });
  await assert.rejects(service.dispatch(wire("callback.sample", "callback", { lease }), peer), { code: "prior_draw_unconfirmed" });
  assert.deepEqual(await ask("commit"), { state: "pending" });
  const finished = { frame: 29, commitID: "commit" };
  assert.deepEqual(await ask("commit"), { state: "completed", response: { id: "commit", ok: true, result: finished } });
  assert.deepEqual(await service.dispatch(commit, peer), finished);
  assert.equal(calls.filter(request => request.method === "run.commit").length, 1);
});

test("revocation fences a pending writer independently, purges peer receipts, and blocks replacement when unconfirmed", async () => {
  const draw = deferred<Row>();
  const revoked = deferred<void>();
  const entered = deferred<string>();
  const { service, ask } = fixture({
    async execute(request) { return request.method === "run.commit" ? draw.promise : {}; },
    async revoke(id) { entered.resolve(id); await revoked.promise; },
  });
  await service.dispatch(wire("run.prepare", "prepare", { lease }), peer);
  const pending = service.dispatch(wire("run.commit", "commit", { lease }), peer);
  const revoke = service.revoke(peer);
  assert.equal(await entered.promise, lease);
  assert.deepEqual(await ask("prepare"), { state: "unknown" });
  assert.deepEqual(await ask("commit"), { state: "unknown" });
  await assert.rejects(service.dispatch(wire("run.prepare", "new", { lease: replacement }), other), { code: "busy" });
  revoked.reject(new WireError("renderer_timeout", "Fence reply lost"));
  await assert.rejects(revoke, { code: "renderer_timeout" });
  draw.resolve({ frame: 99 });
  await assert.rejects(pending, { code: "unpaired" });
  assert.deepEqual(await ask("commit"), { state: "unknown" });
  await assert.rejects(service.dispatch(wire("run.prepare", "new", { lease: replacement }), other),
    { code: "revoked_lease_unconfirmed" });
  await assert.rejects(service.dispatch(wire("sample", "sample", { lease }), other),
    { code: "revoked_lease_unconfirmed" });
  await service.reset();
  await service.dispatch(wire("run.prepare", "new", { lease: replacement }), other);
});

test("a fence can run while a normal operation is pending and reset drops its stale late reply", async () => {
  const draw = deferred<Row>();
  let fenced: Row | undefined;
  const { service, ask } = fixture({
    async execute(request) { return request.method === "run.commit" ? draw.promise : {}; },
    async fence(request) { fenced = request.params; return { lease: request.params.lease }; },
  });
  await service.dispatch(wire("run.prepare", "prepare", { lease }), peer);
  const pending = service.dispatch(wire("run.commit", "commit", { lease }), peer);
  await service.dispatch(wire("run.fence", "fence", { lease }), peer);
  assert.deepEqual(fenced, { lease });
  assert.equal((await ask("fence")).state, "completed");
  await service.reset();
  draw.resolve({ frame: 99 });
  await assert.rejects(pending, { code: "draft_state_lost" });
  assert.deepEqual(await ask("commit"), { state: "unknown" });
  assert.deepEqual(await ask("fence"), { state: "unknown" });
  await service.dispatch(wire("run.prepare", "new", { lease: replacement }), other);
});
