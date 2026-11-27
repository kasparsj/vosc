import { constants, randomUUID, X509Certificate } from "node:crypto";
import { once } from "node:events";
import type { AddressInfo, Socket } from "node:net";
import { createServer, type Server, type TLSSocket, type TlsOptions } from "node:tls";
import { startRpcBridge } from "./rpc-bridge";
import { TextDecoder } from "node:util";
import { fingerprint, identityPath, loadApproved, loadIdentity, saveApproved, type ApprovedPeer } from "./identity";

const VERSION = 1;
const FRAME_LIMIT = 256 * 1024;
const SESSION_TIMEOUT = 15_000;
const PAIR_TIMEOUT = 90_000;
const utf8 = new TextDecoder("utf-8", { fatal: true });
const envelopeKeys = new Set(["id", "method", "version", "params", "target", "epoch", "revision"]);

export class WireError extends Error {
  constructor(readonly code: string, message: string, readonly diagnostics?: unknown) {
    super(message);
  }
}

export interface WireRequest {
  id: string;
  method: string;
  version: number;
  params: Record<string, unknown>;
  target?: string;
  epoch?: string;
  revision?: number;
}

export interface TransportOptions {
  bind: string;
  bootstrapPort: number;
  rpcPort: number;
  stateDirectory: string;
  dispatch?: (request: WireRequest, peer: string) => unknown;
  onRevoke?: (fingerprint: string) => void | Promise<void>;
  onLost?: (error: Error) => void;
  pythonPath?: string;
}

interface Pairing {
  request: string;
  name: string;
  fingerprint: string;
  expiresAt: number;
}

type Pending = Pairing & Pick<ApprovedPeer, "certificate"> & {
  state: "pending" | "approved" | "denied";
};

export interface Transport {
  target: string;
  fingerprint: string;
  bootstrapPort: number;
  rpcPort: number;
  listPairing(): Pairing[];
  decide(request: string, fingerprint: string, approve: boolean): Promise<void>;
  revoke(fingerprint: string): Promise<void>;
  close(): Promise<void>;
}

function object(value: unknown): value is Record<string, unknown> {
  return !!value && typeof value === "object" && !Array.isArray(value);
}

function parseRequest(value: Record<string, unknown>): WireRequest {
  const { id, method, version, params, target, epoch, revision } = value;
  if (typeof id !== "string" || !/^[a-zA-Z0-9_-]{1,64}$/.test(id) ||
      typeof method !== "string" || Buffer.byteLength(method) > 32 || !object(params) ||
      Object.keys(value).some(key => !envelopeKeys.has(key)) ||
      (target !== undefined && typeof target !== "string") ||
      (epoch !== undefined && typeof epoch !== "string") ||
      (revision !== undefined && !Number.isSafeInteger(revision))) {
    throw new WireError("invalid_request", "Malformed draft request");
  }
  if (version !== VERSION) {
    throw new WireError("incompatible_protocol", `This host requires protocol version ${VERSION}`);
  }
  return { id, method, version, params, target, epoch, revision: revision as number | undefined };
}

export function requireFields(request: WireRequest, keys: string[]) {
  const actual = Object.keys(request.params);
  if (actual.length !== keys.length || actual.some(key => !keys.includes(key))) {
    throw new WireError("invalid_request", `Unexpected or missing fields for ${request.method}`);
  }
}

function failure(id: string, error: unknown) {
  const detail = error instanceof WireError ? error :
    new WireError("host_error", "Could not complete the private draft operation");
  return { id, ok: false, error: { code: detail.code, message: detail.message, diagnostics: detail.diagnostics } };
}

