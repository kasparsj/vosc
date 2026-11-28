import { app, BrowserWindow, dialog, ipcMain, Menu, protocol, session, type IpcMainEvent, type IpcMainInvokeEvent, type MenuItemConstructorOptions } from "electron";
import { randomBytes } from "node:crypto";
import { constants, mkdirSync, readFileSync } from "node:fs";
import { open, readFile } from "node:fs/promises";
import { basename, dirname, extname, join, resolve } from "node:path";
import { Importer } from "./importer";
import { HostClient } from "./host-client";
import { CALLBACK_UNIFORM, SOURCE_LIMIT, type FileCommand } from "./contracts";
import { sourceName, sourceWithText } from "./draft-project";
import { feedbackExample, feedbackExampleSource } from "./feedback-example";
import { ElectronHydraEvaluator, HYDRA_SCHEME } from "./hydra-evaluator";
import { HostError } from "./local-state";
import { draftFileToken, Workspace } from "./workspace";

const SCHEME = "vosc-authoring";
const APP_URL = `${SCHEME}://app/`;
const FILES = new Map([
  ["/", ["index.html", "text/html; charset=utf-8"]],
  ["/renderer.js", ["renderer.js", "text/javascript; charset=utf-8"]],
  ["/styles.css", ["styles.css", "text/css; charset=utf-8"]],
]);
for (const name of ["plus", "folder", "down", "run", "code", "library-code", "arrow", "export", "close"]) {
  FILES.set(`/icons/${name}.svg`, [`icons/${name}.svg`, "image/svg+xml"]);
}
const CSP = "img-src 'self'; default-src 'none'; script-src 'self'; style-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'";
const packageMetadata = JSON.parse(readFileSync(join(__dirname, "package.json"), "utf8")) as {
  productName: string; voscProductName?: string; voscShadertoyFixture?: boolean;
};
const displayName = packageMetadata.voscProductName ?? packageMetadata.productName;
const fixture = displayName === "VOSC Editor Fixture" && packageMetadata.voscShadertoyFixture === true;
if (!app.commandLine.hasSwitch("user-data-dir")) {
  // Keep saved libraries and controller identities under their original profile.
  const profile = join(app.getPath("appData"), fixture ? "VOSC Authoring Fixture" : "VOSC Authoring");
  mkdirSync(profile, { recursive: true });
  app.setPath("userData", profile);
}
const dataRoot = app.getPath("userData");
const importer = new Importer(process.resourcesPath, join(dataRoot, "library"),
  fixture);
const discoveryHelper = process.platform === "linux" ? join(__dirname, "avahi_browse.py")
  : join(dirname(process.execPath), "vosc-browse");
const host = new HostClient(dataRoot, discoveryHelper);
const workspace = new Workspace(host, new ElectronHydraEvaluator().evaluate);
let window: BrowserWindow | null = null;
let filePicker = false;
let navigating = false;
let allowClose = false;
let closing = false;

protocol.registerSchemesAsPrivileged([
  { scheme: SCHEME, privileges: { standard: true, secure: true } },
  { scheme: HYDRA_SCHEME, privileges: { standard: true, secure: true, supportFetchAPI: true, corsEnabled: true } },
]);

function trusted(event: IpcMainInvokeEvent | IpcMainEvent) {
  if (!window || event.sender !== window.webContents || event.senderFrame !== window.webContents.mainFrame ||
      event.senderFrame.url !== APP_URL) {
    throw new Error("Request came from an unauthorized frame.");
  }
}

type Method = (...args: unknown[]) => unknown;
function handle(channel: string, action: Method) {
  ipcMain.handle(channel, (event, ...args: unknown[]) => {
    trusted(event);
    if (args.length !== action.length) throw new Error("Invalid argument count.");
    return action(...args);
  });
}

function fileCommand(command: FileCommand) {
  if (!navigating && window && !window.webContents.isCrashed()) window.webContents.send("draft:file-command", command);
}

async function navigate<T>(action: () => Promise<T>): Promise<T> {
  if (navigating || closing) throw new HostError("busy", "Wait for the current file command.");
  navigating = true;
  try { return await action(); }
  finally { navigating = false; }
}

function checkGeneration(expected: unknown) {
  if (expected !== workspace.documentContext().generation) {
    throw new HostError("stale_document", "The active draft changed. Choose the shader again.");
  }
}

