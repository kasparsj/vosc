import { spawn } from "node:child_process";
import { randomBytes } from "node:crypto";
import { once } from "node:events";
import { chmodSync, lstatSync, unlinkSync } from "node:fs";
import { createServer, type Socket } from "node:net";
import { join } from "node:path";

const CONTROL_DEADLINE = 4000;
const SESSION_DEADLINE = 15_000;
const ACK = '{"ok":true}\n';

// Fixed exit statuses from rpc_tls.py keep control and identity material out of diagnostics.
function adapterExit(status: number | null, port: number): Error {
  const diagnostics: Record<number, string> = {
    10: "RPC TLS adapter could not read the owner-only host identity. Check the profile and its permissions.",
    11: "RPC TLS adapter rejected a private control message.",
    12: "RPC TLS adapter could not load the host certificate, key or approved peers. Check the saved profile.",
    13: `Authenticated RPC port ${port} is already in use. Choose an unused --rpc-port.`,
    14: "Authenticated RPC bind address is unavailable. Choose an IPv4 address assigned to this host.",
    15: "RPC TLS adapter could not bind the authenticated listener. Check --bind and --rpc-port.",
    16: "RPC TLS adapter could not reload the approved-peer trust set. Check the saved profile before restarting.",
  };
  return new Error(diagnostics[status ?? 0] ?? "RPC TLS adapter exited unexpectedly");
}

interface Options {
  bind: string;
  port: number;
  directory: string;
  identityPath: string;
  pythonPath?: string;
  peers: string[];
  claim(socket: Socket): boolean;
  authenticate(socket: Socket, fingerprint: string): boolean;
  serve(socket: Socket, fingerprint: string): void;
  onLost(error: Error): void;
}

function readHandshake(socket: Socket, deadline: number): Promise<unknown> {
  return new Promise((resolve, reject) => {
    let frame = Buffer.alloc(0);
    const timer = setTimeout(() => finish(new Error("RPC bridge handshake timed out")), deadline - performance.now());

    function finish(error?: Error, result?: unknown) {
      clearTimeout(timer);
      socket.off("data", receive);
      socket.off("close", closed);
      if (error) reject(error);
      else resolve(result);
    }

    function closed() {
      finish(new Error("RPC bridge closed during handshake"));
    }

    function receive(chunk: Buffer) {
      const newline = chunk.indexOf(0x0a);
      const prefix = newline === -1 ? chunk : chunk.subarray(0, newline);
      if (frame.length + prefix.length > 128) {
        finish(new Error("Oversized RPC bridge handshake"));
        return;
      }
      frame = Buffer.concat([frame, prefix]);
      if (newline < 0) return;
      socket.pause();
      const trailing = chunk.subarray(newline + 1);
      if (trailing.length) socket.unshift(trailing);
      try { finish(undefined, JSON.parse(frame.toString("utf8"))); }
      catch { finish(new Error("Invalid RPC bridge handshake")); }
    }

    socket.once("close", closed);
    socket.on("data", receive);
    socket.resume();
  });
}

function hasValue(value: unknown, field: string): value is Record<string, unknown> {
  return typeof value === "object" && value !== null &&
    Object.keys(value).length === 1 && Object.hasOwn(value, field);
}

function clearSocket(path: string) {
  try {
    const info = lstatSync(path);
    if (!info.isSocket() || info.uid !== process.getuid?.()) {
      throw new Error("Unexpected file at the RPC bridge socket path");
    }
    unlinkSync(path);
  } catch (error) {
    if ((error as NodeJS.ErrnoException).code !== "ENOENT") throw error;
  }
}

