import { execFile } from "node:child_process";
import { createPrivateKey, randomUUID, timingSafeEqual, X509Certificate } from "node:crypto";
import { mkdir, readFile } from "node:fs/promises";
import { isIP } from "node:net";
import { join } from "node:path";
import { connect, type PeerCertificate } from "node:tls";
import { promisify } from "node:util";
import { safeStorage } from "electron";
import { hash, HostError, writeAtomically } from "./local-state";
import type { Discovery, Endpoint, PairingRequest, Peer, PeerCandidate } from "./contracts";

const PROTOCOL_VERSION = 1;
const RPC_DEADLINE = 12_000;
const MAX_FRAME = 1024 * 1024;
const PEM = /-----BEGIN (?:EC )?PRIVATE KEY-----[\s\S]+?-----END (?:EC )?PRIVATE KEY-----/;
const CERT = /-----BEGIN CERTIFICATE-----[\s\S]+?-----END CERTIFICATE-----/;

interface ClientIdentity {
  key: string;
  cert: string;
}

function checkedIdentity(identity: ClientIdentity) {
  const certificate = new X509Certificate(identity.cert);
  if (!certificate.checkPrivateKey(createPrivateKey(identity.key))) {
    throw new HostError("identity_failed", "The controller certificate does not match its private key.");
  }
  return identity;
}
interface PinnedPeer extends Peer {
  leaf: Buffer;
  rpcPort: number;
}
type DraftVersion = { epoch: string; revision: number };
export interface HostSession {
  readonly peer: Peer;
  rpc(method: string, params: Record<string, unknown>, state?: DraftVersion, id?: string): Promise<unknown>;
}
interface Candidate {
  endpoint: Endpoint;
  fingerprint: string;
  leaf: Buffer;
  expectedTarget?: string;
}
interface Envelope {
  id: string;
  method: string;
  version: 1;
  target?: string;
  epoch?: string;
  revision?: number;
  params: Record<string, unknown>;
}
type Reply =
  { id: string; ok: true; result: unknown } |
  { id: string; ok: false; error: { code: string; message: string; diagnostics?: unknown } };

function reportedError(error: Extract<Reply, { ok: false }>["error"]) {
  const messages = Array.isArray(error.diagnostics) ? error.diagnostics.slice(0, 8).flatMap(item =>
    item && typeof item.message === "string" ? [item.message] : []) : [];
  return new HostError(error.code, [error.message, ...messages].join("\n").slice(0, 4096));
}

function normalizedFingerprint(value: unknown) {
  return typeof value === "string" ? value.trim().replaceAll(":", "").toLowerCase() : "";
}

function exactLeaf(peer: PeerCertificate, leaf: Buffer) {
  return Boolean(peer.raw && peer.raw.length === leaf.length && timingSafeEqual(peer.raw, leaf));
}

function endpoint(value: unknown): Endpoint {
  const address = value as Partial<Endpoint> | null;
  const host = typeof address?.host === "string" ? address.host.trim().replace(/^\[|\]$/g, "") : "";
  const validName = /^[A-Za-z0-9](?:[A-Za-z0-9.-]{0,251}[A-Za-z0-9.])?$/.test(host);
  const scopedIP = host.includes("%") && isIP(host.slice(0, host.indexOf("%"))) === 6 &&
    /^[A-Za-z0-9._-]{1,24}$/.test(host.slice(host.indexOf("%") + 1));
  if ((!isIP(host) && !validName && !scopedIP) || typeof address?.port !== "number" ||
      !Number.isInteger(address.port) || address.port < 1 || address.port > 65535) {
    throw new HostError("invalid_address", "Enter a host name or IP address and a port from 1 to 65535.");
  }
  return { host, port: address.port };
}

function transportError(error: NodeJS.ErrnoException, requestId?: string) {
  if (error.code === "EPERM" || error.code === "EACCES") {
    return new HostError("network_permission", "macOS denied local network access.", requestId);
  }
  if (error.code?.includes("CERT") || error.code?.includes("VERIFY") || error.code?.includes("SELF_SIGNED")) {
    return new HostError("certificate_changed", "The host no longer presents the verified certificate.", requestId);
  }
  return new HostError("host_unreachable", "The render host did not complete the connection.", requestId);
}

function leafAt(address: Endpoint): Promise<Buffer> {
  // Certificate acquisition sends no application request, user data or client credential.
  return new Promise((resolve, reject) => {
    const socket = connect({ ...address, rejectUnauthorized: false, minVersion: "TLSv1.3" });
    const timer = setTimeout(() => {
      socket.destroy();
      reject(new HostError("host_unreachable", "The render host did not present a certificate."));
    }, 5000);
    socket.on("error", error => { clearTimeout(timer); reject(transportError(error)); });
    socket.once("secureConnect", () => {
      clearTimeout(timer);
      const raw = socket.getPeerCertificate().raw;
      socket.destroy();
      if (!raw || raw.length > 16_384) reject(new HostError("invalid_certificate", "The host certificate is unavailable."));
      else resolve(raw);
    });
  });
}