async function serve(socket: Socket, dispatch: (request: WireRequest) => unknown) {
  let deadline: NodeJS.Timeout | undefined;
  const armDeadline = () => {
    clearTimeout(deadline);
    deadline = setTimeout(() => socket.destroy(), SESSION_TIMEOUT);
  };
  armDeadline();
  let buffer = Buffer.alloc(0);
  try {
    for await (const chunk of socket as AsyncIterable<Buffer>) {
      buffer = Buffer.concat([buffer, chunk]);
      let newline = buffer.indexOf(0x0a);
      while (newline !== -1) {
        if (newline > FRAME_LIMIT || socket.destroyed) return;
        const value: unknown = JSON.parse(utf8.decode(buffer.subarray(0, newline)));
        buffer = buffer.subarray(newline + 1);
        if (!object(value) || typeof value.id !== "string" || Buffer.byteLength(value.id) > 64) return;
        const id = value.id;
        let response: string;
        try {
          const request = parseRequest(value);
          armDeadline();
          response = JSON.stringify({ id, ok: true, result: await dispatch(request) });
        } catch (error) {
          response = JSON.stringify(failure(id, error));
        }
        if (socket.destroyed) return;
        if (Buffer.byteLength(response) > FRAME_LIMIT) {
          response = JSON.stringify(failure(id, new WireError("response_limit", "Host response exceeded its limit")));
        }
        await new Promise<void>((resolve, reject) => {
          socket.write(response + "\n", error => error ? reject(error) : resolve());
        });
        armDeadline();
        newline = buffer.indexOf(0x0a);
      }
      if (buffer.length > FRAME_LIMIT) return;
    }
  } catch {
    // Invalid framing and socket failures end this connection alone.
  } finally {
    clearTimeout(deadline);
    socket.destroy();
  }
}

function connectionKey(socket: Socket): string {
  // These endpoints identify the same live TCP flow on the raw and TLS sockets.
  return JSON.stringify([socket.localAddress, socket.localPort, socket.remoteAddress, socket.remotePort]);
}

function pairingCertificate(encoded: unknown): X509Certificate {
  if (typeof encoded !== "string" || encoded.length > 6000) {
    throw new WireError("invalid_pairing", "Expected a controller name and public TLS certificate");
  }
  const bytes = Buffer.from(encoded, "base64");
  try {
    if (bytes.length < 1 || bytes.length > 4096 || bytes.toString("base64") !== encoded) throw new Error();
    const certificate = new X509Certificate(bytes);
    if (!certificate.raw.equals(bytes)) throw new Error();
    return certificate;
  } catch {
    throw new WireError("invalid_pairing", "Expected a controller name and public TLS certificate");
  }
}

async function listen(server: Server, port: number, host: string) {
  const ready = once(server, "listening");
  server.listen(port, host);
  await ready;
  return (server.address() as AddressInfo).port;
}