export async function startRpcBridge(options: Options) {
  const path = join(options.directory, "rpc.sock");
  clearSocket(path);
  const token = randomBytes(32).toString("hex");
  const sessions = new Set<Socket>();
  let closing = false;
  const server = createServer(socket => {
    if (closing || sessions.size >= 16) return socket.destroy();
    sessions.add(socket);
    socket.once("close", () => sessions.delete(socket));
    socket.on("error", () => socket.destroy());
    const deadline = performance.now() + SESSION_DEADLINE;
    void (async () => {
      const reserve = await readHandshake(socket, deadline);
      if (!hasValue(reserve, "token") || reserve.token !== token || !options.claim(socket)) {
        return socket.destroy();
      }
      socket.write(ACK);
      const hello = await readHandshake(socket, deadline);
      if (!hasValue(hello, "fingerprint") || typeof hello.fingerprint !== "string" ||
          !/^[a-f0-9]{64}$/.test(hello.fingerprint) || !options.authenticate(socket, hello.fingerprint)) {
        return socket.destroy();
      }
      socket.write(ACK);
      options.serve(socket, hello.fingerprint);
    })().catch(() => socket.destroy());
  });
  const ready = once(server, "listening");
  server.listen(path);
  await ready;

  let child: ReturnType<typeof launch> | undefined;
  let closed: Promise<void> | undefined;
  let waitForChild: Promise<unknown> | undefined;
  let requestId = 0;
  let pending: { id: number; timer: NodeJS.Timeout; resolve(value: unknown): void; reject(error: Error): void } | undefined;
  const close = (error = new Error("RPC TLS adapter stopped")) => closed ??= (async () => {
    closing = true;
    if (pending) {
      clearTimeout(pending.timer);
      pending.reject(error);
      pending = undefined;
    }
    for (const socket of sessions) socket.destroy();
    child?.stdin.end();
    const kill = setTimeout(() => child?.kill("SIGKILL"), 1000);
    try {
      await Promise.all([waitForChild, new Promise<void>(resolve => server.close(() => resolve()))]);
    } finally {
      clearTimeout(kill);
    }
  })();

  function lost(error: Error) {
    if (closing) return;
    void close(error);
    options.onLost(error);
  }

  function launch() {
    return spawn(options.pythonPath ?? "/usr/bin/python3",
      ["-I", join(__dirname, "rpc_tls.py"), options.bind, String(options.port), path, options.identityPath],
      { stdio: ["pipe", "pipe", "ignore"] });
  }

  const request = (payload: { token?: string; peers: string[] }) => new Promise<unknown>((resolve, reject) => {
    if (closing || pending || !child) return reject(new Error("RPC TLS adapter is unavailable"));
    const id = ++requestId;
    const timer = setTimeout(() => lost(new Error("RPC TLS adapter acknowledgement timed out")), CONTROL_DEADLINE);
    pending = { id, timer, resolve, reject };
    child.stdin.write(JSON.stringify({ ...payload, id }) + "\n");
  });

  try {
    chmodSync(path, 0o600);
    child = launch();
    waitForChild = new Promise(resolve => child?.once("close", resolve));
    child.on("error", () => lost(new Error("Could not launch the RPC TLS adapter. Check the system Python installation.")));
    child.stdin.on("error", (error: NodeJS.ErrnoException) => {
      // A child exiting during startup may close stdin first. Its exit status gives the safe diagnostic.
      if (error.code !== "EPIPE") lost(new Error("RPC TLS adapter control pipe failed"));
    });
    child.once("close", status => lost(adapterExit(status, options.port)));
    server.on("error", lost);
    let control = Buffer.alloc(0);
    child.stdout.on("data", (chunk: Buffer) => {
      if (closing) return;
      control = Buffer.concat([control, chunk]);
      if (control.length > 4096) return lost(new Error("Oversized RPC TLS adapter acknowledgement"));
      const newline = control.indexOf(0x0a);
      if (newline < 0) return;
      try {
        const result = JSON.parse(control.subarray(0, newline).toString("utf8"));
        if (!pending || newline + 1 !== control.length || result?.id !== pending.id) {
          throw new Error("Unexpected RPC TLS adapter acknowledgement");
        }
        control = Buffer.alloc(0);
        clearTimeout(pending.timer);
        const { resolve } = pending;
        pending = undefined;
        resolve(result);
      } catch (error) {
        lost(error instanceof Error ? error : new Error(String(error)));
      }
    });

    const initial = await request({ token, peers: options.peers }) as { id: number; port?: number };
    if (Object.keys(initial).length !== 2 || !Number.isInteger(initial.port) || initial.port! <= 0 ||
        initial.port! > 65535 || (options.port !== 0 && initial.port !== options.port)) {
      throw new Error("Invalid RPC TLS adapter TCP port");
    }
    return {
      port: initial.port!,
      async update(peers: string[]) {
        const result = await request({ peers }) as { id: number };
        if (Object.keys(result).length !== 1) throw new Error("Invalid RPC TLS adapter trust acknowledgement");
      },
      close,
    };
  } catch (error) {
    await close();
    throw error;
  }
}