async function saveDocument(saveAs: boolean, capture = workspace.captureSave()): Promise<string | null> {
  if (!window) return null;
  const previous = workspace.destinationForSave();
  if (!previous.title) return null;
  const documentWindow = window;
  const baseName = basename(previous.title).replace(/[:\\]/g, "_") || "Untitled";
  let path = saveAs ? null : previous.path;
  let token: string | null;
  if (!path) {
    const result = await dialog.showSaveDialog(documentWindow, {
      title: saveAs ? "Save VOSC Draft As" : "Save VOSC Draft",
      defaultPath: previous.path ?? `${baseName}.vosc-draft`,
      filters: [{ name: "VOSC Draft", extensions: ["vosc-draft"] }],
    });
    if (result.canceled || !result.filePath) return null;
    path = resolve(result.filePath);
    token = await draftFileToken(path);
  } else {
    token = previous.token;
  }

  for (;;) {
    const observed = await draftFileToken(path);
    if (observed !== token) {
      const { response } = await dialog.showMessageBox(documentWindow, {
        type: "warning", message: "The draft file changed on disk.",
        detail: "Save a separate copy or replace the current file.",
        buttons: ["Cancel", "Save As…", "Overwrite"], defaultId: 0, cancelId: 0,
      });
      if (response === 0) return null;
      if (response === 1) return saveDocument(true, capture);
      token = observed;
    }
    try {
      return await workspace.save(path, token, capture);
    } catch (error) {
      if ((error as HostError).code !== "file_changed" && (error as NodeJS.ErrnoException).code !== "EEXIST") throw error;
      // An outside writer changed the destination during the atomic write.
      // The next pass asks before replacing that new file.
    }
  }
}

async function mayReplace() {
  if (!window) return false;
  if (!workspace.needsSave()) return true;
  const { generation, editSequence } = workspace.documentContext();
  const { response } = await dialog.showMessageBox(window, {
    type: "question", message: "Save changes to this draft?",
    buttons: ["Save", "Discard", "Cancel"], defaultId: 0, cancelId: 2,
  });
  if (response === 2) return false;
  checkGeneration(generation);
  if (response === 0) return await saveDocument(false) !== null && !workspace.needsSave();
  if (workspace.documentContext().editSequence !== editSequence) {
    throw new HostError("stale_document", "The draft changed during the prompt. Try again.");
  }
  return true;
}

function installMenu() {
  const file: MenuItemConstructorOptions = { label: "File", submenu: [
    { label: "New", submenu: [
      { label: "GLSL Fragment", accelerator: "CommandOrControl+N", click: () => fileCommand("new") },
      { label: "Hydra Patch", click: () => fileCommand("newHydra") },
    ] },
    { label: "Open Draft…", accelerator: "CommandOrControl+O", click: () => fileCommand("open") },
    { label: "Save Draft", accelerator: "CommandOrControl+S", click: () => fileCommand("save") },
    { label: "Save Draft As…", accelerator: "CommandOrControl+Shift+S", click: () => fileCommand("saveAs") },
    { type: "separator" },
    { label: "Close Window", accelerator: "CommandOrControl+W", click: () => fileCommand("close") },
  ] };
  Menu.setApplicationMenu(Menu.buildFromTemplate([
    ...(process.platform === "darwin" ? [{ role: "appMenu" as const }] : []),
    file, { role: "editMenu" }, { role: "viewMenu" }, { role: "windowMenu" },
  ]));
}

function installImportHandlers() {
  handle("import:file", async () => {
    if (!window || filePicker || importer.busy) return null;
    filePicker = true;
    try {
      const result = await dialog.showOpenDialog({
        title: "Open Fragment Shader",
        properties: ["openFile"],
      });
      return result.canceled ? null : importer.stage("file", result.filePaths[0]);
    } finally {
      filePicker = false;
    }
  });
  handle("import:paste", (source) => importer.stage("paste", source));
  handle("import:github-file", (url) => importer.stage("github", url));
  handle("import:github-repository", (url, ref) => importer.stage("repository", url, ref));
  handle("import:shadertoy", (url) => importer.stage("shadertoy", url));
  handle("import:cancel", () => importer.cancel());
  handle("import:inspect", (batch, item) => importer.inspectSource(batch, item));
  handle("import:validate", (batch, items) => importer.validate(host, batch, items));
  handle("import:open", (batch, item, generation) => navigate(async () => {
    checkGeneration(generation);
    importer.openStaged(batch, item);
    if (!await mayReplace()) return workspace.view();
    checkGeneration(generation);
    return workspace.openSource(importer.openStaged(batch, item));
  }));
  handle("library:list", async () => [feedbackExample, ...await importer.listLibrary()]);
  handle("library:open", (id, generation) => navigate(async () => {
    checkGeneration(generation);
    const example = id === feedbackExample.id;
    const source = example ? feedbackExampleSource() : await importer.openLibrary(id);
    if (!await mayReplace()) return workspace.view();
    checkGeneration(generation);
    return workspace.openSource(source, example ? "hydra-js" : "glsl");
  }));
  handle("library:preview", (id, generation) => navigate(async () => {
    checkGeneration(generation);
    const source = await importer.openLibrary(id);
    if (!await mayReplace()) return workspace.view();
    checkGeneration(generation);
    return workspace.activate(async () => source);
  }));
}

