from __future__ import annotations

import asyncio
import base64
import hashlib
import importlib.metadata
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time
import unittest
import zlib

from mcp import Client
from mcp.client.stdio import StdioServerParameters
from mcp.types import ImageContent, TextContent


PROJECT_DIR = Path(__file__).resolve().parents[1]
REPO_DIR = PROJECT_DIR.parent
EVIDENCE_DIR = Path("/tmp/vosc-mcp-compat-evidence")
FIXTURE_SHA256 = "2ecb2e6bf3ac71819895cabbec6fdd221f7e0d37f485840307f968d66edfffbf"


def _png_dimensions_and_pixels(data: bytes) -> tuple[int, int, bytes]:
    if not data.startswith(b"\x89PNG\r\n\x1a\n"):
        raise AssertionError("missing PNG signature")

    offset = 8
    width = height = 0
    compressed = bytearray()
    while offset < len(data):
        length = struct.unpack(">I", data[offset : offset + 4])[0]
        kind = data[offset + 4 : offset + 8]
        payload = data[offset + 8 : offset + 8 + length]
        offset += 12 + length
        if kind == b"IHDR":
            width, height, depth, color_type, compression, filtering, interlace = struct.unpack(
                ">IIBBBBB", payload
            )
            if (depth, color_type, compression, filtering, interlace) != (8, 2, 0, 0, 0):
                raise AssertionError("unexpected fixture PNG encoding")
        elif kind == b"IDAT":
            compressed.extend(payload)
        elif kind == b"IEND":
            break

    pixels = zlib.decompress(compressed)
    if len(pixels) != height * (1 + width * 3):
        raise AssertionError("unexpected decoded PNG length")
    if any(pixels[row * (1 + width * 3)] != 0 for row in range(height)):
        raise AssertionError("fixture uses an unsupported PNG row filter")
    return width, height, pixels


def _wait_for_process_exit(pid: int, timeout: float = 3.0) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            os.kill(pid, 0)
        except ProcessLookupError:
            return True
        time.sleep(0.02)
    return False


