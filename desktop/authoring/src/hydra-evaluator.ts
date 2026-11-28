import { app, BrowserWindow, session, webContents } from "electron";
import { randomUUID } from "node:crypto";
import { readFile } from "node:fs/promises";
import { join } from "node:path";
import { setTimeout as delay } from "node:timers/promises";
import { compileHydra, HYDRA_EXCHANGE_LIMIT, HYDRA_SOURCE_LIMIT } from "./hydra-compiler";
import { HostError } from "./local-state";

export const HYDRA_SCHEME = "vosc-hydra";
const RESTRICTIONS = "default-src 'none'; connect-src 'none'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'";
const PAGE_CSP = `${RESTRICTIONS}; script-src 'self'; worker-src 'self'`;
const WORKER_CSP = `${RESTRICTIONS}; script-src 'unsafe-eval'; worker-src 'none'`;
const HTML = '<!doctype html><meta charset="utf-8"><script src="/bootstrap.js"></script>';
const PATHS = new Map([
  ["/bootstrap.js", "hydra-bootstrap.js"],
  ["/worker.js", "hydra-worker.js"],
]);

export type HydraEvaluator = (source: string, signal: AbortSignal) => Promise<ReturnType<typeof compileHydra>>;

export class ElectronHydraEvaluator {
  private active: Promise<unknown> | null = null;
  private isolated: Electron.Session | null = null;
  private allowedRequests = new Set<string>();
  private isolationFault = false;

  private evaluatorSession() {
    if (this.isolated) return this.isolated;
    const isolated = session.fromPartition("vosc-hydra-evaluator", { cache: false });
    isolated.setPermissionRequestHandler((_contents, _permission, callback) => callback(false));
    isolated.setPermissionCheckHandler(() => false);
    isolated.on("will-download", event => event.preventDefault());
    isolated.webRequest.onBeforeRequest(({ url, method }, reply) => {
      reply({ cancel: method !== "GET" || !this.allowedRequests.has(url) });
    });
    return this.isolated = isolated;
  }

  evaluate: HydraEvaluator = async (source, signal) => {
    if (Buffer.byteLength(source) > HYDRA_SOURCE_LIMIT) throw new HostError("hydra_source", "Hydra source exceeds 128 KiB.");
    if (this.active) await this.active.catch(() => {});
    if (signal.aborted || this.active) throw new HostError("hydra_cancelled", "Hydra evaluation cancelled.");
    if (this.isolationFault) throw new HostError("hydra_isolation", "Restart the Editor after an evaluator isolation failure.");
    const job = this.run(source, signal);
    this.active = job;
    try { return await job; }
    finally { if (this.active === job) this.active = null; }
  };

