import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { X509Certificate } from "node:crypto";
import { once } from "node:events";
import { copyFileSync, mkdirSync, mkdtempSync, readFileSync, rmSync, statSync, writeFileSync } from "node:fs";
import { createConnection, createServer, type AddressInfo } from "node:net";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { test } from "node:test";
import { setTimeout as delay } from "node:timers/promises";
import { connect, createServer as createTlsServer } from "node:tls";
import { startTransport as startService, type Transport, type TransportOptions } from "../transport";

const startTransport = (options: TransportOptions) => startService({ ...options, pythonPath: process.env.VOSC_TEST_PYTHON });
const linuxOnly = process.platform !== "linux" ? "Linux TLS adapter qualification" : false;

function clientIdentity(directory: string, label: string, subject = "VOSC Authoring Controller") {
  const key = join(directory, `${label}.key`);
  const cert = join(directory, `${label}.cert`);
  execFileSync("/usr/bin/openssl", ["req", "-x509", "-newkey", "ec",
    "-pkeyopt", "ec_paramgen_curve:P-256", "-nodes", "-days", "365",
    "-subj", `/CN=${subject}`, "-keyout", key, "-out", cert], { stdio: "ignore" });
  const credentials = { key: readFileSync(key), cert: readFileSync(cert) };
  const leaf = new X509Certificate(credentials.cert);
  return { credentials, certificate: leaf.raw.toString("base64"), expiresAt: leaf.validToDate.getTime() };
}

function clientIdentityWithoutExtensions(directory: string, label: string) {
  const key = join(directory, `${label}.key`);
  const cert = join(directory, `${label}.cert`);
  const csr = join(directory, `${label}.csr`);
  // A CSR signed without extension copying deliberately omits SKI and AKI.
  execFileSync("/usr/bin/openssl", ["req", "-new", "-newkey", "ec", "-pkeyopt",
    "ec_paramgen_curve:P-256", "-pkeyopt", "ec_param_enc:named_curve", "-nodes", "-subj",
    "/CN=VOSC Authoring Controller", "-keyout", key, "-out", csr], { stdio: "ignore" });
  execFileSync("/usr/bin/openssl", ["x509", "-req", "-in", csr, "-signkey", key, "-sha256",
    "-days", "365", "-out", cert], { stdio: "ignore" });
  const details = execFileSync("/usr/bin/openssl", ["x509", "-in", cert, "-noout", "-text"], { encoding: "utf8" });
  assert.match(details, /Version: 1 \(0x0\)/);
  assert.doesNotMatch(details, /X509v3 extensions|(?:Subject|Authority) Key Identifier/);
  const credentials = { key: readFileSync(key), cert: readFileSync(cert) };
  return { credentials, certificate: new X509Certificate(credentials.cert).raw.toString("base64") };
}

function envelope(method: string, fields: Record<string, unknown> = {}) {
  return JSON.stringify({ id: "test", version: 1, method, params: {}, ...fields }) + "\n";
}

function connection(port: number, credentials = {}, session?: Buffer) {
  const socket = connect({ host: "127.0.0.1", port, rejectUnauthorized: false,
    minVersion: "TLSv1.3", ...credentials, session });
  // The actual Editor pins the host leaf. These tests exercise server-side authentication independently.
  socket.on("error", () => {});
  socket.setTimeout(4000, () => socket.destroy());
  return socket;
}

function replies(socket: ReturnType<typeof connection>, frames: string[], count: number, keep = false) {
  return new Promise<Record<string, any>[]>((resolve, reject) => {
    const results: Record<string, any>[] = [];
    let pending = "";
    const finish = () => {
      socket.off("data", receive);
      socket.off("close", finish);
      if (!keep) socket.destroy();
      resolve(results);
    };
    function receive(chunk: Buffer) {
      pending += chunk.toString("utf8");
      let newline = pending.indexOf("\n");
      while (newline >= 0) {
        try {
          results.push(JSON.parse(pending.slice(0, newline)));
        } catch (error) {
          socket.destroy();
          reject(error);
          return;
        }
        if (results.length === count) {
          finish();
          return;
        }
        pending = pending.slice(newline + 1);
        newline = pending.indexOf("\n");
      }
    }
    socket.on("data", receive);
    socket.once("close", finish);
    for (const frame of frames) socket.write(frame);
  });
}

