import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { X509Certificate } from "node:crypto";
import { createSocket } from "node:dgram";
import { mkdir, mkdtemp, readFile, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { test } from "node:test";
import { setTimeout as delay } from "node:timers/promises";
import { createServer, type TLSSocket } from "node:tls";
import { parseDocument, parseProject, replay, sourceName, sourceWithText, titleFor, writeReplay } from "../src/draft-project";
import { feedbackExample, feedbackExampleSource } from "../src/feedback-example";
import { hash, HostError } from "../src/local-state";
import { readSample } from "../src/osc-samples";
import { TRIODE_COMPILER_ID } from "vosc-triode-identity";
import { compileHydra } from "../src/hydra-compiler";
import { evaluateHydraPatch, patchErrorLocation } from "../src/hydra-patch";
import { Workspace } from "../src/workspace";
import { HostClient } from "../src/host-client";
import { Importer } from "../src/importer";
import { CALLBACK_UNIFORM, type DraftProject, type DraftSource, type DraftUniform, type Peer, type ReflectedDraft } from "../src/contracts";

const SOURCE_BYTES = Buffer.from("#version 330\nuniform vec3 tone;\nout vec4 color;\nvoid main() { color = vec4(tone, 1); }");
const SOURCE: DraftSource = { name: "tone.frag", origin: "Test source", digest: hash(SOURCE_BYTES),
  base64: SOURCE_BYTES.toString("base64") };
const PEER: Peer = { target: "acd631f3-2b03-4d5e-b129-29cadff3ed18", fingerprint: "0".repeat(64),
  endpoint: { host: "127.0.0.1", port: 37209 } };

function oscString(value: string) {
  const buffer = Buffer.alloc((value.length + 4) & ~3);
  buffer.write(value, "ascii");
  return buffer;
}

test("the OSC relay accepts only one configured-shape normalized amplitude", () => {
  const sample = Buffer.alloc(4);
  sample.writeFloatBE(0.5);
  const packet = (slot: string, address = "/input/data", signature = ",sf", value = sample) =>
    Buffer.concat([oscString(address), oscString(signature), oscString(slot), value]);
  assert.deepEqual(readSample(packet("s0123456789ab")), { slot: "s0123456789ab", value: 0.5 });
  assert.equal(readSample(packet("*")), null);
  assert.equal(readSample(packet("s0123456789ab", "/input/set")), null);
  assert.equal(readSample(packet("s0123456789ab", "/input/data", ",si")), null);
  sample.writeFloatBE(1.4);
  assert.equal(readSample(packet("s0123456789ab")), null);
  sample.writeFloatBE(Number.NaN);
  assert.equal(readSample(packet("s0123456789ab")), null);
});

test("a typed project serializes range and sibling fixed components as native OSC tokens", async () => {
  const project: DraftProject = { format: "vosc.authoring.draft", version: 1, title: "Tone",
    destination: PEER, source: SOURCE, slots: [{ id: "s0123456789ab", label: "Kick", default: 0 }],
    bindings: [
      { name: "tone", component: 0, binding: { mode: "fixed", value: 0.2 } },
      { name: "tone", component: 1, binding: { mode: "range", slot: "s0123456789ab", low: 0.1, high: 0.9 } },
      { name: "tone", component: 2, binding: { mode: "fixed", value: 1 } },
    ] };
  assert.deepEqual(parseProject(project), project);
  const manifest = replay(project);
  assert.deepEqual(manifest.commands.at(-1), { address: "/shader/var", args: [
    { type: "i", value: 0 }, { type: "s", value: "tone" },
    { type: "f", value: 0.2 }, { type: "s", value: "(0.1)+(0.8)*in_s0123456789ab" }, { type: "f", value: 1 },
  ] });
  assert.throws(() => parseProject({ ...project, slots: [] }), /configured slot/);
  assert.throws(() => parseProject({ ...project, source: { ...SOURCE, digest: "a".repeat(64) } }), /digest/);
  const output = await mkdtemp(join(tmpdir(), "vosc-native-recipe-"));
  try {
    const recipe = await readFile(await writeReplay(output, project), "utf8");
    assert.deepEqual(JSON.parse(recipe), manifest);
    assert.equal(recipe.match(/"type": "f",\s*"value": 1\.0\s*}/g)?.length, 2);
    assert.match(recipe, /"type": "i",\s*"value": 1\s*}/);
  } finally {
    await rm(output, { recursive: true, force: true });
  }
});

test("generated and saved titles satisfy native single-line rules without changing source provenance", () => {
  const project: DraftProject = { format: "vosc.authoring.draft", version: 1, title: "Tone",
    destination: PEER, source: SOURCE, slots: [], bindings: [] };
  for (const [name, title] of [[" \t.frag", "VOSC Draft"], [" \r\n\t.frag", "VOSC Draft"],
    ["  Gaisma\r\nMēness\r星\n✨ .frag", "Gaisma Mēness 星 ✨"], ["Rīgas ēna 🌙.frag", "Rīgas ēna 🌙"]]) {
    const source = { ...SOURCE, name: sourceName(`folder/${name}`), origin: `repo @ commit · folder/${name}` };
    assert.equal(titleFor(source), title);
    const parsed = parseProject({ ...project, source, title });
    assert.deepEqual(parsed.source, source);
    assert.equal(replay(parsed).title, title);
  }
  for (const title of ["", " \t ", "Gaisma\nMēness", "Gaisma\rMēness", "bad\u0000title", "🌙".repeat(513)]) {
    assert.throws(() => parseProject({ ...project, title }), /title must be a nonempty single-line string/);
    assert.throws(() => replay({ ...project, title }), /title must be a nonempty single-line string/);
  }
  assert.equal(replay({ ...project, title: "🌙".repeat(512) }).title, "🌙".repeat(512));
});

class FixtureHost {
  peer: Peer | null = PEER;
  state: ReflectedDraft = { target: PEER.target, epoch: "epoch-a", revision: 0, needsRestore: true,
    uniforms: [], slots: [] };
  validatedUniforms: DraftUniform[] = [{ name: "tone", values: [0, 0.25, 0.75], writable: true, owner: "layer", type: "vec3" }];
  onReflect?: (snapshot: ReflectedDraft) => Promise<ReflectedDraft>;
  onConnect?: () => Promise<Peer>;
  onValidate?: () => Promise<void>;
  mutations = 0;
  lostNext = false;
  uncertainControl = false;
  controlError: HostError | null = null;
  sampleError: HostError | null = null;
  sampleCount = 0;
  samples: { lease: string; sequence: number; revision: number }[] = [];
  requested: { id?: string; method: string; params: Record<string, unknown> }[] = [];
  prepared: { id: string; params: Record<string, unknown>; bindings: Record<string, unknown> } | null = null;
  conflicts: { name: string; reason: string }[] = [];
  compileError: HostError | null = null;
  commitReply: "valid" | "lost" | "wrong" = "valid";
  receiptAvailable = true;
  onCommit?: () => Promise<void>;
  onCallback?: (drawn: ReflectedDraft) => Promise<ReflectedDraft>;
  afterCommit?: () => Promise<void>;
  unreachablePorts = new Set<number>();
  sessionRequests: { method: string; port: number; recovery: boolean }[] = [];