function installDraftHandlers() {
  handle("host:discover", () => host.browse());
  handle("host:inspect", (endpoint, target) => host.inspect(endpoint, target));
  handle("host:pair", (fingerprint, name) => host.beginPair(fingerprint, name));
  handle("host:pair-status", () => workspace.checkPair());
  handle("host:connect-paired", (fingerprint) => workspace.connectRemembered(fingerprint));
  handle("host:disconnect", () => workspace.disconnect());
  handle("draft:workspace", () => workspace.view());
  handle("draft:reflect", () => workspace.reflect());
  handle("draft:run", (mode) => workspace.run(mode));
  handle("draft:approve-run", (token) => workspace.approveRun(token));
  handle("draft:cancel-run", (token) => workspace.cancelRun(token));
  handle("draft:use-host", () => workspace.useSelectedHost());
  handle("draft:mapping-discard", (name) => workspace.discardMapping(name));
  handle("draft:mapping-remap", (name, target) => workspace.remapMapping(name, target));
  handle("draft:slot", (label) => workspace.configureSlot(label));
  handle("draft:binding", (name, component, binding) => workspace.bindComponent(name, component, binding));
  handle("draft:sample-start", () => workspace.startSamples());
  handle("draft:sample-stop", () => workspace.stopSamples());
  handle("draft:restore", () => workspace.restore());
  ipcMain.on("draft:invalidate-hydra-edit", (event, edit: unknown) => {
    try {
      trusted(event);
      event.returnValue = workspace.invalidateHydraEdit(edit);
    } catch {
      event.returnValue = false;
    }
  });
  handle("draft:edit", (edit) => workspace.edit(edit));
  handle("draft:save", (saveAs) => navigate(() => saveDocument(saveAs === true)));
  handle("draft:new", (language) => navigate(async () => {
    if (language !== "glsl" && language !== "hydra-js") throw new HostError("invalid_request", "Choose a draft language.");
    return await mayReplace() ? workspace.create(language) : workspace.view();
  }));
  handle("draft:open", () => navigate(async () => {
    if (!window) return workspace.view();
    const generation = workspace.documentContext().generation;
    const result = await dialog.showOpenDialog(window, {
      title: "Open VOSC Draft", properties: ["openFile"],
      filters: [{ name: "VOSC Draft or Hydra JavaScript", extensions: ["vosc-draft", "js"] }],
    });
    checkGeneration(generation);
    if (result.canceled) return workspace.view();
    const path = resolve(result.filePaths[0]);
    if (extname(path).toLowerCase() !== ".js") return workspace.open(path, mayReplace);
    const file = await open(path, constants.O_RDONLY | constants.O_NOFOLLOW | constants.O_NONBLOCK);
    let patch;
    try {
      if (!(await file.stat()).isFile()) throw new HostError("invalid_source", "Choose a regular JavaScript file.");
      const bytes = Buffer.alloc(SOURCE_LIMIT + 1);
      const { bytesRead } = await file.read(bytes, 0, bytes.length);
      if (bytesRead > SOURCE_LIMIT) throw new HostError("invalid_source", "Hydra source exceeds 128 KiB.");
      patch = sourceWithText({ name: sourceName(path), origin: "Local Hydra patch", digest: "", base64: "" },
        new TextDecoder("utf-8", { fatal: true, ignoreBOM: true }).decode(bytes.subarray(0, bytesRead)));
    } finally {
      await file.close();
    }
    if (!await mayReplace()) return workspace.view();
    checkGeneration(generation);
    return workspace.openSource(patch, "hydra-js");
  }));
  handle("draft:close", (quit) => navigate(async () => {
    if (!await mayReplace()) return false;
    void workspace.cancelRun();
    const target = window;
    const { generation, editSequence } = workspace.documentContext();
    closing = true;
    setImmediate(() => {
      closing = false;
      if (!target || target !== window) return;
      const current = workspace.documentContext();
      if (current.generation !== generation || current.editSequence !== editSequence) {
        return fileCommand(quit === true ? "quit" : "close");
      }
      allowClose = true;
      if (quit === true) app.quit();
      else target.close();
    });
    return true;
  }));
  handle("draft:export", () => navigate(async () => {
    if (!window || !workspace.view().project) return null;
    const artifact = workspace.captureExport();
    const title = `Export ${artifact.label} for native replay`;
    const frozen = artifact.callbackValue === undefined ? undefined :
      `Standalone replay freezes ${CALLBACK_UNIFORM} at ${artifact.callbackValue}.`;
    const result = await dialog.showOpenDialog(window, {
      title: process.platform === "darwin" ? title : frozen ?? title,
      message: frozen,
      properties: ["openDirectory", "createDirectory"],
    });
    return result.canceled ? null : artifact.write(result.filePaths[0]);
  }));
}