async function resumableSession(stateDirectory: string, credentials: { cert: Buffer; key: Buffer }) {
  const identity = JSON.parse(readFileSync(join(stateDirectory, "identity.json"), "utf8"));
  const issuer = createTlsServer({ cert: identity.cert, key: identity.key, ca: credentials.cert,
    requestCert: true, rejectUnauthorized: true, minVersion: "TLSv1.3", maxVersion: "TLSv1.3",
  });
  issuer.listen(0, "127.0.0.1");
  await once(issuer, "listening");
  const port = (issuer.address() as AddressInfo).port;
  const initial = connection(port, credentials);
  let resumed: ReturnType<typeof connection> | undefined;
  try {
    const [session] = await Promise.race([
      once(initial, "session"),
      once(initial, "close").then(() => { throw new Error("Control issuer closed before supplying a TLS ticket"); }),
    ]);
    assert.equal(initial.getProtocol(), "TLSv1.3");
    assert.ok(Buffer.isBuffer(session) && session.length > 0);
    resumed = connection(port, credentials, session);
    await Promise.race([
      once(resumed, "secureConnect"),
      once(resumed, "close").then(() => { throw new Error("Control issuer rejected its TLS ticket"); }),
    ]);
    assert.equal(resumed.getProtocol(), "TLSv1.3");
    assert.equal(resumed.isSessionReused(), true, "the control issuer must accept the supplied ticket");
    return session;
  } finally {
    initial.destroy();
    resumed?.destroy();
    await new Promise<void>(resolve => issuer.close(() => resolve()));
  }
}

