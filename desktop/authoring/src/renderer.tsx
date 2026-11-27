import { useCallback, useEffect, useRef, useState, type ReactNode } from "react";
import { createRoot } from "react-dom/client";
import type { EditorView } from "@codemirror/view";
import type { AuthoringAPI, Discovery, DocumentEdit, FileCommand, LibraryItem, WorkspaceView } from "./contracts";
import { DraftPane } from "./draft-pane";
import { HostPane, message } from "./host-pane";
import { ImportPane } from "./import-pane";
import { SourceEditor, type SourceLocation } from "./source-editor";

declare global {
  interface Window { voscAuthoring: AuthoringAPI }
}

function Icon({ name }: { name: string }) {
  return <img className="icon" src={`/icons/${name}.svg`} alt="" />;
}

function WorkspaceDialog({ title, close, children, focusFallback }: {
  title: string;
  close: () => void;
  children: ReactNode;
  focusFallback: () => void;
}) {
  const ref = useRef<HTMLDialogElement>(null);
  const fallback = useRef(focusFallback);
  fallback.current = focusFallback;
  useEffect(() => {
    const dialog = ref.current!;
    const trigger = document.activeElement;
    dialog.showModal();
    (dialog.querySelector<HTMLElement>("select, .dialog-body button:not(:disabled), .dialog-body input:not(:disabled)") ??
      dialog.querySelector<HTMLElement>(".dialog-header h2"))?.focus();
    return () => {
      dialog.close();
      if (trigger instanceof HTMLElement && trigger.isConnected && !trigger.closest("[hidden]")) trigger.focus();
      else fallback.current();
    };
  }, []);

  return <dialog className="workspace-dialog" ref={ref} aria-labelledby="workspace-dialog-heading"
    onCancel={event => { event.preventDefault(); close(); }}>
    <div className="dialog-header">
      <h2 id="workspace-dialog-heading" tabIndex={-1}>{title}</h2>
      <button className="quiet icon-button" type="button" aria-label={`Close ${title}`} onClick={close}><Icon name="close" /></button>
    </div>
    <div className="dialog-body">{children}</div>
  </dialog>;
}

function layout() {
  if (matchMedia("(width < 960px), (height < 600px)").matches) return "compact";
  return matchMedia("(width >= 1200px)").matches ? "wide" : "standard";
}

