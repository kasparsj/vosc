import { contextBridge, ipcRenderer } from "electron";
import type { AuthoringAPI } from "./contracts";

const api: AuthoringAPI = {
  stageFile: () => ipcRenderer.invoke("import:file"),
  stagePaste: (source) => ipcRenderer.invoke("import:paste", source),
  stageGitHubFile: (url) => ipcRenderer.invoke("import:github-file", url),
  stageRepository: (url, ref) => ipcRenderer.invoke("import:github-repository", url, ref),
  stageShadertoy: (url) => ipcRenderer.invoke("import:shadertoy", url),
  cancelImport: () => ipcRenderer.invoke("import:cancel"),
  inspectSource: (index) => ipcRenderer.invoke("import:inspect", index),
  validateImports: () => ipcRenderer.invoke("import:validate"),
  activateSource: (index) => ipcRenderer.invoke("import:activate", index),
  discover: () => ipcRenderer.invoke("host:discover"),
  inspectHost: (endpoint, target) => ipcRenderer.invoke("host:inspect", endpoint, target),
  pairHost: (fingerprint, name) => ipcRenderer.invoke("host:pair", fingerprint, name),
  checkPairing: () => ipcRenderer.invoke("host:pair-status"),
  connectPaired: (fingerprint) => ipcRenderer.invoke("host:connect-paired", fingerprint),
  disconnect: () => ipcRenderer.invoke("host:disconnect"),
  workspace: () => ipcRenderer.invoke("draft:workspace"),
  reflect: () => ipcRenderer.invoke("draft:reflect"),
  configureSlot: (label) => ipcRenderer.invoke("draft:slot", label),
  bindComponent: (name, component, binding) => ipcRenderer.invoke("draft:binding", name, component, binding),
  startSamples: () => ipcRenderer.invoke("draft:sample-start"),
  stopSamples: () => ipcRenderer.invoke("draft:sample-stop"),
  saveDraft: () => ipcRenderer.invoke("draft:save"),
  openDraft: () => ipcRenderer.invoke("draft:open"),
  restoreDraft: () => ipcRenderer.invoke("draft:restore"),
  exportReplay: () => ipcRenderer.invoke("draft:export"),
};

contextBridge.exposeInMainWorld("voscAuthoring", api);
