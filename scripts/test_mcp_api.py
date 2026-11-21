#!/usr/bin/env python3
"""Exercise the production MCP adapter against an already-owned vosc renderer.

This harness resets the disposable scene. It never launches, stops, or restarts a
renderer and therefore requires an explicit --allow-reset flag and socket path.
"""

from __future__ import annotations

import argparse
import asyncio
import base64
import json
from pathlib import Path
import sys
import time
import uuid

from mcp import Client
from mcp.client.stdio import StdioServerParameters
from mcp.types import ImageContent


ROOT = Path(__file__).resolve().parents[1]
ADAPTER = ROOT / "mcp" / "adapter.py"
PYTHON = ROOT / "mcp" / ".venv" / "bin" / "python"
SHADER = """#version 330
uniform float time;
uniform vec2 resolution;
uniform vec3 uTint;
out vec4 fragColor;
void main() {
  vec2 uv = gl_FragCoord.xy / resolution;
  fragColor = vec4(uTint * (0.75 + 0.25 * sin(time + uv.x * 8.0)), 1.0);
}
"""
BROKEN = "#version 330\nout vec4 fragColor; void main() { fragColor = BROKEN; }"


def fresh_request_ids(namespace: str | None = None) -> tuple[str, dict[str, str]]:
    """Create one recorded namespace so retained engine receipts cannot cross runs."""
    run_namespace = namespace or uuid.uuid4().hex
    suffixes = (
        "reset", "publish", "u-tint", "alpha", "capture", "compile-error", "after-error"
    )
    request_ids = {suffix: f"mcp-{run_namespace}-{suffix}" for suffix in suffixes}
    if any(len(value) > 64 for value in request_ids.values()):
        raise ValueError("request ID namespace is too long")
    return run_namespace, request_ids


async def run(socket_path: Path, output: Path) -> dict[str, object]:
    output.mkdir(parents=True, exist_ok=True)
    params = StdioServerParameters(
        command=str(PYTHON), args=[str(ADAPTER), "--socket", str(socket_path)], cwd=str(ROOT)
    )
    records: list[dict[str, object]] = []
    run_namespace, request_ids = fresh_request_ids()

    failure: str | None = None
    try:
        async with Client(params) as client:
            tools = await client.list_tools()
            records.append({"step": "discover", "tools": [tool.model_dump(by_alias=True) for tool in tools.tools]})

            async def call(name: str, arguments: dict[str, object], image_name: str | None = None):
                started = time.monotonic()
                result = await client.call_tool(name, arguments)
                item: dict[str, object] = {
                    "step": name,
                    "arguments": arguments,
                    "elapsed_seconds": time.monotonic() - started,
                    "is_error": result.is_error,
                    "structured_content": result.structured_content,
                    "content_types": [block.type for block in result.content],
                }
                images = [block for block in result.content if isinstance(block, ImageContent)]
                if image_name and images:
                    path = output / image_name
                    path.write_bytes(base64.b64decode(images[0].data, validate=True))
                    item["image"] = str(path)
                records.append(item)
                return result

            await call("get_capabilities", {})
            await call("get_scene", {})
            await call("get_diagnostics", {"after": 0, "limit": 128})
            for name, result in [
                ("configure_layers", await call("configure_layers", {"request_id": request_ids["reset"], "count": 1, "layout": "stack", "wait_ms": 5000})),
                ("publish_shader", await call("publish_shader", {"request_id": request_ids["publish"], "layer": 0, "source": SHADER, "wait_ms": 5000})),
                ("set_uniform", await call("set_uniform", {"request_id": request_ids["u-tint"], "layer": 0, "name": "uTint", "value": [0.2, 0.55, 0.9], "wait_ms": 5000})),
                ("set_layer", await call("set_layer", {"request_id": request_ids["alpha"], "layer": 0, "property": "alpha", "value": 1.0, "wait_ms": 5000})),
            ]:
                if result.is_error or result.structured_content.get("outcome") != "completed":
                    raise RuntimeError(f"{name} did not complete")
            captured = await call("capture_preview", {"request_id": request_ids["capture"], "max_width": 640, "minimum_revision": 0, "wait_ms": 5000}, "preview.png")
            if captured.is_error or not any(isinstance(block, ImageContent) for block in captured.content):
                raise RuntimeError("native preview capture failed")
            failed = await call("publish_shader", {"request_id": request_ids["compile-error"], "layer": 0, "source": BROKEN, "wait_ms": 5000})
            if not failed.is_error or failed.structured_content.get("outcome") != "failed":
                raise RuntimeError("invalid shader unexpectedly succeeded")
            lookup = await call("get_request", {"request_id": request_ids["compile-error"], "wait_ms": 0})
            if not lookup.is_error or lookup.structured_content.get("outcome") != "failed":
                raise RuntimeError("failed inner receipt was not projected as an MCP error")
            await call("get_scene", {})
            await call("get_diagnostics", {"after": 0, "limit": 128})
            preserved = await call("capture_preview", {"request_id": request_ids["after-error"], "max_width": 640, "minimum_revision": 0, "wait_ms": 5000}, "after-error.png")
            if preserved.is_error or not any(isinstance(block, ImageContent) for block in preserved.content):
                raise RuntimeError("working preview was not preserved after compile failure")
    except Exception as error:
        failure = f"{type(error).__name__}: {error}"
        raise
    finally:
        evidence = {
            "scope": "production MCP adapter against caller-owned live renderer; harness did not launch or stop it",
            "socket": str(socket_path), "adapter": str(ADAPTER), "python": str(PYTHON),
            "run_namespace": run_namespace, "request_ids": request_ids,
            "failure": failure, "records": records,
        }
        (output / "mcp-api.json").write_text(json.dumps(evidence, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    return evidence


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--socket", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--allow-reset", action="store_true")
    args = parser.parse_args()
    if not args.allow_reset:
        parser.error("--allow-reset is required because this harness replaces the disposable scene")
    if not args.socket.is_absolute() or not args.output.is_absolute():
        parser.error("--socket and --output must be absolute paths")
    asyncio.run(run(args.socket, args.output))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
