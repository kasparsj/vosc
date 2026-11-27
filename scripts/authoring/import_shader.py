#!/usr/bin/env python3
"""Acquire, stage and validate shaders into a user-owned VOSC library."""

from __future__ import annotations

import argparse
import base64
from collections import Counter
import ctypes
from dataclasses import dataclass
from datetime import datetime, timezone
import errno
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import tempfile

if __package__ in (None, ""):
    # Keep direct script invocation and the importable module on the same path.
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
    __package__ = "scripts.authoring"

from .acquire import acquire, ImportFailure, Limits, MAX_SOURCE_BYTES, ShadertoyAPI
from .glsl import Conversion
from .source_profiles import standalone, wrap_shadertoy


CATALOG = "catalog.json"
_SCHEMA = "vosc.shader-library.v1"
_ID = re.compile(r"[0-9a-f]{64}")


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def canonical_json(value):
    return (json.dumps(value, ensure_ascii=False, sort_keys=True, allow_nan=False,
                       separators=(",", ":")) + "\n").encode("utf-8")


@dataclass(frozen=True)
class Prepared:
    origin: dict
    original: bytes | None
    conversion: Conversion | None
    error: ImportFailure | None

    def entry(self):
        if self.conversion is None:
            raise ValueError("A skipped shader has no catalog identity")
        # Equivalent blob/raw/repository inputs share the same identity. Keep
        # the canonical acquisition URL in a result, outside content identity.
        origin = {key: value for key, value in self.origin.items() if key != "url"}
        if self.origin["provider"] in {"file", "shadertoy"}:
            origin["url"] = self.origin["url"]
        core = {"source": origin, "source_sha256": sha256(self.original),
                "generated_sha256": sha256(self.conversion.source),
                "conversion": self.conversion.version}
        return {**core, "id": sha256(canonical_json(core)),
                "conversion_diagnostic": self.conversion.diagnostic}


def prepare(batch):
    results = []
    for candidate in batch.candidates:
        conversion = None
        error = candidate.error
        if error is None:
            analyze = wrap_shadertoy if candidate.origin["provider"] == "shadertoy" else standalone
            try:
                conversion = analyze(candidate.source)
                if len(conversion.source) > MAX_SOURCE_BYTES:
                    conversion = None
                    raise ImportFailure("source_limit", "Converted shader exceeds the engine source limit")
            except ImportFailure as failure:
                error = failure
        results.append(Prepared(candidate.origin, candidate.source, conversion, error))
    return results


def validate_batch(prepared, validate, *, on_validated=None):
    """Use one owned validator sequentially. A failed candidate stays isolated."""
    results = []
    for item in prepared:
        if item.error:
            results.append(None)
            continue
        result = validate(item.conversion.source)
        if result.get("ok") and on_validated is not None:
            on_validated(item, result)
        results.append(result)
    return results


def read_catalog(library):
    path = library / CATALOG
    if path.is_symlink():
        raise ImportFailure("library_collision", "Catalog path collides with a symbolic link")
    if not path.exists():
        return {}
    try:
        value = json.loads(path.read_bytes())
        entries = value["entries"]
        if (value["format"] != _SCHEMA or not isinstance(entries, dict) or
                any(not _ID.fullmatch(key) or not isinstance(entry, dict) or entry.get("id") != key
                    for key, entry in entries.items())):
            raise ValueError()
    except (OSError, ValueError, KeyError, TypeError) as error:
        raise ImportFailure("invalid_catalog", "Existing library manifest is invalid") from error
    return entries


def mkdir_owned(path):
    if path.is_symlink() or (path.exists() and not path.is_dir()):
        raise ImportFailure("library_collision", f"Library directory collides with another file: {path.name}")
    path.mkdir(exist_ok=True)


def write_file(path, content):
    with path.open("xb") as stream:
        stream.write(content)
        stream.flush()
        os.fsync(stream.fileno())


