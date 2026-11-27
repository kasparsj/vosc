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
import { parseProject, replay, sourceName, titleFor, writeReplay } from "../src/draft-project";
import { hash, HostError } from "../src/local-state";
import { readSample } from "../src/osc-samples";
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
  sampleError: HostError | null = null;
  sampleCount = 0;
  lost?: { id: string; ok: boolean; result: ReflectedDraft };
  async rpc(method: string, params: Record<string, unknown>, expected?: { epoch: string; revision: number }, id?: string) {
    if (method === "reflect") {
      const snapshot = structuredClone(this.state);
      const reflect = this.onReflect;
      this.onReflect = undefined;
      return reflect ? reflect(snapshot) : snapshot;
    }
    if (method === "validate") {
      await this.onValidate?.();
      return { target: this.peer?.target, sourceDigest: params.sha256, receipt: { status: "rendered" },
        shader: { uniforms: structuredClone(this.validatedUniforms) } };
    }
    if (method === "receipt.get") {
      assert.equal(params.id, this.lost?.id);
      return { state: "completed", response: this.lost };
    }
    assert.equal(expected?.epoch, this.state.epoch);
    assert.equal(expected?.revision, this.state.revision);
    if (method === "sample") {
      this.sampleCount += 1;
      throw this.sampleError;
    }
    this.mutations += 1;
    if (method === "activate") {
      this.state.sourceDigest = String(params.sha256);
      this.state.needsRestore = false;
      this.state.uniforms = structuredClone(this.validatedUniforms);
    } else if (method === "uniform.set") {
      this.state.uniforms[0].values[Number(params.component)] = Math.fround(Number(params.value));
    } else {
      throw new Error("Unexpected operation " + method);
    }
    this.state.revision += 1;
    this.state.receipt = { id: "native", frame: this.state.revision * 2, revision: this.state.revision, external_epoch: 0, status: "rendered" };
    const result = structuredClone(this.state);
    if (this.lostNext) {
      this.lostNext = false;
      this.lost = { id: id!, ok: true, result };
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
    assert.equal(restored.previewReady, true);
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
    await assert.rejects(workspace.restore(), (error: HostError) => error.code === "busy");
    assert.equal((await select).peer?.target, second.target);
    failOld();
    assert.equal((await oldFailure).reflected?.target, second.target);
    assert.equal(workspace.view().samplePort, null);
  } finally {
    workspace.stopSamples();
  }
});

test("activation checks provenance and native uniform bounds before changing the draft", async () => {
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
  assert.equal(view.project?.bindings.length, 260);
  assert.equal(view.project?.source.origin, source.origin);
  assert.equal(view.project?.source.name, "nested-tone.frag");
  assert.equal(replay({ ...view.project!, source }).commands.filter(command => command.address === "/shader/var").length, 65);

  host.validatedUniforms = Array.from({ length: 129 }, (_, index) => uniform(index, "float", [0]));
  await assert.rejects(workspace.activate(async () => source), (error: HostError) => error.code === "binding_limit");
  assert.equal(host.mutations, 1);
  assert.equal(workspace.view().previewReady, true);
  assert.equal(workspace.view().reflected?.revision, 1);
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
  workspace.edit({ id: document.id, generation: document.generation, editSequence: 1, text: "my late edits" });
  validated.resolve();
  await assert.rejects(preview, { code: "stale_document" });
  assert.equal(host.mutations, 0);
  assert.equal(workspace.view().document?.source.text, "my late edits");
});
