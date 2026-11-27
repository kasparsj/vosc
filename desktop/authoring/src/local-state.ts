import { createHash, randomUUID } from "node:crypto";
import { open, rename, unlink } from "node:fs/promises";
import { dirname, join } from "node:path";

export class HostError extends Error {
  constructor(readonly code: string, message: string, readonly uncertainId?: string) {
    super(message);
  }
}

export async function writeAtomically(path: string, bytes: string | Buffer, mode = 0o600) {
  const stage = join(dirname(path), `.vosc-${randomUUID()}.tmp`);
  const handle = await open(stage, "wx", mode);
  try {
    try {
      await handle.writeFile(bytes);
      await handle.sync();
    } finally {
      await handle.close();
    }
    await rename(stage, path);
  } finally {
    await unlink(stage).catch(error => {
      if (error.code !== "ENOENT") throw error;
    });
  }
}

export function hash(bytes: Buffer) {
  return createHash("sha256").update(bytes).digest("hex");
}