def promote_directory(source, target):
    """Move all staged bytes in one operation without replacing a collision."""
    libc = ctypes.CDLL(None, use_errno=True)
    source_bytes, target_bytes = os.fsencode(source), os.fsencode(target)
    if sys.platform == "darwin":
        rename = libc.renamex_np
        rename.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint]
        result = rename(source_bytes, target_bytes, 0x00000004)  # RENAME_EXCL
    elif sys.platform == "linux" and hasattr(libc, "renameat2"):
        rename = libc.renameat2
        rename.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
        at_fdcwd = -100
        result = rename(at_fdcwd, source_bytes, at_fdcwd, target_bytes, 1)  # RENAME_NOREPLACE
    else:
        raise OSError(errno.ENOTSUP, "Atomic exclusive directory promotion is unavailable")
    if result != 0:
        code = ctypes.get_errno()
        raise OSError(code, os.strerror(code), target)


def same_file(path, content):
    return (not path.is_symlink() and path.is_file() and
            path.stat().st_size == len(content) and path.read_bytes() == content)


def entry_status(target, item, entry):
    if not target.is_dir() or target.is_symlink():
        return "collision"
    expected = {"original.frag": item.original, "generated.frag": item.conversion.source,
                "entry.json": canonical_json(entry)}
    if not all(same_file(target / name, content) for name, content in expected.items()):
        return "collision"
    editable = target / "shader.frag"
    if not editable.is_file() or editable.is_symlink():
        return "collision"
    return "reused" if same_file(editable, item.conversion.source) else "reused_edited"


def publish(library, prepared, validations):
    """Commit only validated entries, with one atomic catalog replacement.

    Shader directories become reachable through the catalog at its commit
    point. A complete directory left by interruption can be reused on retry,
    including an editable copy changed before the retry.
    Existing editable copies and collisions are never replaced.
    """
    library = Path(library).resolve()
    outcomes = [None] * len(prepared)
    accepted = [(index, item) for index, (item, result) in enumerate(zip(prepared, validations, strict=True))
                if result is not None and result.get("ok") is True]
    if not accepted:
        return outcomes
    try:
        library.mkdir(mode=0o700, parents=True, exist_ok=True)
        fd = os.open(library / ".import.lock", os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600)
        with os.fdopen(fd, "rb") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            entries = read_catalog(library)
            objects = library / "shaders"
            mkdir_owned(objects)
            with tempfile.TemporaryDirectory(prefix=".import-", dir=objects) as temporary:
                stage = Path(temporary)
                added = False
                for index, item in accepted:
                    entry = item.entry()
                    identity = entry["id"]
                    target = objects / identity
                    if identity in entries or target.exists() or target.is_symlink():
                        status = entry_status(target, item, entry)
                    else:
                        staged = stage / identity
                        staged.mkdir()
                        write_file(staged / "original.frag", item.original)
                        write_file(staged / "generated.frag", item.conversion.source)
                        write_file(staged / "shader.frag", item.conversion.source)
                        write_file(staged / "entry.json", canonical_json(entry))
                        try:
                            promote_directory(staged, target)
                            status = "published"
                        except FileExistsError:
                            status = entry_status(target, item, entry)
                    outcomes[index] = status
                    if status != "collision" and identity not in entries:
                        entries[identity] = entry
                        added = True
                if added:
                    # Keep the temporary manifest on the same filesystem as
                    # the live catalog, including when shaders is a mount.
                    with tempfile.TemporaryDirectory(prefix=".import-", dir=library) as transaction:
                        manifest = Path(transaction) / CATALOG
                        write_file(manifest, canonical_json({"format": _SCHEMA, "entries": entries}))
                        os.replace(manifest, library / CATALOG)
    except OSError as error:
        raise ImportFailure("library_write_failed", f"Could not publish library transaction: {error.strerror}") from error
    return outcomes