function createWindow() {
  window = new BrowserWindow({
    width: 1040,
    height: 720,
    minWidth: 680,
    minHeight: 540,
    title: displayName,
    backgroundColor: "#171b21",
    show: false,
    webPreferences: {
      preload: join(__dirname, "preload.cjs"),
      sandbox: true,
      contextIsolation: true,
      nodeIntegration: false,
      webviewTag: false,
      // Keep draft reflection responsive when the native preview covers this window.
      backgroundThrottling: false,
      devTools: !app.isPackaged,
    },
  });
  window.webContents.setWindowOpenHandler(() => ({ action: "deny" }));
  window.webContents.on("will-frame-navigate", (event) => event.preventDefault());
  window.webContents.on("will-attach-webview", (event) => event.preventDefault());
  window.once("ready-to-show", () => window?.show());
  window.on("close", (event) => {
    if (allowClose) return;
    if (window?.webContents.isCrashed() && !workspace.needsSave()) return;
    event.preventDefault();
    if (window?.webContents.isCrashed()) {
      void navigate(async () => {
        if (!await mayReplace()) return;
        allowClose = true;
        window?.close();
      }).catch(error => dialog.showErrorBox("VOSC Editor", String(error)));
      return;
    }
    fileCommand("close");
  });
  window.once("closed", () => {
    allowClose = false;
    workspace.clear();
    importer.cancel();
    workspace.stopSamples();
    window = null;
  });
  void window.loadURL(APP_URL);
}

app.disableHardwareAcceleration();
void app.whenReady().then(() => {
  // Startup has already selected the controller's stable Keychain namespace.
  app.setName(displayName);
  session.defaultSession.setPermissionRequestHandler((_contents, _permission, callback) => callback(false));
  session.defaultSession.setPermissionCheckHandler(() => false);
  protocol.handle(SCHEME, async (request) => {
    const url = new URL(request.url);
    const file = url.host === "app" && !url.search ? FILES.get(url.pathname) : undefined;
    if (!file || request.method !== "GET") return new Response(null, { status: 404 });
    const [filename, type] = file;
    const content = await readFile(join(__dirname, filename));
    const nonce = url.pathname === "/" ? randomBytes(18).toString("base64") : null;
    const body = nonce ? content.toString("utf8").replace("</head>", `<meta name="vosc-style-nonce" content="${nonce}">
</head>`) : new Uint8Array(content);
    const csp = nonce ? CSP.replace("style-src 'self'", `style-src 'self' 'nonce-${nonce}'`) : CSP;
    return new Response(body, {
      headers: { "Content-Type": type, "Content-Security-Policy": csp, "X-Content-Type-Options": "nosniff" },
    });
  });
  installImportHandlers();
  installDraftHandlers();
  installMenu();
  createWindow();
});

app.on("activate", () => {
  if (!window) createWindow();
});
app.on("before-quit", (event) => {
  if (allowClose || !window) return;
  event.preventDefault();
  if (window.webContents.isCrashed()) { window.close(); return; }
  fileCommand("quit");
});
app.on("window-all-closed", () => app.quit());