function pinnedRequest(address: Endpoint, leaf: Buffer, request: Envelope,
                       identity?: ClientIdentity, assertSelected?: () => void): Promise<Reply> {
  return new Promise((resolve, reject) => {
    const socket = connect({
      ...address,
      ca: new X509Certificate(leaf).toString(),
      rejectUnauthorized: true,
      minVersion: "TLSv1.3",
      // Treat the independently verified leaf as the trust anchor, even
      // when it is deliberately not a certificate authority.
      allowPartialTrustChain: true,
      checkServerIdentity: (_host, peer) => exactLeaf(peer, leaf) ? undefined :
        new HostError("certificate_changed", "The host no longer presents the verified certificate."),
      ...identity,
    });
    const chunks: Buffer[] = [];
    let received = 0;
    let written = false;
    let settled = false;
    function finish(error?: Error, result?: Reply) {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      socket.destroy();
      if (error) reject(error);
      else if (result) resolve(result);
    }
    const uncertainty = () => written ? request.id : undefined;
    const timer = setTimeout(() => finish(new HostError("host_timeout", "The request timed out.", uncertainty())),
      RPC_DEADLINE);
    socket.on("error", error => finish(transportError(error, uncertainty())));
    socket.once("close", () => finish(new HostError("host_closed", "The host closed the connection.", uncertainty())));
    socket.once("secureConnect", () => {
      if (!socket.authorized || !exactLeaf(socket.getPeerCertificate(), leaf)) {
        return finish(new HostError("certificate_changed", "The host certificate changed. Pair again."));
      }
      try { assertSelected?.(); }
      catch (error) { return finish(error as Error); }
      written = true;
      socket.write(JSON.stringify(request) + "\n");
    });
    socket.on("data", (chunk: Buffer) => {
      const newline = chunk.indexOf(10);
      const part = newline < 0 ? chunk : chunk.subarray(0, newline);
      received += part.length;
      if (received > MAX_FRAME) {
        return finish(new HostError("host_reply_limit", "Host reply exceeded the size limit.", uncertainty()));
      }
      chunks.push(part);
      if (newline < 0) return;
      try {
        const reply = JSON.parse(Buffer.concat(chunks).toString("utf8")) as Reply;
        if (reply?.id !== request.id || typeof reply.ok !== "boolean") throw new TypeError();
        if (!reply.ok && (typeof reply.error?.code !== "string" || typeof reply.error?.message !== "string")) {
          throw new TypeError();
        }
        finish(undefined, reply);
      } catch {
        finish(new HostError("invalid_host_reply", "The host returned an unreadable reply.", uncertainty()));
      }
    });
  });
}

export class HostClient {
  private identityPromise?: Promise<ClientIdentity>;
  private discovery?: Promise<Discovery>;
  private inspection = 0;
  private candidate?: Candidate;
  private verified?: PinnedPeer;
  private pending?: PairingRequest;
  private connected?: PinnedPeer;

  constructor(private readonly data: string, private readonly browseExecutable: string) {}

  get peer(): Peer | null {
    if (!this.connected) return null;
    const { target, endpoint, fingerprint } = this.connected;
    return { target, endpoint: { ...endpoint }, fingerprint };
  }

  browse(): Promise<Discovery> {
    this.discovery ??= promisify(execFile)(this.browseExecutable, { timeout: 7500, maxBuffer: 32768 })
      .then(({ stdout }) => JSON.parse(stdout) as Discovery)
      .catch((): Discovery => ({ peers: [], error: "Bonjour discovery is unavailable." }))
      .finally(() => { this.discovery = undefined; });
    return this.discovery;
  }

  async inspect(addressValue: unknown, expectedTarget?: unknown): Promise<PeerCandidate> {
    const address = endpoint(addressValue);
    const inspection = ++this.inspection;
    this.candidate = undefined;
    this.verified = undefined;
    this.pending = undefined;
    const leaf = await leafAt(address);
    if (inspection !== this.inspection) throw new HostError("host_changed", "A different render host is being inspected.");
    const fingerprint = hash(leaf);
    this.candidate = {
      endpoint: address, leaf, fingerprint,
      expectedTarget: typeof expectedTarget === "string" && expectedTarget.length <= 100 ? expectedTarget : undefined,
    };
    return { endpoint: address, fingerprint };
  }

