export const SOURCE_LIMIT = 128 * 1024;
export const SLOT_LIMIT = 16;
export const BINDING_LIMIT = 1_000_000;

export interface Diagnostic {
  code: string;
  message: string;
}

export interface ImportItem {
  index: number;
  path: string;
  origin: string;
  status: "awaiting_validation" | "skipped" | "published" | "reused" | "reused_edited";
  code: string;
  message: string;
  sourceDigest?: string;
  generatedDigest?: string;
}

export interface StageReport {
  complete: boolean;
  error: Diagnostic | null;
  items: ImportItem[];
}

export interface SourceView {
  original: string;
  generated: string | null;
}

export interface AuthoringAPI {
  stageFile(): Promise<StageReport | null>;
  stagePaste(source: string): Promise<StageReport>;
  stageGitHubFile(url: string): Promise<StageReport>;
  stageRepository(url: string, ref: string): Promise<StageReport>;
  stageShadertoy(url: string): Promise<StageReport>;
  cancelImport(): Promise<void>;
  inspectSource(index: number): Promise<SourceView | null>;
}

export interface Endpoint { host: string; port: number }
export interface Peer { endpoint: Endpoint; target: string; fingerprint: string }
export interface DiscoveredPeer { endpoint: Endpoint; target: string; name: string }
export interface Discovery { peers: DiscoveredPeer[]; error: string | null }
export interface PeerCandidate { endpoint: Endpoint; fingerprint: string }
export interface PairingRequest { request: string; fingerprint: string; expiresAt: number }
export interface DrawReceipt {
  id: string;
  frame: number;
  revision: number;
  external_epoch: number;
  status: string;
}
export interface DraftUniform {
  name: string;
  values: number[];
  writable: boolean;
  owner: "layer";
  type: string;
  binding?: unknown;
}
export interface Slot { id: string; label: string; default: number }
export interface ReflectedDraft {
  target: string;
  epoch: string;
  revision: number;
  frame?: number;
  receipt?: DrawReceipt;
  sourceDigest?: string;
  uniforms: DraftUniform[];
  slots: Slot[];
  needsRestore: boolean;
}

export type Binding =
  { mode: "fixed"; value: number } |
  { mode: "range"; slot: string; low: number; high: number };
export interface ComponentBinding {
  name: string;
  component: number;
  binding: Binding;
}
export interface DraftSource {
  name: string;
  digest: string;
  base64: string;
  origin: string;
}
export interface DraftProject {
  format: "vosc.authoring.draft";
  version: 1;
  title: string;
  destination: Peer;
  source: DraftSource;
  slots: Slot[];
  bindings: ComponentBinding[];
}
export interface WorkspaceView {
  peer: Peer | null;
  reflected: ReflectedDraft | null;
  project: Omit<DraftProject, "source"> & { source: Omit<DraftSource, "base64"> } | null;
  previewReady: boolean;
  drawnReceipt: DrawReceipt | null;
  samplePort: number | null;
  notice: string | null;
}
export interface AuthoringAPI {
  discover(): Promise<Discovery>;
  inspectHost(endpoint: Endpoint, target: string | null): Promise<PeerCandidate>;
  pairHost(fingerprint: string, name: string): Promise<PairingRequest>;
  checkPairing(): Promise<{ state: string; workspace?: WorkspaceView }>;
  connectPaired(fingerprint: string): Promise<WorkspaceView>;
  disconnect(): Promise<WorkspaceView>;
  workspace(): Promise<WorkspaceView>;
  validateImports(): Promise<StageReport>;
  activateSource(index: number): Promise<WorkspaceView>;
  reflect(): Promise<WorkspaceView>;
  configureSlot(label: string): Promise<WorkspaceView>;
  bindComponent(name: string, component: number, binding: Binding): Promise<WorkspaceView>;
  startSamples(): Promise<WorkspaceView>;
  stopSamples(): Promise<WorkspaceView>;
  saveDraft(): Promise<string | null>;
  openDraft(): Promise<WorkspaceView>;
  restoreDraft(): Promise<WorkspaceView>;
  exportReplay(): Promise<string | null>;
}
