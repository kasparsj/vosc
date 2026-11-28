import { useEffect, useState, type FormEvent } from "react";
import { BINDING_LIMIT, CALLBACK_UNIFORM, type Binding, type DraftUniform, type PendingMapping, type Slot, type WorkspaceView } from "./contracts";
import { message } from "./host-pane";

const uniformArity: Readonly<Record<string, number>> = { float: 1, vec2: 2, vec3: 3, vec4: 4 };
const rendererUniforms = new Set(["camPos", "camLook", "camOrbit"]);

function writableUniform(uniform: DraftUniform) {
  return uniform.writable && uniform.owner === "layer" && /^[A-Za-z_]\w{0,63}$/.test(uniform.name) &&
    !rendererUniforms.has(uniform.name) && uniformArity[uniform.type] === uniform.values.length &&
    uniform.values.every(value => Number.isFinite(value) && Math.abs(value) <= BINDING_LIMIT);
}

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

function RetainedMapping({ mapping, workspace, onControl }: {
  mapping: PendingMapping;
  workspace: WorkspaceView;
  onControl: (action: () => Promise<WorkspaceView>) => void;
}) {
  const [target, setTarget] = useState("");
  const reason = {
    inactive: "Unavailable in the current shader",
    renderer_owned: "Renderer-owned uniform",
    retyped: "Uniform type changed",
  }[mapping.reason];
  const candidates = workspace.previewReady ? workspace.reflected?.uniforms.filter(uniform =>
    writableUniform(uniform) &&
    uniform.values.length === mapping.bindings.length && (!mapping.signature || uniform.type === mapping.signature.type)) ?? [] : [];

  return <div className="retained-mapping">
    <h4>{mapping.name}</h4>
    <p className="muted">{reason} · Retained for review</p>
    <ul>{mapping.bindings.map(({ component, binding }) => <li key={component}>
      {mapping.bindings.length > 1 ? `Component ${component + 1} · ` : ""}
      {binding.mode === "fixed" ? `Fixed ${binding.value}` :
        `OSC ${workspace.project?.slots.find(slot => slot.id === binding.slot)?.label ?? binding.slot} · ${binding.low} to ${binding.high}`}
    </li>)}</ul>
    {!!candidates.length && <label className="remap-target">
      Remap {mapping.name} to
      <select value={target} onChange={event => setTarget(event.target.value)}>
        <option value="">Choose compatible uniform</option>
        {candidates.map(item => <option value={item.name} key={item.name}>{item.name}</option>)}
      </select>
    </label>}
    <div className="actions">
      <button type="button" disabled={!candidates.some(item => item.name === target)}
        onClick={() => onControl(() => window.voscAuthoring.remapMapping(mapping.name, target))}>Stage remap</button>
      <button type="button" onClick={() => onControl(() => window.voscAuthoring.discardMapping(mapping.name))}>Discard retained mapping</button>
    </div>
  </div>;
}

export function DraftPane({ workspace, onWorkspace, onControl }: {
  workspace: WorkspaceView | null;
  onWorkspace: (value: WorkspaceView) => void;
  onControl: (action: () => Promise<WorkspaceView>) => void;
}) {
  const api = window.voscAuthoring;
  const [slotName, setSlotName] = useState("");
  const [busy, setBusy] = useState(false);
  const [problem, setProblem] = useState("");
  const draft = workspace?.project;
  const wgsl = workspace?.document?.language === "wgsl";
  const reflected = workspace?.reflected;
  const ready = Boolean(workspace?.previewReady);
  const pending = workspace?.document?.pendingMappings ?? [];
  const uniforms = ready ? reflected?.uniforms ?? [] : [];
  const callback = workspace?.callback;
  const managedUniforms = uniforms.filter(uniform => !writableUniform(uniform) &&
    !(callback && uniform.name === CALLBACK_UNIFORM));
  const savedBindings = draft?.bindings ?? [];
  const liveBindings = new Map(savedBindings.map(binding => [`${binding.name}:${binding.component}`, binding.binding]));
  const controls = ready ? uniforms.filter(writableUniform).flatMap(uniform =>
    uniform.values.map((value, component) => {
      const binding: Binding = liveBindings.get(`${uniform.name}:${component}`) ?? { mode: "fixed", value };
      return { name: uniform.name, component, value, binding };
    })) : savedBindings.map(item => ({
      ...item, value: item.binding.mode === "fixed" ? item.binding.value : item.binding.low,
    }));

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
        onClick={() => onControl(() => api.restoreDraft())}>Restore preview</button>
    </div>}
    {ready && reflected && <details className="native-receipt"><summary>Draw details</summary><dl className="receipt">
      <div><dt>Target</dt><dd>{reflected.target}</dd></div>
      <div><dt>Epoch</dt><dd>{reflected.epoch}</dd></div>
      <div><dt>Revision</dt><dd>{reflected.revision}</dd></div>
      <div><dt>Frame</dt><dd>{reflected.frame ?? reflected.receipt?.frame ?? "Unavailable"}</dd></div>
      <div><dt>Drawn receipt</dt><dd>{workspace?.drawnReceipt?.id ?? "Unavailable"}</dd></div>
    </dl></details>}
    {draft && !wgsl && <section className="feature-section" aria-labelledby="slots-heading">
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
    {draft && !wgsl && <section className="feature-section" aria-labelledby="uniforms-heading">
      <h3 id="uniforms-heading">Shader controls</h3>
      {callback && <div>
        <p><code>{CALLBACK_UNIFORM}</code> · {callback.active ? "Editor callback" : "Last confirmed"} · {callback.value.toFixed(4)}</p>
        {callback.error && <p className="error" role="status">{callback.error}</p>}
      </div>}
      {controls.length === 0 && <p className="muted">{ready ? "This shader has no writable controls." : "No saved shader controls."}</p>}
      {controls.map(item => <ComponentControl key={`${workspace?.documentGeneration}-${item.name}-${item.component}`}
        name={item.name} component={item.component} value={item.value} binding={item.binding}
        slots={draft.slots} disabled={busy || !ready}
        apply={binding => void act(async () => onWorkspace(await api.bindComponent(item.name, item.component, binding)))} />)}
      {!!managedUniforms.length && <p className="muted">Read-only: {managedUniforms.map(value => value.name).join(", ")}</p>}
    </section>}
    {workspace && !!pending.length && <section className="feature-section" aria-labelledby="mapping-heading">
      <h3 id="mapping-heading">Retained mappings</h3>
      {pending.map(item => <RetainedMapping key={item.name} mapping={item} workspace={workspace} onControl={onControl} />)}
    </section>}
  </section>;
}
