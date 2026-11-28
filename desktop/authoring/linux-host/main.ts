import { spawnSync } from "node:child_process";
import { chmodSync, closeSync, constants, fstatSync, lstatSync, mkdirSync, openSync, realpathSync, unlinkSync } from "node:fs";
import { createConnection, createServer, isIPv4, type Server, type Socket } from "node:net";
import { isAbsolute, join, resolve } from "node:path";
import { parseArgs } from "node:util";
import { publishLocalHost } from "../src/local-discovery";
import { publishDiscovery, type Discovery } from "./discovery";
import { Draft } from "./draft";
import { DraftService } from "./draft-service";
import { startTransport, WireError } from "./transport";

const tokens = process.argv.slice(2);
const usage = `Usage:
  vosc-render-host serve --state-dir DIR --bind IPv4 --bootstrap-port PORT --rpc-port PORT [--interface NAME | --no-discovery]
  vosc-render-host pair list --state-dir DIR
  vosc-render-host pair approve|deny REQUEST FINGERPRINT --state-dir DIR
  vosc-render-host peer revoke FINGERPRINT --state-dir DIR`;
const adminLimit = 4096;

function privateDirectory(value: string, create: boolean): string {
  if (!isAbsolute(value)) throw new Error("--state-dir must be absolute");
  const path = resolve(value);
  if (create) {
    try { mkdirSync(path, { mode: 0o700 }); }
    catch (error) { if ((error as NodeJS.ErrnoException).code !== "EEXIST") throw error; }
  }
  const info = lstatSync(path);
  if (!info.isDirectory() || info.uid !== process.getuid?.() || (info.mode & 0o777) !== 0o700) {
    throw new Error("The state directory must be owned by this user and have mode 0700");
  }
  const canonical = realpathSync(path);
  if (Buffer.byteLength(join(canonical, "admin.sock")) > 103) {
    throw new Error("Choose a shorter state directory for the local control socket");
  }
  return canonical;
}

function lockDirectory(stateDirectory: string): number {
  const path = join(stateDirectory, "serve.lock");
  const fd = openSync(path, constants.O_RDWR | constants.O_CREAT | constants.O_NOFOLLOW, 0o600);
  try {
    const info = fstatSync(fd);
    if (!info.isFile() || info.uid !== process.getuid?.() || (info.mode & 0o777) !== 0o600) {
      throw new Error("The service lock must be a user-owned 0600 file");
    }
    // flock and this process share the open file description via descriptor 3.
    const lock = spawnSync("/usr/bin/flock", ["-n", "3"], { stdio: ["ignore", "ignore", "pipe", fd] });
    if (lock.error) throw lock.error;
    if (lock.status !== 0) throw new Error("This Render Host profile is already in use");
    return fd;
  } catch (error) {
    closeSync(fd);
    throw error;
  }
}

function clearAdminSocket(path: string): void {
  try {
    const info = lstatSync(path);
    if (!info.isSocket() || info.uid !== process.getuid?.()) {
      throw new Error("Unexpected file at the local control path");
    }
    unlinkSync(path);
  } catch (error) {
    if ((error as NodeJS.ErrnoException).code !== "ENOENT") throw error;
  }
}

function port(value: string | undefined): number {
  if (!value || !/^[0-9]{1,5}$/.test(value) || Number(value) < 1024 || Number(value) > 65535) {
    throw new Error("Choose explicit user ports between 1024 and 65535");
  }
  return Number(value);
}

type Transport = Awaited<ReturnType<typeof startTransport>>;

