#!/usr/bin/env python3
"""Run shared Shadertoy admission with the explicit signed fixture artifact."""

from __future__ import annotations

import json
from pathlib import Path
import sys


sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from scripts.authoring.acquire import acquire  # noqa: E402
from scripts.authoring.import_shader import canonical_json, prepare, report  # noqa: E402


if __name__ == "__main__":
    fixture = json.loads(Path(__file__).with_name("shadertoy-response.json").read_bytes())
    batch = acquire("shadertoy", sys.argv[1], shadertoy=lambda *_: fixture)
    sys.stdout.buffer.write(canonical_json(report(batch, prepare(batch), include_sources=True)))
    raise SystemExit(0 if batch.complete else 1)
