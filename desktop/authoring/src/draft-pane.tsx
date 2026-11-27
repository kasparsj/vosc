import { useEffect, useState, type FormEvent } from "react";
import type { Binding, Slot, WorkspaceView } from "./contracts";
import { message } from "./host-pane";

function ComponentControl({ name, component, value, binding, slots, disabled, apply }: {
  name: string;
  component: number;
  value: number;
  binding: Binding;
  slots: Slot[];
  disabled: boolean;
  apply: (binding: Binding) => void;
}) {
  const [mode, setMode] = useState(binding.mode);
  const [fixed, setFixed] = useState(binding.mode === "fixed" ? String(binding.value) : String(value));
  const [slot, setSlot] = useState(binding.mode === "range" ? binding.slot : "");
  const [low, setLow] = useState(binding.mode === "range" ? String(binding.low) : "0");
  const [high, setHigh] = useState(binding.mode === "range" ? String(binding.high) : "1");
  const key = `binding-${name}-${component}`;

  useEffect(() => {
    setMode(binding.mode);
    if (binding.mode === "fixed") setFixed(String(binding.value));
    else {
      setSlot(binding.slot);
      setLow(String(binding.low));
      setHigh(String(binding.high));
    }
  }, [binding.mode, binding.mode === "fixed" ? binding.value : binding.slot,
    binding.mode === "range" ? binding.low : null, binding.mode === "range" ? binding.high : null]);

  function submit(event: FormEvent) {
    event.preventDefault();
    apply(mode === "fixed" ? { mode, value: Number(fixed) } : { mode, slot, low: Number(low), high: Number(high) });
  }

  return <form className="uniform-row" onSubmit={submit}>
    <div className="uniform-name"><strong>{name}{component ? `[${component}]` : ""}</strong>
      <output>{value.toLocaleString(undefined, { maximumFractionDigits: 4 })}</output></div>
    <div><label htmlFor={key + "-mode"}>Mode</label>
      <select id={key + "-mode"} value={mode} disabled={disabled}
        onChange={event => setMode(event.target.value === "range" ? "range" : "fixed")}>
        <option value="fixed">Fixed</option><option value="range">OSC range</option>
      </select></div>
    {mode === "fixed" ? <div className="fixed-control">
      <div><label htmlFor={key + "-value"}>Value</label>
        <input id={key + "-value"} type="number" step="any" required value={fixed}
          onChange={event => setFixed(event.target.value)} disabled={disabled} /></div>
      <input aria-label={`${name} component ${component + 1} adjustment`} type="range" min={Math.min(-2, Number(fixed))}
        max={Math.max(2, Number(fixed))} step="0.01" value={fixed}
        onChange={event => setFixed(event.target.value)} disabled={disabled} />
    </div> : <div className="range-fields">
      <div className="slot-select"><label htmlFor={key + "-slot"}>OSC slot</label>
        <select id={key + "-slot"} value={slot} onChange={event => setSlot(event.target.value)} required disabled={disabled}>
          <option value="">Choose a slot</option>{slots.map(entry => <option value={entry.id} key={entry.id}>{entry.label}</option>)}
        </select></div>
      <div><label htmlFor={key + "-low"}>Low</label><input id={key + "-low"} type="number" step="any" required value={low}
        onChange={event => setLow(event.target.value)} disabled={disabled} /></div>
      <div><label htmlFor={key + "-high"}>High</label><input id={key + "-high"} type="number" step="any" required value={high}
        onChange={event => setHigh(event.target.value)} disabled={disabled} /></div>
    </div>}
    <button disabled={disabled || (mode === "range" && !slots.length)}>Apply</button>
  </form>;
}