  session() {
    const peer = this.peer!;
    const dispatch = (recovery: boolean, ...args: Parameters<FixtureHost["rpc"]>) => {
      const port = peer.endpoint.port;
      this.sessionRequests.push({ method: args[0], port, recovery });
      if (this.unreachablePorts.has(port)) {
        return Promise.reject(new HostError("host_unreachable", "Old endpoint is gone."));
      }
      return this.rpc(...args);
    };
    return { peer, rpc: (...args: Parameters<FixtureHost["rpc"]>) => {
      assert.equal(this.peer?.target, peer.target, "writes must remain on the selected target");
      assert.equal(this.peer?.fingerprint, peer.fingerprint, "writes must stay on the selected certificate");
      assert.equal(this.peer?.endpoint.port, peer.endpoint.port, "writes must stay on the selected port");
      return dispatch(false, ...args);
    }, recover: (...args: Parameters<FixtureHost["rpc"]>) => dispatch(true, ...args) };
  }
  lost?: { id: string; ok: boolean; result: ReflectedDraft };
  async rpc(method: string, params: Record<string, unknown>, expected?: { epoch: string; revision: number }, id?: string) {
    this.requested.push({ id, method, params });
    if (method === "reflect") {
      const snapshot = structuredClone(this.state);
      const reflect = this.onReflect;
      this.onReflect = undefined;
      if (!this.receiptAvailable) delete snapshot.receipt;
      return reflect ? reflect(snapshot) : snapshot;
    }
    if (method === "receipt.get") {
      if (!this.receiptAvailable || params.id !== this.lost?.id) return { state: "unknown" };
      return { state: "completed", response: this.lost };
    }
    if (method === "run.fence") {
      if (this.prepared?.params.lease === params.lease) this.prepared = null;
      return { target: this.state.target, epoch: expected?.epoch, lease: params.lease };
    }
    assert.equal(expected?.epoch, this.state.epoch);
    assert.equal(expected?.revision, this.state.revision);
    if (method === "sample") {
      this.sampleCount += 1;
      this.samples.push({ lease: String(params.lease), sequence: Number(params.sequence), revision: expected!.revision });
      if (this.sampleError) throw this.sampleError;
      return structuredClone(this.state);
    }
    if (method === "callback.sample") {
      const state = this.state;
      assert.equal(state.callbackUniform, CALLBACK_UNIFORM);
      const uniform = state.uniforms.find(item => item.name === CALLBACK_UNIFORM)!;
      uniform.values = [Number(params.value)];
      const frame = state.frame! + 1;
      state.frame = frame;
      const drawn = structuredClone({ ...state, receipt: {
        id: id!, revision: state.revision, frame, status: "rendered", external_epoch: 0,
        result: { lease: params.lease, sequence: params.sequence, scene: { frame, rendered_revision: state.revision } },
      } });
      return this.onCallback?.(drawn) ?? drawn;
    }
    if (method === "run.prepare") {
      await this.onValidate?.();
      if (this.compileError) throw this.compileError;
      const bindings = { ...params.bindings as Record<string, { type?: string; components: unknown[] }> };
      for (const binding of Object.values(bindings)) binding.type ??= ["float", "vec2", "vec3", "vec4"][binding.components.length - 1];
      for (const issue of this.conflicts) delete bindings[issue.name];
      this.prepared = { id: id!, params, bindings };
      return { ...params, target: this.state.target, epoch: expected?.epoch, revision: expected?.revision, sourceDigest: params.sha256,
        token: id, controlDigest: SOURCE.digest, bindings, conflicts: this.conflicts };
    }
    if (method === "run.cancel") {
      this.prepared = null;
      return { target: this.state.target, ...expected };
    }
    if (this.controlError && (method === "uniform.set" || method === "slot.configure")) throw this.controlError;
    this.mutations += 1;
    if (method === "run.commit") {
      const prepared = this.prepared;
      await this.onCommit?.();
      if (!prepared || this.prepared !== prepared) throw new HostError("stale_candidate", "The candidate was revoked.");
      this.prepared = null;
      assert.equal(prepared.id, params.token);
      const staged = prepared.params;
      const revision = this.state.revision + 1;
      const frame = revision * 2;
      const uniforms = structuredClone(this.validatedUniforms);
      for (const uniform of uniforms) {
        const binding = (prepared.bindings[uniform.name] as { components: { fixed?: number }[] } | undefined);
        binding?.components.forEach(({ fixed }, i) => { if (fixed !== undefined) uniform.values[i] = Math.fround(fixed); });
      }
      this.state = { ...this.state, revision, frame, needsRestore: false, sourceDigest: String(staged.sha256), uniforms,
        callbackUniform: uniforms.some(uniform => uniform.name === CALLBACK_UNIFORM) ? CALLBACK_UNIFORM : undefined,
        lease: String(staged.lease), token: prepared.id, commitID: id, controlDigest: SOURCE.digest,
        runSequence: Number(staged.runSequence), editSequence: Number(staged.editSequence), generation: Number(staged.generation),
        documentId: String(staged.documentId), slots: staged.slots as ReflectedDraft["slots"],
        receipt: { id: id!, revision, frame, status: "rendered", external_epoch: 0, result: {
          lease: staged.lease, token: prepared.id, source_digest: staged.sha256, control_digest: SOURCE.digest, commit_id: id,
          scene: { rendered_revision: revision, frame, layers: [{ shader: { loaded: true, program: revision } }] },
        } } };
      const result = structuredClone(this.state);
      await this.afterCommit?.();
      if (this.commitReply === "lost") {
        this.lost = { id: id!, ok: true, result };
        throw new HostError("host_closed", "Reply lost", id);
      }
      if (this.commitReply === "wrong") return { ...result, commitID: "different request" };
      return result;
    } else if (method === "slot.configure") {
      this.state.slots.push(params as unknown as ReflectedDraft["slots"][number]);
    } else if (method === "uniform.set") {
      const uniform = this.state.uniforms.find(uniform => uniform.name === params.name)!;
      if (params.mode === "fixed") uniform.values[Number(params.component)] = Math.fround(Number(params.value));
    } else {
      throw new Error("Unexpected operation " + method);
    }
    this.state.revision += 1;
    this.state.frame = this.state.revision * 2;
    this.state.receipt = { id: id!, frame: this.state.frame, revision: this.state.revision, external_epoch: 0, status: "rendered" };
    const result = structuredClone(this.state);
    if (this.lostNext) {
      this.lostNext = false;
      this.lost = { id: id!, ok: true, result };
      if (this.uncertainControl) throw new HostError("renderer_uncertain", "Waiting for the native draw.");
      throw new HostError("host_closed", "Reply lost", id);
    }
    return result;
  }

  async connectRemembered() {
    const peer = await this.onConnect?.();
    if (!peer) throw new Error("Configure test host connection");
    this.peer = peer;
    this.state = { target: peer.target, epoch: "epoch-b", revision: 0, uniforms: [], slots: [], needsRestore: true };
    return peer;
  }

  disconnect() { this.peer = null; }
}

async function waitForCallback(condition: () => boolean) {
  for (let elapsed = 0; !condition() && elapsed < 3_000; elapsed += 10) await delay(10);
  assert.ok(condition(), "Timed out waiting for the Editor callback");
}

function callbackHost() {
  const host = new FixtureHost();
  host.validatedUniforms.push({ name: CALLBACK_UNIFORM, type: "float", owner: "layer", writable: false, values: [0.25] });
  return host;
}

test("callback dispatch has no backlog and ignores a draw reply after disconnect", async () => {
  const host = callbackHost();
  const times: number[] = [];
  const workspace = new Workspace(host as unknown as HostClient, undefined, () => {
    times.push(performance.now());
    return times.length / 10 + 0.5;
  });
  let release!: () => void;
  const gate = new Promise<void>(resolve => { release = resolve; });
  try {
    await workspace.activate(async () => SOURCE);
    assert.equal(workspace.view().callback?.value, 0.25);
    await waitForCallback(() => times.length >= 2 && workspace.view().callback?.value !== 0.25);
    assert.ok(times[1] - times[0] >= 99);
    host.onCallback = async drawn => { await gate; return drawn; };
    await waitForCallback(() => times.length >= 3);
    const count = times.length;
    const confirmed = workspace.view().callback?.value;
    await delay(240);
    assert.equal(times.length, count);
    await workspace.disconnect();
    release();
    await delay(120);
    assert.equal(times.length, count);
    assert.deepEqual(workspace.view().callback, { value: confirmed, active: false, error: null });
  } finally {
    release();
    workspace.clear();
  }
});

test("a snapshot freezes the last confirmed callback across Save, disconnect, and offline export", async () => {
  const host = callbackHost();
  const workspace = new Workspace(host as unknown as HostClient, undefined, () => 0.625);
  const dir = await mkdtemp(join(tmpdir(), "vosc-callback-test-"));
  try {
    await workspace.activate(async () => SOURCE);
    const initial = workspace.captureExport();
    const path = join(dir, "test.vosc-draft");
    await workspace.save(path);
    await waitForCallback(() => workspace.view().callback?.value === 0.625);
    assert.equal(workspace.needsSave(), false);
    await workspace.save(path);
    const latest = workspace.captureExport();
    assert.equal(initial.callbackValue, 0.25);
    assert.equal(latest.callbackValue, 0.625);
    await workspace.disconnect();
    await workspace.open(path);
    assert.deepEqual(workspace.view().callback, { value: 0.625, active: false, error: null });
    const recipe = JSON.parse(await readFile(await workspace.export(dir), "utf8"));
    assert.deepEqual(recipe.commands.at(-1), { address: "/shader/var", args: [
      { type: "i", value: 0 }, { type: "s", value: CALLBACK_UNIFORM }, { type: "f", value: 0.625 },
    ] });
  } finally {
    workspace.clear();
    await rm(dir, { recursive: true, force: true });
  }
});

test("a nonfinite callback pauses, survives unrelated changes, and restarts on explicit Restore", async () => {
  const host = callbackHost();
  let invalid = true;
  const workspace = new Workspace(host as unknown as HostClient, undefined, () => invalid ? Infinity : 0.75);
  try {
    await workspace.activate(async () => SOURCE);
    await waitForCallback(() => !!workspace.view().callback?.error);
    assert.equal(workspace.view().callback?.value, 0.25);
    assert.equal(host.requested.some(item => item.method === "callback.sample"), false);
    await assert.rejects(workspace.bindComponent(CALLBACK_UNIFORM, 0, { mode: "fixed", value: 1 }), { code: "invalid_uniform" });
    await workspace.bindComponent("tone", 0, { mode: "fixed", value: 0.5 });
    assert.match(workspace.view().callback?.error ?? "", /finite value/);
    await workspace.disconnect();
    host.onConnect = async () => PEER;
    await workspace.connectRemembered(PEER.fingerprint);
    invalid = false;
    await delay(220);
    assert.equal(host.requested.some(item => item.method === "callback.sample"), false);
    await workspace.restore();
    await waitForCallback(() => workspace.view().callback?.value === 0.75);
    assert.equal(workspace.view().callback?.error, null);
  } finally {
    workspace.clear();
  }
});

