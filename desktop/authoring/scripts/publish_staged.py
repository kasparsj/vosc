#!/usr/bin/env python3
"""Publish one desktop batch using the shared importer transaction."""

from __future__ import annotations

import base64
import json
from pathlib import Path
import sys


sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from scripts.authoring.acquire import Batch, Candidate, ImportFailure  # noqa: E402
from scripts.authoring.import_shader import canonical_json, prepare, publish  # noqa: E402


def main():
    library = Path(sys.argv[1])
    limit = 96 * 1024 * 1024
    request = sys.stdin.buffer.read(limit + 1)
    try:
        if len(request) > limit:
            raise ImportFailure("publication_limit", "The validated batch exceeds the publication limit.")
        rows = json.loads(request)
        candidates = [Candidate(row["origin"], base64.b64decode(row["original"], validate=True)) for row in rows]
        prepared = prepare(Batch({"provider": "desktop"}, candidates))
        validations = []
        for item, row in zip(prepared, rows, strict=True):
            if item.error or item.entry()["id"] != row["entry_id"] or item.entry()["generated_sha256"] != row["digest"]:
                raise ImportFailure("source_changed", "Staged source identity changed before publication.")
            validations.append({"ok": True})
        result = publish(library, prepared, validations)
        sys.stdout.buffer.write(canonical_json({"outcomes": result}))
    except ImportFailure as error:
        sys.stdout.buffer.write(canonical_json({"error": {"code": error.code, "message": str(error)}}))
    except (OSError, ValueError, KeyError, TypeError):
        sys.stdout.buffer.write(canonical_json({"error": {"code": "publication_failed",
                                                          "message": "Could not publish the validated sources."}}))


if __name__ == "__main__":
    main()
