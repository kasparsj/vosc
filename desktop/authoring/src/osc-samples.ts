import { createSocket, type Socket } from "node:dgram";
import type { AddressInfo } from "node:net";

function stringAt(data: Buffer, offset: number): [string, number] | null {
  const end = data.indexOf(0, offset);
  if (end < 0) return null;
  const next = (end + 4) & ~3;
  if (next > data.length || data.subarray(end, next).some(byte => byte !== 0) ||
      data.subarray(offset, end).some(byte => byte > 127)) return null;
  return [data.toString("ascii", offset, end), next];
}

export function readSample(data: Buffer) {
  if (data.length < 24 || data.length > 128) return null;
  const path = stringAt(data, 0);
  if (!path || path[0] !== "/input/data") return null;
  const types = stringAt(data, path[1]);
  if (!types || types[0] !== ",sf") return null;
  const slot = stringAt(data, types[1]);
  if (!slot || !/^s[a-f0-9]{12}$/.test(slot[0]) || slot[1] + 4 !== data.length) return null;
  const value = data.readFloatBE(slot[1]);
  if (!Number.isFinite(value) || value < 0 || value > 1) return null;
  return { slot: slot[0], value };
}

export class SampleRelay {
  private socket: Socket | null = null;
  private pending = false;
  private lastDispatch = 0;
  port: number | null = null;

  constructor(private readonly deliver: (slot: string, value: number) => Promise<void>) {}

  async start() {
    if (this.socket) return;
    const socket = createSocket("udp4");
    this.socket = socket;
    socket.on("message", (data, peer) => {
      if (peer.address !== "127.0.0.1" || this.pending || Date.now() - this.lastDispatch < 32) return;
      const sample = readSample(data);
      if (!sample) return;
      this.pending = true;
      this.lastDispatch = Date.now();
      void this.deliver(sample.slot, sample.value).finally(() => { this.pending = false; });
    });
    socket.on("error", () => this.stop());
    await new Promise<void>((resolve, reject) => {
      socket.once("error", reject);
      socket.bind(0, "127.0.0.1", () => {
        socket.off("error", reject);
        this.port = (socket.address() as AddressInfo).port;
        resolve();
      });
    });
  }

  stop() {
    const socket = this.socket;
    this.socket = null;
    this.port = null;
    if (socket) {
      try { socket.close(); } catch { /* Closed before bind completed. */ }
    }
  }
}