test("a lost fixed binding reply is read back and can be saved offline", async () => {
  const host = new FixtureHost();
  const workspace = new Workspace(host as unknown as HostClient);
  const root = await mkdtemp(join(tmpdir(), "vosc-controller-test-"));
  try {
    assert.equal((await workspace.activate(async () => SOURCE)).previewReady, true);
    host.lostNext = true;
    const changed = await workspace.bindComponent("tone", 1, { mode: "fixed", value: 0.5 });
    assert.deepEqual(changed.reflected?.uniforms[0].values, [0, 0.5, 0.75]);
    assert.equal(changed.previewReady, true);
    const destination = join(root, "Tone.vosc-draft");
    await workspace.save(destination);
    const saved = await readFile(destination, "utf8");
    const missingParent = join(root, "missing");
    const failedDestination = join(missingParent, "Tone.vosc-draft");
    await assert.rejects(workspace.save(failedDestination), { code: "ENOENT" });
    assert.equal(await readFile(destination, "utf8"), saved);
    await mkdir(missingParent);
    await workspace.save(failedDestination);
    assert.equal(await readFile(failedDestination, "utf8"), saved);
    host.state.revision += 1;
    await workspace.save(destination);
    assert.equal(await readFile(destination, "utf8"), saved);

    const reopened = await workspace.open(destination);
    assert.equal(reopened.previewReady, false);
    assert.equal(reopened.project?.bindings[1].binding.mode, "fixed");
    host.state = { ...host.state, epoch: "epoch-b", revision: 0, needsRestore: true };
    const restored = await workspace.restore();
    assert.equal(restored.previewReady, true, JSON.stringify(restored.run));
    assert.deepEqual(restored.reflected?.uniforms[0].values, [0, 0.5, 0.75]);
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});

test("a validation batch never transfers remaining sources to a different selected TLS host", async () => {
  const root = await mkdtemp(join(tmpdir(), "vosc-pinned-validation-"));
  const keyPath = join(root, "key.pem");
  const certPath = join(root, "cert.pem");
  const sockets = new Set<TLSSocket>();
  type Request = { id: string; target: string; method: string; params: { sha256: string } };
  type PinnedFixture = Peer & { leaf: Buffer; rpcPort: number };
  const servers: ReturnType<typeof createServer>[] = [];
  try {
    execFileSync("/usr/bin/openssl", ["req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
      "-subj", "/CN=VOSC test", "-keyout", keyPath, "-out", certPath], { stdio: "ignore" });
    const key = await readFile(keyPath, "utf8");
    const cert = await readFile(certPath, "utf8");
    const leaf = new X509Certificate(cert).raw;
    const start = async (peer: Peer, receive: (request: Request, socket: TLSSocket) => void): Promise<PinnedFixture> => {
      const server = createServer({ key, cert, minVersion: "TLSv1.3" }, socket => {
        sockets.add(socket);
        socket.on("error", () => {});
        socket.on("close", () => sockets.delete(socket));
        socket.setEncoding("utf8");
        let request = "";
        socket.on("data", chunk => {
          request += chunk;
          if (request.endsWith("\n")) receive(JSON.parse(request), socket);
        });
      });
      servers.push(server);
      await new Promise<void>((resolve, reject) => server.once("error", reject).listen(0, "127.0.0.1", resolve));
      const address = server.address();
      assert.ok(address && typeof address !== "string");
      return { ...peer, fingerprint: hash(leaf), leaf, rpcPort: address.port };
    };
    const host = new HostClient(root, "unused");
    const mutable = host as unknown as { connected: PinnedFixture; identityPromise: Promise<{ key: string; cert: string }> };
    mutable.identityPromise = Promise.resolve({ key, cert });
    const theseRequests: Request[] = [];
    const otherRequests: Request[] = [];
    const other = await start({ ...PEER, target: "88bce241-f530-49cf-8b86-70354afcbdd3" }, (request, socket) => {
      otherRequests.push(request);
      socket.end("x".repeat(1024 * 1024 + 1));
    });
    const first = await start(PEER, (request, socket) => {
      theseRequests.push(request);
      mutable.connected = other;
      socket.end(JSON.stringify({ id: request.id, ok: true, result: {
        target: request.target, sourceDigest: request.params.sha256, receipt: { status: "rendered" },
      } }) + "\n");
    });
    mutable.connected = first;
    const importer = new Importer(root, root);
    const staged = importer as unknown as {
      batch: { id: string; items: { id: string; file: unknown }[]; complete: boolean; issue: null } | null;
    };
    const selectedIds = ["first.frag", "second.frag"];
    staged.batch = {
      id: "fixture-batch", complete: true, issue: null,
      items: selectedIds.map(path => ({ id: path, file: {
        origin: { path }, status: "awaiting_validation",
        entry: { id: SOURCE.digest, generated_sha256: SOURCE.digest }, generated_base64: SOURCE.base64,
      } })),
    };
    const result = await importer.validate(host, "fixture-batch", selectedIds);
    assert.equal(result.error?.code, "host_changed");
    assert.deepEqual(result.items.map(item => item.status), ["awaiting_validation", "awaiting_validation"]);
    assert.deepEqual(theseRequests.map(request => request.target), [first.target]);
    assert.deepEqual(otherRequests, []);

    const lease = host.session();
    await assert.rejects(lease.rpc("uniform.set", {}, undefined, "uncertain"), (error: HostError) =>
      error.code === "host_reply_limit" && error.uncertainId === "uncertain");
    assert.equal(otherRequests.length, 1);
    mutable.connected = first;
    const stale = host.session();
    mutable.connected = other;
    await assert.rejects(stale.rpc("validate", { source: SOURCE.base64 }), (error: HostError) => error.code === "host_changed");
    assert.equal(otherRequests.length, 1);
    assert.equal(theseRequests.length, 1);
  } finally {
    for (const socket of sockets) socket.destroy();
    await Promise.all(servers.map(server => new Promise<void>(resolve => server.close(() => resolve()))));
    await rm(root, { recursive: true, force: true });
  }
});

test("the UDP relay keeps transient busy samples recoverable and stops on an unobserved draw", async () => {
  const host = new FixtureHost();
  const workspace = new Workspace(host as unknown as HostClient);
  const sender = createSocket("udp4");
  try {
    await workspace.activate(async () => SOURCE);
    const slot = "s0123456789ab";
    host.state.slots = [{ id: slot, label: "Kick", default: 0, value: 0 }];
    await workspace.reflect();
    const { samplePort } = await workspace.startSamples();
    assert.ok(samplePort);
    const amplitude = Buffer.alloc(4);
    amplitude.writeFloatBE(0.5);
    const packet = Buffer.concat([oscString("/input/data"), oscString(",sf"), oscString(slot), amplitude]);
    const sendSample = async (count: number) => {
      await new Promise<void>((resolve, reject) => sender.send(packet, samplePort, "127.0.0.1",
        error => error ? reject(error) : resolve()));
      for (let elapsed = 0; host.sampleCount < count && elapsed < 1_000; elapsed += 10) await delay(10);
      assert.equal(host.sampleCount, count);
    };
    host.sampleError = new HostError("busy", "Rendering another sample.");
    await sendSample(1);
    assert.equal(workspace.view().samplePort, samplePort);
    assert.equal(workspace.view().notice, null);
    await delay(40);
    host.sampleError = new HostError("sample_unobserved", "The renderer did not draw the sample.");
    await sendSample(2);
    const view = await workspace.reflect();
    assert.equal(view.samplePort, null);
    assert.equal(view.previewReady, false);
    assert.match(view.notice ?? "", /OSC input stopped.*did not draw.*Restore the draft/);
  } finally {
    workspace.stopSamples();
    sender.close();
  }
});

test("an older reflected draw cannot undo a newer acknowledged mutation or a selected host change", async () => {
  const host = new FixtureHost();
  const workspace = new Workspace(host as unknown as HostClient);
  await workspace.activate(async () => SOURCE);
  const { samplePort } = await workspace.startSamples();
  try {
    let deliverOld!: () => void;
    host.onReflect = snapshot => new Promise(resolve => { deliverOld = () => resolve(snapshot); });
    const oldObservation = workspace.reflect();
    const applied = await workspace.bindComponent("tone", 1, { mode: "fixed", value: 0.6 });
    assert.equal(applied.reflected?.revision, 2);
    deliverOld();
    const afterOld = await oldObservation;
    assert.equal(afterOld.reflected?.revision, 2);
    assert.equal(afterOld.previewReady, true);
    assert.equal(afterOld.samplePort, samplePort);

    let failOld!: () => void;
    host.onReflect = () => new Promise((_resolve, reject) => {
      failOld = () => reject(new HostError("host_closed", "Old destination closed."));
    });
    const oldFailure = workspace.reflect();
    const second = { ...PEER, target: "88bce241-f530-49cf-8b86-70354afcbdd3", fingerprint: "2".repeat(64) };
    host.onConnect = async () => second;
    const select = workspace.connectRemembered(second.fingerprint);
    assert.equal((await select).peer?.target, second.target);
    assert.throws(() => workspace.restore(), (error: HostError) => error.code === "wrong_host");
    failOld();
    assert.equal((await oldFailure).reflected?.target, second.target);
    assert.equal(workspace.view().samplePort, null);
  } finally {
    workspace.stopSamples();
  }
});

test("activation preserves provenance and excludes unconfigured uniforms from the artifact", async () => {
  const host = new FixtureHost();
  const workspace = new Workspace(host as unknown as HostClient);
  const originPath = `${"folder/".repeat(43)}nested-tone.frag`;
  assert.equal(sourceName(originPath), "nested-tone.frag");
  const source = { ...SOURCE, name: sourceName(originPath), origin: `repo @ commit · ${originPath}` };
  const uniform = (index: number, type: string, values: number[]): DraftUniform =>
    ({ name: `uniform${index}`, values, writable: true, owner: "layer", type });
  host.validatedUniforms = Array.from({ length: 65 }, (_, index) => uniform(index, "vec4", [0, 1, 2, 3]));
  host.validatedUniforms.push({ ...uniform(65, "float", [0]), name: `x${"y".repeat(64)}` });
  const view = await workspace.activate(async () => source);
  assert.equal(view.previewReady, true);
  assert.equal(view.project?.bindings.length, 0);
  assert.equal(view.project?.source.origin, source.origin);
  assert.equal(view.project?.source.name, "nested-tone.frag");
  assert.equal(replay({ ...view.project!, source }).commands.filter(command => command.address === "/shader/var").length, 0);

  host.validatedUniforms = Array.from({ length: 129 }, (_, index) => uniform(index, "float", [0]));
  const next = await workspace.activate(async () => source);
  assert.equal(next.previewReady, true, JSON.stringify(next.run));
  assert.equal(host.mutations, 2);
  assert.equal(workspace.view().previewReady, true);
  assert.equal(workspace.view().reflected?.revision, 2);
});

test("edits during delayed library validation survive the preview request", async () => {
  const host = new FixtureHost();
  const workspace = new Workspace(host as unknown as HostClient);
  const document = (await workspace.create()).document!;
  const started = Promise.withResolvers<void>();
  const validated = Promise.withResolvers<void>();
  host.onValidate = () => {
    started.resolve();
    return validated.promise;
  };
  const preview = workspace.activate(async () => SOURCE);
  await started.promise;
  const selected = workspace.view().document!;
  assert.notEqual(selected.id, document.id);
  workspace.edit({ id: selected.id, generation: selected.generation, editSequence: 1, text: "my late edits" });
  validated.resolve();
  await preview;
  assert.equal(workspace.view().document?.acceptedArtifact?.stale, true);
  assert.equal(host.mutations, 1);
  assert.equal(workspace.view().document?.source.text, "my late edits");
});

function setSource(workspace: Workspace, text: string) {
  const document = workspace.view().document!;
  return workspace.edit({ id: document.id, generation: document.generation,
    editSequence: document.editSequence + 1, text });
}

test("Run preserves accepted controls after native compile failure and maps only reliable locations", async () => {
  const host = new FixtureHost();
  const workspace = new Workspace(host as unknown as HostClient);
  await workspace.openSource(SOURCE);
  const first = await workspace.run();
  assert.equal(first.run.phase, "drawn");
  const control = await workspace.configureSlot("Spin");
  const slot = control.project!.slots[0].id;
  await workspace.bindComponent("tone", 1, { mode: "range", slot, low: 0.2, high: 0.8 });
  const prior = workspace.view();
  setSource(workspace, "#version 330\nerror");
  host.compileError = new HostError("shader_failed", "Compilation failed.", undefined, ["ERROR: 0:2: invalid token"]);
  const failed = await workspace.run();
  assert.equal(failed.run.phase, "failed");
  assert.equal(failed.run.problems[0].line, 2);
  assert.equal(failed.run.attempt?.source, "#version 330\nerror");
  assert.equal(failed.previewReady, true);
  assert.equal(failed.document?.acceptedArtifact?.id, prior.document?.acceptedArtifact?.id);
  setSource(workspace, "#version 330\n#line 12\nerror");
  assert.equal((await workspace.run()).run.problems[0].line, undefined);
  host.compileError = null;
  setSource(workspace, "#version 330\n// corrected");
  const recovered = await workspace.run();
  assert.equal(recovered.run.phase, "drawn");
  assert.equal(recovered.reflected?.lease, prior.reflected?.lease);
  assert.equal(recovered.project?.slots[0].id, slot);
  assert.deepEqual(recovered.project?.bindings[1].binding, { mode: "range", slot, low: 0.2, high: 0.8 });
});

test("mapping review retains baseline sampling and a changed control invalidates approval", async () => {
  const host = new FixtureHost();
  const workspace = new Workspace(host as unknown as HostClient);
  await workspace.openSource(SOURCE);
  await workspace.run();
  const control = await workspace.configureSlot("Spin");
  const slot = control.project!.slots[0].id;
  await workspace.bindComponent("tone", 0, { mode: "range", slot, low: 0.2, high: 0.8 });
  const oldRevision = workspace.view().reflected!.revision;
  host.conflicts = [{ name: "tone", reason: "inactive" }];
  setSource(workspace, "#version 330\n// removed");
  const candidate = await workspace.run();
  assert.equal(candidate.run.phase, "mapping_review");
  const token = candidate.run.review!.token;
  assert.equal(candidate.reflected?.revision, oldRevision);
  const engine = workspace as unknown as { relaySample(slot: string, value: number): Promise<void> };
  await engine.relaySample(slot, 0);
  await engine.relaySample(slot, 1);
  assert.deepEqual(host.samples.map(({ sequence, revision }) => [sequence, revision]), [[1, oldRevision], [2, oldRevision]]);
  await workspace.bindComponent("tone", 0, { mode: "range", slot, low: 0.3, high: 0.9 });
  assert.throws(() => workspace.approveRun(token), { code: "stale_candidate" });
  const reviewed = await workspace.run();
  const confirmed = await workspace.approveRun(reviewed.run.review!.token);
  assert.equal(confirmed.run.phase, "drawn");
  assert.deepEqual(confirmed.project?.bindings, []);
  assert.equal(confirmed.document?.pendingMappings[0].name, "tone");
  await engine.relaySample(slot, 1);
  assert.deepEqual(host.samples.map(({ sequence, revision }) => [sequence, revision]),
    [[1, oldRevision], [2, oldRevision], [3, confirmed.reflected!.revision]]);
  host.conflicts = [];
  const next = await workspace.run();
  assert.equal(next.run.phase, "drawn");
  assert.equal(next.project?.bindings.length, 0);
  assert.equal(next.document?.pendingMappings.length, 1);
});

test("a missing commit receipt pauses publishing until exact readback recovers it", async () => {
  const host = new FixtureHost();
  const workspace = new Workspace(host as unknown as HostClient);
  await workspace.openSource(SOURCE);
  const first = await workspace.run();
  assert.equal(first.previewReady, true);
  setSource(workspace, "#version 330\n// new");
  host.commitReply = "lost";
  host.receiptAvailable = false;
  const unknown = await workspace.run();
  assert.equal(unknown.run.phase, "uncertain");
  assert.equal(unknown.previewReady, false);
  assert.equal(unknown.document?.acceptedArtifact?.id, first.document?.acceptedArtifact?.id);
  assert.throws(() => workspace.run(), { code: "restore_required" });
  host.receiptAvailable = true;
  const recovered = await workspace.reflect();
  assert.equal(recovered.run.phase, "drawn");
  assert.equal(recovered.previewReady, true);
  assert.equal(host.requested.filter(item => item.method === "run.commit").length, 2);
});

test("a switched document fences an old in-flight commit and ignores its result", async () => {
  const host = new FixtureHost();
  const workspace = new Workspace(host as unknown as HostClient);
  await workspace.openSource(SOURCE);
  const initial = await workspace.run();
  assert.equal(initial.previewReady, true);
  setSource(workspace, "#version 330\n// in flight");
  const dispatched = Promise.withResolvers<void>();
  const finish = Promise.withResolvers<void>();
  host.onCommit = () => { dispatched.resolve(); return finish.promise; };
  const pending = workspace.run();
  await dispatched.promise;
  const switched = await workspace.create();
  finish.resolve();
  assert.notEqual(switched.document?.id, initial.document?.id);
  const late = await pending;
  assert.equal(late.document?.id, switched.document?.id);
  assert.equal(late.document?.acceptedArtifact, null);
  assert.equal(late.run.phase, "idle");
  assert.ok(host.requested.some(item => item.method === "run.fence" && item.params.lease === initial.reflected?.lease));
  host.onCommit = undefined;
  const next = await workspace.run();
  assert.equal(next.previewReady, true);
  assert.notEqual(next.reflected?.lease, initial.reflected?.lease);
});

for (const navigation of ["already uncertain", "during commit"] as const) {
  test(`same host waits for the old exact outcome when navigating ${navigation}`, async () => {
    const host = new FixtureHost();
    const workspace = new Workspace(host as unknown as HostClient);
    await workspace.openSource(SOURCE);
    const initial = await workspace.run();
    setSource(workspace, "#version 330\n// previous document commit");
    host.commitReply = "lost";
    host.receiptAvailable = false;
    const reply = Promise.withResolvers<void>();
    const drawn = Promise.withResolvers<void>();
    if (navigation === "during commit") {
      host.afterCommit = () => { drawn.resolve(); return reply.promise; };
    }
    const pending = workspace.run();
    if (navigation === "during commit") await drawn.promise;
    else assert.equal((await pending).run.phase, "uncertain");
    const oldCommit = host.requested.filter(item => item.method === "run.commit").at(-1)!;
    const opened = await workspace.create();
    reply.resolve();
    await pending;
    host.afterCommit = undefined;

    const countBefore = host.requested.filter(item => item.method === "run.prepare").length;
    const blocked = await workspace.run();
    assert.equal(blocked.run.phase, "uncertain");
    assert.match(blocked.run.message!, /previous draft’s commit and lease/);
    assert.deepEqual(blocked.run.problems, []);
    assert.equal(host.requested.filter(item => item.method === "run.prepare").length, countBefore,
      "an acknowledged fence must not authorize a new prepare");
    assert.equal(blocked.document?.acceptedArtifact, null);
    assert.ok(host.requested.some(item => item.method === "run.fence" && item.params.lease === initial.reflected?.lease));
    assert.ok(host.requested.some(item => item.method === "receipt.get" && item.params.id === oldCommit.id));

    host.receiptAvailable = true;
    const reconciled = await workspace.reflect();
    assert.equal(reconciled.run.phase, "idle");
    assert.equal(reconciled.document?.id, opened.document?.id);
    assert.equal(reconciled.document?.acceptedArtifact, null, "the prior draw must never be adopted by the new draft");
    host.commitReply = "valid";
    const independent = await workspace.run();
    assert.equal(independent.run.phase, "drawn");
    assert.equal(independent.reflected?.documentId, opened.document?.id);
    assert.notEqual(independent.reflected?.lease, initial.reflected?.lease);
    assert.notEqual(independent.reflected?.commitID, oldCommit.id);
  });
}

test("an uncertain typed control is kept out of accepted history until exact receipt recovery", async () => {
  const host = new FixtureHost();
  const workspace = new Workspace(host as unknown as HostClient);
  await workspace.openSource(SOURCE);
  const initial = await workspace.run();
  host.uncertainControl = true;
  host.lostNext = true;
  host.receiptAvailable = false;
  const unknown = await workspace.bindComponent("tone", 0, { mode: "fixed", value: 0.4 });
  assert.equal(unknown.run.phase, "uncertain");
  assert.equal(unknown.previewReady, false);
  assert.equal(unknown.document?.acceptedArtifact?.id, initial.document?.acceptedArtifact?.id);
  assert.equal(unknown.project?.bindings.length, 0);
  assert.throws(() => workspace.run(), { code: "restore_required" });
  host.receiptAvailable = true;
  const recovered = await workspace.reflect();
  assert.equal(recovered.previewReady, true);
  assert.notEqual(recovered.document?.acceptedArtifact?.id, initial.document?.acceptedArtifact?.id);
  assert.deepEqual(recovered.project?.bindings[0].binding, { mode: "fixed", value: 0.4 });
  assert.equal(host.requested.filter(item => item.method === "uniform.set").length, 1);
});

test("a detached uncertain typed control blocks same-host Run without adopting prior controls", async () => {
  const host = new FixtureHost();
  const workspace = new Workspace(host as unknown as HostClient);
  await workspace.openSource(SOURCE);
  await workspace.run();
  host.uncertainControl = true;
  host.lostNext = true;
  host.receiptAvailable = false;
  assert.equal((await workspace.configureSlot("Before switch")).run.phase, "uncertain");
  const opened = await workspace.create();
  assert.equal((await workspace.run()).run.phase, "uncertain");
  host.receiptAvailable = true;
  const ready = await workspace.reflect();
  assert.equal(ready.run.phase, "idle");
  assert.equal(ready.document?.id, opened.document?.id);
  assert.equal(ready.document?.acceptedArtifact, null);
  const independent = await workspace.run();
  assert.equal(independent.previewReady, true);
  assert.deepEqual(independent.project?.slots, []);
});

test("a restarted same peer can prove a new epoch after the pinned old port disappears", async () => {
  const host = new FixtureHost();
  const workspace = new Workspace(host as unknown as HostClient);
  await workspace.openSource(SOURCE);
  const initial = await workspace.run();
  assert.equal(initial.previewReady, true);
  setSource(workspace, "#version 330\n// uncertain old epoch");
  host.commitReply = "lost";
  host.receiptAvailable = false;
  assert.equal((await workspace.run()).run.phase, "uncertain");
  const oldPort = PEER.endpoint.port;
  host.unreachablePorts.add(oldPort);
  const newPeer = { ...PEER, endpoint: { ...PEER.endpoint, port: oldPort + 1 } };
  host.onConnect = async () => newPeer;
  await workspace.connectRemembered(newPeer.fingerprint);
  assert.equal(workspace.view().document?.acceptedArtifact?.id, initial.document?.acceptedArtifact?.id);

  host.commitReply = "valid";
  const recovered = await workspace.restore();
  assert.equal(recovered.run.phase, "drawn");
  assert.equal(recovered.reflected?.epoch, "epoch-b");
  assert.equal(recovered.document?.acceptedArtifact?.sourceDigest, initial.document?.acceptedArtifact?.sourceDigest);
  assert.ok(host.sessionRequests.some(request => request.recovery && request.port === oldPort));
  assert.ok(host.sessionRequests.some(request => !request.recovery && request.method === "reflect" &&
    request.port === newPeer.endpoint.port));
  assert.ok(!host.sessionRequests.some(request => request.recovery && request.port === newPeer.endpoint.port),
    "old pending request IDs and fences must not be redirected to the fresh port");
});

test("a fresh port with the same epoch does not release the prior proof barrier", async () => {
  const host = new FixtureHost();
  const workspace = new Workspace(host as unknown as HostClient);
  await workspace.openSource(SOURCE);
  const initial = await workspace.run();
  host.commitReply = "lost";
  host.receiptAvailable = false;
  assert.equal((await workspace.run()).run.phase, "uncertain");
  const oldPort = PEER.endpoint.port;
  host.unreachablePorts.add(oldPort);
  const newPeer = { ...PEER, endpoint: { ...PEER.endpoint, port: oldPort + 1 } };
  host.onConnect = async () => newPeer;
  await workspace.connectRemembered(newPeer.fingerprint);
  host.state.epoch = initial.reflected!.epoch;
  const countBefore = host.requested.filter(request => request.method === "run.prepare").length;
  assert.equal((await workspace.run()).run.phase, "uncertain");
  assert.equal(host.requested.filter(request => request.method === "run.prepare").length, countBefore);
  assert.ok(!host.sessionRequests.some(request => request.recovery && request.port === newPeer.endpoint.port));
});

test("an unmarked host change before commit dispatch does not trigger uncertain recovery", async () => {
  const host = new FixtureHost();
  const workspace = new Workspace(host as unknown as HostClient);
  await workspace.openSource(SOURCE);
  const initial = await workspace.run();
  host.onCommit = async () => { throw new HostError("host_changed", "Selected host changed before dispatch."); };
  const blocked = await workspace.run();
  assert.equal(blocked.run.phase, "failed");
  assert.equal(blocked.document?.acceptedArtifact?.id, initial.document?.acceptedArtifact?.id);
  assert.ok(!host.sessionRequests.some(request => request.recovery && request.method === "receipt.get"));
});

test("a definitive typed-control renderer loss clears the live preview immediately", async () => {
  const host = new FixtureHost();
  const workspace = new Workspace(host as unknown as HostClient);
  await workspace.openSource(SOURCE);
  const drawn = await workspace.run();
  host.controlError = new HostError("draft_state_lost", "The private renderer could not confirm its controls.");
  await assert.rejects(() => workspace.configureSlot("Lost"), { code: "draft_state_lost" });
  const lost = workspace.view();
  assert.equal(lost.previewReady, false);
  assert.equal(lost.run.phase, "restore_required");
  assert.equal(lost.document?.acceptedArtifact?.id, drawn.document?.acceptedArtifact?.id);
  assert.deepEqual(lost.project?.slots, []);
});

const HYDRA = 'osc(11, 0, 0.7).blend(osc(7, 0, 1.1), param("mix", 0.5)).rotate(17).out();';
const compilePatch = (text: string) => compileHydra(evaluateHydraPatch(text), text);
const candidateCalls = (host: FixtureHost) =>
  host.requested.filter(({ method }) => method === "run.prepare" || method === "run.commit");

test("Hydra keeps an existing patch's local o1 declaration and source locations", () => {
  const source = "const o1 = osc();\no1.out();";
  assert.deepEqual(compilePatch(source), compilePatch("const a1 = osc();\na1.out();"));
  assert(compilePatch(source).spans.some(span => span.location?.line === 1 && span.location.column === 12));
  const invalid = "const o1 = osc();\no1.src().out();";
  assert.throws(() => compilePatch(invalid), error => {
    assert(error instanceof Error);
    assert.deepEqual(patchErrorLocation(error, invalid), { line: 2, column: 4 });
    return true;
  });
});

test("Hydra compiler maps helper operations, sorts reachable uniforms and bounds the graph", async () => {
  const animated = await readFile("tests/fixtures/hydra/animated.js", "utf8");
  const result = compilePatch(animated);
  assert.deepEqual(result, compilePatch(animated));
  assert.match(result.fragment, /^#version 330\nuniform float time;\nuniform vec2 resolution;\nuniform float u_hydra_mix = 0.5;/);
  assert.match(result.fragment, /vec2 st = gl_FragCoord.xy \/ resolution;/);
  assert.match(result.fragment, /hydraColor = blend\(osc\(rotate\(rotate\(rotate\(st/);
  assert.match(result.fragment, /clamp\(u_hydra_mix, 0.0, 1.0\)/);
  assert(result.spans.some(span => span.location?.line === 2));
  assert.doesNotMatch(compilePatch('param("unused", 0.4); osc().out();').fragment, /u_hydra_unused/);
  assert.throws(() => compilePatch('param("x", 0.4); param("x", 0.5); osc().out();'), /Conflicting/);
  assert.throws(() => compilePatch("osc().out(0);"), /out\(\) or out\(o0\)/);
  assert.throws(() => compilePatch("osc().out(); osc().out();"), /exactly one/);
  assert.throws(() => compilePatch("osc(() => 4).out();"), /osc frequency/);
  assert.throws(() => compilePatch("osc(-1).out();"), /osc frequency/);
  assert.throws(() => compilePatch("osc().blend(osc(), 2).out();"), /blend amount/);
  assert.throws(() => compilePatch("let x = osc(); for(let i = 0; i < 32; i++) x = x.rotate(); x.out();"), /depth 32/);
  assert.throws(() => compilePatch("let x = osc(); for(let i = 0; i < 7; i++) x = x.blend(x); x.out();"), /128 operations/);
  assert.throws(() => compilePatch("for(let i = 0; i < 17; i++) param('x' + i, 0); osc().out();"), /16 parameters/);
  const program = evaluateHydraPatch(HYDRA);
  assert("functions" in program);
  assert.throws(() => compileHydra({ ...program, colorExpression: "sampler2D(st)" }, HYDRA), /Invalid Triode/);
  assert.throws(() => compilePatch("try { osc().out(0); } catch {} osc().out();"), /exactly one/);
  assert.throws(() => compilePatch("osc([1, 2]).out();"), /array sequences/);
  assert.throws(() => compilePatch("osc().src().out();"), /Unsupported Hydra/);
  assert.throws(() => compilePatch("osc().blend(osc(), () => 1).out();"), /Frame callbacks/);
  assert.throws(() => compilePatch('setFunction({name:"tint",type:"color",inputs:[],glsl:"return texture(s0, st);"}); osc().out();'), /dependency/);
  const reused = compilePatch('const a = osc(1e-38); a.rotate(9).blend(a.rotate(7)).out();').fragment;
  assert.match(reused, /blend\(osc\(rotate\(st, 9.0,[^,]*\), [\d.e-]+, [^,]*, 0\.0\), osc\(rotate\(st, 7\.0/);
  assert.match(reused, /9\.999999350456404e-39/);
  assert.doesNotMatch(reused, /offset\/frequency/);
  const commentBody = '// A comment with vec4 tint(vec4 _c0)\nreturn _c0;';
  const colorPatch = (name: string, inputs: unknown[], glsl: string) =>
    `setFunction(${JSON.stringify({ name, type: "color", inputs, glsl })}); osc().${name}().out();`;
  assert.match(compilePatch(colorPatch("tint", [], commentBody)).fragment,
    /vec4 tint\(vec4 _c0\) \{\n +\/\/ A comment with vec4 tint\(vec4 _c0\)\nreturn _c0;/);
  for (const name of ["sampler2D", "precision", "uimage2D", "row_major"]) {
    assert.throws(() => compilePatch(colorPatch(name, [], "return _c0;")), /uniquely named/);
    assert.throws(() => compilePatch(colorPatch("tint", [{ name, type: "float", default: 0 }], "return _c0;")),
      /unique named float inputs/);
  }
  const exact = 'const glsl = "return _c0;"; setFunction({name:"tint",type:"color",inputs:[],' +
    'glsl:glsl.padEnd(16384," ")}); osc().tint().out();';
  assert.match(compilePatch(exact).fragment, /vec4 tint\(vec4 _c0\)/);
  assert.throws(() => compilePatch('setFunction({name:"tint",type:"color",inputs:[],glsl:" ".repeat(16385)}); osc().out();'), /16 KiB/);
  assert.throws(() => compilePatch('for (let i=0; i<5; i++) setFunction({name:"tint"+i,type:"color",inputs:[],glsl:" ".repeat(16000)}); osc().out();'), /64 KiB/);
  const unsupported = 'const wave = osc();\nwave.rotate().src().out();';
  assert.throws(() => compilePatch(unsupported), error => {
    assert(error instanceof Error);
    assert.equal(patchErrorLocation(error, unsupported)?.line, 2);
    return true;
  });
  const badFunctions = program.functions.map((fn, index) =>
    index ? fn : { ...fn, source: fn.source.replace("float frequency", "vec2 frequency") });
  assert.throws(() => compileHydra({ ...program, functions: badFunctions }, HYDRA), /Invalid Triode shader function inputs/);
});

test("Hydra emits a guarded previous-frame sample for its single output", () => {
  const direct = compilePatch("\nsrc(o0).out(o0);");
  assert.deepEqual(direct, compilePatch("\nsrc(o0).out();"));
  assert.match(direct.fragment, /^#version 330\n#ifndef VOSC_SELF_FEEDBACK\n#error VOSC self-feedback requires a matching renderer\n#endif\nuniform sampler2D u_vosc_previous;/);
  assert.match(direct.fragment, /return texture\(u_vosc_previous, st\);/);
  assert.match(direct.fragment, /hydraColor = voscPrevious\(st\);/);
  assert.equal(direct.spans.length, 2);
  for (const span of direct.spans) assert.equal(span.location?.line, 2);

  const blended = compilePatch("osc().blend(src(o0).rotate(12), 0.25).blend(src(o0), 0.5).out(o0);");
  assert.match(blended.fragment, /blend\(blend\(osc\(.+voscPrevious\(rotate\(st, 12\.0, 0\.0\)\), 0\.25\), voscPrevious\(st\), 0\.5\)/);
  assert.equal(blended.fragment.match(/uniform sampler2D/g)?.length, 1);
  assert.equal(blended.fragment.match(/vec4 voscPrevious\(/g)?.length, 1);
  assert.doesNotMatch(compilePatch("src(o0); osc().out(o0);").fragment, /VOSC_SELF_FEEDBACK/);

  for (const patch of ["src().out();", "src(o0, o0).out();", "src({}).out();", "src('camera').out();"]) {
    assert.throws(() => compilePatch(patch), /only src\(o0\)/);
  }
  assert.throws(() => compilePatch("src(o0).out({});"), /exactly one output/);
  assert.throws(() => compilePatch("src(o0).out(o0); osc().out();"), /exactly one output/);
  assert.throws(() => compilePatch(
    'setFunction({name:"voscPrevious",type:"color",inputs:[],glsl:"return _c0;"}); src(o0).out();'), /uniquely named/);
  assert.throws(() => compilePatch('setFunction({name:"tint",type:"color",inputs:[],' +
    'glsl:"return texture(u_vosc_previous, _c0.xy);"}); src(o0).tint().out();'), /unsupported custom color dependency/i);

  const patch = "src(o0).out();";
  const program = evaluateHydraPatch(patch);
  assert("functions" in program);
  assert.throws(() => compileHydra({ ...program, feedback: null }, patch), /Invalid Triode/);
  assert.throws(() => compileHydra({ ...program, feedback: { location: null, source: "untrusted" } }, patch),
    /Invalid Triode previous-frame source/);
});

const TWO_OUTPUT_HYDRA = [
  'const shared = param("shared", 0.25);',
  'const display = param("display", 0.75);',
  'src(o0).blend(osc(11), shared).out(o0);',
  'src(o0).blend(osc(7), shared).blend(osc(13), display).out(o1);',
].join("\n");

test("Hydra emits the same ordered two-pass graph for either declaration order", () => {
  const { fragment, spans } = compilePatch(TWO_OUTPUT_HYDRA);
  const sourceLines = TWO_OUTPUT_HYDRA.split("\n");
  assert.equal(compilePatch([...sourceLines.slice(0, 2), sourceLines[3], sourceLines[2]].join("\n")).fragment, fragment);
  assert.equal(compilePatch(TWO_OUTPUT_HYDRA.replace("out(o0)", "out()")).fragment, fragment);
  const [header, previous, current] = fragment.split(/\/\/ VOSC pass o[01]\n/);
  assert.equal(header, "#version 330\n#ifndef VOSC_HYDRA_TWO_OUTPUT\n#error VOSC two-output Hydra requires a matching renderer\n#endif\n");
  assert.match(previous, /^#version 330\n#ifndef VOSC_SELF_FEEDBACK\n/);
  assert.match(previous, /texture\(u_vosc_previous, st\)/);
  assert.match(current, /^#version 330\n#ifndef VOSC_SELF_FEEDBACK\n/);
  assert.match(current, /texture\(u_vosc_current, st\)/);
  assert.doesNotMatch(previous, /u_vosc_current|u_hydra_display/);
  assert.doesNotMatch(current, /u_vosc_previous/);
  for (const pass of [previous, current]) assert.match(pass, /uniform float u_hydra_shared = 0.25;/);
  assert.match(current, /uniform float u_hydra_display = 0.75;/);
  const lines = fragment.split("\n");
  for (const [sampler, patchLine] of [["u_vosc_previous", 3], ["u_vosc_current", 4]] as const) {
    const generatedLine = lines.findIndex(line => line.includes(`return texture(${sampler},`)) + 1;
    assert(generatedLine > 0);
    assert.equal(spans.find(span => span.firstLine <= generatedLine && generatedLine <= span.lastLine)?.location?.line, patchLine);
  }
});

test("Hydra rejects unbounded or malformed two-output graphs", () => {
  for (const source of [
    "src(o0).out(o1);",
    "src(o0).out(); src(o0).out(o1); src(o0).out(o1);",
    "src(o0).out(); try { src(o0).out(o1, o0); } catch {} src(o0).out(o1);",
    "src(o0).out(); src(o0).out(o2);",
  ]) assert.throws(() => compilePatch(source), /output|o2/);
  assert.throws(() => compilePatch("src(o0).out(); src(o1).out(o1);"), /only src\(o0\)/);
  assert.throws(() => compilePatch("src(o0).out(); src('camera').out(o1);"), /only src\(o0\)/);
  assert.throws(() => compilePatch("osc().out(); src(o0).out(o1);"), /sample src\(o0\) in both passes/);
  assert.throws(() => compilePatch("src(o0).out(); osc().out(o1);"), /sample src\(o0\) in both passes/);
  assert.throws(() => compilePatch('let x = src(o0); for (let i = 0; i < 6; i++) x = x.blend(x); x.out(); x.out(o1);'),
    /128 expanded operations/);
  const markerBody = "/*\n// VOSC pass o1\n*/\nreturn _c0;";
  assert.throws(() => compilePatch(`setFunction({name:"tint",type:"color",inputs:[],glsl:${JSON.stringify(markerBody)}});
    src(o0).tint().out(); src(o0).out(o1);`), /pass framing/);

  const program = evaluateHydraPatch(TWO_OUTPUT_HYDRA);
  assert("passes" in program && program.passes);
  const [previous, current] = program.passes;
  assert.throws(() => compileHydra({ passes: [previous] }, TWO_OUTPUT_HYDRA), /two-output compiler result/);
  assert.throws(() => compileHydra({ passes: [previous, current], feedback: null }, TWO_OUTPUT_HYDRA), /two-output compiler result/);
  const altered = { ...current, uniforms: current.uniforms.map(uniform => uniform.name === "u_hydra_shared" ?
    { ...uniform, value: 0.5 } : uniform) };
  assert.throws(() => compileHydra({ passes: [previous, altered] }, TWO_OUTPUT_HYDRA), /Conflicting defaults/);
});

test("Hydra saves, exports and restores the accepted two-output artifact without re-evaluating", async () => {
  const host = new FixtureHost();
  host.validatedUniforms = [{ name: "u_hydra_display", values: [0.75], type: "float", owner: "layer", writable: true }];
  let evaluations = 0;
  const evaluate = async (source: string) => { evaluations++; return compilePatch(source); };
  const workspace = new Workspace(host as unknown as HostClient, evaluate);
  await workspace.create("hydra-js");
  setSource(workspace, TWO_OUTPUT_HYDRA);
  const result = await workspace.run();
  assert.equal(result.run.phase, "drawn");
  const fragment = compilePatch(TWO_OUTPUT_HYDRA).fragment;
  assert.equal(result.document?.acceptedArtifact?.generated?.digest, hash(fragment));
  await workspace.bindComponent("u_hydra_display", 0, { mode: "fixed", value: 0.4 });
  setSource(workspace, "src(o1).out();");
  const directory = await mkdtemp(join(tmpdir(), "vosc-hydra-two-output-"));
  try {
    const path = join(directory, "two-output.vosc-draft");
    await workspace.save(path);
    host.peer = null;
    const reopened = new Workspace(host as unknown as HostClient, evaluate);
    await reopened.open(path);
    const recipe = await reopened.export(directory);
    assert.equal(await readFile(join(recipe, "..", "draft.frag"), "utf8"), fragment);
    assert.match(await readFile(recipe, "utf8"), /u_hydra_display/);
    host.peer = PEER;
    const restored = await reopened.restore();
    assert.equal(restored.run.phase, "drawn");
    assert.equal(restored.document?.source.text, "src(o1).out();");
    assert.equal(restored.document?.acceptedArtifact?.generated?.digest, hash(fragment));
    assert.equal(evaluations, 1);
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
});

test("Feedback Echo stays cold until manual Run and retains accepted GLSL across offline save, export and Restore", async () => {
  const host = new FixtureHost();
  host.validatedUniforms = [{ name: "u_hydra_trail", values: [0.91], type: "float", owner: "layer", writable: true }];
  let evaluations = 0;
  const evaluate = async (source: string) => { evaluations++; return compilePatch(source); };
  const workspace = new Workspace(host as unknown as HostClient, evaluate);
  await workspace.create();
  await workspace.run();
  assert.equal(workspace.view().run.manualReady, true);
  const before = candidateCalls(host).length;
  const example = feedbackExampleSource();
  const opened = await workspace.openSource(example, "hydra-js");
  assert.equal(opened.document?.source.name, feedbackExample.path);
  const patch = opened.document!.source.text;
  await workspace.run("automatic");
  assert.equal(evaluations, 0);
  assert.equal(candidateCalls(host).length, before);
  const success = await workspace.run();
  assert.equal(success.run.phase, "drawn");
  assert.equal(success.run.manualReady, false);
  assert.equal(evaluations, 1);
  const generated = success.run.attempt!.generated!;
  assert.match(generated.source, /uniform sampler2D u_vosc_previous;/);
  const prepare = candidateCalls(host).at(-2)!;
  assert.equal(prepare.params.sha256, hash(Buffer.from(String(prepare.params.source), "base64")));
  assert.equal(prepare.params.sha256, generated.digest);
  assert.equal(success.run.attempt?.sourceDigest, hash(patch));
  await workspace.configureSlot("Trail");
  const slot = workspace.view().project!.slots[0].id;
  await workspace.bindComponent("u_hydra_trail", 0, { mode: "range", slot, low: 0.15, high: 0.85 });
  const working = patch.replace("rotate(3)", "rotate(5)");
  setSource(workspace, working);
  const temporary = await mkdtemp(join(tmpdir(), "vosc-hydra-offline-"));
  try {
    const path = join(temporary, "hydra.vosc-draft");
    await workspace.save(path);
    const saved = JSON.parse(await readFile(path, "utf8"));
    assert.equal(saved.accepted.hydra.patch.digest, hash(patch));
    assert.equal(saved.accepted.hydra.generatedDigest, generated.digest);
    assert.equal(saved.accepted.hydra.version, 2);
    assert.equal(saved.accepted.hydra.compilerId, TRIODE_COMPILER_ID);
    assert.throws(() => parseDocument({ ...saved, accepted: { ...saved.accepted,
      hydra: { ...saved.accepted.hydra, version: 3 } } }), /provenance/);
    assert.throws(() => parseDocument({ ...saved, accepted: { ...saved.accepted,
      hydra: { ...saved.accepted.hydra, compilerId: "unpinned" } } }), /provenance/);
    assert.throws(() => parseDocument({ ...saved, accepted: { ...saved.accepted,
      hydra: { ...saved.accepted.hydra, generatedDigest: hash("different") } } }), /provenance/);
    const invalid = structuredClone(saved);
    invalid.accepted.hydra.spans[0].lastLine = 99999;
    assert.throws(() => parseDocument(invalid), /provenance/);
    invalid.accepted.hydra.spans = saved.accepted.hydra.spans;
    invalid.accepted.hydra.patch.digest = hash("corrupt patch");
    assert.throws(() => parseDocument(invalid), /digest/);

    host.peer = null;
    const reopened = new Workspace(host as unknown as HostClient, evaluate);
    await reopened.open(path);
    assert.equal(reopened.view().document?.source.text, working);
    assert(reopened.view().document?.acceptedArtifact?.stale);
    const recipe = await reopened.export(temporary);
    assert.equal(await readFile(join(recipe, "..", "draft.frag"), "utf8"), generated.source);
    assert.match(await readFile(recipe, "utf8"), /u_hydra_trail/);
    host.peer = PEER;
    const restored = await reopened.restore();
    assert.equal(restored.run.phase, "drawn");
    assert.equal(restored.document?.source.text, working);
    assert.equal(restored.document?.acceptedArtifact?.generated?.digest, generated.digest);
    assert(restored.document?.acceptedArtifact?.stale);
    assert.equal(evaluations, 1);
  } finally {
    await rm(temporary, { recursive: true, force: true });
  }
});

test("Legacy M4 accepted GLSL and OSC mappings survive Restore before the first Triode Run", async () => {
  const path = "tests/fixtures/hydra/legacy-m4.vosc-draft";
  const original = parseDocument(JSON.parse(await readFile(path, "utf8")));
  const artifact = original.accepted!;
  assert.equal(artifact.hydra?.version, 1);
  const host = new FixtureHost();
  host.validatedUniforms = [{ name: "u_hydra_mix", values: [0.5], type: "float", owner: "layer", writable: true }];
  host.peer = null;
  let evaluations = 0;
  const workspace = new Workspace(host as unknown as HostClient, async js => { evaluations++; return compilePatch(js); });
  const temporary = await mkdtemp(join(tmpdir(), "vosc-hydra-legacy-"));
  try {
    await workspace.open(path);
    assert.equal(candidateCalls(host).length, 0);
    const saved = join(temporary, "saved.vosc-draft");
    await workspace.save(saved);
    assert.deepEqual(parseDocument(JSON.parse(await readFile(saved, "utf8"))), original);
    const recipe = await workspace.export(temporary);
    assert.equal(hash(await readFile(join(recipe, "..", "draft.frag"))), artifact.project.source.digest);
    host.peer = PEER;
    assert.equal((await workspace.restore()).run.phase, "drawn");
    assert.equal(candidateCalls(host).at(-2)?.params.sha256, artifact.project.source.digest);
    assert.equal(evaluations, 0);
    assert.equal((await workspace.run()).run.phase, "drawn");
    assert.equal(evaluations, 1);
    const migrated = join(temporary, "triode.vosc-draft");
    await workspace.save(migrated);
    const upgraded = parseDocument(JSON.parse(await readFile(migrated, "utf8")));
    assert.equal(upgraded.accepted?.hydra?.version, 2);
    assert.deepEqual(upgraded.accepted?.project.bindings, artifact.project.bindings);
  } finally {
    await rm(temporary, { recursive: true, force: true });
  }
});

test("Hydra late evaluations cannot prepare after editing, superseding, switching, closing or cancellation", async () => {
  const replacement = HYDRA.replace("rotate(17)", "rotate(19)");
  for (const trigger of ["edit", "unflushed edit", "supersede", "destination", "close", "cancel", "crash"]) {
    const host = new FixtureHost();
    const dispatch = Promise.withResolvers<AbortSignal>();
    const gate = Promise.withResolvers<ReturnType<typeof compilePatch>>();
    let count = 0;
    const workspace = new Workspace(host as unknown as HostClient, async (text, signal) => {
      if (++count !== 1) return compilePatch(text);
      dispatch.resolve(signal);
      return gate.promise;
    });
    await workspace.openSource(sourceWithText(SOURCE, HYDRA), "hydra-js");
    const pending = workspace.run();
    const signal = await dispatch.promise;
    if (trigger === "edit") setSource(workspace, replacement);
    if (trigger === "unflushed edit") {
      const { id, generation, editSequence } = workspace.view().document!;
      assert(workspace.invalidateHydraEdit({ id, generation, editSequence: editSequence + 1 }));
    }
    if (trigger === "supersede") await workspace.run();
    if (trigger === "destination") await workspace.disconnect();
    if (trigger === "close") workspace.clear();
    if (trigger === "cancel") await workspace.cancelRun();
    if (trigger === "crash") {
      gate.reject(new HostError("hydra_crash", "Renderer exited"));
    } else {
      assert(signal.aborted, trigger);
      gate.resolve(compilePatch(HYDRA));
    }
    await pending;
    if (trigger === "supersede") {
      for (let i = 0; i < 20 && workspace.view().run.phase !== "drawn"; i++) await delay(0);
      assert.deepEqual(candidateCalls(host).map(({ method }) => method), ["run.prepare", "run.commit"]);
      assert.equal(candidateCalls(host)[0].params.runSequence, 2);
    } else {
      assert.equal(candidateCalls(host).length, 0, trigger);
      assert.equal(workspace.view().document?.acceptedArtifact ?? null, null);
    }
  }
});

test("Hydra native diagnostics refer to immutable generated bytes, and edited mapping review stays cold", async () => {
  const host = new FixtureHost();
  host.validatedUniforms = [{ name: "u_hydra_mix", values: [0.5], type: "float", owner: "layer", writable: true }];
  let evaluations = 0;
  const workspace = new Workspace(host as unknown as HostClient, async source => { evaluations++; return compilePatch(source); });
  await workspace.openSource(sourceWithText(SOURCE, HYDRA), "hydra-js");
  await workspace.run();
  await workspace.bindComponent("u_hydra_mix", 0, { mode: "fixed", value: 0.4 });
  const accepted = workspace.view().document?.acceptedArtifact?.id;
  const custom = [
    'setFunction({name:"nativeFailure", type:"color", inputs:[],',
    '  glsl:"float bad = vec3(1.0); return vec4(bad);"});',
    HYDRA.replace(".out()", ".nativeFailure().out()"),
  ].join("\n");
  setSource(workspace, custom);
  const generatedLine = compilePatch(custom).fragment.split("\n").findIndex(line => line.includes("float bad")) + 1;
  assert(generatedLine > 0);
  host.compileError = new HostError("compile_error", "Failed", undefined, [`0(${generatedLine}) : invalid type`, "unknown driver record"]);
  const failed = await workspace.run();
  setSource(workspace, "osc().out();");
  assert.deepEqual(failed.run.problems.map(({ generatedLine, line, message }) => [generatedLine, line, message]),
    [[generatedLine, 1, `0(${generatedLine}) : invalid type`], [undefined, undefined, "unknown driver record"]]);
  assert.equal(failed.run.attempt?.source, custom);
  assert.equal(workspace.view().document?.acceptedArtifact?.id, accepted);
  host.compileError = null;
  host.conflicts = [{ name: "u_hydra_mix", reason: "inactive" }];
  const review = await workspace.run();
  assert.equal(review.run.phase, "mapping_review");
  setSource(workspace, HYDRA);
  await workspace.approveRun(review.run.review!.token);
  assert.equal(host.requested.at(-1)?.method, "run.cancel");
  assert.equal(evaluations, 3);
  assert.equal(workspace.view().document?.acceptedArtifact?.id, accepted);
});

test("Hydra cancellation after evaluation prevents admission after native reflection", async () => {
  const host = new FixtureHost();
  host.state.lease = "prior-native-lease";
  const entered = Promise.withResolvers<void>();
  const release = Promise.withResolvers<void>();
  host.onReflect = async state => {
    entered.resolve();
    await release.promise;
    return state;
  };
  const workspace = new Workspace(host as unknown as HostClient, async text => compilePatch(text));
  await workspace.openSource(sourceWithText(SOURCE, HYDRA), "hydra-js");
  const pending = workspace.run();
  await entered.promise;
  assert.equal(workspace.view().run.phase, "preparing");
  await workspace.cancelRun();
  release.resolve();
  await pending;
  assert.equal(candidateCalls(host).length, 0);
  assert.equal(workspace.view().run.phase, "idle");
  assert.equal((await workspace.run()).run.phase, "drawn");
  assert(host.requested.some(request => request.method === "run.fence" && request.params.lease === "prior-native-lease"));
});
