import { useCallback, useEffect, useRef, useState, type ReactNode } from "react";
import { createRoot } from "react-dom/client";
import type { EditorView } from "@codemirror/view";
import type { AuthoringAPI, Discovery, DocumentEdit, FileCommand, LibraryItem, RunMode, WorkspaceView } from "./contracts";
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

function WorkspaceDialog({ title, close, children, focusFallback, initialFocus }: {
  title: string;
  close: () => void;
  children: ReactNode;
  focusFallback: () => void;
  initialFocus?: (dialog: HTMLDialogElement) => void;
}) {
  const ref = useRef<HTMLDialogElement>(null);
  const fallback = useRef(focusFallback);
  fallback.current = focusFallback;
  useEffect(() => {
    const dialog = ref.current!;
    const trigger = document.activeElement;
    dialog.showModal();
    if (initialFocus) initialFocus(dialog);
    else (dialog.querySelector<HTMLElement>("select, .dialog-body button:not(:disabled), .dialog-body input:not(:disabled)") ??
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

function sourceLine(source: string, number: number, hydra = false) {
  if (!Number.isInteger(number) || number < 1) return null;
  const lines = source.split(hydra ? /[\n\u2028\u2029]/ : "\n");
  if (number > lines.length) return null;
  let start = 0;
  for (let i = 0; i < number - 1; i++) start += lines[i].length + 1;
  return { start, end: start + lines[number - 1].length };
}

const pendingRun = new Set(["evaluating", "preparing", "committing", "reconciling"]);

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
  const [runsWaiting, setRunsWaiting] = useState(0);
  const [reviewBusy, setReviewBusy] = useState(false);
  const [dismissedReview, setDismissedReview] = useState<string | null>(null);
  const [attempted, setAttempted] = useState<{ source: string; line: number; sequence: number } | null>(null);
  const [generatedInspection, inspectGenerated] = useState<{ source: string; digest: string; label: string } | null>(null);
  const [autoScope, setAutoScope] = useState<string | null>(null);
  const autoScopeRef = useRef<string | null>(null);
  const autoTimer = useRef<ReturnType<typeof setTimeout> | null>(null);
  const view = useRef<EditorView>(null);
  const focusAfterNavigation = useRef<number | null>(null);
  const activeLocation = location?.document === view.current?.state.doc ? location : undefined;
  const newButton = useRef<HTMLButtonElement>(null);
  const newDraftChoices = useRef<HTMLDivElement>(null);
  const libraryButton = useRef<HTMLButtonElement>(null);
  const inspectorButton = useRef<HTMLButtonElement>(null);
  const commandRunning = useRef(false);
  const importRunning = useRef(false);
  const pending = useRef<DocumentEdit | null>(null);
  const editSequence = useRef(0);
  const syncTimer = useRef<ReturnType<typeof setTimeout> | null>(null);
  const syncing = useRef<Promise<void> | null>(null);
  const draft = workspace?.document;
  const generation = workspace?.documentGeneration ?? 0;
  const workspaceRef = useRef(workspace);
  workspaceRef.current = workspace;
  const connected = Boolean(workspace?.peer && workspace.reflected);
  const run = workspace?.run;
  const peer = workspace?.peer;
  const peerMatches = !draft?.destination || (draft.destination.target === peer?.target &&
    draft.destination.fingerprint === peer?.fingerprint);
  const executionScope = connected && peerMatches && draft
    ? `${generation}:${peer?.target}:${peer?.fingerprint}:${workspace.reflected?.epoch}` : null;
  const currentScope = useRef(executionScope);
  currentScope.current = executionScope;
  const awaitingNative = runsWaiting > 0 || Boolean(run && pendingRun.has(run.phase));
  const hydra = draft?.language === "hydra-js";
  const wgsl = draft?.language === "wgsl";
  const fragmentLanguage = wgsl ? "WGSL" : "GLSL";
  const autoEnabled = Boolean(draft?.language === "glsl" && autoScope && autoScope === executionScope);
  const review = run?.review;
  const showReview = Boolean(review && review.token !== dismissedReview && !dialog && !attempted && !generatedInspection);
  const modal = Boolean(dialog || attempted || generatedInspection || showReview);
  const modalRef = useRef(modal);
  modalRef.current = modal;
  const canRun = Boolean(draft && executionScope && !busy && !modal && run &&
    run.phase !== "uncertain" && run.phase !== "reconciling" && (run.phase !== "restore_required" ||
      workspace?.project?.destination.target !== peer?.target ||
      workspace?.project?.destination.fingerprint !== peer?.fingerprint));
  const accepted = draft?.acceptedArtifact;
  const webgpuArtifact = accepted?.backend === "webgpu";
  const generated = run?.attempt?.generated ?? accepted?.generated;
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

  function jumpToNative(line: number) {
    const attempt = run?.attempt;
    const editor = view.current;
    if (!attempt) return;
    const range = sourceLine(attempt.source, line, hydra);
    if (!range) return;
    if (editor && draft?.source.digest === attempt.sourceDigest &&
        editor.state.doc.toString() === attempt.source) {
      editor.dispatch({ selection: { anchor: range.start, head: range.end }, scrollIntoView: true });
      editor.focus();
    } else {
      setAttempted({ line, source: attempt.source, sequence: attempt.runSequence });
    }
  }

  async function refreshLibrary() {
    try { setLibrary(await api.listLibrary()); }
    catch (error) { setProblem(message(error)); }
  }

  async function discover() {
    try { setDiscovery(await api.discover()); }
    catch { setDiscovery({ peers: [], error: "Render host discovery is unavailable." }); }
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

  useEffect(() => {
    if (!awaitingNative) return;
    let active = true;
    let reading = false;
    const timer = setInterval(() => {
      if (reading) return;
      reading = true;
      void api.workspace().then(next => { if (active) adopt(next); })
        .catch(error => { if (active) setProblem(message(error)); })
        .finally(() => { reading = false; });
    }, 160);
    return () => { active = false; clearInterval(timer); };
  }, [api, adopt, awaitingNative]);

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

  const manualRunRef = useRef(() => {});
  manualRunRef.current = () => { if (canRun) void submitRun("manual"); };
  useEffect(() => {
    const key = (event: KeyboardEvent) => {
      if (event.defaultPrevented || event.isComposing || view.current?.composing || modalRef.current) return;
      if (event.key === "Enter" && event.metaKey && !event.shiftKey && !event.altKey && !event.ctrlKey) {
        event.preventDefault();
        event.stopPropagation();
        if (!event.repeat) manualRunRef.current();
      } else if (event.key === "Escape" && drawer) {
        event.preventDefault();
        setDrawer(null);
        (drawer === "library" ? libraryButton : inspectorButton).current?.focus();
      }
    };
    // Handle Cmd+Return before the editor's keymap consumes the event.
    window.addEventListener("keydown", key, true);
    return () => window.removeEventListener("keydown", key, true);
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
    const identity = { id: draft.id, generation: draft.generation, editSequence: editSequence.current };
    // Invalidate before the normal debounced text write, including during IME composition.
    const invalidated = !hydra || api.invalidateHydraEdit(identity);
    pending.current = { ...identity, text };
    setEditing(true);
    setProblem(invalidated ? "" : "The draft changed before this edit could be synchronized.");
    scheduleSync();
    scheduleAutomatic();
  }

  function clearAutomaticTimer() {
    if (autoTimer.current) clearTimeout(autoTimer.current);
    autoTimer.current = null;
  }

  function pauseAutomatic() {
    clearAutomaticTimer();
    if (!autoScopeRef.current) return;
    autoScopeRef.current = null;
    setAutoScope(null);
    void api.cancelRun(undefined, true).catch(error => setProblem(message(error)));
  }

  useEffect(() => {
    if (autoScope && (!autoEnabled || run?.paused || !run?.manualReady)) pauseAutomatic();
  }, [autoScope, autoEnabled, run?.paused, run?.manualReady]);

  useEffect(() => () => clearAutomaticTimer(), []);

  function scheduleAutomatic() {
    clearAutomaticTimer();
    const scope = autoScopeRef.current;
    if (!scope || currentScope.current !== scope) return;
    autoTimer.current = setTimeout(() => {
      autoTimer.current = null;
      if (autoScopeRef.current !== scope || currentScope.current !== scope ||
          modalRef.current || commandRunning.current || view.current?.composing) return;
      void submitRun("automatic");
    }, 400);
  }

  async function submitRun(mode: RunMode) {
    const scope = currentScope.current;
    if (!scope || commandRunning.current || modalRef.current || view.current?.composing) return;
    if (mode === "manual") clearAutomaticTimer();
    setProblem("");
    setRunsWaiting(value => value + 1);
    try {
      await flush();
      if (currentScope.current !== scope || commandRunning.current || modalRef.current ||
          (mode === "automatic" && autoScopeRef.current !== scope)) return;
      await api.runDraft(mode);
      const next = await api.workspace();
      if (currentScope.current === scope) adopt(next);
    } catch (error) {
      if (currentScope.current === scope) setProblem(message(error));
    } finally {
      setRunsWaiting(value => value - 1);
    }
  }

  async function finishReview(token: string, approve: boolean) {
    if (reviewBusy) return;
    setReviewBusy(true);
    setRunsWaiting(value => value + 1);
    try {
      await flush();
      if (approve) await api.approveRun(token);
      else await api.cancelRun(token);
      const next = await api.workspace();
      adopt(next);
      if (next.run.review?.token === token && !approve) setDismissedReview(token);
    } catch (error) {
      setProblem(message(error));
      adopt(await api.workspace());
    } finally {
      setReviewBusy(false);
      setRunsWaiting(value => value - 1);
    }
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
    if (modalRef.current && name !== "quit" && name !== "close") return;
    if (newDraftChoices.current?.matches(":popover-open")) newDraftChoices.current.hidePopover();
    void command(async () => {
      const expects = workspaceRef.current?.documentGeneration ?? 0;
      switch (name) {
        case "new": navigate(await api.newDraft(), expects); break;
        case "newWgsl": navigate(await api.newDraft("wgsl"), expects); break;
        case "newHydra": navigate(await api.newDraft("hydra-js"), expects); break;
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
      <button ref={newButton} type="button" disabled={busy || !workspace} popoverTarget="new-draft-choices">
        <Icon name="plus" />New<Icon name="down" />
      </button>
      <div id="new-draft-choices" className="new-draft-choices" ref={newDraftChoices} popover="auto" role="group" aria-label="New draft type">
        <button type="button" disabled={busy} onClick={() => fileCommand("new")}>GLSL fragment</button>
        <button type="button" disabled={busy} onClick={() => fileCommand("newWgsl")}>WGSL fragment</button>
        <button type="button" disabled={busy} onClick={() => fileCommand("newHydra")}>Hydra patch</button>
      </div>
      <button type="button" disabled={busy || !workspace} onClick={() => fileCommand("open")}><Icon name="folder" />Open…</button>
      <button type="button" disabled={busy || !workspace}
        onClick={() => setDialog({ type: "import", generation })}>Import…</button>
      <button className="quiet destination" type="button" aria-haspopup="dialog"
        onClick={() => setDialog({ type: "host", generation })}>
        <span aria-hidden="true" className={`status-dot ${connected ? "online" : ""}`} />
        {workspace?.peer?.endpoint.host ?? "Render Host"}<Icon name="down" />
      </button>
      <button className="primary run-command" type="button" disabled={!canRun}
        aria-keyshortcuts="Meta+Enter" onClick={() => void submitRun("manual")}>
        <Icon name="run" />Run <kbd aria-hidden="true">⌘↵</kbd>
      </button>
    </header>

    {size !== "wide" && <div className="library-tools">
      <button type="button" ref={libraryButton} aria-expanded={libraryVisible} aria-controls="workspace-library"
        onClick={() => toggleDrawer("library")}>Library</button>
    </div>}
    <aside id="workspace-library" className="library-sidebar" aria-label="Library" hidden={!libraryVisible}>
      <h2 className="library-heading">Workspace</h2>
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
      {selectedLibraryItem && !selectedLibraryItem.example && <button className="quiet" type="button" disabled={!connected || busy}
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
        <h2 id="source-heading">{hydra ? "Hydra JavaScript" : `${fragmentLanguage} fragment`}</h2>
        {draft && <span className="muted source-origin" title={draft.source.origin}>{draft.source.origin}</span>}
        {generated && <button type="button" onClick={() => inspectGenerated({ ...generated, label: run?.attempt?.generated ? `Run ${run.attempt.runSequence}` : "Accepted artifact" })}>Generated GLSL</button>}
        {(hydra || wgsl || workspace?.reflected?.webgpuAuto === true) && (run?.phase === "evaluating" || run?.phase === "preparing") && <button type="button" onClick={() => {
          void api.cancelRun().then(adopt).catch(error => setProblem(message(error)));
        }}>Cancel Run</button>}
        {draft?.language === "glsl" && <button type="button" className="quiet auto-control" aria-pressed={autoEnabled}
          disabled={!autoEnabled && (!executionScope || !run?.manualReady || run.paused)}
          onClick={() => {
            if (autoEnabled) pauseAutomatic();
            else if (executionScope) {
              autoScopeRef.current = executionScope;
              setAutoScope(executionScope);
            }
          }}>{autoEnabled ? "Pause auto-run" : "Auto-run off"}</button>}
      </div>
      {draft ? <SourceEditor key={draft.generation} source={draft.source.text} language={draft.language} editor={view}
        onChange={edit} onPosition={(line, column) => setPosition({ line, column })} onProblem={setProblem}
        onCompositionEnd={() => { scheduleSync(); scheduleAutomatic(); }} /> :
        <div className="empty-editor"><p>Create or open a draft, open a WGSL file, or import a GLSL fragment.</p></div>}
      <section className="problems" aria-labelledby="problems-heading" tabIndex={0}>
        <h3 id="problems-heading">Problems{(problem || run?.problems.length) ? ` · ${Number(Boolean(problem)) + (run?.problems.length ?? 0)}` : ""}</h3>
        <div className="problem-row">
          <div>
            {problem && <p className="problem-source">{draft?.source.name ?? "Workspace"}
              {activeLocation && ` · line ${activeLocation.line}, column ${activeLocation.column}`}</p>}
            {problem || !run?.problems.length ? <p role="status" className={problem ? "error" : "muted"}>{problem || "No reported problems"}</p> : null}
          </div>
          {activeLocation && <button type="button" onClick={jumpToProblem}>Jump to line</button>}
        </div>
        {!!run?.problems.length && <ul className="native-problems">
          {run.problems.map((item, index) => <li className="problem-row" key={`${run.attempt?.runSequence}-${index}`}>
            <div><p className="problem-source">{draft?.source.name ?? fragmentLanguage}{item.line !== undefined ?
              ` · Line ${item.line}${item.column ? `, column ${item.column}` : ""}` : ""}{item.generatedLine !== undefined && ` · Generated line ${item.generatedLine}`}</p>
              <p className="error native-log">{item.message}</p></div>
            {run.attempt && item.line !== undefined && sourceLine(run.attempt.source, item.line, hydra) &&
              <button type="button" onClick={() => jumpToNative(item.line!)}>Jump to line</button>}
          </li>)}
        </ul>}
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
          {workspace?.previewReady && <p className="muted">Preview window on render host · {workspace.reflected?.backend === "webgpu" ? "WebGPU" : "Layer 0"}</p>}
          {draft?.destination && !peerMatches && <p className="muted">Draft destination: {draft.destination.endpoint.host}</p>}
          {run?.phase === "uncertain" && <button type="button" className="quiet" onClick={() => void command(async () =>
            adopt(await api.reflect()))}>Check native outcome</button>}
          <button className="quiet" type="button" onClick={() => setDialog({ type: "host", generation })}>Manage…</button>
        </section>
        <DraftPane workspace={workspace} onWorkspace={adopt} onControl={(action) =>
          void command(async () => {
            setRunsWaiting(value => value + 1);
            try {
              await action();
              adopt(await api.workspace());
            } finally {
              setRunsWaiting(value => value - 1);
            }
          })} />
      </div>
      <div className="inspector-footer">
        <p className="muted">
          {webgpuArtifact || wgsl ? "Save the WebGPU preview as a VOSC draft." : accepted ? `Export ${artifactName}${accepted.stale || editing ? " · working draft differs" : ""}${accepted.saved ? "" : " · unsaved artifact"}` :
            "Run a native preview before exporting a replay."}
        </p>
        <div className="actions">
          <button type="button" disabled={!draft || busy} onClick={() => fileCommand("save")}>Save draft…</button>
          {!wgsl && !webgpuArtifact && <button type="button" disabled={!accepted || busy} onClick={() => void command(async () => {
            const path = await api.exportReplay();
            if (path) setNotice(`Native replay saved to ${path}`);
          })}><Icon name="export" />Export replay…</button>}
        </div>
      </div>
    </aside>

    <footer className="workspace-status">
      <div className="status-message" role="status">
        <span>{workspace?.previewReady && workspace.reflected ? `Live · revision ${workspace.reflected.revision}` :
          workspace?.lastObservation ? `Preview unconfirmed · last confirmed revision ${workspace.lastObservation.revision} at ${new Date(workspace.lastObservation.time).toLocaleTimeString()}` :
          accepted ? `Preview offline · last accepted ${artifactName}` : "Preview offline"}</span>
        {run?.message && <span className={run.phase === "failed" ? "error" : "muted"}>
          {run.phase === "failed" ? "Run failed · " : ""}{run.message}</span>}
        {review && !showReview && <button type="button" className="quiet"
          onClick={() => setDismissedReview(null)}>Review mappings</button>}
        {dirty && <span>Unsaved changes</span>}
        {notice && <span className="status-notice" title={notice}>{notice}</span>}
      </div>
      {draft && <div className="position"><span>{hydra ? "JavaScript" : fragmentLanguage}</span><span>UTF-8</span><span>Ln {position.line}, Col {position.column}</span></div>}
    </footer>

    {showReview && review && <WorkspaceDialog key={review.token} title="Review changed mappings"
      close={() => { if (!reviewBusy) setDismissedReview(review.token); }} focusFallback={focusSource}>
      <section className="review-mappings">
        <p className="muted">The current preview continues with its active controls while you review the candidate.</p>
        <ul>{review.conflicts.map(item => <li key={item.name}><strong>{item.name}</strong><p>{item.message}</p></li>)}</ul>
        <div className="actions">
          <button type="button" disabled={reviewBusy} onClick={() => void finishReview(review.token, false)}>Cancel candidate</button>
          <button type="button" className="primary" disabled={reviewBusy}
            onClick={() => void finishReview(review.token, true)}>Apply candidate</button>
        </div>
      </section>
    </WorkspaceDialog>}
    {attempted && <WorkspaceDialog key={attempted.sequence} title={`Attempted source · Run ${attempted.sequence}`}
      close={() => setAttempted(null)} focusFallback={focusSource} initialFocus={dialogNode => {
        const text = dialogNode.querySelector("textarea");
        const range = sourceLine(attempted.source, attempted.line, hydra);
        if (!text || !range) return;
        text.focus();
        text.setSelectionRange(range.start, range.end);
        text.scrollTop = (attempted.line - 1) * parseFloat(getComputedStyle(text).lineHeight);
      }}>
      <div className="attempted-source">
        <p className="muted">Read-only snapshot · Line {attempted.line}</p>
        <textarea aria-label={hydra ? "Attempted Hydra source" : `Attempted ${fragmentLanguage} source`} readOnly value={attempted.source} spellCheck={false} wrap="off" />
      </div>
    </WorkspaceDialog>}
    {generatedInspection && <WorkspaceDialog title={`Generated GLSL · ${generatedInspection.label}`}
      close={() => inspectGenerated(null)} focusFallback={focusSource}>
      <div className="attempted-source">
        <p className="muted">SHA-256 {generatedInspection.digest}</p>
        <pre className="generated-source" tabIndex={0} aria-label="Generated GLSL"><code>
          {generatedInspection.source.split("\n").map((line, index, lines) => <span key={index}
            data-line={index + 1}>{line}{index < lines.length - 1 && "\n"}</span>)}
        </code></pre>
      </div>
    </WorkspaceDialog>}
    {dialog && <WorkspaceDialog title={dialog.type === "import" ? "Import GLSL" : "Render Host"} close={closeDialog} focusFallback={focusSource}>
      {dialog.type === "host"
        ? <HostPane discovery={discovery} refresh={() => void discover()} workspace={workspace} onWorkspace={adopt}
          onUseSelectedHost={() => void command(async () => adopt(await api.useSelectedHost()))} />
        : <ImportPane connected={connected} openSource={(id, item) => command(async () =>
            navigate(await api.openImported(id, item, dialog.generation), dialog.generation))}
            onPublished={() => void refreshLibrary()} onRunning={value => { importRunning.current = value; }} />}
    </WorkspaceDialog>}
  </main>;
}

const root = document.getElementById("app");
if (!root) throw new Error("The workspace could not be opened.");
createRoot(root).render(<App />);
