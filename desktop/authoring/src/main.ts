import { app, BrowserWindow, dialog, ipcMain, protocol, session, type IpcMainInvokeEvent } from "electron";
import { readFileSync } from "node:fs";
import { readFile } from "node:fs/promises";
import { dirname, join } from "node:path";
import { Importer } from "./importer";
import { HostClient } from "./host-client";
import { Workspace } from "./workspace";

const SCHEME = "vosc-authoring";
const APP_URL = `${SCHEME}://app/`;
const FILES = new Map([
  ["/", ["index.html", "text/html; charset=utf-8"]],
  ["/renderer.js", ["renderer.js", "text/javascript; charset=utf-8"]],
  ["/styles.css", ["styles.css", "text/css; charset=utf-8"]],
]);
const CSP = "default-src 'none'; script-src 'self'; style-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'";
const dataRoot = app.getPath("userData");
const packageMetadata = JSON.parse(readFileSync(join(__dirname, "package.json"), "utf8")) as {
  productName: string; voscShadertoyFixture?: boolean;
};
const importer = new Importer(process.resourcesPath, join(dataRoot, "library"),
  packageMetadata.productName === "VOSC Authoring Fixture" && packageMetadata.voscShadertoyFixture === true);
const host = new HostClient(dataRoot, join(dirname(process.execPath), "vosc-browse"));
const workspace = new Workspace(host);
let window: BrowserWindow | null = null;
let filePicker = false;

protocol.registerSchemesAsPrivileged([{ scheme: SCHEME, privileges: { standard: true, secure: true } }]);

function trusted(event: IpcMainInvokeEvent) {
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
  handle("import:inspect", (index) => importer.inspectSource(index));
  handle("import:validate", () => importer.validate(host));
  handle("import:activate", (index) => workspace.activate(() => importer.activateSource(index)));
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
  handle("draft:slot", (label) => workspace.configureSlot(label));
  handle("draft:binding", (name, component, binding) => workspace.bindComponent(name, component, binding));
  handle("draft:sample-start", () => workspace.startSamples());
  handle("draft:sample-stop", () => workspace.stopSamples());
  handle("draft:restore", () => workspace.restore());
  handle("draft:save", async () => {
    if (!window || !workspace.view().previewReady) return null;
    const result = await dialog.showSaveDialog({
      title: "Save VOSC Draft",
      defaultPath: "Untitled.vosc-draft",
    });
    return result.canceled || !result.filePath ? null : workspace.save(result.filePath);
  });
  handle("draft:open", async () => {
    if (!window) return workspace.view();
    const result = await dialog.showOpenDialog({
      title: "Open VOSC Draft", properties: ["openFile"],
    });
    return result.canceled ? workspace.view() : workspace.open(result.filePaths[0]);
  });
  handle("draft:export", async () => {
    if (!window || !workspace.view().project) return null;
    const result = await dialog.showOpenDialog({
      title: "Choose a folder for native replay", properties: ["openDirectory", "createDirectory"],
    });
    return result.canceled ? null : workspace.export(result.filePaths[0]);
  });
}

function createWindow() {
  window = new BrowserWindow({
    width: 1040,
    height: 720,
    minWidth: 680,
    minHeight: 540,
    title: packageMetadata.productName,
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
  window.once("closed", () => {
    importer.cancel();
    workspace.stopSamples();
    window = null;
  });
  void window.loadURL(APP_URL);
}

app.disableHardwareAcceleration();
void app.whenReady().then(() => {
  session.defaultSession.setPermissionRequestHandler((_contents, _permission, callback) => callback(false));
  session.defaultSession.setPermissionCheckHandler(() => false);
  protocol.handle(SCHEME, async (request) => {
    const url = new URL(request.url);
    const file = url.host === "app" && !url.search ? FILES.get(url.pathname) : undefined;
    if (!file || request.method !== "GET") return new Response(null, { status: 404 });
    const [filename, type] = file;
    const content = await readFile(join(__dirname, filename));
    return new Response(new Uint8Array(content), {
      headers: { "Content-Type": type, "Content-Security-Policy": CSP, "X-Content-Type-Options": "nosniff" },
    });
  });
  installImportHandlers();
  installDraftHandlers();
  createWindow();
});

app.on("activate", () => {
  if (!window) createWindow();
});
app.on("window-all-closed", () => app.quit());
