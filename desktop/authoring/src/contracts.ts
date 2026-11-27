export const SOURCE_LIMIT = 128 * 1024;
export const SLOT_LIMIT = 16;
export const BINDING_LIMIT = 1_000_000;

export interface Diagnostic {
  code: string;
  message: string;
}

export interface ImportItem {
  id: string;
  libraryId?: string;
  path: string;
  origin: string;
  status: "awaiting_validation" | "validation_failed" | "skipped" | "published" | "reused" | "reused_edited";
  code: string;
  message: string;
  sourceDigest?: string;
  generatedDigest?: string;
}

export interface StageReport {
  batchId: string | null;
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
  inspectSource(batchId: string, itemId: string): Promise<SourceView | null>;
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
export interface SourceProvenance {
  provider: string;
  path?: string;
  url?: string;
  repository?: string;
  requestedRef?: string;
  commit?: string;
  originalDigest?: string;
  convertedDigest?: string;
  convertedBase64?: string;
}
export interface DraftSource {
  name: string;
  digest: string;
  base64: string;
  origin: string;
  provenance?: SourceProvenance;
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
export interface ControlSignature {
  name: string;
  type: string;
  arity: number;
  owner: "layer";
}
export interface DraftControls {
  slots: Slot[];
  bindings: ComponentBinding[];
  signatures: ControlSignature[];
}
export interface AcceptedArtifact {
  id: string;
  revision: number | null;
  project: DraftProject;
  signatures: ControlSignature[];
}
export interface DraftDocument {
  format: "vosc.authoring.draft";
  version: 2;
  id: string;
  title: string;
  language: "glsl";
  source: DraftSource;
  destination: Peer | null;
  controls: DraftControls;
  accepted: AcceptedArtifact | null;
}
export interface DocumentEdit {
  id: string;
  generation: number;
  editSequence: number;
  text: string;
}
export interface DocumentView {
  id: string;
  generation: number;
  editSequence: number;
  title: string;
  language: "glsl";
  source: { name: string; digest: string; origin: string; text: string };
  dirty: boolean;
  acceptedArtifact: {
    id: string;
    sourceDigest: string;
    revision: number | null;
    saved: boolean;
    stale: boolean;
  } | null;
}
export type FileCommand = "new" | "open" | "save" | "saveAs" | "close" | "quit";
export interface LibraryItem { id: string; path: string; origin: string }

export interface WorkspaceView {
  document: DocumentView | null;
  documentGeneration: number;
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
  validateImports(batchId: string, itemIds: string[]): Promise<StageReport>;
  openImported(batchId: string, itemId: string, generation: number): Promise<WorkspaceView>;
  listLibrary(): Promise<LibraryItem[]>;
  openLibrary(libraryId: string, generation: number): Promise<WorkspaceView>;
  previewLibrary(libraryId: string, generation: number): Promise<WorkspaceView>;
  reflect(): Promise<WorkspaceView>;
  configureSlot(label: string): Promise<WorkspaceView>;
  bindComponent(name: string, component: number, binding: Binding): Promise<WorkspaceView>;
  startSamples(): Promise<WorkspaceView>;
  stopSamples(): Promise<WorkspaceView>;
  editDraft(edit: DocumentEdit): Promise<DocumentView>;
  newDraft(): Promise<WorkspaceView>;
  saveDraft(saveAs?: boolean): Promise<string | null>;
  onFileCommand(listener: (command: FileCommand) => void): () => void;
  closeDraftWindow(quit?: boolean): Promise<boolean>;
  openDraft(): Promise<WorkspaceView>;
  restoreDraft(): Promise<WorkspaceView>;
  exportReplay(): Promise<string | null>;
}
