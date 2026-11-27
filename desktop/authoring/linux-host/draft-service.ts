import type { Draft } from "./draft";
import { requireFields, WireError, type WireRequest } from "./transport";

type Row = Record<string, unknown>;
type DraftState = Pick<Draft, "execute" | "fence" | "revoke" | "recoverMutation" | "completedMutation" | "reset" | "onReset">;
type Outcome = Exclude<ReturnType<Draft["completedMutation"]>, undefined>;
type DrawKind = "run" | "controls";
type Operation = {
  id: string;
  key: string;
  peer: string;
  bytes: string;
  epoch: number;
  revoked: boolean;
};
type Uncertain = Operation & { draw: DrawKind };
type Recorded = { peer: string; bytes: string; outcome: Outcome };
type Lease = { peer: string; id: string };

const mutations = new Set(["run.prepare", "run.commit", "run.cancel", "run.fence", "slot.configure", "uniform.set"]);
const explicitLease = new Set(["run.commit", "run.cancel", "run.fence", "sample"]);
const controlled = new Set([...explicitLease, "slot.configure", "uniform.set"]);

function drawKind(method: string): DrawKind | undefined {
  if (method === "run.commit") return "run";
  if (method === "slot.configure" || method === "uniform.set") return "controls";
}

function stableJSON(value: unknown): string {
  if (Array.isArray(value)) return "[" + value.map(stableJSON).join(",") + "]";
  if (value && typeof value === "object") {
    const row = value as Row;
    return "{" + Object.keys(row).filter(key => row[key] !== undefined).sort()
      .map(key => JSON.stringify(key) + ":" + stableJSON(row[key])).join(",") + "}";
  }
  if (typeof value === "number" && !Number.isFinite(value)) {
    throw new WireError("invalid_request", "Malformed draft request");
  }
  return JSON.stringify(value);
}

function wireError(error: unknown, fallback: string): WireError {
  return error instanceof WireError ? error : new WireError("host_error", fallback);
}

function response(id: string, outcome: Outcome): Row {
  if (outcome.ok) return { id, ok: true, result: outcome.result };
  const error = wireError(outcome.error, "Could not recover the native draw");
  return { id, ok: false, error: { code: error.code, message: error.message,
    ...(error.diagnostics !== undefined && error.diagnostics !== null ? { diagnostics: error.diagnostics } : {}) } };
}

function result(outcome: Outcome) {
  if (!outcome.ok) throw outcome.error;
  return outcome.result;
}

/** Owns admission and terminal receipts for one paired native preview. The transport owns peer authentication. */
export class DraftService {
  private epoch = 0;
  private normal?: Operation;
  private fences = 0;
  private fenceQueue: Promise<unknown> = Promise.resolve();
  private lease?: Lease;
  private revokedLease?: Lease;
  private uncertain?: Uncertain;
  private inFlight = new Map<string, Operation>();
  private receipts = new Map<string, Recorded>();
  private lastDraws = new Map<DrawKind, Recorded & { key: string }>();

  constructor(private readonly target: string, private readonly draft: DraftState) {
    draft.onReset = () => {
      this.epoch++;
      this.lease = undefined;
      this.revokedLease = undefined;
      this.uncertain = undefined;
      this.inFlight.clear();
      this.receipts.clear();
      this.lastDraws.clear();
    };
  }

  reset(): Promise<void> {
    return this.draft.reset();
  }

  async revoke(peer: string): Promise<void> {
    if (this.normal?.peer === peer) this.normal.revoked = true;
    for (const [key, operation] of this.inFlight) {
      if (operation.peer === peer) {
        operation.revoked = true;
        this.inFlight.delete(key);
      }
    }
    if (this.uncertain?.peer === peer) this.uncertain.revoked = true;
    for (const [key, record] of this.receipts) {
      if (record.peer === peer) this.receipts.delete(key);
    }
    for (const [draw, record] of this.lastDraws) {
      if (record.peer === peer) this.lastDraws.delete(draw);
    }
    const lease = this.lease;
    if (lease?.peer !== peer || this.revokedLease === lease) return;
    this.revokedLease = lease;
    await this.enqueueFence(async () => {
      if (this.lease !== lease || this.revokedLease !== lease) return;
      await this.draft.revoke(lease.id);
      this.releaseLease(lease);
    });
  }

