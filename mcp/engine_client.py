"""Bounded asynchronous client for the opt-in vosc Unix NDJSON API."""

from __future__ import annotations

import asyncio
import json
import math
import os
from pathlib import Path
import socket
import stat
import time
from typing import Any
import uuid


REQUEST_LIMIT = 262_144
RESPONSE_LIMIT = 8_388_608
MAX_DEPTH = 64
EXCHANGE_TIMEOUT = 2.0
SLOT_TIMEOUT = 0.1
REQUIRED_OPERATIONS = frozenset(
    {
        "get_capabilities",
        "get_scene",
        "get_diagnostics",
        "get_request",
        "configure_layers",
        "publish_shader",
        "set_uniform",
        "set_layer",
        "capture_preview",
    }
)


class EngineClientError(Exception):
    """A classified adapter-side engine exchange failure."""

    def __init__(self, code: str, message: str, *, wrote_mutation: bool = False) -> None:
        super().__init__(message)
        self.code = code
        self.message = message
        self.wrote_mutation = wrote_mutation


def query_id() -> str:
    return f"_q.{uuid.uuid4()}"


def canonical_request(request_id: str, method: str, params: dict[str, Any]) -> bytes:
    data = json.dumps(
        {"id": request_id, "method": method, "params": params},
        allow_nan=False,
        ensure_ascii=False,
        separators=(",", ":"),
        sort_keys=True,
    ).encode("utf-8") + b"\n"
    if len(data) > REQUEST_LIMIT:
        raise EngineClientError("adapter_protocol_error", "serialized engine request exceeds 262144 bytes")
    return data


def _reject_constant(value: str) -> None:
    raise ValueError(f"non-finite JSON number {value}")


def _validate_tree(value: Any, depth: int = 0) -> None:
    if isinstance(value, float) and not math.isfinite(value):
        raise ValueError("non-finite JSON number")
    if isinstance(value, dict):
        depth += 1
        if depth > MAX_DEPTH:
            raise ValueError("JSON nesting exceeds 64")
        for key, child in value.items():
            if not isinstance(key, str):
                raise ValueError("JSON object key is not a string")
            _validate_tree(child, depth)
    elif isinstance(value, list):
        depth += 1
        if depth > MAX_DEPTH:
            raise ValueError("JSON nesting exceeds 64")
        for child in value:
            _validate_tree(child, depth)


def _validate_wire_nesting(line: bytes) -> None:
    """Reject excessive JSON containers before the recursive decoder sees them."""
    quoted = False
    escaped = False
    depth = 0
    for value in line:
        if quoted:
            if escaped:
                escaped = False
            elif value == 0x5C:  # backslash
                escaped = True
            elif value == 0x22:  # quote
                quoted = False
        elif value == 0x22:
            quoted = True
        elif value in (0x7B, 0x5B):  # { [
            depth += 1
            if depth > MAX_DEPTH:
                raise EngineClientError("adapter_protocol_error", "JSON nesting exceeds 64")
        elif value in (0x7D, 0x5D):  # } ]
            depth -= 1


def decode_response(line: bytes, expected_id: str) -> dict[str, Any]:
    if not line or not line.endswith(b"\n"):
        raise EngineClientError("adapter_protocol_error", "engine closed before a complete response line")
    if len(line) > RESPONSE_LIMIT:
        raise EngineClientError("adapter_protocol_error", "engine response exceeds 8388608 bytes")
    _validate_wire_nesting(line)
    try:
        value = json.loads(line, parse_constant=_reject_constant)
        _validate_tree(value)
    except (UnicodeDecodeError, json.JSONDecodeError, ValueError, RecursionError) as error:
        raise EngineClientError("adapter_protocol_error", f"invalid engine JSON response: {error}") from error
    if not isinstance(value, dict):
        raise EngineClientError("adapter_protocol_error", "engine response is not an object")
    if value.get("id") != expected_id:
        raise EngineClientError("adapter_protocol_error", "engine response ID mismatch")
    return value