export function DraftPane({ workspace, onWorkspace }: {
  workspace: WorkspaceView | null;
  onWorkspace: (value: WorkspaceView) => void;
}) {
  const api = window.voscAuthoring;
  const [slotName, setSlotName] = useState("");
  const [busy, setBusy] = useState(false);
  const [problem, setProblem] = useState("");
  const draft = workspace?.project;
  const reflected = workspace?.reflected;
  const ready = Boolean(workspace?.previewReady);
  const controls = new Set(draft?.bindings.map(binding => binding.name));
  const managedUniforms = reflected?.uniforms.filter(uniform => !controls.has(uniform.name));

  async function act(action: () => Promise<void>) {
    setBusy(true);
    setProblem("");
    try { await action(); } catch (error) { setProblem(message(error)); }
    finally { setBusy(false); }
  }

  return <section className="feature" aria-labelledby="draft-heading">
    <header className="section-heading">
      <h2 id="draft-heading">Controls</h2>
    </header>
    {problem && <p role="alert" className="error">{problem}</p>}
    {!draft && <p className="empty">Controls appear when a validated source is previewed.</p>}
    {draft && !ready && <div className="form-strip">
      <p>{workspace?.notice ?? "Restore this draft to its paired host."}</p>
      <button className="primary" disabled={busy || workspace?.peer?.target !== draft.destination.target || workspace?.peer?.fingerprint !== draft.destination.fingerprint}
        onClick={() => void act(async () => onWorkspace(await api.restoreDraft()))}>Restore preview</button>
    </div>}
    {ready && reflected && <details className="native-receipt"><summary>Draw details</summary><dl className="receipt">
      <div><dt>Target</dt><dd>{reflected.target}</dd></div>
      <div><dt>Epoch</dt><dd>{reflected.epoch}</dd></div>
      <div><dt>Revision</dt><dd>{reflected.revision}</dd></div>
      <div><dt>Frame</dt><dd>{reflected.frame ?? reflected.receipt?.frame ?? "Unavailable"}</dd></div>
      <div><dt>Drawn receipt</dt><dd>{workspace?.drawnReceipt?.id ?? "Unavailable"}</dd></div>
    </dl></details>}
    {draft && <section className="feature-section" aria-labelledby="slots-heading">
      <h3 id="slots-heading">OSC slots</h3>
      <form className="form-fields" onSubmit={event => {
        event.preventDefault();
        void act(async () => { onWorkspace(await api.configureSlot(slotName)); setSlotName(""); });
      }}>
        <div className="wide"><label htmlFor="slot-name">Slot name</label>
          <input id="slot-name" value={slotName} onChange={event => setSlotName(event.target.value)}
            maxLength={80} required disabled={busy || !ready} /></div>
        <button disabled={busy || !ready}>Add slot</button>
      </form>
      {draft.slots.length > 0 && <dl className="slot-list">{draft.slots.map(slot =>
        <div key={slot.id}><dt>{slot.label}</dt><dd><code>{slot.id}</code></dd></div>)}</dl>}
      {ready && <div className="form-strip">
        {workspace?.samplePort ? <>
          <p>Send <code>/input/data</code> with a slot ID and amplitude to <code>127.0.0.1:{workspace.samplePort}</code>.</p>
          <button disabled={busy} onClick={() => void act(async () => onWorkspace(await api.stopSamples()))}>Stop OSC input</button>
        </> : <button disabled={busy || !draft.slots.length}
          onClick={() => void act(async () => onWorkspace(await api.startSamples()))}>Enable OSC input</button>}
      </div>}
    </section>}
    {draft && <section className="feature-section" aria-labelledby="uniforms-heading">
      <h3 id="uniforms-heading">Shader controls</h3>
      {draft.bindings.length === 0 && <p className="muted">This shader has no writable controls.</p>}
      {draft.bindings.map(item => {
        const uniform = reflected?.uniforms.find(value => value.name === item.name);
        const value = uniform?.values[item.component] ?? (item.binding.mode === "fixed" ? item.binding.value : item.binding.low);
        return <ComponentControl key={`${item.name}-${item.component}`} name={item.name} component={item.component}
          value={value} binding={item.binding} slots={draft.slots} disabled={busy || !ready}
          apply={binding => void act(async () => onWorkspace(await api.bindComponent(item.name, item.component, binding)))} />;
      })}
      {ready && managedUniforms && managedUniforms.length > 0 && <p className="muted">Read-only: {managedUniforms.map(value => value.name).join(", ")}</p>}
    </section>}
  </section>;
}
