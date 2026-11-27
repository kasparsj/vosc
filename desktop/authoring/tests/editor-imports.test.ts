import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { copyFile, mkdir, mkdtemp, readFile, rm, symlink, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { test } from "node:test";
import type { HostClient } from "../src/host-client";
import { Importer } from "../src/importer";
import { hash, HostError } from "../src/local-state";

const FIRST = "#version 330\nout vec4 color;\nvoid main() { color = vec4(1); }\n";
const SECOND = FIRST.replace("vec4(1)", "vec4(0.5)");
const fixtures = ["acquire.py", "glsl.py", "import_shader.py", "source_profiles.py", "native_validation.py"];

async function createFixture(root: string) {
  const python = execFileSync("python3", ["-I", "-c", "import sys; print(sys.executable)"], { encoding: "utf8" }).trim();
  const bin = join(root, "python", "bin");
  const scripts = join(root, "importer", "scripts", "authoring");
  await mkdir(bin, { recursive: true });
  await mkdir(scripts, { recursive: true });
  await symlink(python, join(bin, "python3.13"));
  await Promise.all(fixtures.map(name => copyFile(resolve("../../scripts/authoring", name), join(scripts, name))));
  await copyFile(resolve("scripts/publish_staged.py"), join(scripts, "publish_staged.py"));
  return new Importer(root, join(root, "library"));
}

function testHost(rpc: (digest: string) => void) {
  const target = "a6dc9300-aae3-4bae-86f4-7752f93c1ef9";
  return {
    session: () => ({
      peer: { target },
      rpc: async (_method: string, params: { sha256: string }) => {
        rpc(params.sha256);
        return { target, sourceDigest: params.sha256, receipt: { status: "rendered" } };
      },
    }),
  } as unknown as HostClient;
}

test("offline opening binds exact converted bytes and provenance to the current stage", async () => {
  const root = await mkdtemp(join(tmpdir(), "vosc-editor-import-"));
  try {
    const importer = await createFixture(root);
    const first = await importer.stage("paste", FIRST);
    assert.ok(first.batchId);
    assert.equal(first.items[0]?.status, "awaiting_validation");
    const id = first.items[0].id;
    const source = importer.openStaged(first.batchId, id);
    assert.equal(source.name, "pasted.frag");
    assert.equal(source.digest, first.items[0].generatedDigest);
    assert.equal(source.base64, Buffer.from(importer.inspectSource(first.batchId, id)?.generated ?? "").toString("base64"));
    assert.equal(source.provenance?.provider, "paste");
    assert.equal(source.provenance?.originalDigest, first.items[0].sourceDigest);
    assert.deepEqual(await importer.listLibrary(), []);

    const second = await importer.stage("paste", SECOND);
    assert.ok(second.batchId);
    assert.notEqual(second.batchId, first.batchId);
    assert.throws(() => importer.openStaged(first.batchId, id), { code: "stale_import" });
    assert.throws(() => importer.openStaged(second.batchId, id), { code: "invalid_selection" });
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});

test("selection, cancelled native validation and retry preserve the actual edited library copy", async () => {
  const root = await mkdtemp(join(tmpdir(), "vosc-editor-import-"));
  try {
    const importer = await createFixture(root);
    type Batch = { id: string; items: { id: string; file: unknown }[] };
    const staged = importer as unknown as { batch: Batch };
    const pathA = join(root, "first.frag");
    const pathB = join(root, "second.frag");
    await writeFile(pathA, FIRST);
    await writeFile(pathB, SECOND);
    const first = await importer.stage("file", pathA);
    assert.ok(first.batchId);
    const firstRow = staged.batch.items[0];
    const second = await importer.stage("file", pathB);
    assert.ok(second.batchId);
    staged.batch.items.unshift(firstRow);
    const itemA = first.items[0];
    const itemB = second.items[0];
    const received: string[] = [];

    const cancelled = await importer.validate(testHost(digest => {
      received.push(digest);
      importer.cancel();
    }), second.batchId, [itemA.id, itemB.id]);
    assert.equal(cancelled.error?.code, "cancelled");
    assert.deepEqual(received, [itemA.generatedDigest]);
    assert.deepEqual(await importer.listLibrary(), []);

    const selected = await importer.validate(testHost(digest => {
      received.push(digest);
      if (digest === itemB.generatedDigest) throw new HostError("shader_failed", "Native fixture failure");
    }), second.batchId, [itemB.id]);
    assert.deepEqual(received, [itemA.generatedDigest, itemB.generatedDigest]);
    assert.deepEqual(selected.items.map(item => item.status), ["awaiting_validation", "validation_failed"]);
    assert.deepEqual(await importer.listLibrary(), []);

    const partial = await importer.validate(testHost(digest => {
      if (digest === itemB.generatedDigest) throw new HostError("shader_failed", "Native fixture failure");
    }), second.batchId, [itemA.id, itemB.id]);
    assert.deepEqual(partial.items.map(item => item.status), ["published", "validation_failed"]);
    const libraryId = partial.items[0].libraryId;
    assert.ok(libraryId);
    assert.deepEqual((await importer.listLibrary()).map(item => item.id), [libraryId]);

    const edited = Buffer.from(FIRST.replace("vec4(1)", "vec4(0.8)"));
    const editable = join(root, "library", "shaders", libraryId, "shader.frag");
    await writeFile(editable, edited);
    const reimport = await importer.stage("file", pathA);
    assert.ok(reimport.batchId);
    const result = await importer.validate(testHost(() => {}), reimport.batchId, [reimport.items[0].id]);
    assert.equal(result.items[0].status, "reused_edited");
    const opened = await importer.openLibrary(libraryId);
    assert.equal(opened.digest, hash(edited));
    assert.equal(opened.provenance?.convertedDigest, itemA.generatedDigest);
    assert.deepEqual(Buffer.from(opened.base64, "base64"), edited);
    assert.deepEqual(await readFile(editable), edited);
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});
