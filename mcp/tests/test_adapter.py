from __future__ import annotations

import asyncio
import base64
import binascii
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
from mcp.types import ImageContent


PROJECT_DIR = Path(__file__).resolve().parents[1]
REPO_DIR = PROJECT_DIR.parent
sys.path.insert(0, str(PROJECT_DIR))
sys.path.insert(0, str(REPO_DIR / "scripts"))

from engine_client import (EngineClient, EngineClientError, REQUEST_LIMIT, RESPONSE_LIMIT,
                           canonical_request, decode_response)  # noqa: E402
from scene_recipe_schema import RECIPE_LIMITS  # noqa: E402
import test_mcp_api as live_harness  # noqa: E402


OPERATIONS = [
    "get_capabilities", "get_scene", "get_performance", "get_diagnostics", "get_request",
    "export_scene",
    "configure_layers", "publish_shader", "set_uniform", "set_layer", "capture_preview",
    "trial_shader", "accept_trial", "cancel_trial",
]


def envelope(request_id: str, *, status: str = "ok", ok: bool = True, **extra):
    return {"id": request_id, "ok": ok, "status": status, "revision": 3,
            "frame": 9, "external_epoch": 1, **extra}


def capabilities(request_id: str, *, version: int = 1, operations=OPERATIONS):
    recipe = "export_scene" in operations
    return envelope(request_id, result={"version": version, "operations": operations,
                                       "limits": dict(RECIPE_LIMITS) if recipe else {},
                                       "features": {"scene_recipe_export": recipe}})


def scene(request_id: str):
    return envelope(request_id, result={"layers": [], "pending": [], "layout": "stack",
        "revision": 3, "rendered_revision": 3, "frame": 9, "external_epoch": 1,
        "camera_enabled": False, "onset_gate": False, "legacy_pending": 0,
        "trial": None,
        "width": 1280, "height": 720, "fps": 60.0})


def performance(request_id: str):
    empty = {"samples": 0, "mean": None, "p50": None, "p95": None, "max": None}
    return envelope(request_id, result={
        "version": 1, "scope": "composed_scene", "window_frames": 120,
        "context": {"revision": 3, "external_epoch": 1, "width": 1280, "height": 720},
        "matches_current_context": True, "target_fps": 60.0,
        "frame_interval_ms": {**empty}, "cpu_submit_ms": {**empty},
        "feedback": {"supported": False, "state": "unsupported", "reason": "timer unavailable",
                     "feedback_generation": 0, "matches_current_context": True,
                     "update_gpu_ms": {**empty}, "paired_gpu_ms": {**empty}, "samples": [],
                     "last_attempt_frame": 9, "attempts": 0, "draw_attempts": 0,
                     "pending_queries": 0, "draw_pending_queries": 0,
                     "dropped_samples": 0, "draw_dropped_samples": 0},
        "gpu_ms": {**empty, "supported": False, "state": "unsupported",
                   "reason": "timer unavailable", "last_frame": None,
                   "pending_queries": 0, "dropped_samples": 0},
    })


def trial_metadata(trial_id: str, phase: str) -> dict:
    return {"trial_id": trial_id, "layer": 0, "phase": phase, "gpu_budget_ms": 8.0,
        "lease_remaining_ms": 5000, "evaluation_revision": 4, "first_frame": 10,
        "warmup_frames": 8, "minimum_samples": 30, "freshness_frames": 8,
        "context": {"revision": 4, "external_epoch": 1, "width": 1280, "height": 720},
        "samples": 30, "p95_ms": 3.0, "hard_max_ms": 4.0, "last_gpu_frame": 45}


def png(width: int = 2, height: int = 1) -> bytes:
    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", binascii.crc32(kind + data) & 0xffffffff)
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    pixels = b"".join(b"\0" + b"\x12\x34\x56" * width for _ in range(height))
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(pixels)) + chunk(b"IEND", b"")


def png_header(*, compression: int = 0, filtering: int = 0, interlace: int = 0,
               bit_depth: int = 8, color_type: int = 2) -> bytes:
    ihdr = struct.pack(">IIBBBBB", 2, 1, bit_depth, color_type, compression, filtering, interlace)
    return (b"\x89PNG\r\n\x1a\n" + struct.pack(">I4s", 13, b"IHDR") + ihdr
            + struct.pack(">I", binascii.crc32(b"IHDR" + ihdr) & 0xffffffff))


class Peer:
    def __init__(self, handler):
        self.handler = handler
        self.requests = []
        self.peak = 0
        self.active = 0

    async def __aenter__(self):
        self.temp = tempfile.TemporaryDirectory(prefix="vosc-mcp-peer-")
        self.directory = Path(self.temp.name)
        os.chmod(self.directory, 0o700)
        self.path = self.directory / "control.sock"
        self.server = await asyncio.start_unix_server(self._client, path=self.path)
        os.chmod(self.path, 0o600)
        return self

    async def __aexit__(self, *_):
        self.server.close()
        await self.server.wait_closed()
        self.temp.cleanup()

    async def _client(self, reader, writer):
        self.active += 1
        self.peak = max(self.peak, self.active)
        try:
            while line := await reader.readline():
                request = json.loads(line)
                self.requests.append(request)
                response = await self.handler(request, len(self.requests))
                if response is None:
                    writer.close()
                    return
                writer.write(json.dumps(response, allow_nan=False, separators=(",", ":")).encode() + b"\n")
                await writer.drain()
        finally:
            writer.close()
            self.active -= 1


