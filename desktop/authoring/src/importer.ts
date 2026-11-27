import { execFile, spawn, type ChildProcess } from "node:child_process";
import { constants } from "node:fs";
import { open } from "node:fs/promises";
import { dirname, join } from "node:path";
import { promisify } from "node:util";
import { SOURCE_LIMIT, type DraftSource, type ImportItem, type StageReport, type SourceView } from "./contracts";
import type { HostClient } from "./host-client";
import { sourceName } from "./draft-project";
import { hash, HostError } from "./local-state";

const MAX_REPORT_BYTES = 96 * 1024 * 1024;
const IMPORT_DEADLINE_MS = 65_000;
type ImportKind = "file" | "paste" | "github" | "repository" | "shadertoy";

interface ImportedSource {
  origin: { path?: string; url?: string; repository?: string; ref?: string; commit?: string };
  status: ImportItem["status"];
  code: string;
  message: string;
  source_sha256?: string;
  entry?: { id: string; generated_sha256: string };
  original_base64?: string;
  generated_base64?: string;
}

interface ImporterReport {
  format: string;
  complete: boolean;
  error: StageReport["error"];
  files: ImportedSource[];
}

function fail(code: string, message: string): StageReport {
  return { complete: false, error: { code, message }, items: [] };
}

function boundedText(value: unknown, bytes: number): value is string {
  return typeof value === "string" && Buffer.byteLength(value, "utf8") <= bytes && !value.includes("\u0000");
}

async function shadertoyKey(): Promise<string | undefined> {
  try {
    const { stdout } = await promisify(execFile)("/usr/bin/security", [
      "find-generic-password", "-s", "com.vosc.authoring.shadertoy", "-a", "api-key", "-w",
    ], { timeout: 5_000, maxBuffer: 1024, env: { PATH: "/usr/bin:/bin" } });
    const key = stdout.trim();
    return boundedText(key, 256) && key ? key : undefined;
  } catch {
    return undefined;
  }
}

class Importer {
  private active: ChildProcess | null = null;
  private cancelled = false;
  private pending = false;
  private files: ImportedSource[] = [];
  private complete = true;
  private issue: StageReport["error"] = null;

  constructor(private readonly resources: string, private readonly library: string,
              private readonly fixture = false) {}

  get busy() { return this.pending; }

  cancel() {
    this.cancelled = this.pending;
    this.active?.kill();
  }

  inspectSource(index: unknown): SourceView | null {
    if (typeof index !== "number" || !Number.isSafeInteger(index)) return null;
    const item = this.files[index];
    if (!item?.original_base64) return null;
    const decode = (base64: string) => Buffer.from(base64, "base64").toString("utf8");
    return {
      original: decode(item.original_base64),
      generated: item.generated_base64 ? decode(item.generated_base64) : null,
    };
  }

  async stage(kind: ImportKind, input: unknown, ref?: unknown): Promise<StageReport> {
    const limit = kind === "paste" ? SOURCE_LIMIT : 4096;
    if (!boundedText(input, limit) || input.length === 0) {
      return fail("invalid_input", kind === "paste" ? "Paste a UTF-8 fragment shader up to 128 KiB." : "Enter a source.");
    }
    if (kind === "repository" && (!boundedText(ref, 250) || !ref.trim())) {
      return fail("invalid_ref", "Enter the branch, tag or commit.");
    }
    if (this.pending) return fail("busy", "An import is already running.");

    this.pending = true;
    this.cancelled = false;
    try {
      const key = kind === "shadertoy" && !this.fixture ? await shadertoyKey() : undefined;
      if (this.cancelled) return fail("cancelled", "Import cancelled.");
      const report = await this.run(kind, input, typeof ref === "string" ? ref.trim() : undefined, key);
      if (this.cancelled) return fail("cancelled", "Import cancelled.");
      if (report.format !== "vosc.shader-import.v1" || !Array.isArray(report.files)) {
        return fail("importer_report", "The importer returned an unreadable report.");
      }
      this.files = report.files;
      this.complete = report.complete;
      this.issue = report.error;
      return this.report();
    } catch (error) {
      if (this.cancelled) return fail("cancelled", "Import cancelled.");
      return fail("importer_unavailable", error instanceof Error ? error.message : "Importer unavailable.");
    } finally {
      this.active = null;
      this.pending = false;
    }
  }

  private itemOrigin(item: ImportedSource) {
    const { commit, path, repository, url } = item.origin;
    return commit ? `${repository} @ ${commit}${path ? ` · ${path}` : ""}` : url ?? "Pasted source";
  }

  private report(issue = this.issue): StageReport {
    return {
      complete: this.complete && issue === null,
      error: issue,
      items: this.files.map((item, index) => ({
        index, path: item.origin.path ?? "Fragment shader", origin: this.itemOrigin(item),
        status: item.status, code: item.code, message: item.message,
        sourceDigest: item.source_sha256, generatedDigest: item.entry?.generated_sha256,
      })),
    };
  }