def report(batch, prepared, validations=None, publication=None, *, include_sources=False):
    files = []
    for index, item in enumerate(prepared):
        result = {"origin": item.origin}
        if item.original is not None:
            result["source_sha256"] = sha256(item.original)
            if include_sources:
                result["original_base64"] = base64.b64encode(item.original).decode("ascii")
        if item.error:
            result.update(status="skipped", code=item.error.code, message=str(item.error))
        else:
            result["entry"] = item.entry()
            if include_sources:
                result["generated_base64"] = base64.b64encode(item.conversion.source).decode("ascii")
            validation = validations[index] if validations is not None else None
            if validation is None:
                result.update(status="awaiting_validation", code="awaiting_validation",
                              message="Candidate is staged and awaits native validation")
            else:
                result["validation"] = validation
                result.update(status="validated" if validation["ok"] else "skipped",
                              code=validation["code"], message=validation["message"])
                if publication is not None and publication[index] is not None:
                    state = publication[index]
                    result["status"] = state
                    if state == "collision":
                        result.update(code="library_collision", message="Existing library path was preserved")
        files.append(result)
    issue = None
    if batch.error:
        issue = {"code": batch.error.code, "message": str(batch.error)}
    return {"format": "vosc.shader-import.v1", "origin": batch.origin, "complete": batch.complete,
            "error": issue, "observed_at": datetime.now(timezone.utc).isoformat(), "files": files,
            "counts": dict(Counter(file["status"] for file in files))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kind", choices=("file", "paste", "github", "repository", "shadertoy"))
    parser.add_argument("source", nargs="?", help="File path or provider URL. Paste reads UTF-8 bytes from stdin.")
    parser.add_argument("--ref", help="Explicit Git ref for a repository import")
    parser.add_argument("--library", type=Path, help="User-owned library directory")
    parser.add_argument("--renderer", type=Path, help="Launch this VOSC binary in an owned validation process")
    parser.add_argument("--stage-only", action="store_true", help="Return converted candidates without native validation")
    parser.add_argument("--archive-entries", type=int, default=Limits.archive_entries)
    parser.add_argument("--report", type=Path, help="Write structured diagnostics to a new local file")
    args = parser.parse_args()
    if args.kind == "paste" and args.source is not None:
        parser.error("Paste reads from stdin")
    if args.kind != "paste" and not args.source:
        parser.error("Provide a path or URL")
    if (args.kind == "repository") != bool(args.ref):
        parser.error("Repository import requires --ref")
    if args.stage_only == bool(args.library or args.renderer) or (not args.stage_only and
                                                                  not (args.library and args.renderer)):
        parser.error("Use --stage-only or supply both --library and --renderer")
    if not 1 <= args.archive_entries <= Limits.archive_entries:
        parser.error(f"--archive-entries must be in the range 1–{Limits.archive_entries}")
    source = sys.stdin.buffer.read(MAX_SOURCE_BYTES + 1) if args.kind == "paste" else args.source
    batch = acquire(args.kind, source, ref=args.ref, limits=Limits(archive_entries=args.archive_entries),
                    shadertoy=ShadertoyAPI(os.environ.get("VOSC_SHADERTOY_API_KEY")))
    prepared = prepare(batch)
    validations = None
    publication = None
    failure = None
    if not args.stage_only and any(item.error is None for item in prepared):
        from .native_validation import NativeValidator

        try:
            with NativeValidator(args.renderer) as native:
                validations = validate_batch(prepared, native.validate)
            publication = publish(args.library, prepared, validations)
        except ImportFailure as error:
            failure = {"code": error.code, "message": str(error)}
        except (OSError, ValueError) as error:
            failure = {"code": "validation_unavailable", "message": str(error)}
    result = report(batch, prepared, validations, publication, include_sources=args.stage_only)
    if failure:
        result["publication_error"] = failure
    data = canonical_json(result)
    if args.report:
        write_file(args.report, data)
    sys.stdout.buffer.write(data)
    accepted = {"published", "reused", "reused_edited", "awaiting_validation"}
    return 0 if batch.complete and not failure and accepted.intersection(result["counts"]) else 1


if __name__ == "__main__":
    raise SystemExit(main())
