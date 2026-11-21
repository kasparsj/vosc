#!/usr/bin/env python3
"""Owned two-renderer native/SC acceptance harness for editable scene recipes."""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
from contextlib import closing
from pathlib import Path
import re
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
import struct
import uuid
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "mcp"))
sys.path.insert(0, str(ROOT / "scripts"))
from control_client import Client
from runtime_smoke import encode_message
from test_engine_api import png_pixels
from scene_recipe_schema import validate_descriptor
from visuals_quark import checkout_path as quark_checkout_path, class_config as quark_class_config
from visuals_quark import probe as probe_visuals_quark, source_identity as quark_source_identity

CONTEXTUAL_IDENTIFIER_FIXTURES = json.loads(
    (ROOT / "tests/fixtures/contextual_identifiers.json").read_text())

LAYER0_SHADER = """#version 330
uniform vec2 resolution;
uniform sampler2D imageTex;
uniform float gain;
uniform vec4 globalColor;
out vec4 fragColor;
void main() {
    vec2 uv = gl_FragCoord.xy / max(resolution, vec2(1.0));
    fragColor = vec4(texture(imageTex, uv).rgb * gain, 0.72) * globalColor;
}
"""
LAYER1_SHADER = """#version 330
uniform vec2 resolution;
uniform vec4 globalColor;
uniform vec4 wideValue;
uniform vec2 pairValue;
uniform vec3 tripleValue;
out vec4 fragColor;
void main() {
    vec2 uv = gl_FragCoord.xy / max(resolution, vec2(1.0));
    bool exact = all(equal(wideValue, vec4(2.0, 3.0, 4.0, 5.0)))
        && all(equal(pairValue, vec2(0.25, 0.75)))
        && all(equal(tripleValue, vec3(0.125, 0.5, 0.875)));
    fragColor = exact
        ? vec4(0.08 + uv.x * 0.12, 0.18, 0.82 - uv.y * 0.15, 0.68) * globalColor
        : vec4(1.0, 0.0, 0.0, 1.0);
}
"""
INT_VECTOR_SHADER = """#version 330
uniform ivec3 pairValue;
out vec4 fragColor;
void main() {
    bool exact = pairValue.x == 16777217 && pairValue.y == 2147483647 && pairValue.z == 3;
    fragColor = exact ? vec4(0.0, 1.0, 0.0, 1.0) : vec4(1.0, 0.0, 0.0, 1.0);
}
"""
FLOAT_VECTOR_SHADER = """#version 330
uniform vec4 wideValue;
out vec4 fragColor;
void main() {
    bool exact = all(equal(wideValue, vec4(2.0, 1e-12, 1.0, 5.0)));
    fragColor = exact ? vec4(0.0, 1.0, 0.0, 1.0) : vec4(1.0, 0.0, 0.0, 1.0);
}
"""
MIXED_VECTOR_SHADER = """#version 330
uniform vec2 pairValue;
uniform vec3 tripleValue;
out vec4 fragColor;
void main() {
    bool exact = all(equal(pairValue, vec2(2.0, 1.0)))
        && all(equal(tripleValue, vec3(1e-12, 3.0, 5.0)));
    fragColor = exact ? vec4(0.0, 1.0, 0.0, 1.0) : vec4(1.0, 0.0, 0.0, 1.0);
}
"""


def free_udp_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    if port == 33333 or port == 0:
        return free_udp_port()
    return port


def wait_socket(process: subprocess.Popen, path: Path, log: Path) -> None:
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        if path.is_socket(): return
        if process.poll() is not None: raise RuntimeError(log.read_text(errors="replace"))
        time.sleep(0.05)
    raise TimeoutError("owned renderer control socket did not appear")


