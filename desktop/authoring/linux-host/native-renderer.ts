import { spawn, type ChildProcess } from "node:child_process";
import { randomUUID } from "node:crypto";
import { access, copyFile, lstat, mkdir, mkdtemp, open, rm } from "node:fs/promises";
import { constants } from "node:fs";
import { createConnection } from "node:net";
import { dirname, isAbsolute, join } from "node:path";
import { setTimeout as delay } from "node:timers/promises";
import { TextDecoder } from "node:util";
import { WireError } from "./transport";

export type NativeObject = Record<string, unknown>;

export class NativeCleanupError extends WireError {
  constructor() {
    super("renderer_stop", "Could not confirm native renderer cleanup. Restart the supervised Render Host");
  }
}

interface Options {
  installDirectory: string;
  validation: boolean;
  onExit?: (renderer: NativeRenderer) => void;
}

const requestLimit = 256 * 1024;
const responseLimit = 1024 * 1024;
const exchangeMs = 1850;
const pending = new Set(["queued", "applied", "rolling_back"]);
const utf8 = new TextDecoder("utf-8", { fatal: true });
const assets = [
  "shaders/passthru.vert",
  "shaders/include/of_default_uniforms.glsl",
  "shaders/include/of_default_vertex_in_attributes.glsl",
];
const childEnvironment = [
  "DISPLAY", "XAUTHORITY", "WAYLAND_DISPLAY", "XDG_RUNTIME_DIR", "XDG_SESSION_TYPE",
  "DBUS_SESSION_BUS_ADDRESS", "HOME", "LANG", "LC_ALL", "PATH",
];