test("v1 framing, exact-leaf mutual TLS, pairing decisions, persistence and revocation", { skip: linuxOnly }, async () => {
  const root = mkdtempSync(join(tmpdir(), "vosc-host-test-"));
  const stateDirectory = join(root, "profile");
  let host: Transport | undefined;
  const revoked: string[] = [];
  let admissions = 0;
  const start = async () => startTransport({ bind: "127.0.0.1", bootstrapPort: 0, rpcPort: 0, stateDirectory,
    dispatch(request, peer) {
      admissions++;
      return request.method === "large" ? "a".repeat(256 * 1024) : { peer };
    },
    onRevoke: peer => { revoked.push(peer); },
  });
  try {
    const approved = clientIdentity(root, "approved");
    const other = clientIdentity(root, "other");
    host = await start();
    const { target, fingerprint, rpcPort, bootstrapPort } = host;
    const bootstrap = async (method: string, params = {}) =>
      (await replies(connection(bootstrapPort), [envelope(method, { target, params })], 1))[0];
    const submit = (certificate: string) => bootstrap("pair.start", { name: "Editor", certificate });
    const rpc = (credentials = {}, session?: Buffer) => connection(rpcPort, credentials, session);
    const health = envelope("health", { target });

    const hello = envelope("hello");
    const result = await replies(connection(bootstrapPort),
      [hello.slice(0, 17), hello.slice(17) + envelope("hello", { version: 2 }) +
        envelope("hello", { revision: true }) + envelope("hello", { target: "wrong" })], 4);
    assert.deepEqual(result[0].result, { role: "draft", version: 1, target, rpcPort });
    assert.deepEqual(result.slice(1).map(frame => frame.error.code),
      ["incompatible_protocol", "invalid_request", "wrong_target"]);
    assert.equal((await bootstrap("health")).error.code, "unpaired");
    assert.deepEqual(await replies(connection(bootstrapPort), ["a".repeat(256 * 1024 + 1) + "\n"], 1), []);

    assert.deepEqual(await replies(rpc(), [health], 1), []);
    assert.deepEqual(await replies(rpc(approved.credentials), [health], 1), []);
    assert.equal(admissions, 0);
    const first = (await submit(approved.certificate)).result;
    const second = (await submit(other.certificate)).result;
    assert.notEqual(first.fingerprint, second.fingerprint);
    assert.equal((await bootstrap("pair.status", { request: first.request })).result.state, "pending");
    await assert.rejects(() => host!.decide(first.request, second.fingerprint, true), { code: "pairing_mismatch" });
    await host.decide(second.request, second.fingerprint, false);
    assert.equal((await bootstrap("pair.status", { request: second.request })).result.state, "denied");
    assert.equal((await bootstrap("pair.status", { request: second.request })).result.state, "expired");
    assert.equal(host.listPairing().length, 1);
    await host.decide(first.request, first.fingerprint, true);
    assert.equal((await bootstrap("pair.status", { request: first.request })).result.state, "approved");
    assert.equal(statSync(stateDirectory).mode & 0o777, 0o700);
    for (const name of ["identity.json", "peers.json"]) {
      assert.equal(statSync(join(stateDirectory, name)).mode & 0o777, 0o600);
    }

    const session = await resumableSession(stateDirectory, approved.credentials);
    const admitted = rpc(approved.credentials);
    const adapterTickets: Buffer[] = [];
    admitted.on("session", ticket => adapterTickets.push(ticket));
    const authorized = await replies(admitted, [health + envelope("large", { target })], 2, true);
    assert.equal(authorized[0].result.peer, first.fingerprint);
    assert.equal(authorized[1].error.code, "response_limit");
    assert.equal(admitted.isSessionReused(), false);
    assert.deepEqual(await replies(rpc(other.credentials), [health], 1), []);
    await delay(250);
    assert.deepEqual(adapterTickets, [], "the adapter must issue no TLS 1.3 session tickets");
    const attempted = rpc(approved.credentials, session);
    const response = await replies(attempted, [health], 1, true);
    assert.equal(response[0].result.peer, first.fingerprint);
    assert.equal(attempted.isSessionReused(), false);
    attempted.destroy();

    // Move wall time beyond the leaf's validity with the original TLS session still open.
    const clock = Date.now;
    try {
      Date.now = () => approved.expiresAt + 1000;
      assert.deepEqual(await replies(admitted, [health], 1), []);
    } finally {
      Date.now = clock;
    }

    const pending: { request: string; fingerprint: string; expiresAt: number }[] = [];
    for (let index = 0; index < 8; index++) {
      pending.push((await submit(other.certificate)).result);
    }
    assert.equal((await submit(other.certificate)).error.code, "pair_capacity");
    const now = Date.now;
    try {
      Date.now = () => pending[0].expiresAt;
      assert.equal((await bootstrap("pair.status", { request: pending[0].request })).result.state, "expired");
      await assert.rejects(() => host!.decide(pending[0].request, pending[0].fingerprint, true), { code: "pairing_mismatch" });
      Date.now = () => pending[7].expiresAt + 60_000;
      assert.equal(host.listPairing().length, 0);
      assert.ok((await submit(other.certificate)).result.request);
    } finally {
      Date.now = now;
    }

    await host.close();
    host = await start();
    assert.equal(host.target, target);
    assert.equal(host.fingerprint, fingerprint);
    const afterRestart = connection(host.rpcPort, approved.credentials, session);
    const after = await replies(afterRestart, [health], 1, true);
    assert.equal(after[0].result.peer, first.fingerprint);
    assert.equal(afterRestart.isSessionReused(), false);
    const disconnected = once(afterRestart, "close");
    await host.revoke(first.fingerprint);
    await disconnected;
    assert.deepEqual(revoked, [first.fingerprint]);
    const afterRevocation = connection(host.rpcPort, approved.credentials, session);
    const reused: boolean[] = [];
    afterRevocation.on("secureConnect", () => reused.push(afterRevocation.isSessionReused()));
    const before = admissions;
    assert.deepEqual(await replies(afterRevocation, [health], 1), []);
    assert.ok(reused.every(value => !value), "the offered ticket must not restore a revoked session");
    assert.equal(admissions, before);
  } finally {
    await host?.close();
    rmSync(root, { force: true, recursive: true });
  }
});