class EngineClient:
    """Open a fresh bounded connection for each logical exchange."""

    def __init__(self, socket_path: Path) -> None:
        self.socket_path = socket_path
        self._slots = asyncio.Semaphore(4)

    def validate_endpoint(self) -> None:
        path = self.socket_path
        if not path.is_absolute():
            raise EngineClientError("adapter_unsafe_endpoint", "engine socket path must be absolute")
        parent = path.parent
        try:
            parent_info = parent.stat()
        except FileNotFoundError as error:
            raise EngineClientError("adapter_engine_unavailable", "engine socket parent does not exist") from error
        except OSError as error:
            raise EngineClientError(
                "adapter_unsafe_endpoint", f"cannot safely inspect engine socket parent: {error}"
            ) from error
        uid = os.getuid()
        if not stat.S_ISDIR(parent_info.st_mode) or parent_info.st_uid != uid or parent_info.st_mode & 0o077:
            raise EngineClientError(
                "adapter_unsafe_endpoint",
                "engine socket parent must be an owned private directory",
            )
        try:
            info = path.lstat()
        except FileNotFoundError as error:
            raise EngineClientError("adapter_engine_unavailable", "engine socket does not exist") from error
        except OSError as error:
            raise EngineClientError(
                "adapter_unsafe_endpoint", f"cannot safely inspect engine endpoint: {error}"
            ) from error
        if stat.S_ISLNK(info.st_mode) or not stat.S_ISSOCK(info.st_mode):
            raise EngineClientError("adapter_unsafe_endpoint", "engine endpoint must be a Unix socket, not a symlink")
        if info.st_uid != uid or stat.S_IMODE(info.st_mode) != 0o600:
            raise EngineClientError("adapter_unsafe_endpoint", "engine socket must be owned by this user with mode 0600")

    async def query(
        self, method: str, params: dict[str, Any], *, timeout: float = EXCHANGE_TIMEOUT
    ) -> dict[str, Any]:
        request_id = query_id()
        payload = canonical_request(request_id, method, params)
        replies = await self._exchange([(request_id, payload, False)], timeout=timeout)
        return replies[0]

    async def submit(
        self, request_id: str, method: str, params: dict[str, Any]
    ) -> dict[str, Any]:
        mutation = canonical_request(request_id, method, params)
        capability_id = query_id()
        capability = canonical_request(capability_id, "get_capabilities", {})
        replies = await self._exchange(
            [(capability_id, capability, False), (request_id, mutation, True)],
            require_compatible=True,
        )
        return replies[-1]

    async def _exchange(
        self,
        requests: list[tuple[str, bytes, bool]],
        *,
        require_compatible: bool = False,
        timeout: float = EXCHANGE_TIMEOUT,
    ) -> list[dict[str, Any]]:
        wrote_mutation = False
        deadline = time.monotonic() + min(EXCHANGE_TIMEOUT, max(0.0, timeout))

        async def run() -> list[dict[str, Any]]:
            nonlocal wrote_mutation
            self.validate_endpoint()
            try:
                reader, writer = await asyncio.open_unix_connection(
                    str(self.socket_path), limit=RESPONSE_LIMIT + 1
                )
            except (FileNotFoundError, ConnectionRefusedError, OSError) as error:
                raise EngineClientError(
                    "adapter_engine_unavailable", f"cannot connect to engine socket: {error}"
                ) from error
            replies: list[dict[str, Any]] = []
            try:
                for index, (expected_id, payload, is_mutation) in enumerate(requests):
                    if is_mutation:
                        # write() may hand bytes to the transport before drain() fails.
                        wrote_mutation = True
                    try:
                        writer.write(payload)
                        await writer.drain()
                    except asyncio.CancelledError:
                        raise
                    except (ConnectionError, OSError) as error:
                        raise EngineClientError(
                            "adapter_protocol_error",
                            "engine transport failed while writing request",
                            wrote_mutation=wrote_mutation,
                        ) from error
                    try:
                        line = await reader.readuntil(b"\n")
                    except (asyncio.IncompleteReadError, asyncio.LimitOverrunError) as error:
                        raise EngineClientError(
                            "adapter_protocol_error",
                            "engine closed or exceeded response line limit",
                            wrote_mutation=wrote_mutation,
                        ) from error
                    except asyncio.CancelledError:
                        raise
                    except (ConnectionError, OSError) as error:
                        raise EngineClientError(
                            "adapter_protocol_error",
                            "engine transport failed while reading response",
                            wrote_mutation=wrote_mutation,
                        ) from error
                    reply = decode_response(line, expected_id)
                    replies.append(reply)
                    if index == 0 and require_compatible:
                        self._require_compatible(reply)
                return replies
            finally:
                writer.close()
                try:
                    await asyncio.wait_for(writer.wait_closed(), timeout=0.1)
                except asyncio.CancelledError:
                    raise
                except Exception:
                    # Cleanup cannot replace an earlier classified exchange error.
                    pass

        acquired = False
        try:
            await asyncio.wait_for(
                self._slots.acquire(), timeout=min(SLOT_TIMEOUT, max(0.0, deadline - time.monotonic()))
            )
            acquired = True
        except TimeoutError as error:
            if timeout < EXCHANGE_TIMEOUT and time.monotonic() >= deadline:
                raise EngineClientError("adapter_wait_deadline", "local receipt wait expired") from error
            raise EngineClientError("adapter_busy", "all engine exchange slots are busy") from error
        try:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError
            return await asyncio.wait_for(run(), timeout=remaining)
        except TimeoutError as error:
            if timeout < EXCHANGE_TIMEOUT:
                raise EngineClientError(
                    "adapter_wait_deadline",
                    "local receipt wait expired",
                    wrote_mutation=wrote_mutation,
                ) from error
            raise EngineClientError(
                "adapter_protocol_error",
                "engine exchange exceeded 2 seconds",
                wrote_mutation=wrote_mutation,
            ) from error
        except EngineClientError as error:
            if wrote_mutation and not error.wrote_mutation:
                error.wrote_mutation = True
            raise
        finally:
            if acquired:
                self._slots.release()

    @staticmethod
    def _require_compatible(reply: dict[str, Any]) -> None:
        try:
            if type(reply.get("ok")) is not bool or not isinstance(reply.get("status"), str):
                raise ValueError("invalid capability envelope status")
            for field in ("revision", "frame", "external_epoch"):
                value = reply.get(field)
                if type(value) is not int or value < 0 or value > 18_446_744_073_709_551_615:
                    raise ValueError(f"invalid capability {field}")
            if reply["ok"] is not True:
                error = reply.get("error")
                if reply["status"] != "failed" or not isinstance(error, dict):
                    raise ValueError("invalid failed capability envelope")
                if not isinstance(error.get("code"), str) or not isinstance(error.get("message"), str):
                    raise ValueError("invalid capability failure")
                if not isinstance(error.get("diagnostics"), list):
                    raise ValueError("invalid capability failure diagnostics")
                raise EngineClientError(
                    "adapter_incompatible_engine", "engine rejected the capability preflight"
                )
            if reply["status"] != "ok":
                raise ValueError("invalid successful capability status")
            result = reply["result"]
            if not isinstance(result, dict) or type(result.get("version")) is not int:
                raise ValueError("invalid capability result")
            operations = result.get("operations")
            if not isinstance(operations, list) or any(not isinstance(item, str) for item in operations):
                raise ValueError("invalid operations list")
            if len(operations) != len(set(operations)):
                raise ValueError("duplicate operation names")
            if result["version"] != 1 or not REQUIRED_OPERATIONS.issubset(operations):
                raise EngineClientError(
                    "adapter_incompatible_engine",
                    "engine must expose API version 1 and all baseline operations",
                )
            if not isinstance(result.get("limits"), dict) or not isinstance(result.get("features"), dict):
                raise ValueError("missing capability limits or features")
            # Recipe export is optional for otherwise-compatible peers. If it is
            # advertised, its public feature bit and frozen limits are mandatory.
            from scene_recipe_schema import validate_recipe_capabilities
            validate_recipe_capabilities(result)
        except EngineClientError:
            raise
        except (KeyError, TypeError, ValueError) as error:
            raise EngineClientError("adapter_protocol_error", f"invalid capability response: {error}") from error
