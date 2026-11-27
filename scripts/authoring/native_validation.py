"""Validate standalone fragments in a disposable local VOSC renderer."""

from __future__ import annotations

import asyncio
import base64
import hashlib
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from threading import Lock
import time
import uuid

# The repository's shared, bounded client also serves scene export.
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "mcp"))
from engine_client import EngineClient, EngineClientError  # noqa: E402


STARTUP_TIMEOUT = 10.0
MAX_SOURCE_BYTES = 128 * 1024
# openFrameworks expands these directives before the GLSL lexer, even when a
# matching physical line sits inside a block comment.
_FILE_INCLUDE = re.compile(r"^[^\S\r\n]*#[^\S\r\n]*(?:include|pragma[^\S\r\n]+include)(?:\s|$)", re.MULTILINE)
_VERTEX_FILES = (
    "shaders/passthru.vert",
    "shaders/include/of_default_uniforms.glsl",
    "shaders/include/of_default_vertex_in_attributes.glsl",
)


def outcome(ok: bool, code: str, message: str, **evidence) -> dict:
    return {"ok": ok, "code": code, "message": message, **evidence}


def stage_validation_assets(binary: Path, data: Path) -> dict:
    if sys.platform == "darwin":
        bundle = binary.parents[2]
        if bundle.suffix != ".app" or binary.parent.name != "MacOS" or binary.parents[1].name != "Contents":
            raise ValueError("validation requires the executable inside a VOSC app bundle")
        data_roots = (bundle / "Contents/Resources/data", bundle.parent / "data")
    else:
        data_roots = (binary.parent / "data",)

    stock = next((root for root in data_roots if all((root / relative).is_file() for relative in _VERTEX_FILES)), None)
    if stock is None:
        raise ValueError("validation renderer has no staged standard vertex shader")
    data.mkdir(mode=0o700)
    hashes = {}
    for relative in _VERTEX_FILES:
        content = (stock / relative).read_bytes()
        target = data / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(content)
        hashes[relative] = hashlib.sha256(content).hexdigest()
    return hashes