test("bootstrap and RPC share a 16-session budget", { skip: linuxOnly }, async () => {
  const stateDirectory = mkdtempSync(join(tmpdir(), "vosc-host-sessions-"));
  const sockets: ReturnType<typeof connection>[] = [];
  let host: Transport | undefined;
  try {
    host = await startTransport({ bind: "127.0.0.1", bootstrapPort: 0, rpcPort: 0, stateDirectory });
    const hello = envelope("hello");
    const client = clientIdentity(stateDirectory, "capacity-client");
    const { credentials, certificate } = client;
    const reply = await replies(connection(host.bootstrapPort),
      [envelope("pair.start", { target: host.target, params: { name: "Editor", certificate } })], 1);
    const pending = reply[0].result;
    await host.decide(pending.request, pending.fingerprint, true);
    const health = envelope("health", { target: host.target });
    assert.equal((await replies(connection(host.rpcPort, credentials), [health], 1))[0].ok, true);
    for (let index = 0; index < 16; index++) {
      const socket = connection(host.bootstrapPort);
      sockets.push(socket);
      assert.equal((await replies(socket, [hello], 1, true))[0].ok, true);
    }
    assert.deepEqual(await replies(connection(host.rpcPort, credentials), [health], 1), []);
    const closed = once(sockets[0], "close");
    sockets.shift()?.destroy();
    await closed;
    // TLS close is asynchronous on the server. Send a fresh probe after the old socket releases its slot.
    let restored;
    for (let attempt = 0; attempt < 8 && !restored; attempt++) {
      [restored] = await replies(connection(host.rpcPort, credentials), [health], 1);
    }
    assert.equal(restored?.ok, true);
  } finally {
    for (const socket of sockets) socket.destroy();
    await host?.close();
    rmSync(stateDirectory, { force: true, recursive: true });
  }
});

test("inactive pre-TLS connections close within the handshake deadline", { timeout: 18_000, skip: linuxOnly }, async () => {
  const stateDirectory = mkdtempSync(join(tmpdir(), "vosc-host-handshake-"));
  let host: Transport | undefined;
  const stalled: ReturnType<typeof createConnection>[] = [];
  try {
    host = await startTransport({ bind: "127.0.0.1", bootstrapPort: 0, rpcPort: 0, stateDirectory });
    const started = performance.now();
    await Promise.all([host.bootstrapPort, host.rpcPort].map(port => {
      const socket = createConnection({ host: "127.0.0.1", port });
      stalled.push(socket);
      return once(socket, "close");
    }));
    const elapsed = performance.now() - started;
    assert.ok(elapsed >= 14_500 && elapsed < 17_000, `Pre-TLS socket closure took ${elapsed} ms`);
    assert.equal((await replies(connection(host.bootstrapPort), [envelope("hello")], 1))[0].ok, true);
  } finally {
    for (const socket of stalled) socket.destroy();
    await host?.close();
    rmSync(stateDirectory, { force: true, recursive: true });
  }
});

