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
import { hash, HostError } from "../src/local-state";
import { readSample } from "../src/osc-samples";
import { compileHydra } from "../src/hydra-compiler";
import { evaluateHydraPatch } from "../src/hydra-patch";
import { Workspace } from "../src/workspace";
import { HostClient } from "../src/host-client";
import { Importer } from "../src/importer";
import type { DraftProject, DraftSource, DraftUniform, Peer, ReflectedDraft } from "../src/contracts";

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

test("Hydra compiler maps helper operations, sorts reachable uniforms and bounds the graph", async () => {
  const animated = await readFile("tests/fixtures/hydra/animated.js", "utf8");
  const result = compilePatch(animated);
  assert.deepEqual(result, compilePatch(animated));
  assert.match(result.fragment, /^#version 330\nuniform float time;\nuniform vec2 resolution;\nuniform float u_hydra_mix = 0.5;/);
  assert.match(result.fragment, /hydra_8\(gl_FragCoord.xy \/ resolution\)/);
  assert.deepEqual(result.spans.slice(0, 3).map(item => item.location),
    [{ line: 2, column: 14 }, { line: 4, column: 17 }, { line: 4, column: 17 }]);
  assert.doesNotMatch(compilePatch('param("unused", 0.4); osc().out();').fragment, /u_hydra_unused/);
  assert.throws(() => compilePatch('param("x", 0.4); param("x", 0.5); osc().out();'), /Conflicting/);
  assert.throws(() => compilePatch("osc().out(0);"), /out expects 0/);
  assert.throws(() => compilePatch("osc().out(); osc().out();"), /exactly one/);
  assert.throws(() => compilePatch("osc(() => 4).out();"), /osc frequency/);
  assert.throws(() => compilePatch("osc(-1).out();"), /osc frequency/);
  assert.throws(() => compilePatch("osc().blend(osc(), 2).out();"), /blend amount/);
  assert.throws(() => compilePatch("let x = osc(); for(let i = 0; i < 32; i++) x = x.rotate(); x.out();"), /depth 32/);
  assert.throws(() => compilePatch("let x = osc(); for(let i = 0; i < 7; i++) x = x.blend(x); x.out();"), /128 operations/);
  assert.throws(() => compilePatch("for(let i = 0; i < 17; i++) param('x' + i, 0); osc().out();"), /16 parameters/);
  const graph = evaluateHydraPatch(HYDRA);
  assert.throws(() => compileHydra({ ...graph, output: { op: "raw", location: null } }, HYDRA), /Unsupported/);
});

test("Hydra stays cold until manual Run and retains separate accepted bytes across offline save and Restore", async () => {
  const host = new FixtureHost();
  host.validatedUniforms = [{ name: "u_hydra_mix", values: [0.5], type: "float", owner: "layer", writable: true }];
  let evaluations = 0;
  const evaluate = async (source: string) => { evaluations++; return compilePatch(source); };
  const workspace = new Workspace(host as unknown as HostClient, evaluate);
  await workspace.create();
  await workspace.run();
  assert.equal(workspace.view().run.manualReady, true);
  await workspace.create("hydra-js");
  setSource(workspace, HYDRA);
  const before = candidateCalls(host).length;
  await workspace.run("automatic");
  assert.equal(evaluations, 0);
  assert.equal(candidateCalls(host).length, before);
  const success = await workspace.run();
  assert.equal(success.run.phase, "drawn");
  assert.equal(success.run.manualReady, false);
  assert.equal(evaluations, 1);
  const generated = success.run.attempt!.generated!;
  const prepare = candidateCalls(host).at(-2)!;
  assert.equal(prepare.params.sha256, hash(Buffer.from(String(prepare.params.source), "base64")));
  assert.equal(prepare.params.sha256, generated.digest);
  assert.equal(success.run.attempt?.sourceDigest, hash(HYDRA));
  await workspace.configureSlot("Mix");
  const slot = workspace.view().project!.slots[0].id;
  await workspace.bindComponent("u_hydra_mix", 0, { mode: "range", slot, low: 0.15, high: 0.85 });
  const working = HYDRA.replace("rotate(17)", "rotate(19)");
  setSource(workspace, working);
  const temporary = await mkdtemp(join(tmpdir(), "vosc-hydra-offline-"));
  try {
    const path = join(temporary, "hydra.vosc-draft");
    await workspace.save(path);
    const saved = JSON.parse(await readFile(path, "utf8"));
    assert.equal(saved.accepted.hydra.patch.digest, hash(HYDRA));
    assert.equal(saved.accepted.hydra.generatedDigest, generated.digest);
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
    assert.match(await readFile(recipe, "utf8"), /u_hydra_mix/);
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
  host.compileError = new HostError("compile_error", "Failed", undefined, ["0(8) : invalid type", "unknown driver record"]);
  const failed = await workspace.run();
  setSource(workspace, "osc().out();");
  assert.deepEqual(failed.run.problems.map(({ generatedLine, line, message }) => [generatedLine, line, message]),
    [[8, 1, "0(8) : invalid type"], [undefined, undefined, "unknown driver record"]]);
  assert.equal(failed.run.attempt?.source, HYDRA);
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
