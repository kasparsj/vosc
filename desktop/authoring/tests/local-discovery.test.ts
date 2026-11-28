import assert from "node:assert/strict";
import { mkdtemp, mkdir, rm, symlink } from "node:fs/promises";
import { join } from "node:path";
import { test } from "node:test";
import { browseLocalHosts, publishLocalHost } from "../src/local-discovery";

test("lists same-user hosts while their advertisements are listening", async () => {
  const previousRuntime = process.env.XDG_RUNTIME_DIR;
  const runtime = await mkdtemp("/tmp/vosc-discovery-");
  process.env.XDG_RUNTIME_DIR = runtime;
  const target = "11111111-1111-4111-8111-111111111111";
  const endpoint = { host: "127.0.0.1", port: 47491 };
  let host: Awaited<ReturnType<typeof publishLocalHost>>;
  try {
    assert.deepEqual(await browseLocalHosts(), []);
    const otherDirectory = join(runtime, "other");
    const registry = join(runtime, "vosc-hosts");
    await mkdir(otherDirectory, { mode: 0o700 });
    await symlink(otherDirectory, registry);
    assert.equal(await publishLocalHost(target, endpoint), undefined);
    assert.deepEqual(await browseLocalHosts(), []);
    await rm(registry);

    host = await publishLocalHost(target, endpoint);
    assert.ok(host);
    assert.deepEqual(await browseLocalHosts(), [{
      target, endpoint, name: "This computer · VOSC 11111111",
    }]);
    await host.close();
    host = undefined;
    assert.deepEqual(await browseLocalHosts(), []);
  } finally {
    await host?.close();
    if (previousRuntime === undefined) delete process.env.XDG_RUNTIME_DIR;
    else process.env.XDG_RUNTIME_DIR = previousRuntime;
    await rm(runtime, { recursive: true, force: true });
  }
});
