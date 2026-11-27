import { useEffect, useState, type FormEvent } from "react";
import { createRoot } from "react-dom/client";
import type { AuthoringAPI, Discovery, ImportItem, SourceView, StageReport, WorkspaceView } from "./contracts";
import { DraftPane } from "./draft-pane";
import { HostPane, message } from "./host-pane";

declare global {
  interface Window { voscAuthoring: AuthoringAPI }
}

const METHODS = [
  ["file", "Local file"],
  ["paste", "Paste GLSL"],
  ["github", "GitHub file"],
  ["repository", "GitHub repository"],
  ["shadertoy", "Shadertoy URL"],
] as const;
type Method = typeof METHODS[number][0];

function sourceStatus(item: ImportItem) {
  switch (item.status) {
    case "awaiting_validation": return "Awaiting validation";
    case "skipped": return "Skipped";
    case "reused_edited": return "Validated · Edits retained";
    default: return "Validated";
  }
}

function ImportPane({ connected, showDraft, hidden }: {
  connected: boolean;
  showDraft: (value: WorkspaceView) => void;
  hidden: boolean;
}) {
  const [method, setMethod] = useState<Method>("file");
  const [source, setSource] = useState("");
  const [ref, setRef] = useState("");
  const [busy, setBusy] = useState(false);
  const [report, setReport] = useState<StageReport | null>(null);
  const [problem, setProblem] = useState("");
  const [selected, setSelected] = useState<number | null>(null);
  const [view, setView] = useState<SourceView | null>(null);
  const [revision, setRevision] = useState<"original" | "generated">("original");
  const api = window.voscAuthoring;

  async function stage(event: FormEvent) {
    event.preventDefault();
    if (busy) return;
    setBusy(true);
    setProblem("");
    try {
      let next: StageReport | null;
      switch (method) {
        case "file": next = await api.stageFile(); break;
        case "paste": next = await api.stagePaste(source); break;
        case "github": next = await api.stageGitHubFile(source.trim()); break;
        case "repository": next = await api.stageRepository(source.trim(), ref); break;
        case "shadertoy": next = await api.stageShadertoy(source.trim()); break;
      }
      if (!next) return;
      setSelected(null);
      setView(null);
      setReport(next);
    } catch {
      setProblem("The import could not be started. Try again.");
    } finally {
      setBusy(false);
    }
  }

  async function inspect(item: ImportItem) {
    try {
      setView(await api.inspectSource(item.index));
      setSelected(item.index);
      setRevision("original");
    } catch (error) {
      setProblem(message(error));
    }
  }

  function choose(method: Method) {
    setMethod(method);
    setSource("");
  }

  const item = report?.items.find((row) => row.index === selected);
  const ready = report?.items.filter((row) => row.status === "awaiting_validation").length ?? 0;
  const skipped = report?.items.filter((row) => row.status === "skipped").length ?? 0;
  const validated = (report?.items.length ?? 0) - ready - skipped;

  async function validate() {
    setBusy(true);
    setProblem("");
    try { setReport(await api.validateImports()); }
    catch (error) { setProblem(message(error)); }
    finally { setBusy(false); }
  }

  async function activate(index: number) {
    setBusy(true);
    setProblem("");
    try { showDraft(await api.activateSource(index)); }
    catch (error) { setProblem(message(error)); }
    finally { setBusy(false); }
  }

  return <div className="layout" hidden={hidden}>
      <aside>
        <form onSubmit={stage}>
          <label htmlFor="method">Import from</label>
          <select id="method" disabled={busy} value={method} onChange={(event) => choose(event.target.value as Method)}>
            {METHODS.map(([key, title]) => <option value={key} key={key}>{title}</option>)}
          </select>
          {method === "paste" && <>
            <label htmlFor="source">Fragment source</label>
            <textarea id="source" value={source} onChange={(event) => setSource(event.target.value)}
              disabled={busy} required rows={9} spellCheck={false} />
          </>}
          {method !== "file" && method !== "paste" && <>
            <label htmlFor="source">Source URL</label>
            <input id="source" type="url" value={source} onChange={(event) => setSource(event.target.value)}
              disabled={busy} required spellCheck={false} />
          </>}
          {method === "repository" && <>
            <label htmlFor="git-ref">Branch, tag or commit</label>
            <input id="git-ref" value={ref} onChange={(event) => setRef(event.target.value)} disabled={busy}
              required maxLength={250} autoComplete="off" spellCheck={false} />
          </>}
          <div className="actions">
            <button className="primary" disabled={busy} type="submit">
              {busy ? "Importing…" : method === "file" ? "Choose file…" : "Stage source"}
            </button>
            {busy && method !== "file" && <button type="button" onClick={() => void api.cancelImport()}>Cancel</button>}
          </div>
        </form>
      </aside>
      <section className="results" aria-labelledby="results-heading" aria-busy={busy}>
        <div className="section-heading">
          <h2 id="results-heading">Import Results</h2>
          {report && <p role="status">{ready} awaiting validation · {validated} validated · {skipped} skipped</p>}
        </div>
        {report && ready > 0 && <div className="actions">
          <button className="primary" type="button" disabled={!connected || busy}
            onClick={() => void validate()}>Validate imports</button>
        </div>}
        {problem && <p role="alert" className="error">{problem}</p>}
        {report?.error && <p role="alert" className="error">{report.error.message}</p>}
        {!report && <p className="empty">Choose a fragment shader to inspect its source before connecting to a render host.</p>}
        {report && report.items.length > 0 && <ul className="items">
          {report.items.map((row) => <li key={row.index}>
            <button type="button" onClick={() => void inspect(row)} aria-pressed={selected === row.index} disabled={busy}>
              <strong>{row.path}</strong><span>{sourceStatus(row)}</span>
            </button>
          </li>)}
        </ul>}
        {item && <div className="details">
          <h3>{item.path}</h3>
          <p className="origin">{item.origin}</p>
          <p>{item.message}</p>
          {item.generatedDigest && <p className="origin">Converted SHA-256 {item.generatedDigest}</p>}
          {item.status !== "awaiting_validation" && item.status !== "skipped" &&
            <div className="actions"><button type="button" className="primary" disabled={!connected || busy}
              onClick={() => void activate(item.index)}>Open native preview</button></div>}
          {view && <>
            <div className="revision">
              <button aria-pressed={revision === "original"} onClick={() => setRevision("original")}>Original</button>
              <button aria-pressed={revision === "generated"} disabled={!view.generated}
                onClick={() => setRevision("generated")}>Converted</button>
            </div>
            <pre><code>{view[revision]}</code></pre>
          </>}
        </div>}
      </section>
  </div>;
}