test("network pairing is local-decision only, bounded to 32 approvals, and bound to the host leaf", { timeout: 10_000, skip: linuxOnly }, async () => {
  const root = mkdtempSync(join(tmpdir(), "vosc-host-trust-"));
  const original = join(root, "original");
  const copied = join(root, "copied");
  const replacement = join(root, "replacement");
  const start = (stateDirectory: string) =>
    startTransport({ bind: "127.0.0.1", bootstrapPort: 0, rpcPort: 0, stateDirectory });
  let host: Transport | undefined;
  let newHost: Transport | undefined;
  const bootstrap = async (service: Transport, method: string, params: Record<string, unknown>) =>
    (await replies(connection(service.bootstrapPort), [envelope(method, { target: service.target, params })], 1))[0];
  const rpc = (service: Transport, credentials: object, method = "health", params = {}) =>
    replies(connection(service.rpcPort, credentials), [envelope(method, { target: service.target, params })], 1);
  try {
    host = await start(original);
    const first = clientIdentity(root, "first");
    const firstPair = (await bootstrap(host, "pair.start", { name: "Editor", certificate: first.certificate })).result;
    const decision = { request: firstPair.request, fingerprint: firstPair.fingerprint };
    for (const method of ["pair.approve", "pair.deny", "peer.revoke"]) {
      assert.equal((await bootstrap(host, method, decision)).error.code, "unpaired");
    }
    assert.equal((await bootstrap(host, "pair.status", { request: decision.request })).result.state, "pending");
    await host.decide(decision.request, decision.fingerprint, true);
    assert.equal((await bootstrap(host, "pair.status", { request: decision.request })).result.state, "approved");
    for (const method of ["pair.approve", "pair.deny", "peer.revoke"]) {
      assert.equal((await rpc(host, first.credentials, method, decision))[0].error.code, "unsupported_method");
    }

    for (let index = 1; index < 32; index++) {
      const { certificate } = clientIdentity(root, `peer-${index}`, `Capacity Controller ${index}`);
      const request: { request: string; fingerprint: string } =
        (await bootstrap(host, "pair.start", { name: "Editor", certificate })).result;
      await host.decide(request.request, request.fingerprint, true);
      assert.equal((await bootstrap(host, "pair.status", { request: request.request })).result.state, "approved");
    }
    const excess = clientIdentity(root, "excess");
    const overflow = (await bootstrap(host, "pair.start", { name: "Editor", certificate: excess.certificate })).result;
    await assert.rejects(() => host!.decide(overflow.request, overflow.fingerprint, true), { code: "pair_capacity" });
    assert.equal((await bootstrap(host, "pair.status", { request: overflow.request })).result.state, "pending");
    assert.deepEqual(await rpc(host, excess.credentials), []);
    assert.equal((await rpc(host, first.credentials))[0].ok, true);

    // Preserve the original live identity and trust. Reset only a disposable copy.
    mkdirSync(copied, { mode: 0o700 });
    for (const name of ["identity.json", "peers.json"]) {
      copyFileSync(join(original, name), join(copied, name));
    }
    newHost = await start(replacement);
    await newHost.close();
    newHost = undefined;
    copyFileSync(join(replacement, "identity.json"), join(copied, "identity.json"));
    await assert.rejects(start(copied), /certificate changed/);
    rmSync(join(copied, "peers.json"));
    newHost = await start(copied);
    assert.notEqual(newHost.target, host.target);
    assert.notEqual(newHost.fingerprint, host.fingerprint);
    assert.deepEqual(await rpc(newHost, first.credentials), []);
    assert.equal((await rpc(host, first.credentials))[0].ok, true);
    const fresh = (await bootstrap(newHost, "pair.start", { name: "Editor", certificate: first.certificate })).result;
    assert.notEqual(fresh.request, firstPair.request);
    await newHost.decide(fresh.request, fresh.fingerprint, true);
    assert.equal((await rpc(newHost, first.credentials))[0].result.target, newHost.target);
  } finally {
    await newHost?.close();
    await host?.close();
    rmSync(root, { force: true, recursive: true });
  }
});

test("authenticated TLS adapter retains all 32 same-subject Editor leaves after trust reload", {
  timeout: 15_000,
  skip: linuxOnly,
}, async () => {
  const root = mkdtempSync(join(tmpdir(), "vosc-host-same-subject-"));
  const stateDirectory = join(root, "profile");
  const start = () => startTransport({ bind: "127.0.0.1", bootstrapPort: 0, rpcPort: 0, stateDirectory });
  const editor = (index: number) => clientIdentityWithoutExtensions(root, `peer-${index}`);
  const bootstrap = async (host: Transport, method: string, params: Record<string, unknown>) =>
    (await replies(connection(host.bootstrapPort), [envelope(method, { target: host.target, params })], 1))[0];
  const health = (host: Transport, credentials: object) =>
    replies(connection(host.rpcPort, credentials), [envelope("health", { target: host.target })], 1);
  let host: Transport | undefined;
  try {
    host = await start();
    const { target, fingerprint } = host;
    const peers = Array.from({ length: 32 }, (_, index) => editor(index));
    const unknown = editor(33);
    for (const [index, peer] of peers.entries()) {
      const pair: { request: string; fingerprint: string } =
        (await bootstrap(host, "pair.start", { name: "Editor", certificate: peer.certificate })).result;
      await host.decide(pair.request, pair.fingerprint, true);
      assert.equal((await bootstrap(host, "pair.status", { request: pair.request })).result.state,
        "approved", `approval for same-subject peer ${index + 1}`);
    }

    for (const phase of ["live trust update", "after profile restart"]) {
      for (const [index, peer] of peers.entries()) {
        const [response] = await health(host, peer.credentials);
        assert.equal(response?.result?.target, target, `${phase}, same-subject peer ${index + 1}`);
      }
      assert.deepEqual(await health(host, unknown.credentials), []);
      if (phase === "live trust update") {
        await host.close();
        host = await start();
        assert.equal(host.fingerprint, fingerprint);
      }
    }
  } finally {
    await host?.close();
    rmSync(root, { force: true, recursive: true });
  }
});

