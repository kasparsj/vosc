import { contextBridge, ipcRenderer } from "electron";
import type { AuthoringAPI, FileCommand } from "./contracts";

const api: AuthoringAPI = {
  stageFile: () => ipcRenderer.invoke("import:file"),
  stagePaste: (source) => ipcRenderer.invoke("import:paste", source),
  stageGitHubFile: (url) => ipcRenderer.invoke("import:github-file", url),
  stageRepository: (url, ref) => ipcRenderer.invoke("import:github-repository", url, ref),
  stageShadertoy: (url) => ipcRenderer.invoke("import:shadertoy", url),
  cancelImport: () => ipcRenderer.invoke("import:cancel"),
  inspectSource: (batch, item) => ipcRenderer.invoke("import:inspect", batch, item),
  validateImports: (batch, items) => ipcRenderer.invoke("import:validate", batch, items),
  openImported: (batch, item, generation) => ipcRenderer.invoke("import:open", batch, item, generation),
  listLibrary: () => ipcRenderer.invoke("library:list"),
  openLibrary: (id, generation) => ipcRenderer.invoke("library:open", id, generation),
  previewLibrary: (id, generation) => ipcRenderer.invoke("library:preview", id, generation),
  discover: () => ipcRenderer.invoke("host:discover"),
  inspectHost: (endpoint, target) => ipcRenderer.invoke("host:inspect", endpoint, target),
  pairHost: (fingerprint, name) => ipcRenderer.invoke("host:pair", fingerprint, name),
  checkPairing: () => ipcRenderer.invoke("host:pair-status"),
  connectPaired: (fingerprint) => ipcRenderer.invoke("host:connect-paired", fingerprint),
  disconnect: () => ipcRenderer.invoke("host:disconnect"),
  workspace: () => ipcRenderer.invoke("draft:workspace"),
  reflect: () => ipcRenderer.invoke("draft:reflect"),
  runDraft: (mode = "manual") => ipcRenderer.invoke("draft:run", mode),
  approveRun: token => ipcRenderer.invoke("draft:approve-run", token),
  cancelRun: token => ipcRenderer.invoke("draft:cancel-run", token),
  useSelectedHost: () => ipcRenderer.invoke("draft:use-host"),
  discardMapping: name => ipcRenderer.invoke("draft:mapping-discard", name),
  remapMapping: (name, target) => ipcRenderer.invoke("draft:mapping-remap", name, target),
  configureSlot: (label) => ipcRenderer.invoke("draft:slot", label),
  bindComponent: (name, component, binding) => ipcRenderer.invoke("draft:binding", name, component, binding),
  startSamples: () => ipcRenderer.invoke("draft:sample-start"),
  stopSamples: () => ipcRenderer.invoke("draft:sample-stop"),
  editDraft: edit => ipcRenderer.invoke("draft:edit", edit),
  newDraft: () => ipcRenderer.invoke("draft:new"),
  saveDraft: (saveAs = false) => ipcRenderer.invoke("draft:save", saveAs),
  onFileCommand: listener => {
    const forward = (_event: Electron.IpcRendererEvent, command: FileCommand) => listener(command);
    ipcRenderer.on("draft:file-command", forward);
    return () => { ipcRenderer.removeListener("draft:file-command", forward); };
  },
  closeDraftWindow: (quit = false) => ipcRenderer.invoke("draft:close", quit),
  openDraft: () => ipcRenderer.invoke("draft:open"),
  restoreDraft: () => ipcRenderer.invoke("draft:restore"),
  exportReplay: () => ipcRenderer.invoke("draft:export"),
};

contextBridge.exposeInMainWorld("voscAuthoring", api);