export async function startTransport(options: TransportOptions): Promise<Transport> {
  const identity = loadIdentity(options.stateDirectory);
  const { target } = identity;
  const epoch = randomUUID();
  let approved = loadApproved(options.stateDirectory, identity.fingerprint);
  const pairs = new Map<string, Pending>();
  const connections = new Set<Socket>();
  const authenticated = new Map<Socket, { fingerprint: string; validFrom: number; validTo: number }>();
  const handshakes = new Map<string, NodeJS.Timeout>();
  const tls: TlsOptions = {
    key: identity.key, cert: identity.cert, minVersion: "TLSv1.3",
    secureOptions: constants.SSL_OP_NO_TICKET, handshakeTimeout: SESSION_TIMEOUT,
  };
  let rpcPort = 0;
  let stopped = false;
  const prunePairs = (now: number) => {
    for (const [id, entry] of pairs) {
      if (entry.expiresAt + 60_000 <= now) pairs.delete(id);
    }
  };

  function bootstrapRequest(request: WireRequest) {
    if (request.method === "hello") {
      requireFields(request, []);
      if (request.target !== undefined && request.target !== target) {
        throw new WireError("wrong_target", "Discovered target does not match this render host");
      }
      return { role: "draft", target, version: VERSION, rpcPort };
    }
    if (request.target !== target) {
      throw new WireError("wrong_target", "Verify the render host identity before pairing");
    }
    const now = Date.now();
    prunePairs(now);
    if (request.method === "pair.start") {
      requireFields(request, ["name", "certificate"]);
      if (pairs.size >= 8) throw new WireError("pair_capacity", "Too many pending pairing requests");
      const { name, certificate } = request.params;
      if (typeof name !== "string" || !name || Buffer.byteLength(name) > 80 || /[\p{Cc}\p{Cf}]/u.test(name)) {
        throw new WireError("invalid_pairing", "Expected a controller name and public TLS certificate");
      }
      const leaf = pairingCertificate(certificate);
      const pairing = { request: randomUUID(), fingerprint: fingerprint(leaf.raw), expiresAt: now + PAIR_TIMEOUT };
      pairs.set(pairing.request, { ...pairing, name, certificate: leaf.toString(), state: "pending" });
      return pairing;
    }
    if (request.method === "pair.status") {
      requireFields(request, ["request"]);
      const id = request.params.request;
      const entry = typeof id === "string" ? pairs.get(id) : undefined;
      if (!entry || entry.expiresAt <= now) return { state: "expired" };
      if (entry.state !== "pending") pairs.delete(entry.request);
      return { state: entry.state };
    }
    throw new WireError("unpaired", "Draft operations require an approved client certificate in TLS");
  }

  const certificates = (peers: Map<string, ApprovedPeer>) => [...peers.values()].map(peer => peer.certificate);
  let bridge: Awaited<ReturnType<typeof startRpcBridge>> | undefined;
  let closed: Promise<void> | undefined;
  let startup = true;
  let startupError: Error | undefined;
  const claim = (socket: Socket) => {
    if (stopped || connections.size >= 16) return false;
    connections.add(socket);
    socket.once("close", () => connections.delete(socket));
    return true;
  };
  const bootstrap = createServer(tls, socket => {
    if (stopped || socket.destroyed) socket.destroy();
    else void serve(socket, bootstrapRequest);
  });
  bootstrap.on("resumeSession", (_id, callback) => callback(null));
  bootstrap.on("tlsClientError", (_error, socket) => socket.destroy());
  bootstrap.prependListener("secureConnection", (socket: TLSSocket) => {
    const key = connectionKey(socket);
    clearTimeout(handshakes.get(key));
    handshakes.delete(key);
  });
  bootstrap.prependListener("connection", (connection: Socket) => {
    if (!claim(connection)) return connection.destroy();
    const key = connectionKey(connection);
    const deadline = setTimeout(() => connection.destroy(), SESSION_TIMEOUT);
    handshakes.set(key, deadline);
    connection.once("close", () => {
      clearTimeout(deadline);
      if (handshakes.get(key) === deadline) handshakes.delete(key);
    });
  });
  const close = () => closed ??= (async () => {
    stopped = true;
    for (const socket of connections) socket.destroy();
    await Promise.all([bridge?.close(),
      bootstrap.listening ? new Promise<void>(resolve => bootstrap.close(() => resolve())) : undefined]);
  })();

  try {
    bridge = await startRpcBridge({
      bind: options.bind, port: options.rpcPort, directory: options.stateDirectory,
      identityPath: identityPath(options.stateDirectory), pythonPath: options.pythonPath,
      peers: certificates(approved), claim,
      authenticate(socket, peer) {
        const trusted = approved.get(peer);
        if (!trusted) return false;
        const leaf = new X509Certificate(trusted.certificate);
        const validFrom = leaf.validFromDate.getTime();
        const validTo = leaf.validToDate.getTime();
        const now = Date.now();
        if (!(now >= validFrom && now < validTo)) return false;
        authenticated.set(socket, { fingerprint: peer, validFrom, validTo });
        socket.once("close", () => authenticated.delete(socket));
        return true;
      },
      serve(socket, peer) {
        const admission = authenticated.get(socket);
        if (!admission) return socket.destroy();
        const { validFrom, validTo } = admission;
        void serve(socket, request => {
          const now = Date.now();
          if (!approved.has(peer) || !(now >= validFrom && now < validTo)) {
            socket.destroy();
            return;
          }
          if (request.target !== target) {
            throw new WireError("wrong_target", "The selected service identity does not match this render host");
          }
          if (["hello", "pair.start", "pair.status"].includes(request.method)) {
            throw new WireError("unsupported_method", "This draft operation is unavailable");
          }
          if (options.dispatch) return options.dispatch(request, peer);
          if (request.method === "health" || request.method === "reflect") {
            requireFields(request, []);
            return { role: "draft", version: VERSION, target, epoch, revision: 0, uniforms: [], slots: [], needsRestore: true };
          }
          throw new WireError("unsupported_method", "This draft operation is unavailable");
        });
      },
      onLost(error) {
        if (stopped) return;
        if (startup) {
          // Startup still owns resources whose asynchronous listen may not have finished.
          // Refuse new sockets now and let its error path close the eventual listeners.
          stopped = true;
          startupError = error;
          return;
        }
        void close();
        options.onLost?.(error);
      },
    });
    if (startupError) throw startupError;
    const rpc = bridge;
    rpcPort = rpc.port;
    const bootstrapPort = await listen(bootstrap, options.bootstrapPort, options.bind);
    if (startupError) throw startupError;
    startup = false;
    let decisions = Promise.resolve();
    const decideInOrder = (operation: () => Promise<void>) => {
      const result = decisions.then(operation);
      decisions = result.catch(() => {});
      return result;
    };
    const updateApproved = async (next: Map<string, ApprovedPeer>) => {
      try {
        saveApproved(options.stateDirectory, identity.fingerprint, next);
        await rpc.update(certificates(next));
        approved = next;
      } catch (error) {
        // Never keep a listener running with divergent durable and active trust.
        if (!stopped) {
          void close();
          options.onLost?.(error instanceof Error ? error : new Error(String(error)));
        }
        throw error;
      }
    };
    return {
      target, fingerprint: identity.fingerprint, bootstrapPort, rpcPort,
      listPairing() {
        const now = Date.now();
        prunePairs(now);
        return [...pairs.values()].filter(entry => entry.state === "pending" && entry.expiresAt > now)
          .map(({ request, name, fingerprint, expiresAt }) => ({ request, name, fingerprint, expiresAt }));
      },
      decide(request, expectedFingerprint, approve) {
        return decideInOrder(async () => {
          const entry = pairs.get(request);
          if (stopped || entry?.state !== "pending" || entry.expiresAt <= Date.now() ||
              entry.fingerprint !== expectedFingerprint) {
            throw new WireError("pairing_mismatch", "Pairing request or fingerprint is no longer pending");
          }
          if (approve) {
            if (approved.size >= 32 && !approved.has(entry.fingerprint)) {
              throw new WireError("pair_capacity", "Revoke a controller before approving another");
            }
            const next = new Map(approved).set(entry.fingerprint, {
              fingerprint: entry.fingerprint, certificate: entry.certificate, name: entry.name,
            });
            await updateApproved(next);
          }
          entry.state = approve ? "approved" : "denied";
        });
      },
      revoke(peer) {
        return decideInOrder(async () => {
          if (stopped || !approved.has(peer)) throw new WireError("unpaired", "Peer is not approved");
          const next = new Map(approved);
          next.delete(peer);
          approved = next;
          for (const [socket, admission] of authenticated) {
            if (admission.fingerprint === peer) socket.destroy();
          }
          for (const [key, pair] of pairs) {
            if (pair.fingerprint === peer) pairs.delete(key);
          }
          const fenced = Promise.resolve().then(() => options.onRevoke?.(peer));
          const outcomes = await Promise.allSettled([updateApproved(next), fenced]);
          for (const outcome of outcomes) {
            if (outcome.status === "rejected") throw outcome.reason;
          }
        });
      },
      close,
    };
  } catch (error) {
    await close();
    throw error;
  }
}