def png_rgba(width: int, height: int, first: tuple[int, int, int, int], second: tuple[int, int, int, int]) -> bytes:
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        for x in range(width):
            rows.extend(first if ((x // 8) + (y // 8)) % 2 == 0 else second)
    def chunk(kind: bytes, data: bytes) -> bytes:
        checksum = zlib.crc32(kind + data) & 0xFFFFFFFF
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", checksum)
    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(bytes(rows), 9)) + chunk(b"IEND", b""))


def osc(port: int, address: str, *arguments) -> None:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as connection:
        connection.sendto(encode_message(address, arguments), ("127.0.0.1", port))
    time.sleep(0.08)


def osc_bundle(messages: list[bytes]) -> bytes:
    return b"#bundle\0" + struct.pack(">Q", 1) + b"".join(
        struct.pack(">I", len(message)) + message for message in messages)


def decode_osc_message(data: bytes) -> tuple[str, list]:
    def string_at(offset: int) -> tuple[str, int]:
        end = data.index(b"\0", offset)
        return data[offset:end].decode("utf-8"), (end + 4) & ~3
    address, offset = string_at(0)
    types, offset = string_at(offset)
    values = []
    for kind in types[1:]:
        if kind == "s": value, offset = string_at(offset)
        elif kind == "i": value, offset = struct.unpack(">i", data[offset:offset + 4])[0], offset + 4
        elif kind == "f": value, offset = struct.unpack(">f", data[offset:offset + 4])[0], offset + 4
        else: raise RuntimeError(f"unsupported OSC reply type {kind}")
        values.append(value)
    return address, values


def recipe_messages(token: str, reply_port: int, commands: list[tuple[str, tuple]], *,
                    expected: int | None = None, final_gate: int = 0) -> list[bytes]:
    messages = [encode_message("/scene/reset", (token, reply_port, 800, 600))]
    messages.extend(encode_message("/scene/command", (token, index, address, *arguments))
                    for index, (address, arguments) in enumerate(commands, 1))
    messages.append(encode_message("/scene/replay/end",
        (token, reply_port, len(commands) if expected is None else expected, final_gate)))
    return messages


def receive_results(listener: socket.socket, count: int, timeout: float = 15) -> list[dict]:
    listener.settimeout(timeout)
    results = []
    for _ in range(count):
        data, sender = listener.recvfrom(4096)
        address, values = decode_osc_message(data)
        if address != "/scene/replay/result" or len(values) != 4:
            raise RuntimeError(f"unexpected replay reply: {address} {values}")
        results.append({"token": values[0], "status": values[1], "frame": values[2],
                        "detail": values[3], "sender": sender})
    return results


def arm_recovery_hook(renderer: "OwnedRenderer", command: dict) -> int:
    if not renderer.hooks_enabled:
        raise RuntimeError("recovery hook requested from an ordinary binary")
    command_path = renderer.runtime / "command.json"
    events_path = renderer.runtime / "events.jsonl"
    existing = len(events_path.read_text().splitlines()) if events_path.exists() else 0
    command_path.write_text(json.dumps(command) + "\n")
    return existing


def wait_recovery_hook(renderer: "OwnedRenderer", command: dict, existing: int) -> dict:
    events_path = renderer.runtime / "events.jsonl"
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        lines = events_path.read_text().splitlines() if events_path.exists() else []
        for line in lines[existing:]:
            event = json.loads(line)
            if event.get("id") == command["id"]:
                return event
        time.sleep(0.02)
    raise TimeoutError(f"recovery hook did not complete: {command}")


def resize_scene(port: int, width: int, height: int) -> dict:
    """Use the recipe wire itself so producer pixels are native 800x600."""
    token = str(uuid.uuid4())
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reply:
        reply.bind(("127.0.0.1", 0))
        reply.settimeout(15)
        reply_port = reply.getsockname()[1]
        messages = [
            encode_message("/scene/reset", (token, reply_port, width, height)),
            encode_message("/scene/replay/end", (token, reply_port, 0, 0)),
        ]
        bundle = b"#bundle\0" + struct.pack(">Q", 1) + b"".join(
            struct.pack(">I", len(message)) + message for message in messages)
        reply.sendto(bundle, ("127.0.0.1", port))
        response, sender = reply.recvfrom(4096)
    if b"applied" not in response or token.encode() not in response:
        raise RuntimeError(f"native resize replay did not apply: {response!r}")
    return {"token": token, "reply_port": reply_port, "sender": sender,
            "width": width, "height": height, "response_hex": response.hex()}


class RendererCleanupError(RuntimeError):
    def __init__(self, message: str, receipt: dict):
        super().__init__(message)
        self.receipt = receipt


class OwnedRenderer:
    def __init__(self, binary: Path, output: Path, name: str, port: int, sanitizers: list[str],
                 *, arguments=(), cwd: Path | None = None):
        self.binary, self.port = binary, port
        self.runtime = Path(tempfile.mkdtemp(prefix=f"vosc-recipe-{name}-", dir="/tmp"))
        self.runtime.chmod(0o700)
        self.socket = self.runtime / "control.sock"
        self.log = output / f"renderer-{name}.log"
        self.stream = self.log.open("wb")
        environment = {**os.environ, "VOSC_CONTROL_SOCKET": str(self.socket), "VOSC_OSC_PORT": str(port)}
        environment["VOSC_RECOVERY_TEST_DIR"] = str(self.runtime)
        if "address" in sanitizers:
            environment["ASAN_OPTIONS"] = "abort_on_error=1:halt_on_error=1:detect_leaks=0"
        if "undefined" in sanitizers:
            environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
        self.process = subprocess.Popen([str(binary), *map(str, arguments)], cwd=cwd or binary.parents[3],
            env=environment,
            stdout=self.stream, stderr=subprocess.STDOUT, start_new_session=True)
        try:
            wait_socket(self.process, self.socket, self.log)
        except Exception:
            if self.process.poll() is None:
                self.process.terminate()
                try:
                    self.process.wait(timeout=8)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait(timeout=8)
            self.stream.close()
            shutil.rmtree(self.runtime, ignore_errors=True)
            raise
        self.hooks_enabled = (self.runtime / "events.jsonl").is_file()
        self.owner = {"pid": self.process.pid, "binary": str(binary),
            "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
            "control_socket": str(self.socket), "osc_port": port, "sanitizers_expected": sanitizers,
                      "arguments": list(map(str, arguments)), "cwd": str(cwd or binary.parents[3])}
        self.owner["recovery_test_hooks"] = self.hooks_enabled

    def client(self) -> Client:
        return Client(self.socket)

    def stop(self) -> dict:
        identity = ""
        forced_kill = False
        sanitizer_grace = 0.0
        exited_during_grace = False
        sanitizer_marker_seen = False
        sanitizer_forced_cleanup = False
        cleanup_errors: list[str] = []
        try:
            if self.owner["sanitizers_expected"] and self.process.poll() is None:
                sanitizer_grace = 1.0
                deadline = time.monotonic() + sanitizer_grace
                while self.process.poll() is None and time.monotonic() < deadline:
                    log_snapshot = self.log.read_text(errors="replace") if self.log.exists() else ""
                    if (not sanitizer_marker_seen and
                            ("ERROR: AddressSanitizer" in log_snapshot or "runtime error:" in log_snapshot)):
                        sanitizer_marker_seen = True
                        sanitizer_grace += 5.0
                        deadline = time.monotonic() + 5.0
                    time.sleep(0.05)
                exited_during_grace = self.process.poll() is not None
                if sanitizer_marker_seen and self.process.poll() is None:
                    self.process.kill()
                    self.process.wait(timeout=8)
                    forced_kill = True
                    sanitizer_forced_cleanup = True
            if self.process.poll() is None:
                identity = subprocess.check_output(
                    ["ps", "-p", str(self.process.pid), "-o", "comm="], text=True).strip()
                if identity != str(self.binary):
                    raise RuntimeError(f"refusing to stop unexpected process identity {identity!r}")
                os.kill(self.process.pid, signal.SIGTERM)
                try:
                    self.process.wait(timeout=8)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait(timeout=8)
                    forced_kill = True
        except BaseException as error:
            cleanup_errors.append(f"cleanup operation failed: {error!r}")
        try:
            self.stream.close()
        except BaseException as error:
            cleanup_errors.append(f"renderer log close failed: {error!r}")
        try:
            log_text = self.log.read_text(errors="replace")
        except BaseException as error:
            log_text = ""
            cleanup_errors.append(f"renderer log read failed: {error!r}")
        try:
            socket_removed = not self.socket.exists()
        except BaseException as error:
            socket_removed = False
            cleanup_errors.append(f"control socket inspection failed: {error!r}")
        try:
            return_code = self.process.returncode
        except BaseException as error:
            return_code = None
            cleanup_errors.append(f"process return-code inspection failed: {error!r}")
        result = {"pid": getattr(self.process, "pid", None), "identity": identity, "return_code": return_code,
                  "socket_removed": socket_removed, "forced_kill": forced_kill,
                  "sanitizer_grace_seconds": sanitizer_grace,
                  "exited_during_sanitizer_grace": exited_during_grace,
                  "sanitizer_marker_seen_before_signal": sanitizer_marker_seen,
                  "sanitizer_forced_cleanup": sanitizer_forced_cleanup}
        findings = []
        if "address" in self.owner["sanitizers_expected"] and "AddressSanitizer" in log_text:
            findings.append(f"AddressSanitizer reported in {self.log}")
        if "undefined" in self.owner["sanitizers_expected"]:
            runtime_errors = [line for line in log_text.splitlines() if "runtime error:" in line]
            if runtime_errors:
                findings.append(f"UndefinedBehaviorSanitizer reported in {self.log}: {runtime_errors}")
        result["sanitizer_findings"] = findings
        cleanup_errors.extend(findings)
        if result["return_code"] != 0:
            cleanup_errors.append(f"owned renderer exited nonzero: {result['return_code']!r}")
        if result["forced_kill"]:
            cleanup_errors.append("owned renderer required a forced kill")
        if not result["socket_removed"]:
            cleanup_errors.append(f"owned renderer retained control socket: {self.socket}")
        result["cleanup_errors"] = cleanup_errors
        shutil.rmtree(self.runtime, ignore_errors=True)
        if cleanup_errors:
            raise RendererCleanupError("; ".join(cleanup_errors), result)
        return result


def cleanup_receipt_error(receipt: object) -> str | None:
    if not isinstance(receipt, dict):
        return f"cleanup returned a non-object receipt: {receipt!r}"
    errors = []
    if receipt.get("return_code") != 0:
        errors.append(f"invalid cleanup return_code: {receipt.get('return_code')!r}")
    if receipt.get("forced_kill") is not False:
        errors.append(f"invalid cleanup forced_kill: {receipt.get('forced_kill')!r}")
    if receipt.get("socket_removed") is not True:
        errors.append(f"invalid cleanup socket_removed: {receipt.get('socket_removed')!r}")
    return "; ".join(errors) if errors else None


def collect_cleanup(renderer: OwnedRenderer) -> tuple[dict, str | None]:
    """Stop one owned renderer and always return a serializable cleanup receipt."""
    try:
        receipt = renderer.stop()
        return receipt, cleanup_receipt_error(receipt)
    except RendererCleanupError as error:
        return error.receipt, str(error)
    except BaseException as error:
        process = getattr(renderer, "process", None)
        socket_path = getattr(renderer, "socket", None)
        try:
            socket_removed = socket_path is None or not socket_path.exists()
        except BaseException:
            socket_removed = False
        owner = getattr(renderer, "owner", {})
        pid = getattr(process, "pid", None) if process is not None else None
        if not isinstance(pid, int) or isinstance(pid, bool):
            pid = owner.get("pid") if isinstance(owner, dict) else None
        return_code = getattr(process, "returncode", None) if process is not None else None
        if return_code is not None and (not isinstance(return_code, int) or isinstance(return_code, bool)):
            return_code = None
        receipt = {
            "pid": pid,
            "identity": "",
            "return_code": return_code,
            "socket_removed": socket_removed,
            "forced_kill": None,
            "sanitizer_findings": [],
            "cleanup_errors": [f"unexpected cleanup exception: {error!r}"],
        }
        return receipt, receipt["cleanup_errors"][0]


def applied(client: Client, method: str, params: dict) -> dict:
    response = client.call(method, params)
    if not response.get("ok"):
        raise RuntimeError(f"{method} rejected: {response}")
    final = client.wait(response["id"], timeout=15)
    if not final.get("ok") or final.get("status") != "rendered":
        raise RuntimeError(f"{method} did not render: {final}")
    return final


def capture(client: Client, path: Path, *, max_width=800) -> tuple[int, int, list[tuple[int, int, int, int]], dict]:
    response = client.call("capture_preview", {"max_width": max_width})
    final = client.wait(response["id"], timeout=15)
    if not final.get("ok") or final.get("status") != "captured":
        raise RuntimeError(f"capture failed: {final}")
    data = base64.b64decode(final["result"]["data_base64"], validate=True)
    path.write_bytes(data)
    width, height, pixels = png_pixels(data, include_alpha=True)
    return width, height, pixels, final["result"]


def configure_scene(renderer: OwnedRenderer, image: Path, *, edited: bool) -> dict:
    client = renderer.client()
    try:
        applied(client, "configure_layers", {"count": 2, "layout": "stack"})
        applied(client, "publish_shader", {"layer": 0, "source": LAYER0_SHADER})
        applied(client, "publish_shader", {"layer": 1, "source": LAYER1_SHADER})
        for name, value in {"wideValue": [2.0, 3.0, 4.0, 5.0],
                            "pairValue": [0.25, 0.75], "tripleValue": [0.125, 0.5, 0.875]}.items():
            applied(client, "set_uniform", {"layer": 1, "name": name, "value": value})
    finally:
        client.close()
    commands = [
        ("/onset", (0,)),
        ("/input", (0, "", 0, 256)),
        ("/shader/var", (0, "gain", "in_0*0.75+0.1" if edited else "in_0")),
        ("/shader/texture", (0, "imageTex", str(image))),
        ("/shader/texture/size", (0, "imageTex", 320.0, 240.0)),
        ("/shader/texture/set", (0, "imageTex", "aspectRatio", 0)),
        ("/shader/texture/set", (0, "imageTex", "static", 1)),
        ("/shader/texture/set", (0, "imageTex", "blendMode", 2)),
        ("/layer/pos", (0, "[-70.0,45.0,0.0]")),
        ("/layer/size", (0, "[650.0,520.0,0.0]")),
        ("/layer/tint", (0, "[0.9,0.82,1.0,0.92]")),
        ("/layer/alpha", (0, 0.86)),
        ("/layer/align", (0, "[1,16]")),
        ("/layer/blend", (0, 1)),
        ("/layer/pos", (1, "[105.0,-35.0,0.0]")),
        ("/layer/size", (1, "[610.0,490.0,0.0]")),
        ("/layer/tint", (1, "[0.78,1.0,0.88,0.9]")),
        ("/layer/alpha", (1, 0.63 if edited else 0.57)),
        ("/layer/align", (1, "[2,64]")),
        ("/layer/blend", (1, 2)),
    ]
    for address, arguments in commands:
        osc(renderer.port, address, *arguments)
    osc(renderer.port, "/input/data", 0, 0.62, 0.0, 0)
    time.sleep(1.2)
    client = renderer.client()
    try:
        exported = client.call("export_scene", {})
        if not exported.get("ok"):
            raise RuntimeError(f"native export failed: {exported}")
        validate_descriptor(exported["result"]["recipe"])
        recipe = exported["result"]["recipe"]
        gain = [command for command in recipe["commands"] if
                command["address"] == "/shader/var" and command["args"][1]["value"] == "gain"]
        expected_gain = "in_0*0.75+0.1" if edited else "in_0"
        if len(gain) != 1 or gain[0]["args"][2] != {"type": "s", "value": expected_gain}:
            raise RuntimeError(f"gain expression source mismatch: expected {expected_gain!r}, got {gain}")
        wide = [command for command in recipe["commands"] if
                command["address"] == "/shader/var" and command["args"][1]["value"] == "wideValue"]
        expected_wide = [{"type": "i", "value": 1}, {"type": "s", "value": "wideValue"}] + [
            {"type": "f", "value": value} for value in (2.0, 3.0, 4.0, 5.0)]
        if len(wide) != 1 or wide[0]["args"] != expected_wide:
            raise RuntimeError(f"wide float vec4 did not export exactly: {wide}")
        for name, values in {"pairValue": [0.25, 0.75], "tripleValue": [0.125, 0.5, 0.875]}.items():
            bindings = [command for command in recipe["commands"] if
                        command["address"] == "/shader/var" and command["args"][1]["value"] == name]
            expected = [{"type": "i", "value": 1}, {"type": "s", "value": name}] + [
                {"type": "f", "value": value} for value in values]
            if len(bindings) != 1 or bindings[0]["args"] != expected:
                raise RuntimeError(f"API float vector did not export exactly: {name}: {bindings}")
        return recipe
    finally:
        client.close()


def normalize_descriptor(value: dict) -> dict:
    normalized = json.loads(json.dumps(value, sort_keys=True))
    normalized["assets"].sort(key=lambda item: item["name"])
    return normalized


def sc_escape(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"')


def prepare_scene_endpoint(text: str, renderer_port: int) -> tuple[str, str]:
    """Bind generated source to one caller-owned private port or fail closed."""
    if not isinstance(renderer_port, int) or isinstance(renderer_port, bool) or not 1 <= renderer_port <= 65535:
        raise ValueError("renderer port must be an integer in 1..65535")
    if renderer_port == 33333:
        raise ValueError("default renderer port 33333 is never an owned proof endpoint")
    default_assignment = "var rendererPort = 33333;"
    owned_assignment = f"var rendererPort = {renderer_port};"
    exact_assignment = lambda line, prefix: line == prefix or re.fullmatch(
        re.escape(prefix) + r"\s*//[^\r\n]*", line) is not None
    assignment_lines = [line for line in text.splitlines() if line.startswith("var rendererPort")]
    if len(assignment_lines) != 1:
        raise ValueError(f"expected exactly one rendererPort assignment, found {len(assignment_lines)}")
    assignment = assignment_lines[0]
    if exact_assignment(assignment, default_assignment):
        if text.count(default_assignment) != 1 or owned_assignment in text:
            raise ValueError("rendererPort default assignment is ambiguous")
        text = text.replace(default_assignment, owned_assignment, 1)
        mode = "rewritten-default"
    elif exact_assignment(assignment, owned_assignment):
        if text.count(owned_assignment) != 1:
            raise ValueError("owned rendererPort assignment is ambiguous")
        mode = "verified-owned"
    else:
        raise ValueError(f"scene targets an unowned or malformed rendererPort assignment: {assignment!r}")
    final_lines = [line for line in text.splitlines() if line.startswith("var rendererPort")]
    if (len(final_lines) != 1 or not exact_assignment(final_lines[0], owned_assignment) or
            default_assignment in text):
        raise ValueError("rendererPort rewrite did not produce one verified owned endpoint")
    return text, mode


def run_sclang(sclang: Path, scene: Path, output: Path, renderer_port: int, label: str,
               visuals_quark: Path | None = None, *, ordinary: bool = False,
               ordinary_marker: str = "VOSC_POSITION_SEND_COMPLETE",
               prepare_endpoint=prepare_scene_endpoint) -> dict:
    text, endpoint_mode = prepare_endpoint(scene.read_text(), renderer_port)
    quark = quark_checkout_path(visuals_quark)
    before = quark_source_identity(quark)
    scene.write_text(text)
    owned = output / f"sclang-owned-{label}"
    config = quark_class_config(owned, quark)
    runner = owned / "runner.scd"
    runner.write_text("(\n"
        "SystemClock.sched(18.0, { 0.exit; nil });\n"
        f"thisProcess.interpreter.executeFile(\"{sc_escape(str(scene.resolve()))}\");\n"
        ")\n")
    language_port = free_udp_port()
    log = output / f"sclang-{label}.log"
    environment = {**os.environ, "XDG_CONFIG_HOME": str(owned / "config"),
                   "XDG_DATA_HOME": str(owned / "data")}
    command = [str(sclang), "-a", "-l", str(config), "-D", "-u", str(language_port),
               "-i", "none", str(runner)]
    with log.open("wb") as stream, subprocess.Popen(
        command, env=environment, stdout=stream, stderr=subprocess.STDOUT
    ) as process:
        identity = subprocess.check_output(
            ["ps", "-p", str(process.pid), "-o", "pid=,lstart=,command="], text=True
        ).strip()
        try:
            return_code = process.wait(timeout=45)
        except subprocess.TimeoutExpired as error:
            process.kill()
            process.wait()
            raise RuntimeError("owned sclang exceeded 45 second parent timeout") from error
        process_receipt = {"pid": process.pid, "process_identity": identity, "return_code": return_code,
                           "cleanup": "reaped"}
    content = log.read_text(errors="replace")
    terminal = re.findall(r"^VOSC_SCENE_RESULT (applied|failed) frame=(-?\d+) detail=(.*)$", content, re.MULTILINE)
    if ordinary:
        if return_code != 0 or content.splitlines().count(ordinary_marker) != 1:
            raise RuntimeError(f"ordinary position sender did not complete; see {log}")
    elif (return_code != 0 or len(terminal) != 1 or terminal[0][0] != "applied" or
          int(terminal[0][1]) < 0 or "VOSC_SCENE_RESULT unknown" in content):
        raise RuntimeError(f"generated scene did not terminate exactly applied; see {log}")
    after = quark_source_identity(quark)
    if before != after:
        raise RuntimeError("VisualsQuark class sources changed while the scene was executing")
    if ordinary:
        return {"command": command, **process_receipt, "log": str(log)}
    return {"command": command, **process_receipt,
            "language_port": language_port, "renderer_port": renderer_port,
            "endpoint_mode": endpoint_mode, "terminal": terminal[0], "log": str(log),
            "visuals_quark": before, "quark_sources_stable": True}


def edit_export(folder: Path, edited_png: bytes) -> None:
    scene = folder / "scene.scd"
    text = scene.read_text()
    pattern = re.compile(r"(visuals\.send\('/layer/alpha', 1, )[-+0-9.eE]+(\);)")
    text, count = pattern.subn(r"\g<1>0.63\2", text)
    if count != 1:
        raise RuntimeError("expected editable alpha command not found")
    expression_pattern = re.compile(
        r"(visuals\.send\('/shader/var', 0, \"gain\", )\"in_0\"(\);)")
    text, expression_count = expression_pattern.subn(
        r'\g<1>"in_0*0.75+0.1"\2', text)
    if expression_count != 1:
        raise RuntimeError(f"expected exactly one editable gain expression, found {expression_count}")
    scene.write_text(text + "// edited acceptance control\n")
    asset = folder / "assets/layer-0-imageTex.png"
    if not asset.is_file():
        raise RuntimeError("exported named image asset missing")
    asset.write_bytes(edited_png)


def swap_layer_order(source: Path, destination: Path) -> Path:
    shutil.copytree(source, destination)
    scene = destination / "scene.scd"
    text = scene.read_text()
    pattern = re.compile(r"(visuals\.send\('/(?:layer|shader|tex)[^']*', )(0|1)(,)")
    swapped, count = pattern.subn(lambda match: match.group(1) +
        ("1" if match.group(2) == "0" else "0") + match.group(3), text)
    if count < 10:
        raise RuntimeError(f"order-only layer remap touched too few commands: {count}")
    scene.write_text(swapped)
    return scene


def source_snapshot(visuals_quark: Path | None = None) -> dict:
    digest = hashlib.sha256()
    count = 0
    roots = ["src", "mcp", "scripts", "tests", "SuperCollider", "examples"]
    for root_name in roots:
        for path in sorted((ROOT / root_name).rglob("*")):
            # Finder view metadata is not an executable/fixture input. Preserve it,
            # but exclude its incidental updates from the source fingerprint.
            if (not path.is_file() or path.name == ".DS_Store" or "__pycache__" in path.parts or
                    path.suffix in (".pyc", ".pyo")):
                continue
            relative = path.relative_to(ROOT).as_posix().encode()
            data = path.read_bytes()
            digest.update(struct.pack(">I", len(relative)))
            digest.update(relative)
            digest.update(struct.pack(">Q", len(data)))
            digest.update(data)
            count += 1
    result = {"sha256": digest.hexdigest(), "file_count": count, "roots": roots}
    if visuals_quark is not None:
        result["visuals_quark"] = quark_source_identity(visuals_quark)
    return result


def run_native_matrix(renderer: OwnedRenderer, output: Path, include_expressions: bool = True) -> dict:
    evidence: dict = {"layer_counts": {}, "replay_cases": {},
                      "expression_cases_included": include_expressions}
    client = renderer.client()
    try:
        for count in (0, 1, 32):
            applied(client, "configure_layers", {"count": count, "layout": "stack"})
            reply = client.call("export_scene", {})
            if not reply.get("ok"):
                raise RuntimeError(f"export failed for {count} layers: {reply}")
            recipe = validate_descriptor(reply["result"]["recipe"])
            reset = recipe["commands"][0]
            if reset["args"][0]["value"] != count:
                raise RuntimeError(f"export count mismatch for {count} layers")
            evidence["layer_counts"][str(count)] = {"commands": len(recipe["commands"]),
                "assets": len(recipe["assets"]), "frame": reply["frame"]}
    finally:
        client.close()

    target = ("127.0.0.1", renderer.port)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as listener:
        listener.bind(("127.0.0.1", 0))
        reply_port = listener.getsockname()[1]

        token_a, token_b = "A" * 24, "B" * 24
        messages = [encode_message("/scene/reset", (token_a, reply_port, 800, 600)),
                    encode_message("/scene/reset", (token_b, reply_port, 800, 600)),
                    encode_message("/scene/replay/end", (token_b, "wrong-port-type", 0, 0)),
                    encode_message("/scene/command", (token_a, 1, "/layers/reset", 0, "stack")),
                    encode_message("/scene/replay/end", (token_a, reply_port, 1, 0))]
        listener.sendto(osc_bundle(messages), target)
        overlap = receive_results(listener, 2)
        by_token = {result["token"]: result for result in overlap}
        if by_token[token_a]["status"] != "applied" or by_token[token_b]["status"] != "failed":
            raise RuntimeError(f"overlap/refusal semantics failed: {overlap}")
        evidence["replay_cases"]["overlap_busy_malformed_foreign"] = overlap

        mismatch = "C" * 24
        listener.sendto(osc_bundle(recipe_messages(mismatch, reply_port,
            [("/layers/reset", (0, "stack"))], expected=2)), target)
        mismatch_result = receive_results(listener, 1)[0]
        if mismatch_result["status"] != "failed" or "count" not in mismatch_result["detail"]:
            raise RuntimeError(f"count mismatch was not truthful: {mismatch_result}")
        evidence["replay_cases"]["count_mismatch"] = mismatch_result

        gap = "D" * 24
        gap_messages = [encode_message("/scene/reset", (gap, reply_port, 800, 600)),
                        encode_message("/scene/command", (gap, 2, "/layers/reset", 0, "stack"))]
        listener.sendto(osc_bundle(gap_messages), target)
        gap_result = receive_results(listener, 1)[0]
        if gap_result["status"] != "failed": raise RuntimeError(f"sequence gap applied: {gap_result}")
        evidence["replay_cases"]["sequence_gap"] = gap_result

        foreign = "G" * 24
        foreign_messages = recipe_messages(foreign, reply_port, [("/layers/reset", (0, "stack"))])
        foreign_messages.append(encode_message("/onset", (0,)))
        listener.sendto(osc_bundle(foreign_messages), target)
        foreign_result = receive_results(listener, 1)[0]
        if foreign_result["status"] != "failed":
            raise RuntimeError(f"foreign bare OSC did not supersede replay: {foreign_result}")
        evidence["replay_cases"]["foreign_osc_before_draw"] = foreign_result

        if renderer.hooks_enabled:
            hook_cases = [
                ("foreign_ui_change", {"operation": "external_ui_change"}),
                ("foreign_resize", {"operation": "native_resize", "width": 640, "height": 480}),
                ("foreign_api_admission", {"operation": "dispatch_control", "request": {
                    "id": "queued-api", "method": "configure_layers",
                    "params": {"count": 1, "layout": "stack", "timing": "onset"}}}),
            ]
            for case_index, (case_name, hook) in enumerate(hook_cases):
                token = chr(ord("J") + case_index) * 24
                hook.update({"id": case_name, "require_scene_replay_ended": True})
                previous_events = arm_recovery_hook(renderer, hook)
                listener.sendto(osc_bundle(recipe_messages(
                    token, reply_port, [("/layers/reset", (0, "stack"))])), target)
                result = receive_results(listener, 1)[0]
                event = wait_recovery_hook(renderer, hook, previous_events)
                if result["status"] != "failed":
                    raise RuntimeError(f"{case_name} did not supersede replay: {result}, {event}")
                evidence["replay_cases"][case_name] = {"result": result, "hook_event": event}
                if case_name == "foreign_resize":
                    evidence["replay_cases"][case_name]["restored"] = resize_scene(renderer.port, 800, 600)

        client = renderer.client()
        try:
            epoch_before = client.call("get_scene", {})["external_epoch"]
        finally:
            client.close()
        sample = "E" * 24
        sample_commands = [("/layers/reset", (0, "stack")), ("/input", (0, "", 0, 256)),
                           ("/input/set", (0, "maxAmp", 1.0))]
        if include_expressions:
            sample_commands.extend((("/var", ("base", 0.25)), ("/var", ("mod", 0.0)),
                                    ("/var", ("mod", "base+in_0"))))
        sample_messages = recipe_messages(sample, reply_port, sample_commands)
        sample_messages.append(encode_message("/input/data", ("0", 0.33, 0.0, 0)))
        listener.sendto(osc_bundle(sample_messages), target)
        sample_result = receive_results(listener, 1)[0]
        client = renderer.client()
        try:
            epoch_after = client.call("get_scene", {})["external_epoch"]
            first_mapping_export = client.call("export_scene", {})["result"]["recipe"]
            second_epoch_before = epoch_after
            osc(renderer.port, "/input/data", "0", 0.77, 0.0, 0)
            deadline = time.monotonic() + 5
            while True:
                second_scene = client.call("get_scene", {})
                if second_scene["external_epoch"] > second_epoch_before:
                    break
                if time.monotonic() >= deadline:
                    raise RuntimeError("second input sample was not observed before export")
                time.sleep(0.02)
            second_mapping_export = client.call("export_scene", {})["result"]["recipe"]
        finally:
            client.close()
        if (sample_result["status"] != "applied" or epoch_after <= epoch_before or
                normalize_descriptor(first_mapping_export) != normalize_descriptor(second_mapping_export)):
            raise RuntimeError(f"input sample epoch/replay semantics failed: {sample_result}, {epoch_before}->{epoch_after}")
        input_commands = [command for command in first_mapping_export["commands"]
                          if command["address"] == "/input"]
        if (len(input_commands) != 1 or input_commands[0]["args"][0] != {"type": "i", "value": 0}):
            raise RuntimeError(f"string-equivalent sample rewrote configured integer input ID: {input_commands}")
        shared = [command for command in first_mapping_export["commands"] if command["address"] == "/var"]
        if include_expressions and not any(
                command["args"][0]["value"] == "mod" and
                command["args"][1] == {"type": "f", "value": 0.0} for command in shared):
            raise RuntimeError(f"shared expression bootstrap is not deterministic: {shared}")
        evidence["replay_cases"]["pending_input_sample"] = {
            "result": sample_result, "external_epoch_before": epoch_before, "external_epoch_after": epoch_after,
            "second_external_epoch": second_scene["external_epoch"], "sample_message_position": "same_bundle_after_end",
            "shared_commands": shared, "configured_input": input_commands[0],
            "descriptor_stable_after_second_sample": True}

        unknown = "U" * 24
        unknown_messages = recipe_messages(unknown, reply_port, [("/layers/reset", (0, "stack"))])
        unknown_messages.append(encode_message("/input/data", ("undeclaredInput", 0.91, 0.0, 0)))
        listener.sendto(osc_bundle(unknown_messages), target)
        unknown_result = receive_results(listener, 1)[0]
        client = renderer.client()
        try:
            unknown_export = client.call("export_scene", {})
        finally:
            client.close()
        if unknown_result["status"] != "applied" or not unknown_export.get("ok"):
            raise RuntimeError(f"unknown input sample falsely changed replay outcome: {unknown_result}, {unknown_export}")
        unknown_inputs = [command for command in unknown_export["result"]["recipe"]["commands"]
                          if command["address"] in ("/input", "/input/set")]
        if unknown_inputs:
            raise RuntimeError(f"unknown input sample created retained configuration: {unknown_inputs}")
        osc(renderer.port, "/input/data", "anotherUndeclaredInput", 0.42, 0.0, 0)
        client = renderer.client()
        try:
            direct_unknown_export = client.call("export_scene", {})
        finally:
            client.close()
        if (not direct_unknown_export.get("ok") or normalize_descriptor(
                direct_unknown_export["result"]["recipe"]) != normalize_descriptor(
                    unknown_export["result"]["recipe"])):
            raise RuntimeError(f"ordinary unknown input sample mutated retained configuration: {direct_unknown_export}")
        evidence["replay_cases"]["unknown_input_sample_after_end_before_draw"] = {
            "result": unknown_result, "retained_input_commands": unknown_inputs,
            "sample_message_position": "same_bundle_after_end",
            "descriptor_stable_after_ordinary_unknown_sample": True}

        configured = "C" * 24
        configured_messages = recipe_messages(configured, reply_port, [("/layers/reset", (0, "stack"))])
        configured_messages.append(encode_message("/input", (7, "", 0, 256)))
        listener.sendto(osc_bundle(configured_messages), target)
        configured_result = receive_results(listener, 1)[0]
        client = renderer.client()
        try:
            configured_export = client.call("export_scene", {})
        finally:
            client.close()
        configured_inputs = [] if not configured_export.get("ok") else [
            command for command in configured_export["result"]["recipe"]["commands"]
            if command["address"] == "/input"]
        if (configured_result["status"] != "failed" or len(configured_inputs) != 1 or
                configured_inputs[0]["args"][0] != {"type": "i", "value": 7}):
            raise RuntimeError(f"explicit input configuration did not supersede replay and survive: {configured_result}, {configured_export}")
        evidence["replay_cases"]["input_configuration_after_end_before_draw"] = {
            "result": configured_result, "retained_input": configured_inputs[0]}

        if include_expressions:
            repeated_cases = [
                ("scalar", "/layer/alpha", (0, "1/total", 2)),
                ("vector", "/layer/pos", (0, '["i/total","i/total","i/total"]', 2)),
                ("color", "/layer/tint", (0, '["i/total","i/total","i/total","i/total"]', 2)),
            ]
            rejected = {}
            for label, address, arguments in repeated_cases:
                client = renderer.client()
                try:
                    applied(client, "configure_layers", {"count": 1, "layout": "stack"})
                finally:
                    client.close()
                osc(renderer.port, address, *arguments)
                client = renderer.client()
                try:
                    response = client.call("export_scene", {})
                finally:
                    client.close()
                if response.get("ok") or response.get("error", {}).get("code") != "scene_unsupported":
                    raise RuntimeError(f"repeated {label} expression was not rejected at export: {response}")
                rejected[label] = response["error"]
            client = renderer.client()
            try:
                applied(client, "configure_layers", {"count": 1, "layout": "stack"})
            finally:
                client.close()
            osc(renderer.port, "/layer/alpha", 0, "i/total")
            client = renderer.client()
            try:
                canonical = client.call("export_scene", {})
            finally:
                client.close()
            canonical_alpha = [] if not canonical.get("ok") else [
                command for command in canonical["result"]["recipe"]["commands"]
                if command["address"] == "/layer/alpha"]
            expected_alpha = [{"type": "i", "value": 0}, {"type": "s", "value": "i/total"}]
            if len(canonical_alpha) != 1 or canonical_alpha[0]["args"] != expected_alpha:
                raise RuntimeError(f"canonical single i/total expression did not export: {canonical}")
            evidence["replay_cases"]["expression_repetition_export_policy"] = {
                "rejected": rejected, "canonical_binding": canonical_alpha[0]}

        speed_image = output / "speed-shape.png"
        speed_image.write_bytes(png_rgba(1, 1, (255, 255, 255, 255), (255, 255, 255, 255)))
        speed_results = {}
        for named in (False, True):
            client = renderer.client()
            try:
                applied(client, "configure_layers", {"count": 1, "layout": "stack"})
                if named:
                    applied(client, "publish_shader", {"layer": 0, "source": LAYER0_SHADER})
            finally:
                client.close()
            if named:
                osc(renderer.port, "/shader/texture", 0, "imageTex", str(speed_image))
                osc(renderer.port, "/shader/texture/speed", 0, "imageTex", 0.5, 2)
            else:
                osc(renderer.port, "/tex", 0, str(speed_image))
                osc(renderer.port, "/tex/speed", 0, 0.5, 2)
            client = renderer.client()
            try:
                response = client.call("export_scene", {})
            finally:
                client.close()
            if (response.get("ok") or response.get("error", {}).get("code") != "scene_unsupported" or
                    "noncanonical float variable shape" not in response["error"]["message"]):
                raise RuntimeError(f"texture speed repetition was not rejected: named={named}: {response}")
            speed_results["named" if named else "default"] = response["error"]
        evidence["replay_cases"]["texture_speed_repetition"] = speed_results

        client = renderer.client()
        try:
            applied(client, "configure_layers", {"count": 1, "layout": "stack"})
            applied(client, "publish_shader", {"layer": 0, "source": LAYER1_SHADER})
            applied(client, "set_uniform", {"layer": 0, "name": "wideValue", "value": [2.0, 3.0, 4.0, 5.0]})
            applied(client, "publish_shader", {"layer": 0, "source":
                "#version 330\nout vec4 fragColor; void main(){ fragColor=vec4(0.5); }\n"})
            unreflected = client.call("export_scene", {})
        finally:
            client.close()
        if (unreflected.get("ok") or unreflected.get("error", {}).get("code") != "scene_unsupported" or
                "shader binding is absent" not in unreflected["error"]["message"]):
            raise RuntimeError(f"unreflected retained vec4 binding was not rejected: {unreflected}")
        evidence["replay_cases"]["unreflected_retained_uniform"] = unreflected["error"]

        contextual_image = output / "contextual-name.png"
        contextual_image.write_bytes(png_rgba(1, 1, (255, 255, 255, 255), (255, 255, 255, 255)))
        contextual_results = []
        uniform_types = {"camPos": "vec3", "camLook": "vec3", "camOrbit": "float"}
        for fixture in CONTEXTUAL_IDENTIFIER_FIXTURES:
            resize_scene(renderer.port, 800, 600)
            context, name = fixture["context"], fixture["name"]
            client = renderer.client()
            try:
                applied(client, "configure_layers", {"count": 1, "layout": "stack"})
                if context == "uniform":
                    glsl_type = uniform_types.get(name, "float")
                    color = f"vec4({name}, 1.0)" if glsl_type == "vec3" else f"vec4({name})"
                    source = ("#version 330\n" f"uniform {glsl_type} {name};\n"
                              f"out vec4 fragColor; void main(){{ fragColor={color}; }}\n")
                    applied(client, "publish_shader", {"layer": 0, "source": source})
                elif context == "sampler":
                    source = ("#version 330\n" f"uniform sampler2D {name};\n"
                              "out vec4 fragColor; void main(){ fragColor=texture("
                              f"{name}, vec2(0.5)); }}\n")
                    applied(client, "publish_shader", {"layer": 0, "source": source})
            finally:
                client.close()
            if context == "input":
                osc(renderer.port, "/input", name, "", 0, 256)
            elif context == "uniform":
                value = 1.0 if uniform_types.get(name, "float") == "float" else "[1.0,2.0,3.0]"
                osc(renderer.port, "/shader/var", 0, name, value)
            elif context == "sampler":
                osc(renderer.port, "/shader/texture", 0, name, str(contextual_image))
            else:
                raise RuntimeError(f"unknown contextual identifier fixture: {fixture}")
            client = renderer.client()
            try:
                response = client.call("export_scene", {})
            finally:
                client.close()
            if fixture["accepted"]:
                if not response.get("ok"):
                    raise RuntimeError(f"ordinary {context} identifier rejected natively: {name}: {response}")
                validate_descriptor(response["result"]["recipe"])
            elif response.get("ok") or response.get("error", {}).get("code") != "scene_unsupported":
                raise RuntimeError(f"reserved {context} identifier was not rejected natively: {name}: {response}")
            if not fixture["accepted"]:
                rejected_address = {"input": "/input", "uniform": "/shader/var",
                                    "sampler": "/shader/texture"}[context]
                if not response["error"]["message"].endswith("rejected " + rejected_address):
                    raise RuntimeError(f"contextual fixture rejected another command: {fixture}: {response}")
            contextual_results.append({**fixture, "native_status": response.get("status"),
                                       "error": response.get("error")})
        evidence["replay_cases"]["contextual_identifier_policy"] = contextual_results

        integer_shader = output / "matrix-ivec.frag"
        integer_shader.write_text(INT_VECTOR_SHADER)
        integer_token = "I" * 24
        integer_commands = [("/layers/reset", (1, "stack")),
                            ("/shader", (0, str(integer_shader))),
                            ("/shader/var", (0, "pairValue", 16777217, 2147483647, "1+2"))]
        listener.sendto(osc_bundle(recipe_messages(integer_token, reply_port, integer_commands)), target)
        integer_result = receive_results(listener, 1)[0]
        client = renderer.client()
        try:
            integer_export = client.call("export_scene", {})
            width, height, pixels, _ = capture(client, output / "typed-integer-vector.png")
        finally:
            client.close()
        if integer_result["status"] != "applied" or not integer_export.get("ok"):
            raise RuntimeError(f"typed integer vector replay/export failed: {integer_result}, {integer_export}")
        integer_bindings = [command for command in integer_export["result"]["recipe"]["commands"]
                            if command["address"] == "/shader/var" and
                            command["args"][1]["value"] == "pairValue"]
        expected_args = [{"type": "i", "value": 0}, {"type": "s", "value": "pairValue"},
                         {"type": "i", "value": 16777217}, {"type": "i", "value": 2147483647},
                         {"type": "s", "value": "1+2"}]
        if len(integer_bindings) != 1 or integer_bindings[0]["args"] != expected_args:
            raise RuntimeError(f"typed integer binding did not round trip: {integer_bindings}")
        center = pixels[(height // 2) * width + width // 2]
        if not (center[1] > 240 and center[0] < 10 and center[3] > 240):
            raise RuntimeError(f"typed integer binding did not preserve exact int32 constants: {center}")
        evidence["replay_cases"]["typed_integer_vector_expression"] = {
            "result": integer_result, "binding": integer_bindings[0], "center_rgba": center}

        mixed_shader = output / "matrix-mixed-vectors.frag"
        mixed_shader.write_text(MIXED_VECTOR_SHADER)
        mixed_token = "M" * 24
        mixed_commands = [("/layers/reset", (1, "stack")),
                          ("/shader", (0, str(mixed_shader))),
                          ("/shader/var", (0, "pairValue", 2.0, "1/total")),
                          ("/shader/var", (0, "tripleValue", 1e-12, "2+1", 5.0)),
                          ("/layer/pos", (0, 100.0, "25+25", 0.0)),
                          ("/layer/rot", (0, 0.0, "0", 0.0)),
                          ("/layer/scale", (0, 1.0, "1", 1.0)),
                          ("/layer/pivot", (0, 0.0, "0", 0.0)),
                          ("/layer/size", (0, 800.0, "600", 0.0)),
                          ("/layer/tint", (0, 1.0, "1", 1.0, "1"))]
        listener.sendto(osc_bundle(recipe_messages(mixed_token, reply_port, mixed_commands)), target)
        mixed_result = receive_results(listener, 1)[0]
        client = renderer.client()
        try:
            mixed_export = client.call("export_scene", {})
            width, height, pixels, _ = capture(client, output / "typed-mixed-vectors.png")
        finally:
            client.close()
        mixed_commands = [] if not mixed_export.get("ok") else mixed_export["result"]["recipe"]["commands"]
        mixed_bindings = {command["args"][1]["value"]: command for command in mixed_commands
                          if command["address"] == "/shader/var"}
        mixed_layers = {command["address"]: command for command in mixed_commands
                        if command["address"] in {"/layer/pos", "/layer/rot", "/layer/scale",
                                                   "/layer/pivot", "/layer/size", "/layer/tint"}}
        float32_tiny = struct.unpack("!f", struct.pack("!f", 1e-12))[0]
        expected_pair = [{"type": "i", "value": 0}, {"type": "s", "value": "pairValue"},
                         {"type": "f", "value": 2.0}, {"type": "s", "value": "1/total"}]
        expected_triple = [{"type": "i", "value": 0}, {"type": "s", "value": "tripleValue"},
                           {"type": "f", "value": float32_tiny}, {"type": "s", "value": "2+1"},
                           {"type": "f", "value": 5.0}]
        expected_layers = {
            "/layer/pos": [100.0, "25+25", 0.0],
            "/layer/rot": [0.0, "0", 0.0],
            "/layer/scale": [1.0, "1", 1.0],
            "/layer/pivot": [0.0, "0", 0.0],
            "/layer/size": [800.0, "600", 0.0],
            "/layer/tint": [1.0, "1", 1.0, "1"],
        }
        expected_layers = {address: [{"type": "i", "value": 0}] + [
            {"type": "s" if isinstance(value, str) else "f", "value": value} for value in values]
            for address, values in expected_layers.items()}
        if (mixed_result["status"] != "applied" or
                mixed_bindings.get("pairValue", {}).get("args") != expected_pair or
                mixed_bindings.get("tripleValue", {}).get("args") != expected_triple or
                {address: command["args"] for address, command in mixed_layers.items()} != expected_layers):
            raise RuntimeError(f"typed mixed vec2/vec3/layer components did not round trip: "
                               f"{mixed_result}, {mixed_export}")
        center = pixels[(height // 2) * width + width // 2]
        uncovered = pixels[10 * width + 10]
        if not (center[1] > 240 and center[0] < 10 and center[3] > 240 and
                max(uncovered[:3]) < 10):
            raise RuntimeError(f"typed mixed vectors/layer position rendered unexpectedly: "
                               f"center={center}, uncovered={uncovered}")
        evidence["replay_cases"]["typed_mixed_vec2_vec3_and_layer"] = {
            "result": mixed_result, "pair": mixed_bindings["pairValue"],
            "triple": mixed_bindings["tripleValue"], "layer_vectors": mixed_layers,
            "center_rgba": center, "uncovered_rgba": uncovered}

        # Keep the CPU descriptor oracle separate from GPU subnormal handling.
        subnormal = struct.unpack("!f", bytes.fromhex("00000001"))[0]
        subnormal_token = "S" * 24
        subnormal_commands = [("/layers/reset", (1, "stack")),
                              ("/shader", (0, str(mixed_shader))),
                              ("/shader/var", (0, "pairValue", subnormal, "1/total"))]
        listener.sendto(osc_bundle(recipe_messages(subnormal_token, reply_port, subnormal_commands)), target)
        subnormal_result = receive_results(listener, 1)[0]
        client = renderer.client()
        try:
            subnormal_export = client.call("export_scene", {})
        finally:
            client.close()
        subnormal_bindings = [] if not subnormal_export.get("ok") else [
            command for command in subnormal_export["result"]["recipe"]["commands"]
            if command["address"] == "/shader/var" and command["args"][1]["value"] == "pairValue"]
        expected_subnormal = [{"type": "i", "value": 0}, {"type": "s", "value": "pairValue"},
                              {"type": "f", "value": subnormal}, {"type": "s", "value": "1/total"}]
        if (subnormal_result["status"] != "applied" or len(subnormal_bindings) != 1 or
                subnormal_bindings[0]["args"] != expected_subnormal):
            raise RuntimeError(f"float32 subnormal did not round trip: {subnormal_result}, {subnormal_export}")
        evidence["replay_cases"]["minimum_float32_subnormal"] = {
            "result": subnormal_result, "binding": subnormal_bindings[0]}

        float_shader = output / "matrix-vec4.frag"
        float_shader.write_text(FLOAT_VECTOR_SHADER)
        float_token = "V" * 24
        float_commands = [("/layers/reset", (1, "stack")),
                          ("/shader", (0, str(float_shader))),
                          ("/shader/var", (0, "wideValue", 2.0, 1e-12, "1/total", 5.0))]
        listener.sendto(osc_bundle(recipe_messages(float_token, reply_port, float_commands)), target)
        float_result = receive_results(listener, 1)[0]
        client = renderer.client()
        try:
            float_export = client.call("export_scene", {})
            width, height, pixels, _ = capture(client, output / "typed-float-vector.png")
        finally:
            client.close()
        float_bindings = [] if not float_export.get("ok") else [
            command for command in float_export["result"]["recipe"]["commands"]
            if command["address"] == "/shader/var" and command["args"][1]["value"] == "wideValue"]
        expected_float_args = [
            {"type": "i", "value": 0}, {"type": "s", "value": "wideValue"},
            {"type": "f", "value": 2.0},
            {"type": "f", "value": struct.unpack("!f", struct.pack("!f", 1e-12))[0]},
            {"type": "s", "value": "1/total"}, {"type": "f", "value": 5.0},
        ]
        if (float_result["status"] != "applied" or len(float_bindings) != 1 or
                float_bindings[0]["args"] != expected_float_args):
            raise RuntimeError(f"typed mixed float vec4 did not round trip: {float_result}, {float_export}")
        center = pixels[(height // 2) * width + width // 2]
        if not (center[1] > 240 and center[0] < 10 and center[3] > 240):
            raise RuntimeError(f"typed mixed float vec4 did not preserve values above one: {center}")
        evidence["replay_cases"]["typed_float_vec4_mixed_expression_above_one"] = {
            "result": float_result, "binding": float_bindings[0], "center_rgba": center}

        good = output / "matrix-good.frag"; bad = output / "matrix-bad.frag"
        good.write_text(LAYER1_SHADER); bad.write_text("#version 330\nout vec4 fragColor; void main(){ fragColor = BROKEN; }\n")
        partial = "F" * 24
        partial_commands = [("/layers/reset", (2, "stack")), ("/shader", (0, str(good))),
                            ("/shader", (1, str(bad)))]
        listener.sendto(osc_bundle(recipe_messages(partial, reply_port, partial_commands)), target)
        partial_result = receive_results(listener, 1)[0]
        if partial_result["status"] != "failed":
            raise RuntimeError(f"invalid second shader falsely applied: {partial_result}")
        client = renderer.client()
        try:
            partial_export = client.call("export_scene", {})
        finally:
            client.close()
        if not partial_export.get("ok"):
            raise RuntimeError(f"partial-state export unavailable after second shader failure: {partial_export}")
        shaders = [command for command in partial_export["result"]["recipe"]["commands"]
                   if command["address"] == "/shader"]
        if len(shaders) != 1 or shaders[0]["args"][0]["value"] != 0:
            raise RuntimeError(f"partial-state shader identity is not truthful: {shaders}")
        evidence["replay_cases"]["invalid_second_shader_partial_state"] = {
            "result": partial_result, "exported_shader_layers": [item["args"][0]["value"] for item in shaders]}
    return evidence


def run_matrix_only(binary: Path, output: Path, sanitizers: list[str], include_expressions: bool,
                    core_library: Path | None) -> int:
    before = source_snapshot()
    evidence = {"status": "FAILED", "source_before": before,
                "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
                "core_library": None}
    if core_library is not None:
        evidence["core_library"] = {"path": str(core_library),
            "sha256": hashlib.sha256(core_library.read_bytes()).hexdigest()}
    renderer = None
    work_error: BaseException | None = None
    cleanup_error: str | None = None
    try:
        renderer = OwnedRenderer(binary, output, "matrix", free_udp_port(), sanitizers)
        evidence["owner"] = renderer.owner
        evidence["matrix"] = run_native_matrix(renderer, output, include_expressions)
    except BaseException as error:
        work_error = error
        evidence["error"] = repr(error)
    finally:
        if renderer is not None:
            evidence["cleanup"], cleanup_error = collect_cleanup(renderer)
            if cleanup_error is not None:
                evidence["cleanup_error"] = cleanup_error
        if work_error is None and cleanup_error is None:
            evidence["status"] = "PASS"
        evidence["source_after"] = source_snapshot()
        evidence["source_stable"] = evidence["source_before"] == evidence["source_after"]
        if not evidence["source_stable"]:
            evidence["status"] = "FAILED"
            evidence["source_error"] = "acceptance sources changed during execution"
            if work_error is None:
                work_error = RuntimeError(evidence["source_error"])
        (output / "matrix-evidence.json").write_text(json.dumps(evidence, indent=2, sort_keys=True) + "\n")
    if work_error is not None:
        raise work_error
    if cleanup_error is not None:
        raise RuntimeError(cleanup_error)
    print(f"PASS native recipe matrix; evidence: {output}")
    return 0


def position_snapshot(client: Client, output: Path, label: str) -> tuple[dict, list]:
    reply = client.call("export_scene", {})
    if reply["ok"]:
        positions = [item["args"] for item in reply["result"]["recipe"]["commands"]
                     if item["address"] == "/layer/pos"]
        state = {"positions": positions}
    else:
        state = {"error": reply["error"]["code"]}
    width, height, pixels, _ = capture(client, output / f"{label}.png")
    marker = [divmod(i, width) for i, (r, g, b, _) in enumerate(pixels)
              if r > 245 and g < 10 and b > 245]
    if not marker:
        raise RuntimeError(f"{label}: expected a visible marker")
    ys, xs = zip(*marker)
    state["marker"] = [min(xs), min(ys), max(xs), max(ys)]
    state["resolution"] = [width, height]
    return state, pixels


def position_settings(components: list) -> list:
    return [{"type": "i", "value": 0}] + [
        {"type": "s", "value": value} if isinstance(value, str) else
        {"type": "f", "value": struct.unpack("!f", struct.pack("!f", value))[0]}
        for value in components
    ]


def run_position_only(args, binary: Path, sclang: Path, output: Path) -> int:
    baseline = args.baseline_binary.resolve()
    if baseline == binary or not baseline.is_file():
        raise ValueError("position acceptance requires a distinct baseline app")
    quark = quark_checkout_path(args.visuals_quark)
    shader = "#version 330\nout vec4 color;\nvoid main(){color=vec4(1.,0.,1.,1.);}\n"
    (output / "assets").mkdir()
    (output / "assets" / "marker.frag").write_text(shader)
    evidence = {"status": "FAILED", "baseline": str(baseline), "candidate": str(binary)}

    def send(receiver, address, *values):
        osc(receiver.port, address, *values)

    def configure(receiver, client, count=1):
        applied(client, "configure_layers", {"count": count, "layout": "stack"})
        send(receiver, "/onset", 0)
        applied(client, "publish_shader", {"layer": 0, "source": shader})
        send(receiver, "/layer/size", 0, "[64.0,48.0,0.0]")

    def preserved_forms(receiver, client, prefix):
        result = {}
        for index, values in enumerate(((26.0,), ("13+13",), ('{"x":26,"y":18,"z":0}',),
                                       (26.0, 41.0), (26.0, 41.0, 53.0),
                                       ('["i/total","(i+1)/total","total+i"]', 2))):
            send(receiver, "/layer/pos", 0, "[0.0,0.0,0.0]")
            send(receiver, "/layer/pos", 0, *values)
            label = f"{prefix}-preserved-{index}"
            result[label.removeprefix(f"{prefix}-")] = position_snapshot(client, output, label)[0]
        send(receiver, "/layer/pos", 0, "[35.0,20.0,0.0]")
        original, _ = position_snapshot(client, output, f"{prefix}-arity-original")
        for index, payload in enumerate(("[4.0,5.0]", "[4.0,5.0,6.0,7.0]")):
            send(receiver, "/layer/pos", 0, payload)
            observed, _ = position_snapshot(client, output, f"{prefix}-arity-{index}")
            if observed != original:
                raise RuntimeError(f"{prefix}: existing vec3 arity changed: {observed}, want {original}")
        result["arity"] = original
        return result

    def language(receiver, label, vector, *, ordinary):
        scene = output / f"{label}.scd"
        if ordinary:
            statements = f"""
visuals.send('/layer/pos', 0, {vector});
"VOSC_POSITION_SEND_COMPLETE".postln;
SystemClock.sched(0.25, {{ 0.exit; nil }});
"""
        else:
            statements = f"""
visuals.replay({{ |scene|
    scene.send('/layers/reset', 1, "stack");
    scene.send('/var', "posOffset", 26.0);
    scene.send('/shader', 0, "assets/marker.frag");
    scene.send('/layer/size', 0, [64.0,48.0,0.0]);
    scene.send('/layer/pos', 0, {vector});
}}, sceneRoot: thisProcess.nowExecutingPath.dirname, width: 800, height: 600,
    finalGate: 0, onComplete: {{ |status|
        SystemClock.sched(0.25, {{ 0.exit; nil }});
    }});
"""
        scene.write_text(f'(\nvar rendererPort = 33333;\nvar visuals = OSCVisuals("127.0.0.1", rendererPort);\n'
                         f'{statements})\n')
        return run_sclang(sclang, scene, output, receiver.port, label, quark, ordinary=ordinary)

    def expect_position(snapshot, components, marker_xy):
        if snapshot.get("positions") != [position_settings(components)]:
            raise RuntimeError(f"incorrect position descriptor: {snapshot}, want {components}")
        x, y = snapshot["marker"][:2]
        if abs(x - marker_xy[0]) > 1 or abs(y - marker_xy[1]) > 1:
            raise RuntimeError(f"incorrect marker location: {snapshot}, want {marker_xy}")

    def positive(receiver, client, ordinary):
        prefix = "ordinary" if ordinary else "replay"
        configure(receiver, client)
        send(receiver, "/var", "posOffset", 26.0)
        cases = {}
        formula = "posOffset+i+total-1"
        for label, vector, components, xy in (
            ("constant", "[24,-12.5,0.0]" if ordinary else "[24.0,-12.5,0.0]",
             [24, -12.5, 0], (24, 0)),
            ("mixed", json.dumps([24.25, formula, 1e-12]), [24.25, formula, 1e-12], (24, 26)),
        ):
            name = f"{prefix}-{label}"
            evidence[name + "-language"] = language(receiver, name, vector, ordinary=ordinary)
            cases[label] = position_snapshot(client, output, name)
            expect_position(cases[label][0], components, xy)

        send(receiver, "/var", "posOffset", 43.0)
        moved, _ = position_snapshot(client, output, f"{prefix}-dependency-update")
        expect_position(moved, [24.25, formula, 1e-12], (24, 43))
        send(receiver, "/layer/pos", 0, "[16,31,0]")
        send(receiver, "/var", "posOffset", 58.0)
        held, _ = position_snapshot(client, output, f"{prefix}-constant-hold")
        expect_position(held, [16, 31, 0], (16, 31))
        send(receiver, "/layer/pos", 0, json.dumps([24.25, formula, 1e-12]))
        send(receiver, "/var", "posOffset", 63.0)
        reattached, _ = position_snapshot(client, output, f"{prefix}-dependency-reattached")
        expect_position(reattached, [24.25, formula, 1e-12], (24, 63))
        evidence[prefix] = {label: snapshot for label, (snapshot, _) in cases.items()}
        evidence[prefix]["transition"] = [moved, held, reattached]
        return cases

    def negative(receiver, client):
        configure(receiver, client, 2)
        send(receiver, "/layer/pos", 0, "[35,20,0]")
        before = position_snapshot(client, output, "invalid-ordinary-before")
        diagnosed = []
        for index, (encoded, reason) in enumerate((
            ("[1,2,", "invalid array syntax"), ("[1,2]", "expected three components"),
            ("[1,2,3,4]", "expected three components"), ("[true,2,3]", "invalid component type"),
            ("[1,null,3]", "invalid component type"), ("[[1],2,3]", "invalid component type"),
            ("[1e39,2,3]", "component outside float32 range"),
            ("[1e400,2,3]", "component outside float32 range"),
            ('[8,"1+",0]', "could not initialize position"),
            (r'[8,"2\u0000+",0]', "could not initialize position"),
            ('[8,"posOffest",0]', "could not initialize position"),
            ('[8,"1+1",0]', "could not initialize position"),
        )):
            if encoded == '[8,"1+1",0]':
                # Separately exercise a setup exception during dependency registration.
                send(receiver, "/var", "_pi", 1.0)
            cursor = client.call("get_diagnostics", {})["result"]["latest_cursor"]
            send(receiver, "/layer/pos", 0, encoded)
            diagnostic = None
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline:
                entries = client.call("get_diagnostics", {"after": cursor})["result"]["entries"]
                diagnostic = next((entry for entry in entries if entry["sequence"] > cursor and
                                   f"/layer/pos rejected: {reason}" in entry["message"]), None)
                if diagnostic is not None:
                    break
                time.sleep(0.03)
            if diagnostic is None or position_snapshot(client, output, f"invalid-ordinary-{index}") != before:
                raise RuntimeError(f"ordinary malformed position was not observably rejected: {encoded}")
            diagnosed.append(diagnostic)
        send(receiver, "/layer/pos", "*", "[28,37,0]")
        wildcard, _ = position_snapshot(client, output, "valid-wildcard")
        first = position_settings([28, 37, 0])
        if wildcard.get("positions") != [first, [{"type": "i", "value": 1}, *first[1:]]]:
            raise RuntimeError(f"wildcard update did not reach both layers: {wildcard}")
        evidence["ordinary_rejections"] = diagnosed
        evidence["wildcard"] = wildcard

        failures = []
        destination = ("127.0.0.1", receiver.port)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as wire:
            wire.bind(("127.0.0.1", 0))
            for malformed in ((4.0, 5.0), (4.0, 5.0, 0.0, 0.0), (4, 5.0, 0.0),
                              (8.0, "1+", 0.0), (8.0, "posOffest", 0.0)):
                token = uuid.uuid4().hex
                commands = [("/layers/reset", (1, "stack")),
                            ("/layer/pos", (0, 35.0, 20.0, 0.0)),
                            ("/layer/pos", (0, *malformed))]
                packets = recipe_messages(token, wire.getsockname()[1], commands)
                wire.sendto(osc_bundle(packets[:-1]), destination)
                result = receive_results(wire, 1, timeout=3)[0]
                exported = client.call("export_scene", {})
                positions = [item["args"] for item in exported["result"]["recipe"]["commands"]
                             if item["address"] == "/layer/pos"]
                if (result["token"], result["status"], result["detail"], positions) != (
                    token, "failed", "invalid, out-of-order, or unroutable setup command",
                    [position_settings([35, 20, 0])],
                ):
                    raise RuntimeError(f"replay did not reject the position command: {result}, {positions}")
                failures.append(result)
        evidence["replay_rejections"] = failures

    def run_owned(app, label, action):
        receiver = OwnedRenderer(app, output, label, free_udp_port(), [])
        evidence[label + "-owner"] = receiver.owner
        try:
            evidence[label + "-resize"] = resize_scene(receiver.port, 800, 600)
            with closing(receiver.client()) as client:
                return action(receiver, client)
        finally:
            evidence[label + "-cleanup"], error = collect_cleanup(receiver)
            if error is not None:
                raise RuntimeError(error)

    def baseline_cases(receiver, client):
        configure(receiver, client)
        return preserved_forms(receiver, client, "baseline")

    def candidate_cases(receiver, client):
        configure(receiver, client)
        candidate = preserved_forms(receiver, client, "candidate")
        if candidate != evidence["baseline_forms"]:
            raise RuntimeError(f"ordinary compatibility differed: {candidate}, {evidence['baseline_forms']}")
        evidence["candidate_forms"] = candidate
        ordinary = positive(receiver, client, True)
        replay = positive(receiver, client, False)
        if ordinary != replay:
            raise RuntimeError("ordinary and replay position exports or RGBA captures differed")
        negative(receiver, client)

    try:
        evidence["baseline_forms"] = run_owned(baseline, "baseline", baseline_cases)
        run_owned(binary, "candidate", candidate_cases)
        evidence["status"] = "PASS"
    finally:
        (output / "position-evidence.json").write_text(json.dumps(evidence, indent=2) + "\n")
    print(f"PASS layer position acceptance; evidence: {output}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--sclang", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--allow-owned-restart", action="store_true")
    parser.add_argument("--expect-sanitizer", action="append", choices=("address", "undefined"), default=[])
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--native-matrix-only", action="store_true")
    modes.add_argument("--layer-position-only", action="store_true")
    modes.add_argument("--shader-ivec-only", action="store_true")
    modes.add_argument("--shader-fvec-only", action="store_true")
    parser.add_argument("--baseline-binary", type=Path)
    parser.add_argument("--without-expression-cases", action="store_true")
    parser.add_argument("--core-library", type=Path)
    parser.add_argument("--visuals-quark", type=Path,
                        help="updated VisualsQuark checkout; alternatively set VISUALS_QUARK_PATH")
    args = parser.parse_args()
    if args.layer_position_only and args.expect_sanitizer:
        parser.error("--layer-position-only does not support --expect-sanitizer")
    if args.shader_ivec_only and args.expect_sanitizer:
        parser.error("--shader-ivec-only does not support --expect-sanitizer")
    if args.shader_fvec_only and args.expect_sanitizer:
        parser.error("--shader-fvec-only does not support --expect-sanitizer")
    if not args.allow_owned_restart: parser.error("--allow-owned-restart is required")
    binary, sclang, output = args.binary.resolve(), args.sclang.resolve(), args.output.resolve()
    if not binary.is_file() or not sclang.is_file(): parser.error("binary and sclang must be existing files")
    if output.exists(): parser.error("--output must be an absent task-owned path")
    output.mkdir(parents=True); output.chmod(0o700)
    core_library = args.core_library.resolve() if args.core_library else None
    if core_library is not None and not core_library.is_file():
        parser.error("--core-library must be an existing file")
    if args.layer_position_only:
        if args.baseline_binary is None:
            parser.error("--layer-position-only requires --baseline-binary")
        return run_position_only(args, binary, sclang, output)
    if args.shader_ivec_only:
        from shader_ivec_acceptance import run_shader_ivec
        return run_shader_ivec(args, binary, sclang, output)
    if args.shader_fvec_only:
        from shader_fvec_acceptance import run_shader_fvec
        return run_shader_fvec(args, binary, sclang, output)
    if args.native_matrix_only:
        return run_matrix_only(binary, output, args.expect_sanitizer,
            not args.without_expression_cases, core_library)
    try:
        visuals_quark = quark_checkout_path(args.visuals_quark)
        quark_probe = probe_visuals_quark(sclang, visuals_quark, output / "quark-probe")
    except (ValueError, RuntimeError, OSError) as error:
        parser.error(str(error))
    alpha_a = png_pixels(png_rgba(1, 1, (10, 20, 30, 40), (10, 20, 30, 40)), include_alpha=True)[2]
    alpha_b = png_pixels(png_rgba(1, 1, (10, 20, 30, 41), (10, 20, 30, 41)), include_alpha=True)[2]
    if alpha_a == alpha_b:
        raise RuntimeError("RGBA oracle discarded an alpha-only difference")
    source_image = output / "source-image.png"
    edited_image = output / "edited-image.png"
    source_image.write_bytes(png_rgba(64, 64, (235, 35, 45, 255), (255, 185, 25, 255)))
    edited_bytes = png_rgba(64, 64, (25, 215, 95, 255), (35, 95, 245, 255))
    edited_image.write_bytes(edited_bytes)
    evidence = {"status": "FAILED", "subcases": {}, "source_before": source_snapshot(visuals_quark),
                "visuals_quark_probe": quark_probe, "visuals_quark": quark_source_identity(visuals_quark), "hashes": {
        "scene_recipe.py": hashlib.sha256((ROOT / "scripts/scene_recipe.py").read_bytes()).hexdigest(),
        "test_scene_recipe.py": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "scene_recipe_schema.py": hashlib.sha256((ROOT / "mcp/scene_recipe_schema.py").read_bytes()).hexdigest(),
    }}
    first = second = None
    cleanup_errors: list[str] = []
    primary_error: BaseException | None = None
    success = False
    try:
        port = free_udp_port()
        first = OwnedRenderer(binary, output, "producer", port, args.expect_sanitizer)
        evidence["producer"] = first.owner
        client = first.client()
        try:
            fresh_before = client.call("get_scene", {})
            for target in ("unknownFreshInput", 7123, "*"):
                osc(port, "/input/data", target, 0.5, 0.0, 0)
            fresh_after = client.call("get_scene", {})
            if (fresh_before["result"]["onset_gate"] != fresh_after["result"]["onset_gate"] or
                    fresh_before["external_epoch"] != fresh_after["external_epoch"]):
                raise RuntimeError(f"unknown fresh samples changed gate or epoch: {fresh_before}, {fresh_after}")
            osc(port, "/input", "knownFreshInput", "", 0, 256)
            osc(port, "/input/data", "knownFreshInput", 0.5, 0.0, 0)
            known_after = client.call("get_scene", {})
            if not known_after["result"]["onset_gate"] or known_after["external_epoch"] <= fresh_after["external_epoch"]:
                raise RuntimeError(f"configured fresh sample did not enable its gate: {known_after}")
            evidence["fresh_sample_policy"] = {"unknown_before": fresh_before, "unknown_after": fresh_after,
                                               "configured_after": known_after}
        finally:
            client.close()
        evidence["producer_resize"] = resize_scene(port, 800, 600)
        original = configure_scene(first, source_image, edited=False)
        exported = output / "Exported scene"
        cli = subprocess.run([sys.executable, str(ROOT / "scripts/scene_recipe.py"), "--socket", str(first.socket),
                              "export", str(exported)], cwd=ROOT, capture_output=True, text=True, timeout=30)
        if cli.returncode != 0:
            raise RuntimeError(f"real CLI export failed: {cli.stdout}\n{cli.stderr}")
        relocated = output / "Relocated scene ü with spaces"
        os.rename(exported, relocated)
        edit_export(relocated, edited_bytes)
        expected = configure_scene(first, edited_image, edited=True)
        client = first.client()
        try:
            expected_rgba = capture(client, output / "expected.png")
        finally:
            client.close()
        evidence["producer_cleanup"], producer_cleanup_error = collect_cleanup(first)
        first = None
        if producer_cleanup_error is not None:
            raise RuntimeError(producer_cleanup_error)

        second = OwnedRenderer(binary, output, "consumer", port, args.expect_sanitizer)
        evidence["consumer"] = second.owner
        sc_result = run_sclang(sclang, relocated / "scene.scd", output, port, "replay", visuals_quark)
        osc(port, "/input/data", 0, 0.62, 0.0, 0)
        time.sleep(0.5)
        client = second.client()
        try:
            reexported = client.call("export_scene", {})
            if not reexported.get("ok"):
                raise RuntimeError(f"post-replay export failed: {reexported}")
            actual = reexported["result"]["recipe"]
            actual_rgba = capture(client, output / "actual.png")
            if normalize_descriptor(actual) != normalize_descriptor(expected):
                (output / "expected-descriptor.json").write_text(json.dumps(expected, indent=2, sort_keys=True) + "\n")
                (output / "actual-descriptor.json").write_text(json.dumps(actual, indent=2, sort_keys=True) + "\n")
                raise RuntimeError("replayed descriptor differs from edited producer descriptor")
            if actual_rgba != expected_rgba:
                raise RuntimeError("replayed static RGBA differs from edited producer")
            for capture_result in (expected_rgba[3], actual_rgba[3]):
                if (capture_result.get("backing_width"), capture_result.get("backing_height")) != (800, 600):
                    raise RuntimeError(f"capture did not come from native 800x600 backing: {capture_result}")
            baseline = actual_rgba[2]
            osc(port, "/layer/visible", 0, 0)
            hidden0 = capture(client, output / "hidden-layer-0.png")[2]
            osc(port, "/layer/visible", 0, 1)
            osc(port, "/layer/visible", 1, 0)
            hidden1 = capture(client, output / "hidden-layer-1.png")[2]
            if hidden0 == baseline or hidden1 == baseline or hidden0 == hidden1:
                raise RuntimeError("hiding either layer did not independently change pixels")
            osc(port, "/layer/visible", 1, 1)
            osc(port, "/input/data", 0, 0.2, 0.0, 0)
            time.sleep(0.2)
            input_low = capture(client, output / "input-low.png")[2]
            osc(port, "/input/data", 0, 0.8, 0.0, 0)
            time.sleep(0.2)
            input_high = capture(client, output / "input-high.png")[2]
            if input_low == input_high:
                raise RuntimeError("two numeric input0 samples did not change pixels")
            osc(port, "/input/data", 0, 0.62, 0.0, 0)
            old_calls = {name: client.call(name, {}) for name in ("save_scene", "validate_scene", "restore_scene")}
        finally:
            client.close()
        if any(item.get("error", {}).get("code") != "unknown_method" for item in old_calls.values()):
            raise RuntimeError(f"retired snapshot API unexpectedly remains: {old_calls}")
        swapped_scene = swap_layer_order(relocated, output / "Swapped order scene")
        swapped_sc = run_sclang(sclang, swapped_scene, output, port, "swapped", visuals_quark)
        osc(port, "/input/data", 0, 0.62, 0.0, 0)
        time.sleep(0.5)
        client = second.client()
        try:
            swapped = capture(client, output / "swapped-order.png")[2]
        finally:
            client.close()
        if swapped == baseline:
            raise RuntimeError("complete order-only layer remap did not change pixels")
        native_matrix = run_native_matrix(second, output, include_expressions=True)
        evidence.update({"status": "PASS", "original_descriptor": original, "edited_descriptor": expected,
                         "reexport": reexported, "retired_api": old_calls, "sclang": sc_result,
                         "swapped_sclang": swapped_sc, "native_matrix": native_matrix,
                         "cli": {"command": cli.args, "stdout": cli.stdout.strip(), "return_code": cli.returncode}})
        evidence["subcases"].update({"cli_export_relocate_edit": "PASS", "descriptor_equality": "PASS",
            "static_rgba_equality": "PASS", "hide_each_layer": "PASS", "swap_order": "PASS",
            "float_vec4_above_one_api_export_sc_replay_reexport_pixel": [2.0, 3.0, 4.0, 5.0],
            "edited_expression_source": "in_0*0.75+0.1",
            "numeric_input0_seed": 0.62, "numeric_input0_values": [0.2, 0.8]})
        success = True
    except BaseException as error:
        primary_error = error
        evidence["status"] = "FAILED"
        evidence["error"] = repr(error)
    finally:
        if first is not None:
            evidence["producer_cleanup"], cleanup_error = collect_cleanup(first)
            if cleanup_error is not None:
                cleanup_errors.append(cleanup_error)
        if second is not None:
            evidence["consumer_cleanup"], cleanup_error = collect_cleanup(second)
            if cleanup_error is not None:
                cleanup_errors.append(cleanup_error)
        if cleanup_errors:
            evidence["status"] = "FAILED"
            evidence["cleanup_errors"] = cleanup_errors
        evidence["source_after"] = source_snapshot(visuals_quark)
        evidence["source_stable"] = evidence["source_before"] == evidence["source_after"]
        if not evidence["source_stable"]:
            evidence["status"] = "FAILED"
            evidence["source_error"] = "acceptance sources changed during execution"
            if primary_error is None:
                primary_error = RuntimeError(evidence["source_error"])
        evidence["binary_sha256"] = hashlib.sha256(binary.read_bytes()).hexdigest()
        if core_library is not None:
            evidence["core_library"] = {"path": str(core_library),
                "sha256": hashlib.sha256(core_library.read_bytes()).hexdigest()}
        (output / "evidence.json").write_text(json.dumps(evidence, indent=2, sort_keys=True) + "\n")
    if primary_error is not None:
        raise primary_error
    if cleanup_errors:
        raise RuntimeError("; ".join(cleanup_errors))
    if not success:
        raise RuntimeError("editable scene acceptance did not reach success")
    print(f"PASS editable two-renderer scene acceptance; evidence: {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
