import { randomBytes } from "node:crypto";
import { lstat, mkdir, readdir } from "node:fs/promises";
import { createConnection, createServer, isIPv4 } from "node:net";
import { isAbsolute, join } from "node:path";
import type { DiscoveredPeer, Endpoint } from "./contracts";

const socketName = /^([1-9][0-9]{0,8})-[a-f0-9]{8}\.sock$/;
const targetId = /^[a-f0-9]{8}(?:-[a-f0-9]{4}){3}-[a-f0-9]{12}$/i;
const replyLimit = 1024;
const hostLimit = 32;

async function privateDirectory(path: string) {
  const info = await lstat(path).catch(() => null);
  return info?.isDirectory() && info.uid === process.getuid?.() && (info.mode & 0o777) === 0o700;
}

async function discoveryDirectory(create = false) {
  const root = process.env.XDG_RUNTIME_DIR;
  if (!root || !isAbsolute(root) || !(await privateDirectory(root))) return undefined;
  const path = join(root, "vosc-hosts");
  if (Buffer.byteLength(join(path, "000000000-00000000.sock")) > 103) return undefined;
  if (create) await mkdir(path, { mode: 0o700, recursive: true });
  return await privateDirectory(path) ? path : undefined;
}

export async function publishLocalHost(target: string, endpoint: Endpoint) {
  const directory = await discoveryDirectory(true);
  if (!directory) return undefined;
  const path = join(directory, `${process.pid}-${randomBytes(4).toString("hex")}.sock`);
  const peer = { target, endpoint, name: `This computer · VOSC ${target.slice(0, 8)}` };
  const response = JSON.stringify(peer) + "\n";
  const server = createServer(socket => {
    socket.on("error", () => socket.destroy());
    socket.setTimeout(500, () => socket.destroy());
    socket.end(response);
  });
  await new Promise<void>((resolve, reject) => {
    server.once("error", reject);
    server.listen(path, () => {
      server.off("error", reject);
      resolve();
    });
  });
  return { close: () => new Promise<void>(resolve => server.close(() => resolve())) };
}

function readHost(path: string): Promise<DiscoveredPeer | null> {
  return new Promise(resolve => {
    const socket = createConnection(path);
    let response = "";
    const timer = setTimeout(() => socket.destroy(), 500);
    socket.setEncoding("utf8");
    socket.on("error", () => socket.destroy());
    socket.on("close", () => {
      clearTimeout(timer);
      resolve(null);
    });
    socket.on("data", (chunk: string) => {
      response += chunk;
      if (Buffer.byteLength(response) > replyLimit) return socket.destroy();
      const end = response.indexOf("\n");
      if (end < 0) return;
      try {
        const peer = JSON.parse(response.slice(0, end)) as Partial<DiscoveredPeer> | null;
        if (typeof peer?.target === "string" && targetId.test(peer.target) &&
            typeof peer.endpoint?.host === "string" && isIPv4(peer.endpoint.host) && peer.endpoint.host !== "0.0.0.0" &&
            Number.isInteger(peer.endpoint.port) && peer.endpoint.port >= 1 && peer.endpoint.port <= 65535 &&
            typeof peer.name === "string" && peer.name.length <= 80) {
          resolve(peer as DiscoveredPeer);
        }
      } catch {
        // A failed or incomplete advertisement is omitted from the list.
      }
      socket.destroy();
    });
  });
}

export async function browseLocalHosts(): Promise<DiscoveredPeer[]> {
  const directory = await discoveryDirectory();
  if (!directory) return [];
  const entries = await readdir(directory, { withFileTypes: true }).catch(() => []);
  const peers = await Promise.all(entries.filter(entry => entry.isSocket() && runningAdvertiser(entry.name))
    .slice(0, hostLimit).map(entry => readHost(join(directory, entry.name))));
  return peers.filter(peer => peer !== null).sort((a, b) => a.target.localeCompare(b.target));
}

function runningAdvertiser(name: string) {
  const match = socketName.exec(name);
  if (!match) return false;
  try {
    process.kill(Number(match[1]), 0);
    return true;
  } catch {
    return false;
  }
}
