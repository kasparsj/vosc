import { execFile, spawn, type ChildProcess } from "node:child_process";
import { randomUUID } from "node:crypto";
import { constants } from "node:fs";
import { lstat, open } from "node:fs/promises";
import { dirname, join } from "node:path";
import { promisify } from "node:util";
import { SOURCE_LIMIT, type DraftSource, type ImportItem, type LibraryItem, type StageReport, type SourceView } from "./contracts";
import type { HostClient } from "./host-client";
import { sourceName } from "./draft-project";
import { hash, HostError } from "./local-state";

const MAX_REPORT_BYTES = 96 * 1024 * 1024;
const IMPORT_DEADLINE_MS = 65_000;
type ImportKind = "file" | "paste" | "github" | "repository" | "shadertoy";

interface Origin {
  provider?: string;
  path?: string;
  url?: string;
  repository?: string;
  ref?: string;
  commit?: string;
}

interface ImportedSource {
  origin: Origin;
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

interface StagedBatch {
  id: string;
  items: { id: string; file: ImportedSource }[];
  complete: boolean;
  issue: StageReport["error"];
}

interface LibraryRecord {
  id: string;
  source: Origin;
  source_sha256: string;
  generated_sha256: string;
}

const SHA256 = /^[a-f0-9]{64}$/;

function fail(code: string, message: string): StageReport {
  return { batchId: null, complete: false, error: { code, message }, items: [] };
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
  private publishing = false;
  private batch: StagedBatch | null = null;

  constructor(private readonly resources: string, private readonly library: string,
              private readonly fixture = false) {}

  get busy() { return this.pending; }

  cancel() {
    if (!this.pending || this.publishing) return;
    this.cancelled = true;
    this.active?.kill();
  }

  private stagedBatch(id: unknown): StagedBatch {
    if (!this.batch || this.batch.id !== id) {
      throw new HostError("stale_import", "The import selection is no longer current. Review the latest import.");
    }
    return this.batch;
  }

  private stagedFile(batchId: unknown, itemId: unknown): ImportedSource {
    const item = this.stagedBatch(batchId).items.find(row => row.id === itemId);
    if (!item) throw new HostError("invalid_selection", "The selected shader is missing from this import.");
    return item.file;
  }

  inspectSource(batchId: unknown, itemId: unknown): SourceView | null {
    const item = this.stagedFile(batchId, itemId);
    if (!item.original_base64) return null;
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
    this.batch = null;
    try {
      const key = kind === "shadertoy" && !this.fixture ? await shadertoyKey() : undefined;
      if (this.cancelled) return fail("cancelled", "Import cancelled.");
      const report = await this.run(kind, input, typeof ref === "string" ? ref.trim() : undefined, key);
      if (this.cancelled) return fail("cancelled", "Import cancelled.");
      if (report.format !== "vosc.shader-import.v1" || !Array.isArray(report.files)) {
        return fail("importer_report", "The importer returned an unreadable report.");
      }
      this.batch = {
        id: randomUUID(), items: report.files.map(file => ({ id: randomUUID(), file })),
        complete: report.complete, issue: report.error,
      };
      return this.report(this.batch);
    } catch (error) {
      if (this.cancelled) return fail("cancelled", "Import cancelled.");
      return fail("importer_unavailable", error instanceof Error ? error.message : "Importer unavailable.");
    } finally {
      this.active = null;
      this.pending = false;
    }
  }

  private originLabel(origin: Origin) {
    const { commit, path, repository, url } = origin;
    return commit ? `${repository} @ ${commit}${path ? ` · ${path}` : ""}` : url ?? "Pasted source";
  }

  private report(batch: StagedBatch, issue = batch.issue): StageReport {
    return {
      batchId: batch.id,
      complete: batch.complete && issue === null,
      error: issue,
      items: batch.items.map(({ id, file }) => ({
        id, path: file.origin.path ?? "Fragment shader", origin: this.originLabel(file.origin),
        status: file.status, code: file.code, message: file.message,
        sourceDigest: file.source_sha256, generatedDigest: file.entry?.generated_sha256,
        libraryId: ["published", "reused", "reused_edited"].includes(file.status) ? file.entry?.id : undefined,
      })),
    };
  }

  async validate(host: HostClient, batchId: unknown, itemIds: unknown): Promise<StageReport> {
    if (this.pending) throw new HostError("busy", "An import is already running.");
    const batch = this.stagedBatch(batchId);
    if (!Array.isArray(itemIds) || !itemIds.length || itemIds.length > batch.items.length) {
      throw new HostError("invalid_selection", "Select files to validate and add.");
    }
    const selected = new Set(itemIds);
    const rows = batch.items.filter(({ id }) => selected.has(id));
    if (rows.length !== itemIds.length || rows.some(({ file }) => !file.generated_base64)) {
      throw new HostError("invalid_selection", "Only converted files from the current import can be selected.");
    }
    const candidates = rows.map(row => row.file).filter(file =>
      file.status === "awaiting_validation" || file.status === "validation_failed");
    if (!candidates.length) return this.report(batch);
    const session = host.session();
    this.pending = true;
    this.cancelled = false;
    try {
      const accepted: ImportedSource[] = [];
      let issue = batch.issue;
      for (const item of candidates) {
        if (this.cancelled) break;
        if (!item.entry || !item.generated_base64) continue;
        try {
          const result = await session.rpc("validate", {
            source: item.generated_base64, sha256: item.entry.generated_sha256,
          }) as { target: string; sourceDigest: string; receipt?: { status: string } };
          if (this.cancelled) break;
          if (result?.target !== session.peer.target || result.sourceDigest !== item.entry.generated_sha256 ||
              result.receipt?.status !== "rendered") {
            throw new HostError("missing_native_receipt", "The host returned an unmatched shader receipt.");
          }
          accepted.push(item);
        } catch (error) {
          if (this.cancelled) break;
          if (error instanceof HostError && error.code === "shader_failed") {
            item.status = "validation_failed";
            item.code = error.code;
            item.message = error.message;
            continue;
          }
          issue = { code: error instanceof HostError ? error.code : "validation_unavailable",
            message: error instanceof Error ? error.message : "Native validation is unavailable." };
          break;
        }
      }
      if (this.cancelled) {
        return this.report(batch, { code: "cancelled", message: "Validation cancelled before adding any files." });
      }
      if (issue?.code === "host_changed") return this.report(batch, issue);
      if (accepted.length > 0) {
        const input = accepted.map(item => ({
          origin: item.origin, original: item.original_base64,
          entry_id: item.entry!.id, digest: item.entry!.generated_sha256,
        }));
        this.publishing = true;
        const response = await this.python<{ outcomes?: string[]; error?: StageReport["error"] }>(
          "publish_staged.py", [this.library], JSON.stringify(input));
        if (!response.outcomes || response.outcomes.length !== accepted.length) {
          return this.report(batch, response.error ?? { code: "library_failed", message: "Library publication did not complete." });
        }
        for (const [index, item] of accepted.entries()) {
          const state = response.outcomes[index];
          if (state === "published" || state === "reused" || state === "reused_edited") {
            item.status = state;
            item.code = "native_linked";
            item.message = state === "reused_edited" ?
              "Native validation passed. Your edited library copy was preserved." : "Native validation passed.";
          } else {
            item.status = "validation_failed";
            item.code = "library_collision";
            item.message = "An existing library path is in the way and was preserved.";
          }
        }
      }
      return this.report(batch, issue);
    } finally {
      this.pending = false;
      this.publishing = false;
      this.active = null;
    }
  }

  openStaged(batchId: unknown, itemId: unknown): DraftSource {
    const item = this.stagedFile(batchId, itemId);
    if (!item.generated_base64 || !item.entry) {
      throw new HostError("source_unavailable", "Select a fragment that was converted successfully.");
    }
    const converted = Buffer.from(item.generated_base64, "base64");
    if (converted.toString("base64") !== item.generated_base64 || hash(converted) !== item.entry.generated_sha256) {
      throw new HostError("source_changed", "The staged conversion no longer matches its recorded digest.");
    }
    return this.draftSource(item.origin, item.source_sha256, converted, converted);
  }

  private draftSource(origin: Origin, originalDigest: string | undefined,
                      working: Buffer, converted: Buffer): DraftSource {
    const digest = hash(working);
    const convertedDigest = hash(converted);
    if (working.length > SOURCE_LIMIT || converted.length > SOURCE_LIMIT || working.includes(0) || converted.includes(0)) {
      throw new HostError("invalid_source", "The source must be UTF-8 GLSL up to 128 KiB without NUL bytes.");
    }
    try {
      const decoder = new TextDecoder("utf-8", { fatal: true });
      decoder.decode(working);
      decoder.decode(converted);
    } catch {
      throw new HostError("invalid_source", "The source contains invalid UTF-8.");
    }
    return {
      name: sourceName(origin.path), origin: this.originLabel(origin), digest, base64: working.toString("base64"),
      provenance: {
        provider: origin.provider ?? "import", path: origin.path, url: origin.url, repository: origin.repository,
        requestedRef: origin.ref, commit: origin.commit, originalDigest, convertedDigest,
        convertedBase64: converted.toString("base64"),
      },
    };
  }

  private async catalog(): Promise<Record<string, LibraryRecord>> {
    const path = join(this.library, "catalog.json");
    try {
      const bytes = await this.readFile(path, MAX_REPORT_BYTES);
      const catalog = JSON.parse(new TextDecoder("utf-8", { fatal: true }).decode(bytes));
      if (catalog?.format !== "vosc.shader-library.v1" || !catalog.entries ||
          typeof catalog.entries !== "object" || Array.isArray(catalog.entries)) {
        throw new Error("Invalid catalog");
      }
      for (const [id, fields] of Object.entries(catalog.entries)) {
        const entry = fields as Partial<LibraryRecord> | null;
        if (!SHA256.test(id) || entry?.id !== id || !entry.source || typeof entry.source.provider !== "string" ||
            [entry.source.path, entry.source.url, entry.source.repository, entry.source.ref, entry.source.commit]
              .some(value => value !== undefined && typeof value !== "string") ||
            typeof entry.source_sha256 !== "string" || !SHA256.test(entry.source_sha256) ||
            typeof entry.generated_sha256 !== "string" || !SHA256.test(entry.generated_sha256)) {
          throw new Error("Invalid library entry");
        }
      }
      return catalog.entries;
    } catch (error) {
      if ((error as NodeJS.ErrnoException).code === "ENOENT") return {};
      throw new HostError("invalid_catalog", "The saved shader library could not be read.");
    }
  }

  async listLibrary(): Promise<LibraryItem[]> {
    const entries = Object.values(await this.catalog());
    return entries.map(entry => ({
      id: entry.id, path: entry.source.path ?? "Fragment shader", origin: this.originLabel(entry.source),
    })).sort((first, second) => first.path.localeCompare(second.path) || first.id.localeCompare(second.id));
  }

  async openLibrary(id: unknown): Promise<DraftSource> {
    if (typeof id !== "string" || !SHA256.test(id)) {
      throw new HostError("invalid_selection", "Choose a saved library shader.");
    }
    const entry = (await this.catalog())[id];
    if (!entry) throw new HostError("source_unavailable", "The shader is no longer in the library.");
    const shaders = join(this.library, "shaders");
    const folder = join(shaders, id);
    if (!(await lstat(shaders)).isDirectory() || !(await lstat(folder)).isDirectory()) {
      throw new HostError("invalid_source", "The library shader folder is not a regular directory.");
    }
    const converted = await this.readFile(join(folder, "generated.frag"), SOURCE_LIMIT);
    if (hash(converted) !== entry.generated_sha256) {
      throw new HostError("source_changed", "The generated library shader does not match its recorded digest.");
    }
    const working = await this.readFile(join(folder, "shader.frag"), SOURCE_LIMIT);
    return this.draftSource(entry.source, entry.source_sha256, working, converted);
  }

  private async readFile(path: string, limit: number): Promise<Buffer> {
    const file = await open(path, constants.O_RDONLY | constants.O_NOFOLLOW | constants.O_NONBLOCK);
    try {
      const before = await file.stat({ bigint: true });
      if (!before.isFile() || before.size > limit) {
        throw new HostError("source_limit", "The library file is not a regular file or exceeds its size limit.");
      }
      const buffer = Buffer.alloc(Number(before.size) + 1);
      const { bytesRead } = await file.read(buffer, 0, buffer.length, 0);
      const after = await file.stat({ bigint: true });
      if (bytesRead !== Number(before.size) || after.size !== before.size ||
          after.mtimeNs !== before.mtimeNs || after.ctimeNs !== before.ctimeNs) {
        throw new HostError("source_changed", "The library file changed. Retry opening it.");
      }
      return buffer.subarray(0, bytesRead);
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