function object(value: unknown): value is NativeObject {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function check(response: NativeObject) {
  if (response.ok === true) return;
  const error = object(response.error) ? response.error : {};
  const diagnostics = Array.isArray(error.diagnostics) ? error.diagnostics.filter(object).slice(0, 12) : [];
  throw new WireError(
    typeof error.code === "string" ? error.code : "renderer_protocol",
    typeof error.message === "string" ? error.message : "Native renderer returned a failed receipt",
    diagnostics.length ? diagnostics : undefined,
  );
}

async function logTail(path: string) {
  const file = await open(path, "r");
  try {
    const { size } = await file.stat();
    const buffer = Buffer.alloc(Math.min(size, 1200));
    const { bytesRead } = await file.read(buffer, 0, buffer.length, size - buffer.length);
    return buffer.subarray(0, bytesRead).toString("utf8").trim();
  } finally {
    await file.close();
  }
}

export class NativeRenderer {
  private draftWgsl = false;
  get supportsDraftWgsl() { return this.draftWgsl; }

  private exited = false;
  private ready = false;
  private closing?: Promise<void>;
  private readonly death: Promise<void>;

  private constructor(
    private readonly child: ChildProcess,
    private readonly root: string,
    private readonly controlPath: string,
    onExit: Options["onExit"],
  ) {
    this.death = new Promise(resolve => child.once("close", () => {
      this.exited = true;
      resolve();
      if (this.ready && !this.closing) onExit?.(this);
    }));
  }

  get running() {
    return !this.closing && !this.exited && this.child.pid !== undefined &&
      this.child.exitCode === null && this.child.signalCode === null;
  }

  static async start(options: Options) {
    if (!isAbsolute(options.installDirectory)) {
      throw new WireError("renderer_missing", "An absolute Render Host installation path is required");
    }
    const binary = join(options.installDirectory, "renderer", "vosc");
    await access(binary, constants.X_OK).catch(() => {
      throw new WireError("renderer_missing", "Stage the executable native renderer beside VOSC Render Host");
    });
    const root = await mkdtemp("/tmp/vosc-draft-");
    const data = join(root, "data");
    const controlPath = join(root, "control.sock");
    const logPath = join(root, "renderer.log");
    let renderer: NativeRenderer | undefined;

    try {
      await mkdir(data, { mode: 0o700 });
      for (const path of assets) {
        const target = join(data, path);
        await mkdir(dirname(target), { recursive: true, mode: 0o700 });
        await copyFile(join(options.installDirectory, "renderer", "data", path), target);
      }
      const env: NodeJS.ProcessEnv = {};
      for (const key of childEnvironment) {
        if (process.env[key] !== undefined) env[key] = process.env[key];
      }
      env.LD_LIBRARY_PATH = join(options.installDirectory, "runtime-libs");
      env.VOSC_CONTROL_SOCKET = controlPath;
      const args = ["--draft-data-root", data, "--size", options.validation ? "160x120" : "640x480", "--fps", "30"];
      if (!options.validation) args.push("--draft-preview");

      const log = await open(logPath, "wx", 0o600);
      try {
        const child = spawn(binary, args, { cwd: root, env, stdio: ["ignore", log.fd, log.fd] });
        renderer = new NativeRenderer(child, root, controlPath, options.onExit);
        await new Promise<void>((resolve, reject) => {
          child.once("spawn", resolve);
          child.on("error", reject);
        });
      } finally {
        await log.close();
      }

      const startupDeadline = performance.now() + 6000;
      let available = false;
      while (renderer.running && performance.now() < startupDeadline) {
        const info = await lstat(controlPath).catch((error: NodeJS.ErrnoException) => {
          if (error.code !== "ENOENT") throw error;
          return undefined;
        });
        if (info && (!info.isSocket() || info.uid !== process.getuid?.())) {
          throw new WireError("renderer_startup", "Native renderer control path is not an owned socket");
        }
        if (info && (info.mode & 0o777) === 0o600) {
          available = true;
          break;
        }
        await delay(30);
      }
      if (!available) {
        const detail = await logTail(logPath).catch(() => "");
        throw new WireError("renderer_startup",
          `Private renderer did not open its control socket.${detail ? " " + detail : ""}`);
      }
      const capabilities = await renderer.query("get_capabilities");
      const required = ["configure_layers", "publish_shader", "get_scene", "get_request"];
      if (!options.validation) required.push(
        "draft_prepare", "draft_commit", "draft_cancel", "draft_fence",
        "draft_sample", "draft_callback", "draft_configure_slot", "draft_bind_uniform",
      );
      const operations = capabilities.operations;
      if (capabilities.version !== 1 || capabilities.transport !== "unix-ndjson" ||
          !Array.isArray(operations) || !required.every(method => operations.includes(method))) {
        throw new WireError("renderer_capabilities", "Installed VOSC cannot handle private draft operations");
      }
      renderer.draftWgsl = object(capabilities.features) && capabilities.features.draft_wgsl === true;
      await renderer.mutate("configure_layers", { count: 1, layout: "stack" });
      if (!renderer.running) throw new WireError("renderer_startup", "Private renderer exited during startup");
      renderer.ready = true;
      return renderer;
    } catch (error) {
      if (renderer) await renderer.stop();
      else await rm(root, { recursive: true, force: true });
      if (error instanceof WireError) throw error;
      throw new WireError("renderer_startup",
        `Could not start private renderer: ${error instanceof Error ? error.message : "unknown error"}`);
    }
  }

  stop(): Promise<void> {
    return this.closing ??= this.shutdown().catch(error => {
      if (error instanceof NativeCleanupError) throw error;
      throw new NativeCleanupError();
    });
  }

  private async shutdown() {
    // Signal only the exact child owned by this Node process. The systemd
    // service cgroup covers abnormal supervisor loss.
    const alive = () => !this.exited && this.child.pid !== undefined &&
      this.child.exitCode === null && this.child.signalCode === null;
    if (alive()) this.child.kill("SIGTERM");
    const force = setTimeout(() => {
      if (alive()) this.child.kill("SIGKILL");
    }, 500);
    let deadline: NodeJS.Timeout | undefined;
    try {
      await Promise.race([this.death, new Promise<never>((_, reject) => {
        deadline = setTimeout(() => reject(new NativeCleanupError()), 2500);
      })]);
    } finally {
      clearTimeout(force);
      clearTimeout(deadline);
    }
    await rm(this.root, { recursive: true, force: true });
  }

  async query(method: string, params: NativeObject = {}, until = performance.now() + exchangeMs) {
    const response = await this.exchange(method, params, randomUUID(), until);
    if (!object(response.result)) {
      throw new WireError("renderer_protocol", "Native renderer did not return a query result");
    }
    return response.result;
  }

  async mutate(method: string, params: NativeObject, id: string = randomUUID(), expecting = "rendered") {
    const until = performance.now() + (method === "draft_prepare" && params.language === "wgsl" ? 4500 : exchangeMs);
    return this.settle(await this.exchange(method, params, id, until), id, expecting, until);
  }

  async recover(id: string, expecting: string) {
    const until = performance.now() + exchangeMs;
    return this.settle(await this.query("get_request", { request_id: id }, until), id, expecting, until);
  }

  private async settle(initial: NativeObject, id: string, expecting: string, until: number) {
    let receipt = initial;
    for (;;) {
      if (receipt.id !== id) {
        throw new WireError("renderer_protocol", "Native renderer returned a different request receipt");
      }
      if (typeof receipt.status !== "string" || !pending.has(receipt.status)) break;
      await delay(Math.max(0, Math.min(20, until - performance.now())));
      receipt = await this.query("get_request", { request_id: id }, until);
    }
    check(receipt);
    if (receipt.status !== expecting || (expecting === "rendered" &&
        (!Number.isSafeInteger(receipt.frame) || !Number.isSafeInteger(receipt.revision)))) {
      throw new WireError("renderer_protocol", `Native renderer did not return a matching ${expecting} receipt`);
    }
    return receipt;
  }

  private exchange(method: string, params: NativeObject, id: string, until: number): Promise<NativeObject> {
    const remaining = until - performance.now();
    if (remaining <= 0) {
      throw new WireError("renderer_timeout", "Native renderer did not respond before the request deadline");
    }
    if (!this.running) throw new WireError("renderer_transport", "Private renderer is unavailable");
    const request = JSON.stringify({ id, method, params }) + "\n";
    if (Buffer.byteLength(request) > requestLimit) {
      throw new WireError("renderer_transport", "Native request exceeds the private socket limit");
    }

    return new Promise((resolve, reject) => {
      const socket = createConnection(this.controlPath);
      const fragments: Buffer[] = [];
      let length = 0;
      let complete = false;
      const deadline = setTimeout(() => finish(new WireError(
        "renderer_timeout", "Native renderer did not respond before the request deadline",
      )), Math.ceil(remaining));

      function finish(error?: WireError, response?: NativeObject) {
        if (complete) return;
        complete = true;
        clearTimeout(deadline);
        socket.destroy();
        if (error) reject(error);
        else if (response) resolve(response);
      }

      socket.once("connect", () => socket.write(request));
      socket.once("error", () => finish(new WireError("renderer_transport", "Could not reach the owned renderer")));
      socket.once("close", () => finish(new WireError("renderer_transport", "Native renderer closed its socket")));
      socket.on("data", (chunk: Buffer) => {
        const end = chunk.indexOf(10);
        const fragment = end < 0 ? chunk : chunk.subarray(0, end);
        length += fragment.length;
        if (length > responseLimit) {
          finish(new WireError("renderer_transport", "Native response exceeded its private limit"));
          return;
        }
        fragments.push(fragment);
        if (end < 0) return;
        try {
          const response: unknown = JSON.parse(utf8.decode(Buffer.concat(fragments, length)));
          if (!object(response) || response.id !== id) {
            throw new WireError("renderer_protocol", "Native response does not match its private request");
          }
          check(response);
          finish(undefined, response);
        } catch (error) {
          finish(error instanceof WireError ? error :
            new WireError("renderer_protocol", "Native renderer returned malformed JSON"));
        }
      });
    });
  }
}