test("an unapproved leaf that chains to an approved CA never reaches RPC dispatch", { skip: linuxOnly }, async () => {
  const root = mkdtempSync(join(tmpdir(), "vosc-host-leaf-pin-"));
  const stateDirectory = join(root, "profile");
  let host: Transport | undefined;
  let requests = 0;
  try {
    const caKey = join(root, "ca.key");
    const caCert = join(root, "ca.cert");
    execFileSync("/usr/bin/openssl", ["req", "-x509", "-sha256", "-newkey", "ec", "-pkeyopt",
      "ec_paramgen_curve:P-256", "-nodes", "-days", "365", "-subj", "/CN=Wrapper CA",
      "-addext", "basicConstraints=critical,CA:TRUE", "-addext", "keyUsage=critical,keyCertSign,digitalSignature",
      "-keyout", caKey, "-out", caCert], { stdio: "ignore" });
    const leafKey = join(root, "leaf.key");
    const leafCert = join(root, "leaf.cert");
    const leafCsr = join(root, "leaf.csr");
    execFileSync("/usr/bin/openssl", ["req", "-new", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:P-256",
      "-nodes", "-subj", "/CN=Unapproved Issued Leaf", "-keyout", leafKey, "-out", leafCsr], { stdio: "ignore" });
    execFileSync("/usr/bin/openssl", ["x509", "-req", "-sha256", "-in", leafCsr, "-CA", caCert,
      "-CAkey", caKey, "-set_serial", "1", "-days", "365", "-out", leafCert], { stdio: "ignore" });
    execFileSync("/usr/bin/openssl", ["verify", "-purpose", "sslclient", "-CAfile", caCert, leafCert], { stdio: "ignore" });

    host = await startTransport({ bind: "127.0.0.1", bootstrapPort: 0, rpcPort: 0, stateDirectory,
      dispatch() {
        requests++;
        return {};
      },
    });
    const cert = readFileSync(caCert);
    const certificate = new X509Certificate(cert).raw.toString("base64");
    const pair = (await replies(connection(host.bootstrapPort),
      [envelope("pair.start", { target: host.target, params: { name: "Approval CA", certificate } })], 1))[0].result;
    await host.decide(pair.request, pair.fingerprint, true);
    const nativeRequest = envelope("health", { target: host.target });
    assert.equal((await replies(connection(host.rpcPort, { cert, key: readFileSync(caKey) }), [nativeRequest], 1))[0].ok, true);
    const leaf = { cert: readFileSync(leafCert), key: readFileSync(leafKey) };
    assert.deepEqual(await replies(connection(host.rpcPort, leaf), [nativeRequest], 1), []);
    assert.equal(requests, 1);
  } finally {
    await host?.close();
    rmSync(root, { force: true, recursive: true });
  }
});

test("losing the TLS adapter closes live RPC and bootstrap", { timeout: 8_000, skip: linuxOnly }, async () => {
  const stateDirectory = mkdtempSync(join(tmpdir(), "vosc-host-adapter-loss-"));
  let host: Transport | undefined;
  try {
    let reportLoss: (error: Error) => void;
    const lost = new Promise<Error>(resolve => { reportLoss = resolve; });
    host = await startTransport({ bind: "127.0.0.1", bootstrapPort: 0, rpcPort: 0, stateDirectory,
      onLost: error => reportLoss(error),
    });
    const client = clientIdentity(stateDirectory, "adapter-loss");
    const pair = (await replies(connection(host.bootstrapPort), [
      envelope("pair.start", { target: host.target, params: { name: "Editor", certificate: client.certificate } }),
    ], 1))[0].result;
    await host.decide(pair.request, pair.fingerprint, true);
    const rpc = connection(host.rpcPort, client.credentials);
    const health = envelope("health", { target: host.target });
    assert.equal((await replies(rpc, [health], 1, true))[0].ok, true);
    const closed = once(rpc, "close");
    const processes = execFileSync("/bin/ps", ["-ax", "-o", "pid=,ppid=,args="], { encoding: "utf8" });
    const adapters = processes.trim().split("\n")
      .filter(row => row.includes(join(stateDirectory, "rpc.sock")))
      .map(row => row.trim().split(/\s+/, 2))
      .filter(([, parent]) => Number(parent) === process.pid);
    assert.equal(adapters.length, 1, "own test TLS adapter PID");
    process.kill(Number(adapters[0][0]), "SIGKILL");
    assert.match((await lost).message, /exited unexpectedly/);
    await closed;
    await host.close();
    assert.deepEqual(await replies(connection(host.rpcPort, client.credentials), [health], 1), []);
    assert.deepEqual(await replies(connection(host.bootstrapPort), [envelope("hello")], 1), []);
  } finally {
    await host?.close();
    rmSync(stateDirectory, { force: true, recursive: true });
  }
});

