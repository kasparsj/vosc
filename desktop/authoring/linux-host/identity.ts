import { execFileSync } from "node:child_process";
import { createHash, createPrivateKey, randomUUID, X509Certificate } from "node:crypto";
import { closeSync, fsyncSync, lstatSync, mkdirSync, mkdtempSync, openSync, readFileSync,
  renameSync, rmSync, writeFileSync } from "node:fs";
import { join } from "node:path";

export interface HostIdentity {
  target: string;
  key: string;
  cert: string;
  fingerprint: string;
}

export interface ApprovedPeer {
  fingerprint: string;
  name: string;
  certificate: string;
}

export function fingerprint(bytes: Buffer): string {
  return createHash("sha256").update(bytes).digest("hex");
}

function assertOwned(path: string, directory: boolean) {
  const file = lstatSync(path);
  if ((directory ? !file.isDirectory() : !file.isFile()) ||
      file.uid !== process.getuid?.() || (file.mode & 0o777) !== (directory ? 0o700 : 0o600)) {
    throw new Error(`Expected an owner-only ${directory ? "directory" : "regular file"} at ${path}`);
  }
}

function readPrivateJSON(path: string, limit: number): unknown {
  try {
    assertOwned(path, false);
    if (lstatSync(path).size > limit) throw new Error(`State file exceeds its size limit at ${path}`);
    return JSON.parse(readFileSync(path, "utf8"));
  } catch (error) {
    if ((error as NodeJS.ErrnoException).code !== "ENOENT") throw error;
    return undefined;
  }
}

function savePrivateJSON(directory: string, name: string, value: unknown) {
  const temporary = join(directory, `.${name}.${randomUUID()}`);
  const descriptor = openSync(temporary, "wx", 0o600);
  try {
    try {
      writeFileSync(descriptor, JSON.stringify(value) + "\n");
      fsyncSync(descriptor);
    } finally {
      closeSync(descriptor);
    }
    renameSync(temporary, join(directory, name));
    const parent = openSync(directory, "r");
    try {
      fsyncSync(parent);
    } finally {
      closeSync(parent);
    }
  } finally {
    rmSync(temporary, { force: true });
  }
}

function newIdentity(directory: string) {
  const target = randomUUID();
  const temporary = mkdtempSync(join(directory, ".identity."));
  try {
    const certPath = join(temporary, "cert.pem");
    const keyPath = join(temporary, "key.pem");
    execFileSync("/usr/bin/openssl", ["req", "-x509", "-newkey", "ec",
      "-pkeyopt", "ec_paramgen_curve:P-256", "-nodes", "-days", "365",
      "-subj", "/CN=VOSC Render Host", "-keyout", keyPath, "-out", certPath], {
      stdio: ["ignore", "ignore", "pipe"], timeout: 5000,
    });
    const cert = readFileSync(certPath, "utf8");
    const key = readFileSync(keyPath, "utf8");
    savePrivateJSON(directory, "identity.json", { target, cert, key });
    return { target, cert, key };
  } finally {
    rmSync(temporary, { recursive: true, force: true });
  }
}

export function identityPath(directory: string): string {
  return join(directory, "identity.json");
}

export function loadIdentity(directory: string): HostIdentity {
  mkdirSync(directory, { mode: 0o700, recursive: true });
  assertOwned(directory, true);
  const stored = readPrivateJSON(identityPath(directory), 16_384) as
    Partial<Omit<HostIdentity, "fingerprint">> | undefined;
  if (!stored && readPrivateJSON(join(directory, "peers.json"), 262_144) !== undefined) {
    throw new Error("Render Host identity is missing. Restore the original identity or use a fresh profile to re-pair.");
  }
  const value = stored ?? newIdentity(directory);
  if (typeof value.target !== "string" ||
      !/^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$/.test(value.target) ||
      typeof value.cert !== "string" || typeof value.key !== "string") {
    throw new Error("Invalid Render Host identity");
  }
  const certificate = new X509Certificate(value.cert);
  if (!certificate.checkPrivateKey(createPrivateKey(value.key))) {
    throw new Error("Render Host certificate does not match its private key");
  }
  return { target: value.target, cert: value.cert, key: value.key, fingerprint: fingerprint(certificate.raw) };
}

export function loadApproved(directory: string, hostFingerprint: string): Map<string, ApprovedPeer> {
  const state = readPrivateJSON(join(directory, "peers.json"), 262_144) as
    { hostFingerprint?: string; peers?: ApprovedPeer[] } | undefined;
  const peers = new Map<string, ApprovedPeer>();
  if (!state) return peers;
  if (state.hostFingerprint !== hostFingerprint) {
    throw new Error("Render Host certificate changed. Restore the original identity or use a fresh profile to re-pair.");
  }
  if (!Array.isArray(state.peers) || state.peers.length > 32) throw new Error("Invalid Render Host trust store");
  for (const peer of state.peers) {
    if (typeof peer.name !== "string" || typeof peer.certificate !== "string" ||
        fingerprint(new X509Certificate(peer.certificate).raw) !== peer.fingerprint || peers.has(peer.fingerprint)) {
      throw new Error("Invalid Render Host peer");
    }
    peers.set(peer.fingerprint, peer);
  }
  return peers;
}

export function saveApproved(directory: string, hostFingerprint: string, peers: Map<string, ApprovedPeer>) {
  savePrivateJSON(directory, "peers.json", { hostFingerprint, peers: [...peers.values()] });
}