  private async run(source: string, signal: AbortSignal) {
    const isolated = this.evaluatorSession();
    // Electron retains partitions until shutdown. Each patch receives a unique origin
    // inside the dedicated memory-only partition, which is cleared before the next run.
    const origin = `${HYDRA_SCHEME}://run-${randomUUID()}`;
    const bootstrapURL = `${origin}/`;
    let window: BrowserWindow | undefined;
    let protocolInstalled = false;
    let pid = 0;
    let timer: NodeJS.Timeout | undefined;
    let stop!: (reason: HostError) => void;
    const stopped = new Promise<never>((_resolve, reject) => { stop = reject; });
    const cancel = () => stop(new HostError("hydra_cancelled", "Hydra evaluation cancelled."));
    signal.addEventListener("abort", cancel, { once: true });

    const distinct = (candidate: number) => candidate > 0 &&
      !webContents.getAllWebContents().some(other => other !== window?.webContents && other.getOSProcessId() === candidate);
    try {
      this.allowedRequests = new Set([bootstrapURL, ...Array.from(PATHS.keys(), path => origin + path)]);
      isolated.protocol.handle(HYDRA_SCHEME, async request => {
        if (!this.allowedRequests.has(request.url)) return new Response(null, { status: 404 });
        const path = PATHS.get(new URL(request.url).pathname);
        const worker = path === "hydra-worker.js";
        const body = path ? await readFile(join(__dirname, path), "utf8") : HTML;
        return new Response(body, { headers: {
          "Content-Type": path ? "text/javascript; charset=utf-8" : "text/html; charset=utf-8",
          "Content-Security-Policy": worker ? WORKER_CSP : PAGE_CSP,
          "Cache-Control": "no-store",
        } });
      });
      protocolInstalled = true;
      window = new BrowserWindow({ show: false, webPreferences: {
        session: isolated, sandbox: true, contextIsolation: true, nodeIntegration: false,
        nodeIntegrationInWorker: false, webviewTag: false, devTools: false, backgroundThrottling: false,
      } });
      const contents = window.webContents;
      contents.setWindowOpenHandler(() => ({ action: "deny" }));
      contents.on("will-frame-navigate", event => event.preventDefault());
      contents.on("will-attach-webview", event => event.preventDefault());

      contents.once("render-process-gone", () => stop(new HostError("hydra_crash", "The Hydra evaluator process stopped.")));

      timer = setTimeout(() => stop(new HostError("hydra_startup", "The Hydra evaluator did not start.")), 5000);
      await Promise.race([window.loadURL(bootstrapURL), stopped]);
      clearTimeout(timer);
      if (signal.aborted) throw new HostError("hydra_cancelled", "Hydra evaluation cancelled.");
      const candidate = contents.getOSProcessId();
      if (!distinct(candidate)) throw new HostError("hydra_isolation", "The Hydra evaluator requires a separate renderer process.");
      pid = candidate;
      timer = setTimeout(() => stop(new HostError("hydra_timeout", "Hydra evaluation timed out after 1 second.")), 1000);
      const raw: unknown = await Promise.race([
        contents.executeJavaScript(`globalThis.evaluateHydra(${JSON.stringify(source)})`), stopped,
      ]);
      clearTimeout(timer);
      if (typeof raw !== "string" || Buffer.byteLength(raw) > HYDRA_EXCHANGE_LIMIT) {
        throw new HostError("hydra_graph", "Hydra evaluator result exceeds 64 KiB.");
      }
      const result = JSON.parse(raw) as { error?: { code?: string; message?: string } };
      if (result?.error) {
        const { code, message } = result.error;
        throw new HostError(code === "hydra_syntax" || code === "hydra_unsupported" ? code : "hydra_evaluation",
          typeof message === "string" ? message.slice(0, 2048) : "The Hydra patch could not be evaluated.");
      }
      return compileHydra(result, source);
    } catch (error) {
      const contents = window?.webContents;
      if (pid && contents && !contents.isDestroyed() && !contents.isCrashed() && contents.getOSProcessId() === pid && distinct(pid)) {
        contents.forcefullyCrashRenderer();
      }
      if (error instanceof HostError) throw error;
      throw new HostError("hydra_evaluation", error instanceof Error ? error.message : "The Hydra evaluator failed.");
    } finally {
      clearTimeout(timer);
      signal.removeEventListener("abort", cancel);
      if (window && !window.isDestroyed()) window.destroy();
      this.allowedRequests.clear();
      if (protocolInstalled) isolated.protocol.unhandle(HYDRA_SCHEME);
      await this.clearEvaluator(isolated, pid);
    }
  }

  private async clearEvaluator(isolated: Electron.Session, pid: number) {
    const cleanup = new AbortController();
    const retired = async () => {
      while (pid && app.getAppMetrics().some(metric => metric.pid === pid)) {
        await delay(20, undefined, { signal: cleanup.signal });
      }
    };
    try {
      await Promise.race([
        Promise.all([isolated.clearStorageData(), retired()]),
        delay(1250, undefined, { signal: cleanup.signal }).then(() => {
          throw new Error("Evaluator teardown exceeded its deadline.");
        }),
      ]);
    } catch {
      this.isolationFault = true;
      throw new HostError("hydra_isolation", "Hydra evaluator teardown could not be confirmed. Restart the Editor.");
    } finally {
      cleanup.abort();
    }
  }
}