test("an adapter that exits immediately after readiness leaves no bootstrap listener", { timeout: 8_000 }, async () => {
  const stateDirectory = mkdtempSync(join(tmpdir(), "vosc-host-startup-loss-"));
  const adapter = join(stateDirectory, "exit-after-ack");
  let host: Transport | undefined;
  try {
    const reserved = createServer().listen(0, "127.0.0.1");
    await once(reserved, "listening");
    const bootstrapPort = (reserved.address() as AddressInfo).port;
    await new Promise<void>(resolve => reserved.close(() => resolve()));
    writeFileSync(adapter, "#!/bin/sh\nread -r line\nprintf '{\"id\":1,\"port\":20548}\\n'\nexit 1\n",
      { mode: 0o700 });
    let reportLoss: (error: Error) => void;
    const lost = new Promise<Error>(resolve => { reportLoss = resolve; });
    try {
      host = await startService({
        bind: "127.0.0.1", bootstrapPort, rpcPort: 0, stateDirectory,
        pythonPath: adapter, onLost: error => reportLoss(error),
      });
    } catch (error) {
      assert.match((error as Error).message, /adapter stopped|adapter exited unexpectedly/);
    }
    if (host) {
      assert.match((await lost).message, /adapter exited unexpectedly/);
      await host.close();
    }
    assert.deepEqual(await replies(connection(bootstrapPort), [envelope("hello")], 1), []);
  } finally {
    await host?.close();
    rmSync(stateDirectory, { force: true, recursive: true });
  }
});

test("adapter startup delivers a fixed phase diagnostic", { timeout: 8_000 }, async () => {
  const stateDirectory = mkdtempSync(join(tmpdir(), "vosc-host-adapter-startup-"));
  const adapter = join(stateDirectory, "known-startup-failure");
  try {
    writeFileSync(adapter, "#!/bin/sh\nread -r line\nexit 14\n", { mode: 0o700 });
    await assert.rejects(startService({
      bind: "127.0.0.1", bootstrapPort: 0, rpcPort: 0, stateDirectory, pythonPath: adapter,
    }), /Authenticated RPC bind address is unavailable/);
  } finally {
    rmSync(stateDirectory, { force: true, recursive: true });
  }
});

test("Linux TLS trust loads the key from an owner-only anonymous descriptor", {
  skip: linuxOnly,
}, () => {
  const directory = mkdtempSync(join(tmpdir(), "vosc-host-memfd-"));
  try {
    const { credentials } = clientIdentity(directory, "identity");
    const identity = join(directory, "identity.json");
    writeFileSync(identity, JSON.stringify({
      cert: credentials.cert.toString("ascii"), key: credentials.key.toString("ascii"),
    }), { mode: 0o600 });
    execFileSync(process.env.VOSC_TEST_PYTHON ?? "/usr/bin/python3", ["-I", "-c", `
import os, runpy, ssl, stat, sys
helper = runpy.run_path(sys.argv[1])
load_cert_chain = ssl.SSLContext.load_cert_chain
loaded = []
def check_anonymous_key(context, path):
    assert path.startswith("/proc/self/fd/")
    assert os.readlink(path).startswith("/memfd:vosc-rpc-identity")
    descriptor = int(path.rsplit("/", 1)[1])
    assert not os.get_inheritable(descriptor)
    assert stat.S_IMODE(os.fstat(descriptor).st_mode) == 0o600
    loaded.append(path)
    return load_cert_chain(context, path)
ssl.SSLContext.load_cert_chain = check_anonymous_key
helper["trust_context"](helper["read_identity"](sys.argv[2]), [])
assert len(loaded) == 1
assert not os.path.exists(loaded[0])
`, join(__dirname, "rpc_tls.py"), identity], { stdio: "pipe", timeout: 5000 });
  } finally {
    rmSync(directory, { force: true, recursive: true });
  }
});
