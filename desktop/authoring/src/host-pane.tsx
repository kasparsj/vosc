import { useEffect, useRef, useState, type FormEvent } from "react";
import type { Discovery, DiscoveredPeer, PairingRequest, PeerCandidate, WorkspaceView } from "./contracts";

export function message(error: unknown) {
  return error instanceof Error
    ? error.message.replace(/^Error invoking remote method '[^']+': (?:Error: )?/, "")
    : "The operation could not be completed.";
}

export function HostPane({ discovery, refresh, workspace, onWorkspace }: {
  discovery: Discovery | null;
  refresh: () => void;
  workspace: WorkspaceView | null;
  onWorkspace: (value: WorkspaceView) => void;
}) {
  const api = window.voscAuthoring;
  const [host, setHost] = useState("");
  const [port, setPort] = useState("");
  const [candidate, setCandidate] = useState<PeerCandidate | null>(null);
  const [fingerprint, setFingerprint] = useState("");
  const [name, setName] = useState("");
  const [pending, setPending] = useState<PairingRequest | null>(null);
  const [busy, setBusy] = useState(false);
  const running = useRef(false);
  const [problem, setProblem] = useState("");

  async function act(action: () => Promise<void>) {
    if (running.current) return;
    running.current = true;
    setBusy(true);
    setProblem("");
    try { await action(); } catch (error) { setProblem(message(error)); }
    finally { running.current = false; setBusy(false); }
  }

  function inspect(event?: FormEvent, selected?: DiscoveredPeer) {
    event?.preventDefault();
    setCandidate(null);
    setPending(null);
    setFingerprint("");
    void act(async () => {
      setCandidate(await api.inspectHost(selected?.endpoint ?? { host, port: Number(port) }, selected?.target ?? null));
    });
  }

  async function check() {
    const result = await api.checkPairing();
    if (result.workspace) onWorkspace(result.workspace);
    if (result.state !== "pending") setPending(null);
    if (result.state === "denied") setProblem("The render host declined this pairing request.");
    if (result.state === "expired") setProblem("The pairing request expired. Start a new request.");
  }

  useEffect(() => {
    if (!pending) return;
    const timer = setInterval(() => { void act(check); }, 1800);
    return () => clearInterval(timer);
  }, [pending]);

  return <section className="feature" aria-labelledby="host-heading">
    <header className="section-heading"><h2 id="host-heading">Render Host</h2>
      <button onClick={refresh} type="button">Refresh discovery</button></header>
    {workspace?.peer && <div className="form-strip">
      <div><strong>Connected</strong><p>{workspace.peer.endpoint.host}:{workspace.peer.endpoint.port}</p></div>
      <button disabled={busy} onClick={() => void act(async () => onWorkspace(await api.disconnect()))}>Disconnect</button>
    </div>}
    <section className="feature-section" aria-labelledby="discover-heading">
      <h3 id="discover-heading">Discovered hosts</h3>
      {discovery?.error && <p role="alert" className="error">{discovery.error}</p>}
      {discovery === null && <p role="status">Looking for render hosts…</p>}
      {discovery && !discovery.error && discovery.peers.length === 0 &&
        <p className="muted">No render hosts found on this network.</p>}
      {discovery?.peers.map(peer => <div className="form-strip" key={peer.target}>
        <div><strong>{peer.name}</strong><p className="muted">{peer.endpoint.host}:{peer.endpoint.port}</p></div>
        <button type="button" disabled={busy} onClick={() => inspect(undefined, peer)}>Inspect host</button>
      </div>)}
    </section>
    <section className="feature-section" aria-labelledby="manual-heading">
      <h3 id="manual-heading">Enter host address</h3>
      <form className="form-fields" onSubmit={inspect}>
        <div className="wide"><label htmlFor="host-address">Host name or IP address</label>
          <input id="host-address" value={host} onChange={event => setHost(event.target.value)} required /></div>
        <div><label htmlFor="host-port">Port</label><input id="host-port" type="number" min={1} max={65535}
          value={port} onChange={event => setPort(event.target.value)} required /></div>
        <button className="primary" disabled={busy}>Inspect host</button>
      </form>
    </section>
    {candidate && <section className="feature-section" aria-labelledby="pair-heading">
      <h3 id="pair-heading">Verify and pair</h3>
      <p className="muted">Compare this fingerprint with the one displayed on the render host.</p>
      <code className="fingerprint">{candidate.fingerprint}</code>
      {!pending && <form className="pair-form" onSubmit={event => {
        event.preventDefault();
        void act(async () => setPending(await api.pairHost(fingerprint, name)));
      }}>
        <label htmlFor="fingerprint">Fingerprint from the render host</label>
        <input id="fingerprint" value={fingerprint} onChange={event => setFingerprint(event.target.value)}
          spellCheck={false} maxLength={95} required />
        <label htmlFor="controller-name">Controller name</label>
        <input id="controller-name" value={name} onChange={event => setName(event.target.value)} maxLength={80} required />
        <div className="actions">
          <button className="primary" disabled={busy}>Request pairing</button>
          <button type="button" disabled={busy}
            onClick={() => void act(async () => onWorkspace(await api.connectPaired(fingerprint)))}>Connect paired host</button>
        </div>
      </form>}
      {pending && <div className="pair-form" role="status">
        <p>Compare this controller fingerprint on the render host before approving.</p>
        <code className="fingerprint">{pending.fingerprint}</code>
        <button disabled={busy} onClick={() => void act(check)}>Check approval</button>
      </div>}
    </section>}
    {problem && <p className="error" role="alert">{problem}</p>}
  </section>;
}