class NativeValidator:
    """One local process and one outstanding validation or capture at a time."""

    def __init__(self, binary: Path, *, timeout: float = 1.8):
        if not 0 < timeout < 2:
            raise ValueError("validation timeout must be positive and below the two-second transport deadline")
        self.binary = binary.expanduser().resolve(strict=True)
        self.timeout = timeout
        self.process: subprocess.Popen | None = None
        self.owner: dict | None = None
        self._lock = Lock()
        self._sandbox: tempfile.TemporaryDirectory | None = None
        self._epoch = 0
        self._candidate: dict | None = None

    def __enter__(self):
        if self._sandbox is not None:
            raise RuntimeError("validation context is already open")
        self._sandbox = tempfile.TemporaryDirectory(prefix="vosc-validate-", dir="/tmp")
        self._root = Path(self._sandbox.name)
        try:
            self._data = self._root / "data"
            self._vertex_hashes = stage_validation_assets(self.binary, self._data)
            self._binary_sha256 = hashlib.sha256(self.binary.read_bytes()).hexdigest()
        except BaseException:
            self._sandbox.cleanup()
            self._sandbox = None
            raise
        return self

    def __exit__(self, *_args):
        with self._lock:
            try:
                self._stop()
            finally:
                self._sandbox.cleanup()
                self._sandbox = None

    def _stop(self) -> dict | None:
        self._candidate = None
        process = self.process
        if process is None:
            return None
        forced = False
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=0.5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=2)
                forced = True
        socket = Path(self.owner["control_socket"])
        socket.unlink(missing_ok=True)
        self.process = None
        return {"pid": process.pid, "returncode": process.returncode, "forced": forced,
                "socket_removed": not socket.exists()}

    def _start(self) -> None:
        if self.process is not None and self.process.poll() is None:
            return
        self._stop()
        self._epoch += 1
        runtime = self._root / f"epoch-{self._epoch}"
        runtime.mkdir(mode=0o700)
        socket = runtime / "control.sock"
        log = runtime / "renderer.log"
        environment = {key: value for key, value in os.environ.items() if not key.startswith("VOSC_")}
        environment["VOSC_CONTROL_SOCKET"] = str(socket)
        arguments = [str(self.binary), "--draft-data-root", str(self._data), "--size", "160x120", "--fps", "30"]
        with log.open("wb") as stream:
            self.process = subprocess.Popen(arguments, cwd=self._root, env=environment,
                                            stdout=stream, stderr=subprocess.STDOUT,
                                            stdin=subprocess.DEVNULL, start_new_session=True)
        self.owner = {"pid": self.process.pid, "epoch": self._epoch, "binary_sha256": self._binary_sha256,
                      "arguments": arguments, "control_socket": str(socket), "data_root": str(self._data),
                      "vertex_sha256": self._vertex_hashes, "general_osc": False, "log": str(log)}
        self._client = EngineClient(socket)
        deadline = time.monotonic() + STARTUP_TIMEOUT
        while not socket.is_socket():
            if self.process.poll() is not None:
                with log.open("rb") as stream:
                    diagnostic = stream.read(8192).decode("utf-8", errors="replace")
                raise RuntimeError(f"native validation renderer exited at startup: {diagnostic.strip()}")
            if time.monotonic() >= deadline:
                raise TimeoutError("native validation socket did not appear")
            time.sleep(0.05)
        configured = asyncio.run(asyncio.wait_for(
            self._request("configure_layers", {"count": 1, "layout": "stack"}), timeout=self.timeout))
        if configured.get("status") != "rendered":
            raise RuntimeError(f"could not configure native validation layer: {configured}")

    async def _request(self, method: str, params: dict) -> dict:
        request_id = f"validation-{uuid.uuid4().hex}"
        result = await self._client.submit(request_id, method, params)
        while result.get("ok") is True and result.get("status") in {"queued", "applied"}:
            await asyncio.sleep(0.02)
            poll = await self._client.query("get_request", {"request_id": request_id})
            if poll.get("ok") is not True:
                raise EngineClientError("adapter_receipt_error", f"native receipt lookup failed: {poll}")
            result = poll["result"]
            if result.get("id") != request_id:
                raise EngineClientError("adapter_receipt_error", "native receipt ID changed")
        return result

    @staticmethod
    def _failure(receipt: dict) -> dict:
        error = receipt.get("error", {})
        return outcome(False, error.get("code", "renderer_protocol"),
                       error.get("message", "renderer did not return a terminal drawn receipt"),
                       receipt=receipt)

    async def _validate(self, source: bytes) -> dict:
        async with asyncio.timeout(self.timeout):
            payload = base64.b64encode(source).decode("ascii")
            receipt = await self._request("publish_shader", {"layer": 0, "source_base64": payload})
            if receipt.get("ok") is not True or receipt.get("status") != "rendered":
                return self._failure(receipt)
            snapshot = await self._client.query("get_scene", {})
            try:
                state = snapshot["result"]
                layer, = state["layers"]
                shader = layer["shader"]
                if (state["rendered_revision"] != receipt["revision"]
                        or state["external_epoch"] != receipt["external_epoch"]
                        or not shader["loaded"] or shader["program"] <= 0 or shader["path"] != "source"):
                    raise ValueError("drawn receipt does not match the active program")
            except (KeyError, TypeError, ValueError) as error:
                raise EngineClientError("adapter_receipt_error", f"cannot verify native linked program: {error}") from error
            return outcome(True, "native_linked", "Native program linked and drew",
                           receipt=receipt, shader=shader)

    def _interrupted(self, code: str, error: Exception) -> dict:
        return outcome(False, code, str(error) or "native validation deadline elapsed",
                       owner=self.owner, cleanup=self._stop())

    def validate(self, source: bytes) -> dict:
        if not isinstance(source, bytes) or not source or len(source) > MAX_SOURCE_BYTES:
            return outcome(False, "invalid_source", "Expected 1 to 131072 fragment source bytes")
        try:
            text = source.decode("utf-8")
        except UnicodeDecodeError:
            return outcome(False, "invalid_source", "Fragment source must be UTF-8")
        if "\x00" in text:
            return outcome(False, "invalid_source", "Fragment source contains a NUL byte")
        if _FILE_INCLUDE.search(text):
            return outcome(False, "missing_dependency", "Fragment file includes are unsupported")
        if not self._lock.acquire(blocking=False):
            return outcome(False, "validation_busy", "A native validation or capture is already active")
        try:
            if self._sandbox is None:
                raise RuntimeError("open the validation context before submitting a candidate")
            self._candidate = None
            try:
                self._start()
            except (OSError, TimeoutError, RuntimeError, EngineClientError) as error:
                return self._interrupted("renderer_startup", error)
            try:
                result = asyncio.run(self._validate(source))
            except TimeoutError as error:
                return self._interrupted("validation_timeout", error)
            except EngineClientError as error:
                return self._interrupted("renderer_transport", error)
            result.update({"owner": self.owner, "source_sha256": hashlib.sha256(source).hexdigest()})
            if result["ok"]:
                self._candidate = {"receipt": result["receipt"].copy(), "source_sha256": result["source_sha256"]}
            return result
        finally:
            self._lock.release()

    async def _capture(self, candidate: dict) -> dict:
        async with asyncio.timeout(self.timeout):
            revision = candidate["receipt"]["revision"]
            receipt = await self._request("capture_preview", {"max_width": 320, "minimum_revision": revision})
            if receipt.get("ok") is not True or receipt.get("status") != "captured":
                return self._failure(receipt)
            if (receipt["revision"] != revision or receipt["external_epoch"] != candidate["receipt"]["external_epoch"]
                    or receipt["frame"] < candidate["receipt"]["frame"]):
                raise EngineClientError("adapter_receipt_error", "capture does not match the validated candidate")
            return outcome(True, "native_capture", "Captured the validated native program", receipt=receipt)

    def capture(self, path: Path, *, expected_source_sha256: str | None = None) -> dict:
        if not self._lock.acquire(blocking=False):
            return outcome(False, "validation_busy", "A native validation or capture is already active")
        try:
            candidate = self._candidate
            if (candidate is None or self.process is None or self.process.poll() is not None or
                    (expected_source_sha256 is not None and expected_source_sha256 != candidate["source_sha256"])):
                return outcome(False, "no_active_candidate", "No matching validated candidate is drawing")
            try:
                result = asyncio.run(self._capture(candidate))
            except TimeoutError as error:
                return self._interrupted("capture_timeout", error)
            except EngineClientError as error:
                return self._interrupted("renderer_transport", error)
            if result["ok"]:
                data = base64.b64decode(result["receipt"]["result"].pop("data_base64"), validate=True)
                path.write_bytes(data)
                result.update({"path": str(path), "png_sha256": hashlib.sha256(data).hexdigest(),
                               "source_sha256": candidate["source_sha256"], "owner": self.owner})
            return result
        finally:
            self._lock.release()