function App() {
  const api = window.voscAuthoring;
  const [page, setPage] = useState<"import" | "host" | "draft">("import");
  const [workspace, setWorkspace] = useState<WorkspaceView | null>(null);
  const [discovery, setDiscovery] = useState<Discovery | null>(null);

  async function discover() {
    try { setDiscovery(await api.discover()); }
    catch { setDiscovery({ peers: [], error: "Bonjour discovery is unavailable." }); }
  }

  useEffect(() => {
    void api.workspace().then(setWorkspace);
    void discover();
  }, []);
  useEffect(() => {
    if (page !== "host") return;
    void discover();
    const timer = setInterval(() => void discover(), 6000);
    return () => clearInterval(timer);
  }, [page]);
  useEffect(() => {
    if (!workspace?.peer) return;
    let pending = false;
    const timer = setInterval(() => {
      if (pending) return;
      pending = true;
      void api.reflect().then(setWorkspace).catch(() => api.workspace().then(setWorkspace))
        .finally(() => { pending = false; });
    }, 1600);
    return () => clearInterval(timer);
  }, [workspace?.peer?.target]);

  return <main>
    <header className="workspace">
      <h1>VOSC Authoring</h1>
      <nav aria-label="Workspace views">
        <button onClick={() => setPage("import")} aria-current={page === "import" ? "page" : undefined}>Import</button>
        <button onClick={() => setPage("host")} aria-current={page === "host" ? "page" : undefined}>Render Host</button>
        <button onClick={() => setPage("draft")} aria-current={page === "draft" ? "page" : undefined}>Draft</button>
      </nav>
      <p className="host-state"><span aria-hidden="true" />
        {workspace?.peer ? workspace.reflected ? "Render host connected" : "Render host unavailable" : "No render host connected"}</p>
    </header>
    <ImportPane connected={Boolean(workspace?.peer && workspace.reflected)} hidden={page !== "import"}
      showDraft={value => { setWorkspace(value); setPage("draft"); }} />
    {page === "host" && <HostPane discovery={discovery} refresh={() => void discover()} workspace={workspace} onWorkspace={setWorkspace} />}
    {page === "draft" && <DraftPane workspace={workspace} onWorkspace={setWorkspace} />}
  </main>;
}

const root = document.getElementById("app");
if (!root) throw new Error("The workspace could not be opened.");
createRoot(root).render(<App />);
