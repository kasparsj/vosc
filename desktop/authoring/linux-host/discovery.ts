import { spawn } from "node:child_process";
import { networkInterfaces } from "node:os";
import { join } from "node:path";

export type Discovery = { close(): Promise<void> };

export async function publishDiscovery(options: {
  bind: string; interfaceName: string; target: string; port: number; signal: AbortSignal;
  onLost: (error: Error) => void;
}): Promise<Discovery> {
  const addresses = networkInterfaces()[options.interfaceName]?.filter(address => address.family === "IPv4");
  if (addresses?.length !== 1 || addresses[0].internal || addresses[0].address !== options.bind) {
    throw new Error("DNS-SD needs the listener's IPv4 address on the selected LAN interface");
  }
  options.signal.throwIfAborted();
  const child = spawn("/usr/bin/python3", [
    join(__dirname, "avahi_publish.py"), options.interfaceName, options.target, String(options.port),
  ], { stdio: ["pipe", "pipe", "pipe"] });
  // Closing this inherited pipe also withdraws discovery after unexpected parent loss.
  child.stdin.on("error", () => {});
  let expectedExit = false;
  const closed = new Promise<void>(resolve => child.once("close", () => resolve()));
  const close = async () => {
    expectedExit = true;
    child.stdin.end();
    const timeout = setTimeout(() => child.kill("SIGKILL"), 2000);
    try { await closed; }
    finally { clearTimeout(timeout); }
  };
  let diagnostic = "";
  child.stderr.setEncoding("utf8").on("data", chunk => { diagnostic = (diagnostic + chunk).slice(-1024); });
  try {
    await new Promise<void>((resolve, reject) => {
      const timeout = setTimeout(() => reject(new Error("Avahi registration timed out")), 8000);
      const abort = () => reject(new Error("Stopped before DNS-SD registration"));
      let output = "";
      const done = () => {
        clearTimeout(timeout);
        options.signal.removeEventListener("abort", abort);
      };
      options.signal.addEventListener("abort", abort, { once: true });
      child.once("error", error => { done(); reject(error); });
      child.once("close", () => {
        done();
        if (expectedExit) return;
        const error = new Error("Avahi registration ended" + (diagnostic.trim() ? " (" + diagnostic.trim() + ")" : ""));
        if (output === "READY\n") options.onLost(error);
        else reject(error);
      });
      child.stdout.setEncoding("utf8").on("data", chunk => {
        output = (output + chunk).slice(0, 32);
        if (output === "READY\n") {
          done();
          resolve();
        }
      });
    });
    return { close };
  } catch (error) {
    await close();
    throw error;
  }
}