  async beginPair(expectedFingerprint: unknown, displayName: unknown): Promise<PairingRequest> {
    const candidate = this.candidate;
    if (!candidate || normalizedFingerprint(expectedFingerprint) !== candidate.fingerprint) {
      throw new HostError("fingerprint_mismatch", "Compare the full host fingerprint with the one displayed on the render host.");
    }
    if (typeof displayName !== "string" || !displayName.trim() || Buffer.byteLength(displayName, "utf8") > 80) {
      throw new HostError("invalid_name", "Enter a display name up to 80 bytes.");
    }
    const assertCandidate = () => this.requireCandidate(candidate);
    const peer = await this.verifyCandidate(candidate);
    const identity = await this.identity();
    const certificate = new X509Certificate(identity.cert).raw;
    assertCandidate();
    const result = await this.requestBootstrap(peer, "pair.start", {
      name: displayName.trim(),
      certificate: certificate.toString("base64"),
    }, assertCandidate) as { request: string; fingerprint: string; expiresAt: number };
    assertCandidate();
    const fingerprint = hash(certificate);
    if (typeof result.request !== "string" || result.request.length > 100 ||
        normalizedFingerprint(result.fingerprint) !== fingerprint || !Number.isFinite(result.expiresAt)) {
      throw new HostError("invalid_pairing_reply", "The host did not echo the controller certificate fingerprint.");
    }
    this.verified = peer;
    this.pending = { request: result.request, fingerprint, expiresAt: result.expiresAt };
    return this.pending;
  }

  async checkPair(): Promise<{ state: string; peer?: Peer }> {
    const peer = this.verified;
    const pending = this.pending;
    if (!peer || !pending) throw new HostError("pairing_missing", "Start a pairing request first.");
    const assertPending = () => {
      if (this.verified !== peer || this.pending !== pending) {
        throw new HostError("host_changed", "The selected pairing request changed.");
      }
    };
    const result = await this.requestBootstrap(peer, "pair.status", { request: pending.request }, assertPending) as { state: string };
    assertPending();
    if (!["pending", "approved", "denied", "expired"].includes(result.state)) {
      throw new HostError("invalid_pairing_reply", "The host returned an unknown pairing status.");
    }
    if (result.state !== "approved") return { state: result.state };
    await this.connectPeer(peer, assertPending);
    this.pending = undefined;
    return { state: "approved", peer: this.peer ?? undefined };
  }

  async connectRemembered(expectedFingerprint: unknown): Promise<Peer> {
    const candidate = this.candidate;
    if (!candidate) throw new HostError("missing_certificate", "Inspect the host certificate first.");
    const known = await this.trustedHosts();
    const manualMatch = normalizedFingerprint(expectedFingerprint) === candidate.fingerprint;
    const remembered = known.find(peer => peer.fingerprint === candidate.fingerprint);
    if (!remembered && !manualMatch) {
      throw new HostError("fingerprint_mismatch", "Verify the full host fingerprint before connecting.");
    }
    const peer = await this.verifyCandidate(candidate);
    if (!remembered || remembered.target !== peer.target || remembered.fingerprint !== peer.fingerprint) {
      throw new HostError("pairing_required", "This identity needs explicit pairing.");
    }
    await this.connectPeer(peer, () => this.requireCandidate(candidate));
    return this.peer!;
  }

  disconnect() {
    this.connected = undefined;
  }

  session(): HostSession {
    const pinned = this.connected;
    const peer = this.peer;
    if (!pinned || !peer) throw new HostError("host_unavailable", "Connect to a paired render host.");
    const assertSelected = () => {
      if (this.connected !== pinned) throw new HostError("host_changed", "The selected render host changed. Retry on the intended host.");
    };
    return { peer, rpc: async (method, params, state, id) => {
      assertSelected();
      const identity = await this.identity();
      assertSelected();
      try {
        return await this.request(pinned, pinned.rpcPort, method, params, state, identity, id, assertSelected);
      } finally {
        assertSelected();
      }
    } };
  }

  async rpc(method: string, params: Record<string, unknown>, state?: DraftVersion, id?: string) {
    return this.session().rpc(method, params, state, id);
  }

  private requireCandidate(candidate: Candidate) {
    if (this.candidate !== candidate) throw new HostError("host_changed", "A different render host is being inspected.");
  }

