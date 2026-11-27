import assert from "node:assert/strict";
import { mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { test } from "node:test";
import type { DocumentView, DraftSource } from "../src/contracts";
import { parseDocument, sourceWithText } from "../src/draft-project";
import type { HostClient } from "../src/host-client";
import { Workspace } from "../src/workspace";

function workingSource(text: string) {
  return sourceWithText({
    name: "Scene.frag", digest: "", base64: "", origin: "Test source",
  }, text);
}

const offline = {
  peer: null,
  rpc: () => assert.fail("offline file commands made a host request"),
};

function edit(workspace: Workspace, view: DocumentView, text: string) {
  return workspace.edit({ id: view.id, generation: view.generation, editSequence: view.editSequence + 1, text });
}

test("offline save keeps captured invalid source and leaves edits made during save dirty", async () => {
  const root = await mkdtemp(join(tmpdir(), "vosc-document-"));
  const workspace = new Workspace(offline as unknown as HostClient);
  try {
    const initial = (await workspace.create()).document!;
    assert.equal(initial.dirty, false);
    assert.equal(initial.acceptedArtifact, null);
    assert.throws(() => workspace.export(root), /no accepted native artifact/);
    const broken = edit(workspace, initial, "void missing {\n");
    const path = join(root, "Untitled.vosc-draft");
    const saving = workspace.save(path);
    edit(workspace, broken, "");
    await saving;
    assert.equal(workspace.view().document?.dirty, true);
    const saved = parseDocument(JSON.parse(await readFile(path, "utf8")));
    assert.equal(Buffer.from(saved.source.base64, "base64").toString("utf8"), "void missing {\n");
    assert.equal(saved.accepted, null);
    await workspace.save(path);
    assert.equal(workspace.view().document?.dirty, false);
    const reopened = (await workspace.open(path)).document!;
    assert.equal(reopened.source.text, "");
    assert.equal(reopened.id, initial.id);
    assert.notEqual(reopened.generation, initial.generation);
    assert.throws(() => edit(workspace, broken, "late editor"), /no longer matches/);
    assert.throws(() => edit(workspace, reopened, "contains\u0000NUL"), /without NUL/);
    assert.throws(() => edit(workspace, reopened, "🫧".repeat(32769)), /up to 128 KiB/);
    assert.throws(() => edit(workspace, reopened, "\ud800"), /Use UTF-8/);
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});

test("untouched and undone source preserves original line endings and provenance", async () => {
  const workspace = new Workspace(offline as unknown as HostClient);
  const converted = workingSource("// original\r\n");
  const source: DraftSource = {
    ...converted, provenance: {
      provider: "repository", path: "Folder/Scene.frag", requestedRef: "main", commit: "123",
      originalDigest: converted.digest, convertedDigest: converted.digest, convertedBase64: converted.base64,
    },
  };
  const opened = (await workspace.openSource(source)).document!;
  assert.equal(opened.source.text, "// original\n");
  const changed = edit(workspace, opened, "// edited\n");
  assert.equal(changed.dirty, true);
  const undone = edit(workspace, changed, opened.source.text);
  assert.equal(undone.dirty, false);
  assert.equal(undone.source.digest, source.digest);
  const root = await mkdtemp(join(tmpdir(), "vosc-undo-"));
  try {
    const path = join(root, "Scene.vosc-draft");
    await workspace.save(path);
    const saved = parseDocument(JSON.parse(await readFile(path, "utf8")));
    assert.equal(saved.source.base64, source.base64);
    assert.deepEqual(saved.source.provenance, source.provenance);
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});

test("v1 opens as a historical artifact and remains unchanged through v2 Save As and export", async () => {
  const root = await mkdtemp(join(tmpdir(), "vosc-legacy-"));
  const workspace = new Workspace(offline as unknown as HostClient);
  const old = {
    format: "vosc.authoring.draft", version: 1, title: "Saved scene", source: workingSource("#version 330\r\n"),
    destination: { target: "acd631f3-2b03-4d5e-b129-29cadff3ed18", fingerprint: "0".repeat(64),
      endpoint: { host: "127.0.0.1", port: 8123 } },
    slots: [{ id: "s123456789abc", label: "Range", default: 0 }],
    bindings: [{ name: "spin", component: 0, binding: { mode: "range", slot: "s123456789abc", low: 0.2, high: 0.8 } }],
  };
  try {
    const input = join(root, "v1.vosc-draft");
    const exact = JSON.stringify(old);
    await writeFile(input, exact);
    const opened = await workspace.open(input);
    assert.equal(opened.previewReady, false);
    assert.equal(opened.document!.acceptedArtifact?.revision, null);
    assert.equal(opened.document!.acceptedArtifact?.saved, true);
    assert.equal(await readFile(input, "utf8"), exact);
    edit(workspace, opened.document!, "invalid");
    const output = join(root, "v2.vosc-draft");
    await workspace.save(output);
    const saved = parseDocument(JSON.parse(await readFile(output, "utf8")));
    assert.equal(saved.id, opened.document?.id);
    assert.equal(saved.accepted?.project.source.digest, old.source.digest);
    assert.deepEqual(saved.accepted?.project.destination, old.destination);
    assert.deepEqual(saved.controls.bindings, old.bindings);
    assert.equal(workspace.view().document?.acceptedArtifact?.stale, true);
    await workspace.export(root);
    assert.equal(await readFile(input, "utf8"), exact);
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});

test("external changes and cancelled open preserve the active document and user file", async () => {
  const root = await mkdtemp(join(tmpdir(), "vosc-conflict-"));
  const workspace = new Workspace(offline as unknown as HostClient);
  try {
    await workspace.create();
    const path = join(root, "Scene.vosc-draft");
    const copy = join(root, "Copy.vosc-draft");
    await workspace.save(path);
    await workspace.save(copy);
    await workspace.open(path);
    const active = workspace.view().document!;
    await writeFile(path, "outside edit");
    const changed = edit(workspace, active, "unsaved");
    await assert.rejects(workspace.save(path), { code: "file_changed" });
    assert.equal(await readFile(path, "utf8"), "outside edit");
    assert.deepEqual((await workspace.open(copy, async () => false)).document, changed);
    await assert.rejects(workspace.open(path), /valid UTF-8 VOSC draft/);
    assert.deepEqual(workspace.view().document, changed);
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});

test("saving a dirty document while reopening its own path loads the newly saved buffer", async () => {
  const root = await mkdtemp(join(tmpdir(), "vosc-reopen-"));
  const workspace = new Workspace(offline as unknown as HostClient);
  try {
    const initial = (await workspace.create()).document!;
    const path = join(root, "Scene.vosc-draft");
    await workspace.save(path);
    edit(workspace, initial, "new source");
    const reopened = await workspace.open(path, async () => {
      await workspace.save(path);
      return true;
    });
    assert.equal(reopened.document?.source.text, "new source");
    assert.equal(reopened.document?.dirty, false);
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});