class CompatibilitySubprocessTests(unittest.IsolatedAsyncioTestCase):
    async def test_official_client_discovers_calls_validates_and_reaps(self) -> None:
        EVIDENCE_DIR.mkdir(parents=True, exist_ok=True)
        runs: list[dict[str, object]] = []

        for mode in ("auto", "legacy"):
            with tempfile.TemporaryDirectory(prefix=f"vosc-mcp-{mode}-") as directory:
                pid_path = Path(directory) / "server.pid"
                wrapper = (
                    "import os, pathlib, runpy; "
                    f"pathlib.Path({str(pid_path)!r}).write_text(str(os.getpid()), encoding='utf-8'); "
                    "runpy.run_path('server.py', run_name='__main__')"
                )
                params = StdioServerParameters(
                    command=sys.executable,
                    args=["-c", wrapper],
                    cwd=str(PROJECT_DIR),
                )

                async with Client(params, mode=mode) as client:
                    protocol_version = client.protocol_version
                    tools = await client.list_tools()
                    self.assertEqual([tool.name for tool in tools.tools], ["compatibility_probe"])
                    tool = tools.tools[0]
                    self.assertEqual(tool.input_schema["required"], ["request_id"])
                    self.assertEqual(tool.input_schema["properties"]["request_id"]["minLength"], 1)
                    self.assertIsNotNone(tool.output_schema)
                    self.assertIsNotNone(tool.annotations)
                    assert tool.annotations is not None
                    self.assertTrue(tool.annotations.read_only_hint)
                    self.assertFalse(tool.annotations.destructive_hint)
                    self.assertTrue(tool.annotations.idempotent_hint)
                    self.assertFalse(tool.annotations.open_world_hint)

                    await asyncio.sleep(0.2)  # The stdio child remains usable across an idle gap.
                    result = await client.call_tool(
                        "compatibility_probe", {"request_id": f"probe-{mode}"}
                    )
                    self.assertFalse(result.is_error)
                    self.assertEqual(len(result.content), 2)
                    image = next(block for block in result.content if isinstance(block, ImageContent))
                    text = next(block for block in result.content if isinstance(block, TextContent))
                    self.assertEqual(image.mime_type, "image/png")
                    png = base64.b64decode(image.data, validate=True)
                    width, height, pixels = _png_dimensions_and_pixels(png)
                    self.assertGreater(len(set(pixels)), 4)
                    self.assertEqual((width, height), (48, 32))
                    self.assertEqual(hashlib.sha256(png).hexdigest(), FIXTURE_SHA256)
                    self.assertEqual(
                        result.structured_content,
                        {
                            "request_id": f"probe-{mode}",
                            "status": "synthetic_fixture",
                            "width": width,
                            "height": height,
                            "provenance": "synthetic_stdlib_png_v1",
                        },
                    )
                    self.assertNotIn(image.data, text.text)
                    self.assertNotIn(image.data, json.dumps(result.structured_content, sort_keys=True))

                    invalid_empty = await client.call_tool(
                        "compatibility_probe", {"request_id": ""}
                    )
                    self.assertTrue(invalid_empty.is_error)
                    self.assertIsNone(invalid_empty.structured_content)
                    invalid_type = await client.call_tool(
                        "compatibility_probe", {"request_id": 123}
                    )
                    self.assertTrue(invalid_type.is_error)
                    self.assertIsNone(invalid_type.structured_content)

                    EVIDENCE_DIR.joinpath(f"fixture-{mode}.png").write_bytes(png)

                pid = int(pid_path.read_text(encoding="utf-8"))
                child_exited = await asyncio.to_thread(_wait_for_process_exit, pid)
                self.assertTrue(child_exited, f"stdio child {pid} lingered after SDK client close")
                runs.append(
                    {
                        "client_mode": mode,
                        "negotiated_protocol_version": protocol_version,
                        "tool_names": [tool.name for tool in tools.tools],
                        "input_schema": tool.input_schema,
                        "output_schema": tool.output_schema,
                        "annotations": tool.annotations.model_dump(
                            by_alias=True, exclude_none=True
                        ),
                        "invalid_arguments_rejected": {
                            "empty_string": invalid_empty.is_error,
                            "integer_type": invalid_type.is_error,
                        },
                        "content_types": [block.type for block in result.content],
                        "structured_content": result.structured_content,
                        "png_sha256": hashlib.sha256(png).hexdigest(),
                        "png_bytes": len(png),
                        "child_pid": pid,
                        "child_exited_after_client_close": child_exited,
                        "idle_gap_seconds": 0.2,
                    }
                )

        evidence = {
            "scope": "synthetic SDK and stdio compatibility only; no renderer was launched",
            "python": sys.version,
            "sdk_distribution": "mcp",
            "sdk_version": importlib.metadata.version("mcp"),
            "mcp_types_version": importlib.metadata.version("mcp-types"),
            "runs": runs,
        }
        rendered_evidence = json.dumps(evidence, indent=2, sort_keys=True) + "\n"
        EVIDENCE_DIR.joinpath("sdk-subprocess.json").write_text(
            rendered_evidence, encoding="utf-8"
        )
        runtime_suffix = f"python{sys.version_info.major}{sys.version_info.minor}"
        EVIDENCE_DIR.joinpath(f"sdk-subprocess-{runtime_suffix}.json").write_text(
            rendered_evidence, encoding="utf-8"
        )

    def test_bare_stdin_eof_exits_without_stdout_contamination(self) -> None:
        process = subprocess.Popen(
            [sys.executable, "server.py"],
            cwd=PROJECT_DIR,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        assert process.stdin is not None
        process.stdin.close()
        process.stdin = None
        stdout, stderr = process.communicate(timeout=3)
        self.assertEqual(process.returncode, 0, stderr.decode("utf-8", errors="replace"))
        self.assertEqual(stdout, b"")
        child_reaped = _wait_for_process_exit(process.pid, timeout=0.1)
        self.assertTrue(child_reaped)
        EVIDENCE_DIR.mkdir(parents=True, exist_ok=True)
        EVIDENCE_DIR.joinpath("eof-cleanup.json").write_text(
            json.dumps(
                {
                    "python": sys.version,
                    "returncode": process.returncode,
                    "stdout_bytes": len(stdout),
                    "stderr_bytes": len(stderr),
                    "child_reaped": child_reaped,
                },
                indent=2,
                sort_keys=True,
            )
            + "\n",
            encoding="utf-8",
        )


if __name__ == "__main__":
    unittest.main()