  async validate(host: HostClient): Promise<StageReport> {
    if (this.pending) throw new HostError("busy", "An import is already running.");
    const session = host.session();
    this.pending = true;
    this.cancelled = false;
    try {
      const accepted: ImportedSource[] = [];
      let issue = this.issue;
      for (const item of this.files) {
        if (this.cancelled) {
          issue = { code: "cancelled", message: "Validation cancelled. Completed candidates can be published." };
          break;
        }
        if (item.status !== "awaiting_validation" || !item.entry || !item.generated_base64) continue;
        try {
          const result = await session.rpc("validate", {
            source: item.generated_base64, sha256: item.entry.generated_sha256,
          }) as { target: string; sourceDigest: string; receipt?: { status: string } };
          if (result?.target !== session.peer.target || result.sourceDigest !== item.entry.generated_sha256 ||
              result.receipt?.status !== "rendered") {
            throw new HostError("missing_native_receipt", "The host returned an unmatched shader receipt.");
          }
          accepted.push(item);
        } catch (error) {
          if (error instanceof HostError && error.code === "shader_failed") {
            item.status = "skipped";
            item.code = error.code;
            item.message = error.message;
            continue;
          }
          issue = { code: error instanceof HostError ? error.code : "validation_unavailable",
            message: error instanceof Error ? error.message : "Native validation is unavailable." };
          break;
        }
      }
      if (accepted.length > 0) {
        const input = accepted.map(item => ({
          origin: item.origin, original: item.original_base64,
          entry_id: item.entry!.id, digest: item.entry!.generated_sha256,
        }));
        const response = await this.python<{ outcomes?: string[]; error?: StageReport["error"] }>(
          "publish_staged.py", [this.library], JSON.stringify(input));
        if (!response.outcomes || response.outcomes.length !== accepted.length) {
          return this.report(response.error ?? { code: "library_failed", message: "Library publication did not complete." });
        }
        for (const [index, item] of accepted.entries()) {
          const state = response.outcomes[index];
          if (state === "published" || state === "reused" || state === "reused_edited") {
            item.status = state;
            item.code = "native_linked";
            item.message = state === "reused_edited" ?
              "Native validation passed. Your edited library copy was preserved." : "Native validation passed.";
          } else {
            item.status = "skipped";
            item.code = "library_collision";
            item.message = "An existing library path is in the way and was preserved.";
          }
        }
      }
      return this.report(issue);
    } finally {
      this.pending = false;
      this.active = null;
    }
  }

  async activateSource(index: unknown): Promise<DraftSource> {
    const item = typeof index === "number" && Number.isSafeInteger(index) ? this.files[index] : undefined;
    if (!item?.entry || !/^[a-f0-9]{64}$/.test(item.entry.id) ||
        !["published", "reused", "reused_edited"].includes(item.status)) {
      throw new HostError("source_not_validated", "Validate this shader before opening a native preview.");
    }
    const file = await open(join(this.library, "shaders", item.entry.id, "shader.frag"),
                            constants.O_RDONLY | constants.O_NOFOLLOW);
    try {
      const info = await file.stat();
      if (!info.isFile() || info.size < 1 || info.size > SOURCE_LIMIT) {
        throw new HostError("source_limit", "The library shader must be a regular file up to 128 KiB.");
      }
      const buffer = Buffer.alloc(info.size + 1);
      const { bytesRead } = await file.read(buffer, 0, buffer.length, 0);
      if (bytesRead !== info.size) throw new HostError("source_changed", "The library shader changed. Retry opening it.");
      const source = buffer.subarray(0, info.size);
      return { name: sourceName(item.origin.path), origin: this.itemOrigin(item),
        digest: hash(source), base64: source.toString("base64") };
    } finally {
      await file.close();
    }
  }

  private run(kind: ImportKind, input: string, ref?: string, key?: string): Promise<ImporterReport> {
    if (kind === "shadertoy" && this.fixture) return this.python("stage_shadertoy_fixture.py", [input]);
    const args = ["--stage-only", ...(ref !== undefined ? ["--ref", ref] : []), kind];
    if (kind !== "paste") args.push("--", input);
    return this.python("import_shader.py", args, kind === "paste" ? input : undefined, key);
  }

  private python<Result>(scriptName: string, parameters: string[], input?: string, key?: string): Promise<Result> {
    const script = join(this.resources, "importer", "scripts", "authoring", scriptName);
    const python = join(this.resources, "python", "bin", "python3.13");
    const args = ["-I", "-B", script, ...parameters];
    const env: NodeJS.ProcessEnv = { PATH: "/usr/bin:/bin", LANG: "en_US.UTF-8" };
    if (key) env.VOSC_SHADERTOY_API_KEY = key;

    return new Promise((resolve, reject) => {
      const child = spawn(python, args, { cwd: dirname(script), env, stdio: ["pipe", "pipe", "ignore"] });
      this.active = child;
      const chunks: Buffer[] = [];
      let bytes = 0;
      let failure: Error | null = null;
      const timer = setTimeout(() => {
        failure = new Error("Import timed out. Retry the source.");
        child.kill();
      }, IMPORT_DEADLINE_MS);
      child.stdout.on("data", (chunk: Buffer) => {
        bytes += chunk.length;
        if (bytes > MAX_REPORT_BYTES) {
          failure = new Error("The import result exceeded its size limit.");
          child.kill();
        } else {
          chunks.push(chunk);
        }
      });
      child.stdin.on("error", () => { /* Exit status and output describe the result. */ });
      child.stdin.end(input, "utf8");
      child.once("error", () => {
        clearTimeout(timer);
        reject(new Error("The bundled Python importer could not start."));
      });
      child.once("close", (code) => {
        clearTimeout(timer);
        if (failure) {
          reject(failure);
          return;
        }
        if (code !== 0 && code !== 1) {
          reject(new Error("The importer ended before returning a result."));
          return;
        }
        try {
          resolve(JSON.parse(Buffer.concat(chunks).toString("utf8")) as Result);
        } catch {
          reject(new Error("The importer returned an unreadable report."));
        }
      });
    });
  }
}

export { Importer };