function App() {
  const api = window.voscAuthoring;
  const [workspace, setWorkspace] = useState<WorkspaceView | null>(null);
  const [library, setLibrary] = useState<LibraryItem[]>([]);
  const [discovery, setDiscovery] = useState<Discovery | null>(null);
  const [dialog, setDialog] = useState<{ type: "import" | "host"; generation: number } | null>(null);
  const [size, setSize] = useState(layout);
  const [drawer, setDrawer] = useState<"library" | "inspector" | null>(null);
  const [inspectorCollapsed, setInspectorCollapsed] = useState(false);
  const [libraryItem, setLibraryItem] = useState("");
  const [position, setPosition] = useState({ line: 1, column: 1 });
  const [notice, setNotice] = useState("");
  const [diagnostic, setDiagnostic] = useState<{ problem: string; location?: SourceLocation }>({ problem: "" });
  const { problem, location } = diagnostic;
  const [editing, setEditing] = useState(false);
  const [busy, setBusy] = useState(false);
  const view = useRef<EditorView>(null);
  const focusAfterNavigation = useRef<number | null>(null);
  const activeLocation = location?.document === view.current?.state.doc ? location : undefined;
  const newButton = useRef<HTMLButtonElement>(null);
  const libraryButton = useRef<HTMLButtonElement>(null);
  const inspectorButton = useRef<HTMLButtonElement>(null);
  const commandRunning = useRef(false);
  const importRunning = useRef(false);
  const dialogRef = useRef(dialog);
  dialogRef.current = dialog;
  const pending = useRef<DocumentEdit | null>(null);
  const editSequence = useRef(0);
  const syncTimer = useRef<ReturnType<typeof setTimeout> | null>(null);
  const syncing = useRef<Promise<void> | null>(null);
  const draft = workspace?.document;
  const generation = workspace?.documentGeneration ?? 0;
  const workspaceRef = useRef(workspace);
  workspaceRef.current = workspace;
  const connected = Boolean(workspace?.peer && workspace.reflected);
  const accepted = draft?.acceptedArtifact;
  const artifactName = accepted && (accepted.revision === null ? `artifact ${accepted.id.slice(0, 12)}` : `revision ${accepted.revision}`);
  const dirty = Boolean(draft?.dirty || editing);
  const libraryVisible = size === "wide" || drawer === "library";
  const inspectorVisible = size === "compact" ? drawer === "inspector" : !inspectorCollapsed;
  const selectedLibraryItem = library.find(item => item.id === libraryItem);

  function setProblem(problem: string, location?: SourceLocation) {
    setDiagnostic({ problem, location });
  }

  function jumpToProblem() {
    const editor = view.current;
    if (!editor || !activeLocation || editor.state.doc !== activeLocation.document) return;
    const line = editor.state.doc.line(activeLocation.line);
    editor.dispatch({ selection: { anchor: line.from, head: line.to }, scrollIntoView: true });
    editor.focus();
  }

  const adopt = useCallback((next: WorkspaceView) => {
    setWorkspace(previous => {
      if (!previous) return next;
      if (next.documentGeneration < previous.documentGeneration) return previous;
      if (next.documentGeneration === previous.documentGeneration && next.document && previous.document &&
          next.document.editSequence < previous.document.editSequence) {
        return { ...next, document: previous.document };
      }
      return next;
    });
  }, []);

  async function refreshLibrary() {
    try { setLibrary(await api.listLibrary()); }
    catch (error) { setProblem(message(error)); }
  }

  async function discover() {
    try { setDiscovery(await api.discover()); }
    catch { setDiscovery({ peers: [], error: "Bonjour discovery is unavailable." }); }
  }

  useEffect(() => {
    void api.workspace().then(adopt).catch(error => setProblem(message(error)));
    void refreshLibrary();
    const resize = () => setSize(layout());
    window.addEventListener("resize", resize);
    return () => window.removeEventListener("resize", resize);
  }, [api, adopt]);

  useEffect(() => {
    if (dialog?.type !== "host") return;
    void discover();
    const timer = setInterval(() => void discover(), 6000);
    return () => clearInterval(timer);
  }, [dialog?.type]);

  const target = workspace?.peer?.target;
  const fingerprint = workspace?.peer?.fingerprint;
  useEffect(() => {
    if (!target) return;
    let active = true;
    let reflecting = false;
    function update(next: WorkspaceView) {
      if (active && workspaceRef.current?.peer?.target === target &&
          workspaceRef.current?.peer?.fingerprint === fingerprint) adopt(next);
    }
    const timer = setInterval(() => {
      if (reflecting || commandRunning.current) return;
      reflecting = true;
      void api.reflect().then(update).catch(() => api.workspace().then(update))
        .catch(error => { if (active) setProblem(message(error)); })
        .finally(() => { reflecting = false; });
    }, 1600);
    return () => { active = false; clearInterval(timer); };
  }, [api, adopt, target, fingerprint]);

  function focusSource() {
    if (view.current) view.current.focus();
    else newButton.current?.focus();
  }

  useEffect(() => {
    const requested = focusAfterNavigation.current;
    if (requested === null || dialog || generation < requested) return;
    focusAfterNavigation.current = null;
    if (generation === requested) view.current?.focus();
  }, [dialog, generation]);

  useEffect(() => {
    const key = (event: KeyboardEvent) => {
      if (event.key !== "Escape" || event.defaultPrevented || event.isComposing || dialogRef.current || !drawer) return;
      event.preventDefault();
      setDrawer(null);
      (drawer === "library" ? libraryButton : inspectorButton).current?.focus();
    };
    window.addEventListener("keydown", key);
    return () => window.removeEventListener("keydown", key);
  }, [drawer]);

  function flush() {
    if (syncTimer.current) clearTimeout(syncTimer.current);
    syncTimer.current = null;
    if (!syncing.current) {
      syncing.current = (async () => {
        while (pending.current) {
          const edit = pending.current;
          const applied = await api.editDraft(edit);
          if (pending.current === edit) pending.current = null;
          setWorkspace(current => current?.documentGeneration === applied.generation &&
            (!current.document || current.document.editSequence <= applied.editSequence)
              ? { ...current, document: applied } : current);
        }
        setEditing(false);
      })().finally(() => { syncing.current = null; });
    }
    return syncing.current;
  }

  function edit(text: string) {
    if (!draft) return;
    editSequence.current = Math.max(editSequence.current, draft.editSequence) + 1;
    pending.current = { id: draft.id, generation: draft.generation, editSequence: editSequence.current, text };
    setEditing(true);
    setProblem("");
    scheduleSync();
  }

  function scheduleSync() {
    if (syncTimer.current) clearTimeout(syncTimer.current);
    syncTimer.current = setTimeout(() => {
      if (!view.current?.composing && pending.current) void flush().catch(error => setProblem(message(error)));
    }, 160);
  }

  async function command(action: () => Promise<void>) {
    if (commandRunning.current || view.current?.composing) return;
    commandRunning.current = true;
    setBusy(true);
    setNotice("");
    setProblem("");
    try {
      await flush();
      await action();
    } catch (error) { setProblem(message(error)); }
    finally { commandRunning.current = false; setBusy(false); }
  }

  function navigate(next: WorkspaceView, expects: number) {
    const current = workspaceRef.current?.documentGeneration ?? 0;
    if (next.document && next.documentGeneration > Math.max(current, expects)) {
      focusAfterNavigation.current = next.documentGeneration;
      setDialog(null);
      setDrawer(null);
    }
    adopt(next);
  }

  function fileCommand(name: FileCommand) {
    if (dialogRef.current && name !== "quit" && name !== "close") return;
    void command(async () => {
      const expects = workspaceRef.current?.documentGeneration ?? 0;
      switch (name) {
        case "new": navigate(await api.newDraft(), expects); break;
        case "open": navigate(await api.openDraft(), expects); break;
        case "save":
        case "saveAs":
          if (!workspaceRef.current?.document) return;
          if (await api.saveDraft(name === "saveAs")) setNotice("Draft saved.");
          adopt(await api.workspace());
          break;
        case "close":
        case "quit":
          if (!await api.closeDraftWindow(name === "quit")) adopt(await api.workspace());
          break;
      }
    });
  }

  const fileCommandRef = useRef(fileCommand);
  fileCommandRef.current = fileCommand;
  useEffect(() => api.onFileCommand(name => fileCommandRef.current(name)), [api]);

  function pickLibrary(id: string) {
    void command(async () => navigate(await api.openLibrary(id, generation), generation));
  }

  function toggleDrawer(value: "library" | "inspector") {
    setDrawer(current => current === value ? null : value);
  }

  function closeDialog() {
    if (dialog?.type === "import" && importRunning.current) {
      void api.cancelImport().catch(error => setProblem(message(error)));
      return;
    }
    setDialog(null);
  }

  return <main className={`editor-workspace layout-${size} ${libraryVisible ? "with-library" : ""} ${inspectorVisible ? "with-inspector" : ""}`}>
    <h1 className="accessible-only">VOSC Editor</h1>
    <header className="command-bar" aria-label="Workspace commands">
      <button ref={newButton} type="button" disabled={busy || !workspace} onClick={() => fileCommand("new")}><Icon name="plus" />New</button>
      <button type="button" disabled={busy || !workspace} onClick={() => fileCommand("open")}><Icon name="folder" />Open…</button>
      <button type="button" disabled={busy || !workspace}
        onClick={() => setDialog({ type: "import", generation })}>Import…</button>
      <button className="quiet destination" type="button" aria-haspopup="dialog"
        onClick={() => setDialog({ type: "host", generation })}>
        <span aria-hidden="true" className={`status-dot ${connected ? "online" : ""}`} />
        {workspace?.peer?.endpoint.host ?? "Render Host"}<Icon name="down" />
      </button>
    </header>

    {size !== "wide" && <div className="library-tools">
      <button type="button" ref={libraryButton} aria-expanded={libraryVisible} aria-controls="workspace-library"
        onClick={() => toggleDrawer("library")}>Library</button>
    </div>}
    <aside id="workspace-library" className="library-sidebar" aria-label="Library" hidden={!libraryVisible}>
      <div className="library-heading">
        <h2>Workspace</h2>
        <button type="button" className="quiet icon-button" aria-label="New draft" disabled={busy || !workspace}
          onClick={() => fileCommand("new")}><Icon name="plus" /></button>
      </div>
      {draft ? <div className="active-source" title={draft.source.name}>
        <Icon name="code" /><span>{draft.source.name}</span>{dirty && <span className="unsaved-dot" aria-label="Unsaved changes" />}
      </div> : <p className="muted">No draft open</p>}
      <h2 className="library-label">Library</h2>
      <ul className="library-list">
        {library.map(item => <li key={item.id}>
          <button type="button" className="quiet library-link" title={item.path} disabled={busy}
            aria-pressed={item.id === libraryItem} onClick={() => { setLibraryItem(item.id); pickLibrary(item.id); }}>
            <Icon name="library-code" /><span>{item.path}</span>
          </button>
        </li>)}
      </ul>
      {!library.length && <p className="muted">No validated sources</p>}
      {selectedLibraryItem && <button className="quiet" type="button" disabled={!connected || busy}
        title={selectedLibraryItem.path} onClick={() => void command(async () =>
          navigate(await api.previewLibrary(selectedLibraryItem.id, generation), generation))}>
        Preview validated source
      </button>}
      <button type="button" className="quiet browse-library" onClick={() => setDialog({ type: "import", generation })}>
        Import sources…<Icon name="arrow" />
      </button>
    </aside>

    <section className="code-pane" aria-labelledby="source-heading">
      <div className="source-header">
        <h2 id="source-heading">GLSL fragment</h2>
        {draft && <span className="muted source-origin" title={draft.source.origin}>{draft.source.origin}</span>}
      </div>
      {draft ? <SourceEditor key={draft.generation} source={draft.source.text} editor={view}
        onChange={edit} onPosition={(line, column) => setPosition({ line, column })} onProblem={setProblem}
        onCompositionEnd={scheduleSync} /> :
        <div className="empty-editor"><p>Create, open, or import a GLSL draft.</p></div>}
      <section className="problems" aria-labelledby="problems-heading" tabIndex={0}>
        <h3 id="problems-heading">Problems{problem ? " · 1" : ""}</h3>
        <div className="problem-row">
          <div>
            {problem && <p className="problem-source">{draft?.source.name ?? "Workspace"}
              {activeLocation && ` · line ${activeLocation.line}, column ${activeLocation.column}`}</p>}
            <p role="status" className={problem ? "error" : "muted"}>{problem || "No reported problems"}</p>
          </div>
          {activeLocation && <button type="button" onClick={jumpToProblem}>Jump to line</button>}
        </div>
      </section>
    </section>

    <div className="inspector-tools">
      <button type="button" ref={inspectorButton} aria-expanded={inspectorVisible} aria-controls="workspace-inspector"
        onClick={() => size === "compact" ? toggleDrawer("inspector") : setInspectorCollapsed(value => !value)}>Inspector</button>
    </div>
    <aside id="workspace-inspector" className="inspector-sidebar" aria-label="Inspector" hidden={!inspectorVisible}>
      <div className="inspector-scroll">
        <section className="native-target" aria-labelledby="native-title">
          <h2 id="native-title">Native render</h2>
          <p className="native-name">{workspace?.peer?.endpoint.host ?? "No render host connected"}</p>
          {workspace?.previewReady && <p className="muted">Preview on render host · Layer 0</p>}
          <button className="quiet" type="button" onClick={() => setDialog({ type: "host", generation })}>Manage…</button>
        </section>
        <DraftPane workspace={workspace} onWorkspace={adopt} />
      </div>
      <div className="inspector-footer">
        <p className="muted">
          {accepted ? `Export ${artifactName}${accepted.stale || editing ? " · working draft differs" : ""}${accepted.saved ? "" : " · unsaved artifact"}` :
            "Run a native preview before exporting a replay."}
        </p>
        <div className="actions">
          <button type="button" disabled={!draft || busy} onClick={() => fileCommand("save")}>Save draft…</button>
          <button type="button" disabled={!accepted || busy} onClick={() => void command(async () => {
            const path = await api.exportReplay();
            if (path) setNotice(`Native replay saved to ${path}`);
          })}><Icon name="export" />Export replay…</button>
        </div>
      </div>
    </aside>

    <footer className="workspace-status">
      <div className="status-message" role="status">
        <span>{workspace?.previewReady && workspace.reflected ? `Live · revision ${workspace.reflected.revision}` :
          accepted ? `Preview offline · last accepted ${artifactName}` : "Preview offline"}</span>
        {dirty && <span>Unsaved changes</span>}
        {notice && <span className="status-notice" title={notice}>{notice}</span>}
      </div>
      {draft && <div className="position"><span>GLSL</span><span>UTF-8</span><span>Ln {position.line}, Col {position.column}</span></div>}
    </footer>

    {dialog && <WorkspaceDialog title={dialog.type === "import" ? "Import GLSL" : "Render Host"} close={closeDialog} focusFallback={focusSource}>
      {dialog.type === "host"
        ? <HostPane discovery={discovery} refresh={() => void discover()} workspace={workspace} onWorkspace={adopt} />
        : <ImportPane connected={connected} openSource={(id, item) => command(async () =>
            navigate(await api.openImported(id, item, dialog.generation), dialog.generation))}
            onPublished={() => void refreshLibrary()} onRunning={value => { importRunning.current = value; }} />}
    </WorkspaceDialog>}
  </main>;
}

const root = document.getElementById("app");
if (!root) throw new Error("The workspace could not be opened.");
createRoot(root).render(<App />);