class EngineClientTests(unittest.IsolatedAsyncioTestCase):
    async def test_preflight_validates_envelope_before_mutation(self):
        async def handler(request, _):
            response = capabilities(request["id"])
            response["revision"] = "bad"
            return response
        async with Peer(handler) as peer:
            client = EngineClient(peer.path)
            with self.assertRaises(EngineClientError) as caught:
                await client.submit("author-1", "configure_layers", {"count": 1, "layout": "stack", "timing": "immediate"})
            self.assertEqual(caught.exception.code, "adapter_protocol_error")
            self.assertEqual([item["method"] for item in peer.requests], ["get_capabilities"])

        async def malformed_failure(request, _):
            response = capabilities(request["id"])
            response.update(ok=False, status="wat", error={"code": "no", "message": "no", "diagnostics": []})
            return response
        async with Peer(malformed_failure) as peer:
            with self.assertRaises(EngineClientError) as caught:
                await EngineClient(peer.path).submit("author-1b", "configure_layers", {"count": 1, "layout": "stack", "timing": "immediate"})
            self.assertEqual(caught.exception.code, "adapter_protocol_error")
            self.assertEqual([item["method"] for item in peer.requests], ["get_capabilities"])

    async def test_lost_initial_mutation_reply_is_marked_written(self):
        async def handler(request, _):
            if request["method"] == "get_capabilities":
                return capabilities(request["id"])
            return None
        async with Peer(handler) as peer:
            client = EngineClient(peer.path)
            with self.assertRaises(EngineClientError) as caught:
                await client.submit("author-2", "configure_layers", {"count": 1, "layout": "stack", "timing": "immediate"})
            self.assertTrue(caught.exception.wrote_mutation)
            self.assertEqual([item["method"] for item in peer.requests], ["get_capabilities", "configure_layers"])

    async def test_endpoint_permissions_and_request_wire_boundary(self):
        async def handler(request, _):
            return capabilities(request["id"])
        async with Peer(handler) as peer:
            os.chmod(peer.directory, 0o755)
            with self.assertRaises(EngineClientError) as caught:
                await EngineClient(peer.path).query("get_capabilities", {})
            self.assertEqual(caught.exception.code, "adapter_unsafe_endpoint")
            self.assertEqual(peer.requests, [])
            os.chmod(peer.directory, 0o700)
            raw = canonical_request("x", "publish_shader", {"source": "a", "layer": 0, "timing": "immediate"})
            self.assertLessEqual(len(raw), REQUEST_LIMIT)

    async def test_exact_serialized_request_limit(self):
        fixed = len(canonical_request("x", "publish_shader", {"layer": 0, "source": "", "timing": "immediate"}))
        # Control characters occupy six wire bytes but one UTF-8 source byte.
        controls = 30_000
        ascii_count = REQUEST_LIMIT - fixed - controls * 6
        source = "\x01" * controls + "a" * ascii_count
        self.assertLessEqual(len(source.encode()), 131_072)
        self.assertEqual(len(canonical_request("x", "publish_shader", {"layer": 0, "source": source, "timing": "immediate"})), REQUEST_LIMIT)
        with self.assertRaises(EngineClientError):
            canonical_request("x", "publish_shader", {"layer": 0, "source": source + "a", "timing": "immediate"})

    def test_response_wire_limit_depth_and_nonfinite(self):
        base = json.dumps(envelope("q", padding=""), separators=(",", ":")).encode() + b"\n"
        padding = "a" * (RESPONSE_LIMIT - len(base))
        exact = json.dumps(envelope("q", padding=padding), separators=(",", ":")).encode() + b"\n"
        self.assertEqual(len(exact), RESPONSE_LIMIT)
        self.assertEqual(decode_response(exact, "q")["id"], "q")
        with self.assertRaises(EngineClientError):
            decode_response(exact[:-1] + b" \n", "q")
        with self.assertRaises(EngineClientError):
            decode_response(b'{"id":"q","value":NaN}\n', "q")
        within = '{"id":"q","nested":' + '[' * 63 + ']' * 63 + '}\n'
        self.assertEqual(decode_response(within.encode(), "q")["id"], "q")
        nested = '{"id":"q","nested":' + '[' * 64 + ']' * 64 + '}\n'
        with self.assertRaises(EngineClientError):
            decode_response(nested.encode(), "q")
        object_within = '{"id":"q","nested":' + '[' * 62 + '{}' + ']' * 62 + '}\n'
        self.assertEqual(decode_response(object_within.encode(), "q")["id"], "q")
        object_too_deep = '{"id":"q","nested":' + '[' * 63 + '{}' + ']' * 63 + '}\n'
        with self.assertRaises(EngineClientError):
            decode_response(object_too_deep.encode(), "q")


