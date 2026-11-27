import { useEffect, useRef, useState, type FormEvent } from "react";
import type { ImportItem, SourceView, StageReport } from "./contracts";
import { message } from "./host-pane";

const METHODS = [
  ["file", "Local file"], ["paste", "Paste GLSL"], ["github", "GitHub file"],
  ["repository", "GitHub repository"], ["shadertoy", "Shadertoy URL"],
] as const;

const labels: Record<ImportItem["status"], string> = {
  awaiting_validation: "Awaiting native validation",
  validation_failed: "Native validation failed",
  skipped: "Skipped",
  published: "Added",
  reused: "Already in library",
  reused_edited: "Local edits preserved",
};

export function ImportPane({ connected, openSource, onPublished, onRunning }: {
  connected: boolean;
  openSource: (batchId: string, itemId: string) => Promise<void>;
  onPublished: () => void;
  onRunning: (running: boolean) => void;
}) {
  const api = window.voscAuthoring;
  const [method, setMethod] = useState<typeof METHODS[number][0]>("file");
  const [source, setSource] = useState("");
  const [ref, setRef] = useState("");
  const [busy, setBusy] = useState(false);
  const running = useRef(false);
  const [report, setReport] = useState<StageReport | null>(null);
  const [problem, setProblem] = useState("");
  const [selected, setSelected] = useState<string[]>([]);
  const [opening, setOpening] = useState("");
  const [inspection, setInspection] = useState<{ item: ImportItem; view: SourceView | null } | null>(null);
  const [revision, setRevision] = useState<"original" | "generated">("original");
  const inspectionSequence = useRef(0);
  const heading = useRef<HTMLHeadingElement>(null);
  const [reviewSequence, setReviewSequence] = useState(0);
  const reportForReview = report?.batchId ? report : null;

  useEffect(() => {
    if (reviewSequence) heading.current?.focus();
  }, [reviewSequence]);

  async function act(action: () => Promise<void>) {
    if (running.current) return;
    running.current = true;
    onRunning(true);
    setBusy(true);
    setProblem("");
    try { await action(); } catch (error) { setProblem(message(error)); }
    finally { running.current = false; onRunning(false); setBusy(false); }
  }

  function stage(event: FormEvent) {
    event.preventDefault();
    void act(async () => {
      let next: StageReport | null;
      switch (method) {
        case "file": next = await api.stageFile(); break;
        case "paste": next = await api.stagePaste(source); break;
        case "github": next = await api.stageGitHubFile(source.trim()); break;
        case "repository": next = await api.stageRepository(source.trim(), ref); break;
        case "shadertoy": next = await api.stageShadertoy(source.trim()); break;
      }
      if (!next) return;
      inspectionSequence.current += 1;
      setInspection(null);
      setReport(next);
      const choices = next.items.filter(row => row.status !== "skipped").map(row => row.id);
      setSelected(choices);
      setOpening(choices[0] ?? "");
      if (next.batchId) setReviewSequence(value => value + 1);
    });
  }

  async function inspect(batch: string, item: ImportItem) {
    const sequence = ++inspectionSequence.current;
    try {
      const view = await api.inspectSource(batch, item.id);
      if (sequence !== inspectionSequence.current) return;
      setRevision("original");
      setInspection({ item, view });
    } catch (error) {
      if (sequence === inspectionSequence.current) setProblem(message(error));
    }
  }

  function toggle(id: string, checked: boolean) {
    const next = checked ? [...selected, id] : selected.filter(item => item !== id);
    setSelected(next);
    if (!next.includes(opening)) setOpening(next[0] ?? "");
  }

  const reviewRows = reportForReview?.items ?? [];
  const pending = reviewRows.filter(row => selected.includes(row.id) &&
    (row.status === "awaiting_validation" || row.status === "validation_failed")).map(row => row.id);
  const openAvailable = Boolean(opening && selected.includes(opening));

  return <div className="import-pane">
    <form onSubmit={stage} className="import-form">
      <label htmlFor="method">Import from</label>
      <select id="method" disabled={busy} value={method} onChange={event => {
        setMethod(event.target.value as typeof method);
        setSource("");
      }}>
        {METHODS.map(([key, title]) => <option value={key} key={key}>{title}</option>)}
      </select>
      {method === "paste" && <>
        <label htmlFor="import-source">Fragment source</label>
        <textarea id="import-source" value={source} onChange={event => setSource(event.target.value)}
          disabled={busy} required rows={7} spellCheck={false} />
      </>}
      {method !== "file" && method !== "paste" && <>
        <label htmlFor="import-source">Source URL</label>
        <input id="import-source" type="url" value={source} onChange={event => setSource(event.target.value)}
          disabled={busy} required spellCheck={false} />
      </>}
      {method === "repository" && <>
        <label htmlFor="git-ref">Branch, tag or commit</label>
        <input id="git-ref" value={ref} onChange={event => setRef(event.target.value)} disabled={busy}
          required maxLength={250} autoComplete="off" spellCheck={false} />
      </>}
      <div className="actions">
        <button className="primary" disabled={busy}>
          {busy ? "Working…" : method === "file" ? "Choose file…" : "Stage source"}
        </button>
        {busy && <button type="button" onClick={() => void api.cancelImport().catch(error => setProblem(message(error)))}>
          Cancel operation
        </button>}
      </div>
    </form>
    {problem && <p role="alert" className="error">{problem}</p>}
    {report?.error && <p role="status" className="notice">{report.error.message}</p>}
    {reportForReview?.batchId && <section className="import-review" aria-labelledby="import-review-heading">
      <h3 id="import-review-heading" ref={heading} tabIndex={-1}>Import review</h3>
      <p className="muted" role="status">{selected.length} selected · {reviewRows.filter(row => row.status === "skipped").length} skipped</p>
      <ul className="review-list">
        {reviewRows.map(row => <li key={row.id} className="review-row">
          <label className="review-choice">
            {row.status !== "skipped" && <input type="checkbox" checked={selected.includes(row.id)} disabled={busy}
              onChange={event => toggle(row.id, event.target.checked)} />}
            <strong>{row.path}</strong>
          </label>
          <p className="item-status">{labels[row.status]}</p>
          <p className="muted">{row.message}</p>
          <button type="button" className="quiet" aria-label={`Inspect ${row.path}`} disabled={busy}
            onClick={() => void inspect(reportForReview.batchId!, row)}>Inspect</button>
        </li>)}
      </ul>
      {!!selected.length && <div>
        <label htmlFor="open-import-source">File to open</label>
        <select id="open-import-source" value={opening} disabled={busy} onChange={event => setOpening(event.target.value)}>
          {reviewRows.filter(row => selected.includes(row.id)).map(row => <option key={row.id} value={row.id}>{row.path}</option>)}
        </select>
      </div>}
      {inspection && <div className="details">
        <h3>{inspection.item.path}</h3>
        <p className="origin">{inspection.item.origin}</p>
        {inspection.item.sourceDigest && <p className="origin">Original SHA-256 {inspection.item.sourceDigest}</p>}
        {inspection.item.generatedDigest && <p className="origin">Converted SHA-256 {inspection.item.generatedDigest}</p>}
        {inspection.view && <>
          <div className="revision">
            <button type="button" aria-pressed={revision === "original"} onClick={() => setRevision("original")}>Original</button>
            <button type="button" aria-pressed={revision === "generated"} disabled={!inspection.view.generated}
              onClick={() => setRevision("generated")}>Converted</button>
          </div>
          <pre><code>{inspection.view[revision]}</code></pre>
        </>}
      </div>}
      <div className="actions import-footer">
        <button type="button" disabled={!connected || !pending.length || busy}
          onClick={() => void act(async () => {
            const next = await api.validateImports(reportForReview.batchId!, pending);
            setReport(next);
            onPublished();
          })}>Validate and add selected</button>
        <button type="button" className="primary" disabled={!openAvailable || busy}
          onClick={() => void act(() => openSource(reportForReview.batchId!, opening))}>Open selected source</button>
      </div>
      {!connected && <p className="muted">Connect a render host to validate and add files to the library.</p>}
    </section>}
  </div>;
}