  async dispatch(request: WireRequest, peer: string): Promise<Row> {
    if (request.target !== this.target) {
      throw new WireError("wrong_target", "The selected service identity does not match this render host");
    }
    if (request.method === "receipt.get") return this.receipt(request, peer);

    const key = peer + ":" + request.id;
    const bytes = stableJSON(request);
    const retained = this.recorded(key);
    if (retained) {
      this.match(retained.bytes, bytes);
      return result(retained.outcome);
    }
    const pending = this.inFlight.get(key);
    if (pending) {
      this.match(pending.bytes, bytes);
      throw new WireError("busy", "The same request is already in flight. Check its receipt");
    }
    if (this.uncertain?.key === key) {
      this.match(this.uncertain.bytes, bytes);
      throw new WireError("renderer_uncertain", "Check the receipt for the earlier request before retrying");
    }

    const isFence = request.method === "run.fence";
    if (this.fences || (!isFence && this.normal)) {
      throw new WireError("busy", "A private renderer operation is already in flight");
    }
    const mutation = mutations.has(request.method);
    const affectsPreview = mutation || request.method === "sample";
    if (this.revokedLease && affectsPreview) {
      throw new WireError("revoked_lease_unconfirmed", "Restart the draft service before publishing or relaying samples");
    }
    if (this.uncertain && !isFence && affectsPreview) {
      throw new WireError("prior_draw_unconfirmed", "Verify the earlier native receipt before publishing or relaying samples");
    }

    const initial = this.lease;
    const lease = this.admitLease(request, peer);
    const operation = { id: request.id, key, peer, bytes, epoch: this.epoch, revoked: false };
    const draw = drawKind(request.method);
    const previous = isFence ? undefined : this.uncertain;
    if (mutation) this.inFlight.set(key, operation);
    if (!isFence) this.normal = operation;
    let outcome: Outcome;
    try {
      const value = isFence
        ? await this.enqueueFence(() => this.draft.fence(request))
        : await this.draft.execute(request);
      outcome = { ok: true, result: value };
    } catch (error) {
      outcome = { ok: false, error: wireError(error, "Could not complete the private draft operation") };
    } finally {
      if (this.normal === operation) this.normal = undefined;
      if (this.inFlight.get(key) === operation) this.inFlight.delete(key);
    }

    if (operation.epoch !== this.epoch) {
      if (outcome.ok && (request.method === "health" || request.method === "reflect") && !operation.revoked) {
        return outcome.result;
      }
      if (!outcome.ok && outcome.error instanceof WireError && outcome.error.code === "draft_state_lost") {
        throw outcome.error;
      }
      throw new WireError("draft_state_lost", "The draft renderer changed before confirming the operation");
    }
    if (previous && this.uncertain === previous) {
      const recovered = this.draft.completedMutation(previous.id);
      if (recovered) this.retire(previous, recovered);
    }
    if (lease && (isFence && outcome.ok ||
        request.method === "run.prepare" && !outcome.ok && !initial && this.revokedLease !== lease)) {
      this.releaseLease(lease);
    }
    const uncertain = !outcome.ok && outcome.error instanceof WireError && outcome.error.code === "renderer_uncertain";
    if (uncertain && draw) this.uncertain = { ...operation, draw };
    if (operation.revoked) throw new WireError("unpaired", "Draft operations require an approved client certificate in TLS");
    if (mutation && !uncertain) this.remember(operation, outcome, draw);
    return result(outcome);
  }

  private async receipt(request: WireRequest, peer: string): Promise<Row> {
    requireFields(request, ["id"]);
    const id = request.params.id;
    if (typeof id !== "string" || Buffer.byteLength(id) > 64) {
      throw new WireError("invalid_request", "A bounded request ID is required");
    }
    const key = peer + ":" + id;
    const retained = this.recorded(key);
    if (retained) return { state: "completed", response: response(id, retained.outcome) };
    const uncertain = this.uncertain;
    if (this.inFlight.has(key)) return { state: "pending" };
    if (uncertain?.key !== key || uncertain.revoked) return { state: "unknown" };
    if (this.normal) return { state: "pending" };

    const operation = { ...uncertain };
    this.normal = operation;
    let outcome: Outcome | "pending" | "unknown";
    try {
      const recovered = await this.draft.recoverMutation(id);
      outcome = recovered === undefined ? "unknown" : { ok: true, result: recovered };
    } catch (error) {
      outcome = error instanceof WireError && error.code === "renderer_uncertain" ? "pending" :
        { ok: false, error: wireError(error, "Could not recover the native draw") };
    } finally {
      if (this.normal === operation) this.normal = undefined;
    }
    if (operation.epoch !== this.epoch) return { state: "unknown" };
    if (typeof outcome !== "string") this.retire(uncertain, outcome);
    if (operation.revoked) throw new WireError("unpaired", "Draft operations require an approved client certificate in TLS");
    return typeof outcome === "string" ? { state: outcome } : { state: "completed", response: response(id, outcome) };
  }

  private admitLease(request: WireRequest, peer: string): Lease | undefined {
    const id = request.params.lease;
    if (request.method === "run.prepare") {
      if (typeof id !== "string" || !/^[\da-f]{8}(?:-[\da-f]{4}){3}-[\da-f]{12}$/i.test(id)) {
        throw new WireError("invalid_request", "A preview lease is required");
      }
      if (this.lease && (this.lease.peer !== peer || this.lease.id !== id)) {
        throw new WireError("lease_owned", "The current preview lease must be fenced before switching drafts");
      }
      return this.lease ??= { peer, id };
    }
    if (controlled.has(request.method) && (this.lease?.peer !== peer ||
        (explicitLease.has(request.method) && this.lease.id !== id))) {
      throw new WireError("stale_lease", "This paired controller does not own the requested preview lease");
    }
    return this.lease;
  }

  private releaseLease(lease: Lease) {
    if (this.lease !== lease) return;
    this.lease = undefined;
    if (this.revokedLease === lease) this.revokedLease = undefined;
  }

  private async enqueueFence<T>(work: () => Promise<T>): Promise<T> {
    this.fences++;
    const operation = this.fenceQueue.then(work);
    this.fenceQueue = operation.catch(() => {});
    try {
      return await operation;
    } finally {
      this.fences--;
    }
  }

  private match(stored: string, incoming: string) {
    if (stored !== incoming) throw new WireError("id_conflict", "Request ID has different content");
  }

  private recorded(key: string) {
    return this.receipts.get(key) ?? [...this.lastDraws.values()].find(record => record.key === key);
  }

  private remember(operation: Operation, outcome: Outcome, draw?: DrawKind) {
    const record = { peer: operation.peer, bytes: operation.bytes, outcome };
    const key = operation.key;
    this.receipts.delete(key);
    this.receipts.set(key, record);
    if (this.receipts.size > 64) this.receipts.delete(this.receipts.keys().next().value!);
    if (draw && outcome.ok) this.lastDraws.set(draw, { key, ...record });
  }

  private retire(uncertain: Uncertain, outcome: Outcome) {
    if (this.uncertain !== uncertain) return;
    this.uncertain = undefined;
    if (!uncertain.revoked) this.remember(uncertain, outcome, uncertain.draw);
  }
}