class AdapterSubprocessTests(unittest.IsolatedAsyncioTestCase):
    async def test_performance_validates_peer_and_preserves_old_engine_compatibility(self):
        old_operations = [name for name in OPERATIONS if name != "get_performance"]

        async def old_handler(request, _):
            if request["method"] == "get_capabilities":
                return capabilities(request["id"], operations=old_operations)
            if request["method"] == "get_scene":
                return scene(request["id"])
            if request["method"] == "get_performance":
                return envelope(request["id"], ok=False, status="failed",
                                error={"code": "unknown_method", "message": "unsupported method", "diagnostics": []})
            raise AssertionError(request)

        async with Peer(old_handler) as peer:
            params = StdioServerParameters(command=sys.executable,
                args=["adapter.py", "--socket", str(peer.path)], cwd=str(PROJECT_DIR))
            async with Client(params) as client:
                compatible = await client.call_tool("get_capabilities", {})
                self.assertFalse(compatible.is_error)
                original = await client.call_tool("get_scene", {})
                self.assertFalse(original.is_error)
                unsupported = await client.call_tool("get_performance", {})
                self.assertTrue(unsupported.is_error)
                self.assertEqual(unsupported.structured_content["error"]["code"], "unknown_method")

        valid = performance("unused")
        valid["result"]["gpu_ms"].update({
            "samples": 1, "mean": 2.0, "p50": 2.0, "p95": 2.0, "max": 2.0,
            "supported": True, "state": "available", "reason": None, "last_frame": 8,
            "pending_queries": 2,
        })
        valid["result"]["feedback"].update({
            "supported": True, "state": "available", "reason": None,
            "feedback_generation": 7, "attempts": 1, "draw_attempts": 1,
            "samples": [{"frame": 8, "update_ms": 3.0, "draw_ms": 2.0}],
            "update_gpu_ms": {"samples": 1, "mean": 3.0, "p50": 3.0, "p95": 3.0, "max": 3.0},
            "paired_gpu_ms": {"samples": 1, "mean": 5.0, "p50": 5.0, "p95": 5.0, "max": 5.0},
        })
        malformed = []
        for field, bad in (("frame", 10), ("update_ms", -1.0), ("draw_ms", 8.0)):
            bad_pair = json.loads(json.dumps(valid))
            bad_pair["result"]["feedback"]["samples"][0][field] = bad
            malformed.append(bad_pair)
        stale_pair = json.loads(json.dumps(valid))
        stale_pair["result"]["feedback"].update({"matches_current_context": False, "state": "stale", "reason": "changed"})
        malformed.append(stale_pair)
        wrong_nulls = performance("unused")
        wrong_nulls["result"]["cpu_submit_ms"]["mean"] = 0.0
        malformed.append(wrong_nulls)
        stale = json.loads(json.dumps(valid))
        stale["result"]["gpu_ms"]["last_frame"] = 10
        malformed.append(stale)
        contradiction = performance("unused")
        contradiction["result"]["gpu_ms"]["supported"] = True
        malformed.append(contradiction)
        for bad_state in ([], {}):
            invalid_state = performance("unused")
            invalid_state["result"]["gpu_ms"]["state"] = bad_state
            malformed.append(invalid_state)
        huge_target = performance("unused")
        huge_target["result"]["target_fps"] = 10 ** 400
        malformed.append(huge_target)
        huge_stat = json.loads(json.dumps(valid))
        huge_stat["result"]["gpu_ms"]["mean"] = 10 ** 400
        malformed.append(huge_stat)

        for field, bad in (("pending_queries", 9), ("draw_pending_queries", 9),
                           ("feedback_generation", -1), ("state", "available"),
                           ("samples", [{"frame": 8, "update_ms": 1, "draw_ms": 2}])):
            invalid_feedback = performance("unused")
            invalid_feedback["result"]["feedback"][field] = bad
            malformed.append(invalid_feedback)
        responses = [valid, *malformed]

        async def telemetry_handler(request, index):
            response = json.loads(json.dumps(responses[index - 1]))
            response["id"] = request["id"]
            return response

        async with Peer(telemetry_handler) as peer:
            params = StdioServerParameters(command=sys.executable,
                args=["adapter.py", "--socket", str(peer.path)], cwd=str(PROJECT_DIR))
            async with Client(params) as client:
                observed = await client.call_tool("get_performance", {})
                self.assertFalse(observed.is_error)
                for _ in malformed:
                    rejected = await client.call_tool("get_performance", {})
                    self.assertTrue(rejected.is_error)
                    self.assertEqual(rejected.structured_content["error"]["code"], "adapter_protocol_error")

    async def test_discovery_closed_schemas_annotations_and_native_capture(self):
        image = png()
        encoded = base64.b64encode(image).decode()

        async def handler(request, _):
            if request["method"] == "get_capabilities":
                return capabilities(request["id"])
            if request["method"] == "capture_preview":
                return envelope(request["id"], status="captured", method="capture_preview",
                                result={"mime_type": "image/png", "data_base64": encoded,
                                        "width": 2, "height": 1, "backing_width": 2,
                                        "backing_height": 1, "minimum_revision": 0})
            raise AssertionError(request)

        async with Peer(handler) as peer:
            params = StdioServerParameters(command=sys.executable,
                args=["adapter.py", "--socket", str(peer.path)], cwd=str(PROJECT_DIR))
            async with Client(params, mode="auto") as client:
                tools = await client.list_tools()
                instructions = client.session.instructions or ""
                self.assertIn("prefer trial_shader", instructions)
                self.assertIn("explicitly accept_trial", instructions)
                self.assertIn("unbudgeted compatibility bypasses", instructions)
                self.assertIn("media/update work", instructions)
                self.assertEqual([tool.name for tool in tools.tools], OPERATIONS)
                by_name = {tool.name: tool for tool in tools.tools}
                for tool in tools.tools:
                    self.assertFalse(tool.input_schema["additionalProperties"])
                    self.assertFalse(tool.annotations.open_world_hint)
                    self.assertIsNotNone(tool.output_schema)
                    self.assertFalse(tool.output_schema["additionalProperties"])
                    self.assertEqual(set(tool.output_schema["required"]), {
                        "adapter_version", "operation", "request_id", "outcome",
                        "delivery", "engine", "error",
                    })
                self.assertTrue(by_name["configure_layers"].annotations.destructive_hint)
                self.assertFalse(by_name["capture_preview"].annotations.read_only_hint)
                self.assertTrue(by_name["get_scene"].annotations.read_only_hint)
                self.assertIn("without a GPU budget", by_name["publish_shader"].description)
                self.assertIn("without GPU admission", by_name["set_uniform"].description)
                self.assertIn("GPU admission", by_name["trial_shader"].description)
                result = await client.call_tool("capture_preview", {"request_id": "capture-1", "wait_ms": 0})
                self.assertFalse(result.is_error)
                blocks = [block for block in result.content if isinstance(block, ImageContent)]
                self.assertEqual(len(blocks), 1)
                self.assertEqual(base64.b64decode(blocks[0].data), image)
                self.assertNotIn("data_base64", json.dumps(result.structured_content))
                self.assertEqual(result.structured_content["outcome"], "completed")
                close_started = time.monotonic()
            self.assertLess(time.monotonic() - close_started, 3.25)

            legacy_params = StdioServerParameters(command=sys.executable,
                args=["adapter.py", "--socket", str(peer.path)], cwd=str(PROJECT_DIR))
            async with Client(legacy_params, mode="legacy") as client:
                self.assertEqual(len((await client.list_tools()).tools), 14)
                result = await client.call_tool("capture_preview", {"request_id": "capture-legacy", "wait_ms": 0})
                self.assertFalse(result.is_error)
                self.assertEqual(len([block for block in result.content if isinstance(block, ImageContent)]), 1)
                close_started = time.monotonic()
            self.assertLess(time.monotonic() - close_started, 3.25)

    async def test_missing_endpoint_is_discoverable_and_reports_not_submitted(self):
        with tempfile.TemporaryDirectory(prefix="vosc-mcp-missing-") as directory:
            os.chmod(directory, 0o700)
            path = Path(directory) / "missing.sock"
            params = StdioServerParameters(command=sys.executable,
                args=["adapter.py", "--socket", str(path)], cwd=str(PROJECT_DIR))
            async with Client(params) as client:
                self.assertEqual(len((await client.list_tools()).tools), 14)
                result = await client.call_tool("configure_layers", {
                    "request_id": "missing-1", "count": 1, "layout": "stack", "wait_ms": 0})
                self.assertTrue(result.is_error)
                self.assertEqual(result.structured_content["delivery"], "not_submitted")
                self.assertEqual(result.structured_content["error"]["code"], "adapter_engine_unavailable")
                invalid_calls = [
                    ("configure_layers", {"request_id": "bad-extra", "count": 1, "layout": "stack", "extra": 1}),
                    ("configure_layers", {"request_id": "bad-bool", "count": True, "layout": "stack"}),
                    ("publish_shader", {"request_id": "_q.reserved", "layer": 0, "source": "x"}),
                    ("set_uniform", {"request_id": "bad-vector", "layer": 0, "name": "u", "value": [1]}),
                    ("set_layer", {"request_id": "bad-visible", "layer": 0, "property": "visible", "value": 1}),
                    ("capture_preview", {"request_id": "bad-width", "max_width": 63}),
                ]
                for name, arguments in invalid_calls:
                    invalid = await client.call_tool(name, arguments)
                    self.assertTrue(invalid.is_error, (name, arguments, invalid))
                    self.assertIsNone(invalid.structured_content)

    async def test_unreadable_endpoint_returns_structured_unsafe_error(self):
        with tempfile.TemporaryDirectory(prefix="vosc-mcp-unreadable-") as directory:
            root = Path(directory)
            os.chmod(root, 0o700)
            locked = root / "locked"
            locked.mkdir()
            os.chmod(locked, 0o000)
            try:
                params = StdioServerParameters(command=sys.executable,
                    args=["adapter.py", "--socket", str(locked / "control.sock")], cwd=str(PROJECT_DIR))
                async with Client(params) as client:
                    result = await client.call_tool("get_scene", {})
                    self.assertTrue(result.is_error)
                    self.assertIsNotNone(result.structured_content)
                    self.assertEqual(result.structured_content["delivery"], "query")
                    self.assertEqual(result.structured_content["error"]["code"], "adapter_unsafe_endpoint")
            finally:
                os.chmod(locked, 0o700)

    async def test_deep_valid_json_after_mutation_is_unknown_uncertain(self):
        with tempfile.TemporaryDirectory(prefix="vosc-mcp-deep-") as directory:
            root = Path(directory)
            os.chmod(root, 0o700)
            socket_path = root / "control.sock"
            mutation_seen = 0

            async def raw_peer(reader, writer):
                nonlocal mutation_seen
                try:
                    while line := await reader.readline():
                        request = json.loads(line)
                        if request["method"] == "get_capabilities":
                            writer.write(json.dumps(capabilities(request["id"]), separators=(",", ":")).encode() + b"\n")
                        else:
                            mutation_seen += 1
                            prefix = ("{\"id\":" + json.dumps(request["id"])
                                + ",\"ok\":true,\"status\":\"queued\",\"revision\":0,"
                                + "\"frame\":0,\"external_epoch\":0,\"nested\":")
                            writer.write((prefix + "[" * 50_000 + "0" + "]" * 50_000 + "}\n").encode())
                        await writer.drain()
                finally:
                    writer.close()

            server = await asyncio.start_unix_server(raw_peer, path=socket_path)
            os.chmod(socket_path, 0o600)
            try:
                params = StdioServerParameters(command=sys.executable,
                    args=["adapter.py", "--socket", str(socket_path)], cwd=str(PROJECT_DIR))
                async with Client(params) as client:
                    result = await client.call_tool("configure_layers", {
                        "request_id": "deep-after-write", "count": 1, "layout": "stack", "wait_ms": 0})
                    self.assertTrue(result.is_error)
                    self.assertEqual(mutation_seen, 1)
                    self.assertEqual((result.structured_content["outcome"], result.structured_content["delivery"]),
                                     ("unknown", "uncertain"))
                    self.assertEqual(result.structured_content["error"]["code"], "adapter_outcome_unknown")
            finally:
                server.close()
                await server.wait_closed()

    async def test_midwrite_abort_after_mutation_bytes_is_unknown_uncertain(self):
        with tempfile.TemporaryDirectory(prefix="vosc-mcp-midwrite-") as directory:
            root = Path(directory)
            os.chmod(root, 0o700)
            socket_path = root / "control.sock"
            observed: list[bytes] = []

            async def aborting_peer(reader, writer):
                try:
                    capability_request = json.loads(await reader.readline())
                    writer.write(json.dumps(capabilities(capability_request["id"]), separators=(",", ":")).encode() + b"\n")
                    await writer.drain()
                    observed.append(await reader.read(4096))
                    writer.transport.abort()
                finally:
                    writer.close()

            server = await asyncio.start_unix_server(aborting_peer, path=socket_path, limit=300_000)
            os.chmod(socket_path, 0o600)
            try:
                params = StdioServerParameters(command=sys.executable,
                    args=["adapter.py", "--socket", str(socket_path)], cwd=str(PROJECT_DIR))
                async with Client(params) as client:
                    result = await client.call_tool("publish_shader", {
                        "request_id": "midwrite-public", "layer": 0,
                        "source": "a" * 131_072, "wait_ms": 0,
                    })
                    self.assertTrue(result.is_error)
                    self.assertEqual(len(observed), 1)
                    self.assertEqual(len(observed[0]), 4096)
                    prefix = json.loads(observed[0].split(b',"params"', 1)[0] + b"}")
                    self.assertEqual(prefix["id"], "midwrite-public")
                    self.assertEqual((result.structured_content["outcome"], result.structured_content["delivery"]),
                                     ("unknown", "uncertain"))
                    self.assertEqual(result.structured_content["request_id"], "midwrite-public")
                    self.assertEqual(result.structured_content["error"]["code"], "adapter_outcome_unknown")
            finally:
                server.close()
                await server.wait_closed()

    async def test_concurrency_admission_and_exchange_bounds(self):
        async def handler(request, _):
            if request["method"] == "get_capabilities":
                return capabilities(request["id"])
            if request["method"] == "configure_layers":
                return envelope(request["id"], status="queued", method="configure_layers")
            if request["method"] == "get_request":
                return envelope(request["id"], result=envelope(request["params"]["request_id"],
                                status="queued", method="configure_layers"))
            if request["method"] == "get_scene":
                await asyncio.sleep(0.3)
                return envelope(request["id"], result={})
            raise AssertionError(request)

        async with Peer(handler) as peer:
            params = StdioServerParameters(command=sys.executable,
                args=["adapter.py", "--socket", str(peer.path)], cwd=str(PROJECT_DIR))
            async with Client(params) as client:
                mutations = [asyncio.create_task(client.call_tool("configure_layers", {
                    "request_id": f"onset-{index}", "count": 1, "layout": "stack",
                    "timing": "onset", "wait_ms": 500,
                })) for index in range(14)]
                deadline = time.monotonic() + 1
                while len([item for item in peer.requests if item["method"] == "configure_layers"]) < 14:
                    self.assertLess(time.monotonic(), deadline)
                    await asyncio.sleep(0.01)
                started = time.monotonic()
                overflow = await client.call_tool("configure_layers", {
                    "request_id": "onset-overflow", "count": 1, "layout": "stack",
                    "timing": "onset", "wait_ms": 500,
                })
                self.assertLess(time.monotonic() - started, 0.25)
                self.assertTrue(overflow.is_error)
                self.assertEqual(overflow.structured_content["error"]["code"], "adapter_busy")
                self.assertEqual(len([item for item in peer.requests if item.get("id") == "onset-overflow"]), 0)
                results = await asyncio.gather(*mutations)
                self.assertTrue(all(item.structured_content["outcome"] == "pending" for item in results))
                self.assertLessEqual(peer.peak, 4)

    async def test_query_admission_and_failed_inner_receipt_projection(self):
        scene_started = 0
        both_started = asyncio.Event()

        async def handler(request, _):
            nonlocal scene_started
            if request["method"] == "get_scene":
                scene_started += 1
                if scene_started == 2:
                    both_started.set()
                await asyncio.sleep(0.3)
                return scene(request["id"])
            if request["method"] == "get_request":
                if request["params"]["request_id"] == "bad-captured":
                    captured = envelope("bad-captured", status="captured", method="publish_shader",
                        result={"mime_type": "image/png", "data_base64": base64.b64encode(png()).decode(),
                                "width": 2, "height": 1, "backing_width": 2,
                                "backing_height": 1, "minimum_revision": 0})
                    return envelope(request["id"], result=captured)
                failed = envelope(request["params"]["request_id"], status="failed", ok=False,
                    error={"code": "shader_failed", "message": "compile failed", "diagnostics": []})
                return envelope(request["id"], result=failed)
            raise AssertionError(request)

        async with Peer(handler) as peer:
            params = StdioServerParameters(command=sys.executable,
                args=["adapter.py", "--socket", str(peer.path)], cwd=str(PROJECT_DIR))
            async with Client(params) as client:
                active = [asyncio.create_task(client.call_tool("get_scene", {})) for _ in range(2)]
                await asyncio.wait_for(both_started.wait(), 1)
                overflow = await client.call_tool("get_scene", {})
                self.assertTrue(overflow.is_error)
                self.assertEqual(overflow.structured_content["error"]["code"], "adapter_busy")
                self.assertTrue(all(not item.is_error for item in await asyncio.gather(*active)))
                failed = await client.call_tool("get_request", {"request_id": "failed-1"})
                self.assertTrue(failed.is_error)
                self.assertEqual(failed.structured_content["outcome"], "failed")
                self.assertEqual(failed.structured_content["engine"]["id"], "failed-1")
                self.assertEqual(failed.structured_content["error"]["code"], "shader_failed")
                wrong_status = await client.call_tool("get_request", {"request_id": "bad-captured"})
                self.assertTrue(wrong_status.is_error)
                self.assertEqual(wrong_status.structured_content["error"]["code"], "adapter_protocol_error")

    async def test_poll_failure_deadline_png_and_sanitizer_precedence(self):
        truncated = b"\x89PNG\r\n\x1a\n" + struct.pack(">I4sII", 13, b"IHDR", 2, 1)

        async def handler(request, _):
            method = request["method"]
            if method == "get_capabilities":
                return capabilities(request["id"])
            if method == "configure_layers":
                return envelope(request["id"], status="queued", method=method,
                                result={"data_base64": "must-not-leak"})
            if method == "capture_preview":
                image = truncated if request["id"] == "truncated-png" else png_header(compression=1)
                return envelope(request["id"], status="captured", method=method,
                    result={"mime_type": "image/png", "data_base64": base64.b64encode(image).decode(),
                            "width": 2, "height": 1, "backing_width": 2,
                            "backing_height": 1, "minimum_revision": 0})
            if method == "get_request":
                target = request["params"]["request_id"]
                if target == "poll-eof":
                    return None
                if target == "poll-method-mismatch":
                    return envelope(request["id"], result=envelope(
                        target, status="rendered", method="set_layer", applied_after_frame=1))
                if target == "invalid-ihdr-lookup":
                    captured = envelope(target, status="captured",
                        result={"mime_type": "image/png",
                                "data_base64": base64.b64encode(png_header(bit_depth=4, color_type=2)).decode(),
                                "width": 2, "height": 1, "backing_width": 2,
                                "backing_height": 1, "minimum_revision": 0})
                    return envelope(request["id"], result=captured)
                await asyncio.sleep(0.5)
                return envelope(request["id"], result=envelope(target, status="queued", method="configure_layers"))
            raise AssertionError(request)

        async with Peer(handler) as peer:
            params = StdioServerParameters(command=sys.executable,
                args=["adapter.py", "--socket", str(peer.path)], cwd=str(PROJECT_DIR))
            async with Client(params) as client:
                unknown = await client.call_tool("configure_layers", {
                    "request_id": "poll-eof", "count": 1, "layout": "stack", "wait_ms": 1000})
                self.assertTrue(unknown.is_error)
                self.assertEqual((unknown.structured_content["outcome"], unknown.structured_content["delivery"]),
                                 ("unknown", "submitted"))
                self.assertNotIn("data_base64", json.dumps(unknown.structured_content))
                mismatched = await client.call_tool("configure_layers", {
                    "request_id": "poll-method-mismatch", "count": 1, "layout": "stack", "wait_ms": 1000})
                self.assertTrue(mismatched.is_error)
                self.assertEqual(mismatched.structured_content["outcome"], "unknown")
                self.assertEqual(mismatched.structured_content["engine"]["status"], "queued")
                started = time.monotonic()
                pending = await client.call_tool("configure_layers", {
                    "request_id": "poll-deadline", "count": 1, "layout": "stack", "wait_ms": 100})
                self.assertLess(time.monotonic() - started, 0.35)
                self.assertFalse(pending.is_error)
                self.assertEqual(pending.structured_content["outcome"], "pending")
                invalid_png = await client.call_tool("capture_preview", {
                    "request_id": "truncated-png", "wait_ms": 0})
                self.assertTrue(invalid_png.is_error)
                self.assertEqual(invalid_png.structured_content["delivery"], "submitted")
                self.assertEqual(invalid_png.structured_content["error"]["code"], "adapter_protocol_error")
                self.assertNotIn("data_base64", json.dumps(invalid_png.structured_content))
                invalid_header = await client.call_tool("capture_preview", {
                    "request_id": "invalid-ihdr-direct", "wait_ms": 0})
                self.assertTrue(invalid_header.is_error)
                self.assertEqual(invalid_header.structured_content["error"]["code"], "adapter_protocol_error")
                self.assertFalse(any(isinstance(block, ImageContent) for block in invalid_header.content))
                invalid_lookup = await client.call_tool("get_request", {
                    "request_id": "invalid-ihdr-lookup", "wait_ms": 0})
                self.assertTrue(invalid_lookup.is_error)
                self.assertEqual(invalid_lookup.structured_content["delivery"], "query")
                self.assertEqual(invalid_lookup.structured_content["error"]["code"], "adapter_protocol_error")
                self.assertFalse(any(isinstance(block, ImageContent) for block in invalid_lookup.content))

    async def test_live_harness_reruns_use_fresh_ids_on_same_peer(self):
        receipts: dict[str, dict] = {}
        encoded = base64.b64encode(png()).decode()

        async def handler(request, _):
            method = request["method"]
            if method == "get_capabilities":
                return capabilities(request["id"])
            if method == "get_scene":
                return scene(request["id"])
            if method == "get_diagnostics":
                return envelope(request["id"], result={"entries": [], "next_cursor": 0,
                    "latest_cursor": 0, "gap": False})
            if method == "get_request":
                return envelope(request["id"], result=receipts[request["params"]["request_id"]])
            if request["id"] in receipts:
                return receipts[request["id"]]
            if method == "publish_shader" and request["params"]["source"] == live_harness.BROKEN:
                receipt = envelope(request["id"], status="failed", ok=False,
                    error={"code": "shader_failed", "message": "compile failed", "diagnostics": []})
            elif method == "capture_preview":
                receipt = envelope(request["id"], status="captured",
                    result={"mime_type": "image/png", "data_base64": encoded,
                            "width": 2, "height": 1, "backing_width": 2,
                            "backing_height": 1, "minimum_revision": 0})
            else:
                receipt = envelope(request["id"], status="rendered", method=method,
                                   applied_after_frame=8)
            receipts[request["id"]] = receipt
            return receipt

        async with Peer(handler) as peer:
            with tempfile.TemporaryDirectory(prefix="vosc-mcp-harness-") as output:
                first = await live_harness.run(peer.path, Path(output) / "first")
                second = await live_harness.run(peer.path, Path(output) / "second")
                first_ids = set(first["request_ids"].values())
                second_ids = set(second["request_ids"].values())
                self.assertTrue(first_ids.isdisjoint(second_ids))
                self.assertEqual(len(first_ids), 7)
                self.assertEqual(len(second_ids), 7)
                submitted = [item["id"] for item in peer.requests if item["method"] in {
                    "configure_layers", "publish_shader", "set_uniform", "set_layer", "capture_preview"
                }]
                self.assertEqual(len(submitted), 14)
                self.assertEqual(len(set(submitted)), 14)
                for name, evidence in (("first", first), ("second", second)):
                    saved = json.loads((Path(output) / name / "mcp-api.json").read_text())
                    self.assertEqual(saved["run_namespace"], evidence["run_namespace"])
                    self.assertEqual(saved["request_ids"], evidence["request_ids"])
                    self.assertIsNone(saved["failure"])

    async def test_trial_tools_project_ready_rejection_and_old_engine_unknown_method(self):
        async def handler(request, _):
            method = request["method"]
            if method == "get_capabilities":
                return capabilities(request["id"])
            if method == "get_scene":
                return scene(request["id"])
            if method == "trial_shader":
                if request["id"] == "trial-reject":
                    trial = trial_metadata(request["id"], "rolled_back")
                    trial.update(rollback_code="trial_budget_exceeded", restored_previous=True,
                                 restoration_revision=5, rendered_frame=46)
                    return envelope(request["id"], status="failed", ok=False, method=method, frame=46, revision=5,
                        error={"code": "trial_budget_exceeded", "message": "over budget", "diagnostics": []},
                        result={"trial": trial})
                return envelope(request["id"], status="ready", method=method, applied_after_frame=9, frame=45, revision=4,
                                result={"trial": trial_metadata(request["id"], "ready")})
            if method in {"accept_trial", "cancel_trial"}:
                return envelope(request["id"], status="rendered", method=method, applied_after_frame=45)
            raise AssertionError(request)

        async with Peer(handler) as peer:
            params = StdioServerParameters(command=sys.executable,
                args=["adapter.py", "--socket", str(peer.path)], cwd=str(PROJECT_DIR))
            async with Client(params) as client:
                self.assertEqual(len((await client.list_tools()).tools), 14)
                ready = await client.call_tool("trial_shader", {"request_id": "trial-ready", "layer": 0,
                    "source": "void main(){}", "wait_ms": 0})
                self.assertFalse(ready.is_error)
                self.assertEqual(ready.structured_content["outcome"], "pending")
                accepted = await client.call_tool("accept_trial", {"request_id": "accept-1",
                    "trial_id": "trial-ready", "wait_ms": 0})
                self.assertFalse(accepted.is_error)
                self.assertEqual(accepted.structured_content["outcome"], "completed")
                rejected = await client.call_tool("trial_shader", {"request_id": "trial-reject", "layer": 0,
                    "source": "void main(){}", "wait_ms": 0})
                self.assertTrue(rejected.is_error)
                self.assertEqual(rejected.structured_content["error"]["code"], "trial_budget_exceeded")

        async def old_handler(request, _):
            if request["method"] == "get_capabilities":
                return capabilities(request["id"], operations=OPERATIONS[:-3])
            if request["method"] == "get_scene":
                return scene(request["id"])
            return envelope(request["id"], status="failed", ok=False,
                error={"code": "unknown_method", "message": "unsupported method", "diagnostics": []})

        async with Peer(old_handler) as peer:
            params = StdioServerParameters(command=sys.executable,
                args=["adapter.py", "--socket", str(peer.path)], cwd=str(PROJECT_DIR))
            async with Client(params) as client:
                unsupported = await client.call_tool("trial_shader", {"request_id": "old-trial", "layer": 0,
                    "source": "void main(){}", "wait_ms": 0})
                self.assertTrue(unsupported.is_error)
                self.assertEqual(unsupported.structured_content["error"]["code"], "unknown_method")
                observed = await client.call_tool("get_scene", {})
                self.assertFalse(observed.is_error)

    async def test_get_request_rejects_unattributed_trial_metadata(self):
        async def handler(request, _):
            if request["method"] == "get_capabilities":
                return capabilities(request["id"])
            self.assertEqual(request["method"], "get_request")
            target = request["params"]["request_id"]
            failed = envelope(target, status="failed", ok=False, frame=46, revision=5,
                error={"code": "trial_cancelled", "message": "rolled back", "diagnostics": []})
            if target == "legacy-failure":
                failed["error"]["code"] = "unknown_method"
                return envelope(request["id"], frame=46, revision=5, result=failed)
            trial = trial_metadata(target, "rolled_back")
            trial.update(rollback_code="trial_cancelled", restored_previous=True,
                         restoration_revision=5, rendered_frame=46)
            if target == "malformed-unattributed":
                trial.update(phase=["rolled_back"], restored_previous="yes")
            elif target == "wrong-method":
                failed["method"] = "set_uniform"
            elif target == "valid-trial":
                failed["method"] = "trial_shader"
            failed["result"] = {"trial": trial}
            return envelope(request["id"], frame=46, revision=5, result=failed)

        async with Peer(handler) as peer:
            params = StdioServerParameters(command=sys.executable,
                args=["adapter.py", "--socket", str(peer.path)], cwd=str(PROJECT_DIR))
            async with Client(params) as client:
                for target in ("malformed-unattributed", "valid-unattributed", "wrong-method"):
                    with self.subTest(target=target):
                        result = await client.call_tool("get_request", {"request_id": target})
                        self.assertTrue(result.is_error)
                        self.assertEqual(result.structured_content["error"]["code"], "adapter_protocol_error")
                        self.assertIsNone(result.structured_content["engine"])
                for target, code in (("legacy-failure", "unknown_method"),
                                     ("valid-trial", "trial_cancelled")):
                    with self.subTest(target=target):
                        result = await client.call_tool("get_request", {"request_id": target})
                        self.assertTrue(result.is_error)
                        self.assertEqual(result.structured_content["error"]["code"], code)
                        self.assertEqual(result.structured_content["engine"]["id"], target)

    def test_adapter_sigterm_exits_promptly(self):
        with tempfile.TemporaryDirectory(prefix="vosc-mcp-lifecycle-") as directory:
            path = Path(directory) / "missing.sock"
            process = subprocess.Popen(
                [sys.executable, "adapter.py", "--socket", str(path)], cwd=PROJECT_DIR,
                stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            )
            started = time.monotonic()
            process.terminate()
            stdout, stderr = process.communicate(timeout=3.25)
            self.assertLess(time.monotonic() - started, 3.25)
            self.assertEqual(process.returncode, -15, stderr.decode(errors="replace"))
            self.assertEqual(stdout, b"")


if __name__ == "__main__":
    unittest.main()