  private async verifyCandidate(candidate: Candidate): Promise<PinnedPeer> {
    this.requireCandidate(candidate);
    const response = await pinnedRequest(candidate.endpoint, candidate.leaf, {
      id: randomUUID(), method: "hello", version: PROTOCOL_VERSION, params: {},
    }, undefined, () => this.requireCandidate(candidate));
    this.requireCandidate(candidate);
    if (!response.ok) throw reportedError(response.error);
    const data = response.result as { role: string; version: number; target: string; rpcPort: number };
    if (data?.role !== "draft" || data.version !== PROTOCOL_VERSION || typeof data.target !== "string" ||
        !/^[A-Fa-f0-9-]{36}$/.test(data.target) ||
        (candidate.expectedTarget && candidate.expectedTarget !== data.target)) {
      throw new HostError("wrong_host", "This endpoint is not the selected compatible draft host.");
    }
    const rpc = endpoint({ host: candidate.endpoint.host, port: data.rpcPort });
    return { target: data.target, endpoint: candidate.endpoint, leaf: candidate.leaf,
      fingerprint: candidate.fingerprint, rpcPort: rpc.port };
  }

  private async connectPeer(peer: PinnedPeer, assertSelected: () => void) {
    const identity = await this.identity();
    assertSelected();
    const result = await this.request(peer, peer.rpcPort, "health", {}, undefined, identity, undefined, assertSelected) as
      { role?: string; version?: number; target?: string };
    if (result?.role !== "draft" || result.version !== PROTOCOL_VERSION || result.target !== peer.target) {
      throw new HostError("wrong_host", "The authenticated endpoint is not the selected draft host.");
    }
    const known = (await this.trustedHosts()).filter(item => item.target !== peer.target);
    await mkdir(this.data, { recursive: true, mode: 0o700 });
    assertSelected();
    const entry = { target: peer.target, endpoint: peer.endpoint, fingerprint: peer.fingerprint };
    await writeAtomically(join(this.data, "paired-hosts.json"), JSON.stringify([...known.slice(-63), entry]) + "\n");
    assertSelected();
    this.connected = peer;
  }

  private requestBootstrap(peer: PinnedPeer, method: string, params: Record<string, unknown>, assertSelected: () => void) {
    return this.request(peer, peer.endpoint.port, method, params, undefined, undefined, undefined, assertSelected);
  }

  private async request(peer: PinnedPeer, port: number, method: string, params: Record<string, unknown>,
                        state?: DraftVersion, identity?: ClientIdentity,
                        id: string = randomUUID(), assertSelected?: () => void) {
    const response = await pinnedRequest({ host: peer.endpoint.host, port }, peer.leaf, {
      id, method, params, version: PROTOCOL_VERSION, target: peer.target,
      epoch: state?.epoch, revision: state?.revision,
    }, identity, assertSelected);
    if (!response.ok) throw reportedError(response.error);
    return response.result;
  }

  private async trustedHosts(): Promise<Peer[]> {
    try {
      const stored = JSON.parse(await readFile(join(this.data, "paired-hosts.json"), "utf8"));
      return Array.isArray(stored) ? stored.slice(0, 64) as Peer[] : [];
    } catch (error) {
      if ((error as NodeJS.ErrnoException).code === "ENOENT") return [];
      throw new HostError("trust_store", "The paired host settings could not be read.");
    }
  }

  private identity() {
    this.identityPromise ??= this.loadIdentity().catch(error => {
      this.identityPromise = undefined;
      throw error;
    });
    return this.identityPromise;
  }

  private async loadIdentity(): Promise<ClientIdentity> {
    if (!safeStorage.isEncryptionAvailable()) {
      throw new HostError("keychain_unavailable", "Unlock the macOS Keychain to access the controller identity.");
    }
    const path = join(this.data, "controller-identity");
    try {
      return checkedIdentity(JSON.parse(safeStorage.decryptString(await readFile(path))) as ClientIdentity);
    } catch (error) {
      if ((error as NodeJS.ErrnoException).code !== "ENOENT") {
        throw new HostError("keychain_unavailable", "The controller identity could not be read from protected storage.");
      }
    }
    const { stdout } = await promisify(execFile)("/usr/bin/openssl", [
      "req", "-x509", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:P-256",
      "-pkeyopt", "ec_param_enc:named_curve", "-nodes",
      "-days", "365", "-subj", "/CN=VOSC Authoring Controller", "-keyout", "/dev/stdout", "-out", "/dev/stdout",
    ], { timeout: 5000, maxBuffer: 16384, env: { PATH: "/usr/bin:/bin" } });
    const key = stdout.match(PEM)?.[0];
    const cert = stdout.match(CERT)?.[0];
    if (!key || !cert) throw new HostError("identity_failed", "Could not create the controller identity.");
    const identity = checkedIdentity({ key, cert });
    await mkdir(this.data, { recursive: true, mode: 0o700 });
    await writeAtomically(path, safeStorage.encryptString(JSON.stringify(identity)));
    return identity;
  }
}