async function adminCommand(host: Transport, value: unknown): Promise<unknown> {
  if (!Array.isArray(value) || value.some(token => typeof token !== "string")) {
    throw new Error("Invalid local command");
  }
  const [group, action, id, fingerprint] = value as string[];
  if (group === "pair" && action === "list" && value.length === 2) return host.listPairing();
  if (group === "pair" && (action === "approve" || action === "deny") && value.length === 4) {
    if (!/^[a-f0-9]{64}$/i.test(fingerprint)) throw new Error("Enter the full controller SHA-256 fingerprint");
    await host.decide(id, fingerprint.toLowerCase(), action === "approve");
    return { decision: action, request: id, fingerprint: fingerprint.toLowerCase() };
  }
  if (group === "peer" && action === "revoke" && value.length === 3) {
    if (!/^[a-f0-9]{64}$/i.test(id)) throw new Error("Enter the full controller SHA-256 fingerprint");
    await host.revoke(id.toLowerCase());
    return { revoked: id.toLowerCase() };
  }
  throw new Error("Unknown local command");
}

function message(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}

function listen(server: Server, socketPath: string): Promise<void> {
  return new Promise((resolve, reject) => {
    server.once("error", reject);
    server.listen(socketPath, () => {
      server.off("error", reject);
      resolve();
    });
  });
}

async function serve(stateDirectory: string, values: {
  bind?: string; "bootstrap-port"?: string; "rpc-port"?: string; interface?: string; "no-discovery"?: boolean;
}): Promise<void> {
  const bind = values.bind;
  if (!bind || !isIPv4(bind) || bind === "0.0.0.0") throw new Error("Choose a concrete IPv4 bind address");
  const bootstrapPort = port(values["bootstrap-port"]);
  const rpcPort = port(values["rpc-port"]);
  if (bootstrapPort === rpcPort) throw new Error("Bootstrap and RPC ports must be different");
  if (values.interface && values["no-discovery"]) {
    throw new Error("Select at most one of --interface and --no-discovery");
  }

  const lock = lockDirectory(stateDirectory);
  const socketPath = join(stateDirectory, "admin.sock");
  const sessions = new Set<Socket>();
  const operations = new Set<Promise<unknown>>();
  let host: Transport | undefined;
  let draft: DraftService | undefined;
  let admin: Server | undefined;
  let discovery: Discovery | undefined;
  let localDiscovery: Discovery | undefined;
  const stop = new AbortController();
  const onSignal = () => stop.abort();
  function dispatch<T>(operation: (service: DraftService) => Promise<T>): Promise<T> {
    if (!draft || stop.signal.aborted) {
      throw new WireError("not_ready", "The Render Host is not ready for private draft operations");
    }
    const pending = operation(draft).finally(() => operations.delete(pending));
    operations.add(pending);
    return pending;
  }
  process.once("SIGINT", onSignal);
  process.once("SIGTERM", onSignal);
  try {
    clearAdminSocket(socketPath);
    const transport = host = await startTransport({
      bind, bootstrapPort, rpcPort, stateDirectory,
      dispatch: (request, peer) => dispatch(service => service.dispatch(request, peer)),
      onRevoke: peer => dispatch(service => service.revoke(peer)),
      onLost: error => {
        process.stderr.write(message(error) + "\n");
        process.exitCode = 1;
        stop.abort();
      },
    });
    draft = new DraftService(transport.target, new Draft(transport.target, __dirname));
    admin = createServer(socket => {
      if (sessions.size >= 8) return socket.destroy();
      sessions.add(socket);
      socket.on("close", () => sessions.delete(socket));
      socket.on("error", () => socket.destroy());
      socket.setTimeout(5000, () => socket.destroy());
      let buffered = Buffer.alloc(0);
      const receive = (chunk: Buffer) => {
        if (buffered.length + chunk.length > adminLimit) return socket.destroy();
        buffered = Buffer.concat([buffered, chunk]);
        const end = buffered.indexOf(10);
        if (end < 0) return;
        socket.off("data", receive);
        Promise.resolve().then(() => adminCommand(transport, JSON.parse(buffered.subarray(0, end).toString())))
          .then(result => socket.end(JSON.stringify({ ok: true, result }) + "\n"),
            error => socket.end(JSON.stringify({ ok: false, error: message(error) }) + "\n"));
      };
      socket.on("data", receive);
    });
    await listen(admin, socketPath);
    chmodSync(socketPath, 0o600);
    if (values.interface) {
      discovery = await publishDiscovery({
        bind, interfaceName: values.interface, target: transport.target,
        port: transport.bootstrapPort, signal: stop.signal,
        onLost: error => {
          process.stderr.write(message(error) + "\n");
          process.exitCode = 1;
          stop.abort();
        },
      });
    }
    if (!stop.signal.aborted && !values["no-discovery"]) {
      try {
        localDiscovery = await publishLocalHost(transport.target, { host: bind, port: transport.bootstrapPort });
      } catch (error) {
        process.stderr.write(`Could not list this host locally: ${message(error)}\n`);
      }
    }
    if (!stop.signal.aborted) {
      process.stdout.write(JSON.stringify({
        event: "ready", target: transport.target, fingerprint: transport.fingerprint,
        bind, bootstrapPort: transport.bootstrapPort, rpcPort: transport.rpcPort,
        discovery: values.interface ?? false, display: process.env.DISPLAY ?? null,
        profile: stateDirectory,
      }) + "\n");
      await new Promise<void>(resolve => stop.signal.addEventListener("abort", () => resolve(), { once: true }));
    }
  } finally {
    stop.abort();
    try {
      for (const socket of sessions) socket.destroy();
      const closed = await Promise.allSettled([
        localDiscovery?.close(),
        discovery?.close(),
        host?.close(),
        admin?.listening ? new Promise<void>(resolve => admin?.close(() => resolve())) : undefined,
      ]);
      await Promise.allSettled(operations);
      await draft?.reset();
      const failure = closed.find(result => result.status === "rejected");
      if (failure) throw failure.reason;
    } finally {
      process.removeListener("SIGINT", onSignal);
      process.removeListener("SIGTERM", onSignal);
      closeSync(lock);
    }
  }
}

function localRequest(path: string, command: string[]): Promise<unknown> {
  return new Promise((resolve, reject) => {
    const info = lstatSync(path);
    if (!info.isSocket() || info.uid !== process.getuid?.() || (info.mode & 0o777) !== 0o600) {
      throw new Error("The local operator socket must be owned by this user and have mode 0600");
    }
    const socket = createConnection(path);
    let buffered = Buffer.alloc(0);
    socket.once("connect", () => socket.write(JSON.stringify(command) + "\n"));
    socket.on("error", reject);
    socket.on("close", () => reject(new Error("The local operator socket closed before replying")));
    socket.setTimeout(5000, () => socket.destroy(new Error("The local operator command timed out")));
    socket.on("data", (chunk: Buffer) => {
      if (buffered.length + chunk.length > adminLimit) return socket.destroy(new Error("Local reply too large"));
      buffered = Buffer.concat([buffered, chunk]);
      const end = buffered.indexOf(10);
      if (end < 0) return;
      try {
        const reply = JSON.parse(buffered.subarray(0, end).toString());
        if (reply.ok === true) resolve(reply.result);
        else reject(new Error(reply.error));
      } catch (error) { reject(error); }
      socket.destroy();
    });
  });
}

async function main(): Promise<void> {
  if (tokens.length === 1 && tokens[0] === "--help") return console.log(usage);
  const { values, positionals } = parseArgs({
    args: tokens, strict: true, allowPositionals: true,
    options: {
      "state-dir": { type: "string" }, bind: { type: "string" },
      "bootstrap-port": { type: "string" }, "rpc-port": { type: "string" },
      interface: { type: "string" }, "no-discovery": { type: "boolean" },
    },
  });
  if (!values["state-dir"]) throw new Error(usage);
  if (positionals[0] === "serve" && positionals.length === 1) {
    await serve(privateDirectory(values["state-dir"], true), values);
    return;
  }
  if (Object.keys(values).some(key => key !== "state-dir")) throw new Error(usage);
  const stateDirectory = privateDirectory(values["state-dir"], false);
  console.log(JSON.stringify(await localRequest(join(stateDirectory, "admin.sock"), positionals)));
}

process.umask(0o077);
main().catch(error => {
  process.stderr.write("VOSC Render Host: " + message(error) + "\n");
  process.exitCode = 1;
});
