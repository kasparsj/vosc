#!/usr/bin/env python3
"""Production stdio MCP adapter for the opt-in local vosc engine API."""

from __future__ import annotations

import argparse
import asyncio
import base64
import binascii
from copy import deepcopy
import json
import math
from pathlib import Path
import re
import struct
import time
from typing import Annotated, Any, Literal

from mcp.server import MCPServer
from mcp.server.mcpserver.exceptions import ToolError
from mcp.server.mcpserver.utilities.func_metadata import ArgModelBase
from mcp.types import CallToolResult, ImageContent, TextContent, ToolAnnotations
from pydantic import BaseModel, ConfigDict, Field, StrictBool, StrictFloat, StrictInt as PStrictInt

from engine_client import EngineClient, EngineClientError, REQUIRED_OPERATIONS
from scene_recipe_schema import validate_export_reply, validate_recipe_capabilities

# The pinned high-level SDK derives each function argument model from this base.
# Set the policy before tool registration so advertised and runtime schemas are
# both closed-world without rebuilding SDK models during process shutdown.
ArgModelBase.model_config["extra"] = "forbid"


UINT64_MAX = 18_446_744_073_709_551_615
PENDING = frozenset({"queued", "applied", "evaluating", "ready", "committing", "rolling_back"})
ID_PATTERN = re.compile(r"^[A-Za-z0-9._:-]+$")

StrictInt = Annotated[int, Field(strict=True)]
RequestId = Annotated[str, Field(strict=True, min_length=1, max_length=64, pattern=r"^[A-Za-z0-9._:-]+$")]
Layer = Annotated[int, Field(strict=True, ge=0, le=31)]
WaitMs = Annotated[int, Field(strict=True, ge=0, le=5000)]
Cursor = Annotated[int, Field(strict=True, ge=0, le=UINT64_MAX)]
BoundedFloat = Annotated[StrictFloat, Field(ge=-1_000_000, le=1_000_000)]
BoundedInt = Annotated[PStrictInt, Field(ge=-1_000_000, le=1_000_000)]
UniformVector = Annotated[list[BoundedFloat | BoundedInt], Field(min_length=2, max_length=4)]
AlphaFloat = Annotated[StrictFloat, Field(ge=0, le=1)]
AlphaInt = Annotated[PStrictInt, Field(ge=0, le=1)]
Operation = Literal[
    "get_capabilities",
    "get_scene",
    "get_performance",
    "get_diagnostics",
    "get_request",
    "export_scene",
    "configure_layers",
    "publish_shader",
    "set_uniform",
    "set_layer",
    "capture_preview",
    "trial_shader",
    "accept_trial",
    "cancel_trial",
]


class AdapterError(BaseModel):
    model_config = ConfigDict(extra="forbid")
    code: str
    message: str


class AdapterResult(BaseModel):
    """Closed-world structured result returned by every normal execution."""

    model_config = ConfigDict(extra="forbid")
    adapter_version: Literal[1]
    operation: Operation
    request_id: str | None
    outcome: Literal["observed", "pending", "completed", "failed", "unknown"]
    delivery: Literal["query", "not_submitted", "submitted", "uncertain"]
    engine: dict[str, Any] | None
    error: AdapterError | None


QUERY_ANNOTATIONS = ToolAnnotations(
    readOnlyHint=True, destructiveHint=False, idempotentHint=True, openWorldHint=False
)
MUTATION_ANNOTATIONS = ToolAnnotations(
    readOnlyHint=False, destructiveHint=False, idempotentHint=False, openWorldHint=False
)
RESET_ANNOTATIONS = ToolAnnotations(
    readOnlyHint=False, destructiveHint=True, idempotentHint=False, openWorldHint=False
)


def _strict_uint(value: Any, label: str, *, maximum: int = UINT64_MAX) -> int:
    if type(value) is not int or value < 0 or value > maximum:
        raise ValueError(f"{label} must be a nonnegative integer")
    return value


def _finite_number(value: Any, label: str) -> float | int:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{label} must be a finite number")
    try:
        finite = math.isfinite(value)
    except (OverflowError, TypeError) as error:
        raise ValueError(f"{label} must be a finite number") from error
    if not finite:
        raise ValueError(f"{label} must be a finite number")
    return value


def _common(reply: dict[str, Any]) -> tuple[bool, str]:
    if not isinstance(reply.get("id"), str):
        raise ValueError("response id must be a string")
    if type(reply.get("ok")) is not bool or not isinstance(reply.get("status"), str):
        raise ValueError("response ok/status fields are invalid")
    for field in ("revision", "frame", "external_epoch"):
        _strict_uint(reply.get(field), field)
    return reply["ok"], reply["status"]


def _failure(reply: dict[str, Any], *, allow_expired: bool) -> None:
    ok, status = _common(reply)
    allowed = {"failed", "expired"} if allow_expired else {"failed"}
    if ok or status not in allowed:
        raise ValueError("invalid failure status combination")
    error = reply.get("error")
    if not isinstance(error, dict) or not isinstance(error.get("code"), str) or not isinstance(
        error.get("message"), str
    ):
        raise ValueError("failure is missing code/message")
    if not isinstance(error.get("diagnostics"), list):
        raise ValueError("failure diagnostics must be an array")


def _validate_capabilities(result: Any) -> None:
    if not isinstance(result, dict) or type(result.get("version")) is not int:
        raise ValueError("invalid capability result")
    operations = result.get("operations")
    if not isinstance(operations, list) or any(not isinstance(item, str) for item in operations):
        raise ValueError("invalid capability operations")
    if len(operations) != len(set(operations)):
        raise ValueError("capability operations are not distinct")
    if not isinstance(result.get("limits"), dict) or not isinstance(result.get("features"), dict):
        raise ValueError("capability objects are missing")
    validate_recipe_capabilities(result)


def _validate_scene(result: Any, outer_frame: int, outer_revision: int) -> None:
    if not isinstance(result, dict) or not isinstance(result.get("layers"), list) or not isinstance(
        result.get("pending"), list
    ):
        raise ValueError("invalid scene collections")
    if not isinstance(result.get("layout"), str):
        raise ValueError("invalid scene layout")
    for field in ("revision", "rendered_revision", "frame", "external_epoch", "legacy_pending", "width", "height"):
        _strict_uint(result.get(field), field)
    if type(result.get("camera_enabled")) is not bool or type(result.get("onset_gate")) is not bool:
        raise ValueError("invalid scene boolean")
    if result.get("trial") is not None:
        _validate_trial(result["trial"], outer_frame=outer_frame, outer_revision=outer_revision)
    _finite_number(result.get("fps"), "fps")
    for layer in result["layers"]:
        if not isinstance(layer, dict) or type(layer.get("index")) is not int:
            raise ValueError("invalid scene layer")
        _finite_number(layer.get("alpha"), "layer alpha")
        if type(layer.get("visible")) is not bool or type(layer.get("geometry_loaded")) is not bool:
            raise ValueError("invalid layer booleans")
        size = layer.get("size")
        if not isinstance(size, list) or len(size) != 2:
            raise ValueError("invalid layer size")
        for item in size:
            _finite_number(item, "layer size")
        shader = layer.get("shader")
        if not isinstance(shader, dict) or type(shader.get("loaded")) is not bool:
            raise ValueError("invalid layer shader")
        if not isinstance(shader.get("path"), str):
            raise ValueError("invalid shader path")
        _strict_uint(shader.get("program"), "shader program")
        if not isinstance(shader.get("uniforms"), dict) or not isinstance(shader.get("textures"), dict):
            raise ValueError("invalid shader inspection objects")


def _validate_diagnostics(result: Any) -> None:
    if not isinstance(result, dict) or not isinstance(result.get("entries"), list):
        raise ValueError("invalid diagnostic result")
    if len(result["entries"]) > 128:
        raise ValueError("diagnostic page exceeds 128 entries")
    for field in ("next_cursor", "latest_cursor"):
        _strict_uint(result.get(field), field)
    if type(result.get("gap")) is not bool:
        raise ValueError("invalid diagnostic gap")
    for entry in result["entries"]:
        if not isinstance(entry, dict):
            raise ValueError("invalid diagnostic entry")
        _strict_uint(entry.get("sequence"), "diagnostic sequence")
        _finite_number(entry.get("time"), "diagnostic time")
        if type(entry.get("level")) is not int or not isinstance(entry.get("module"), str) or not isinstance(
            entry.get("message"), str
        ):
            raise ValueError("invalid diagnostic fields")


def _exact_fields(value: Any, fields: set[str], label: str) -> dict[str, Any]:
    if not isinstance(value, dict) or set(value) != fields:
        raise ValueError(f"{label} has invalid fields")
    return value


def _validate_timing_stats(value: Any, label: str) -> int:
    stats = _exact_fields(value, {"samples", "mean", "p50", "p95", "max"}, label)
    samples = _strict_uint(stats["samples"], f"{label} samples", maximum=120)
    numbers = []
    for field in ("mean", "p50", "p95", "max"):
        item = stats[field]
        if samples == 0:
            if item is not None:
                raise ValueError(f"empty {label} statistics must be null")
        else:
            number = _finite_number(item, f"{label} {field}")
            if number < 0:
                raise ValueError(f"{label} {field} must be nonnegative")
            numbers.append(number)
    if samples and not (numbers[1] <= numbers[2] <= numbers[3] and numbers[0] <= numbers[3]):
        raise ValueError(f"{label} statistics are inconsistent")
    return samples


def _validate_feedback_measurement(value: Any, outer_frame: int) -> None:
    counters = {"id", "configuration", "generation", "program", "owner_revision", "width", "height",
                "attempts", "update_drops", "draw_drops", "crossing_frames", "last_crossing_frame"}
    flags = {"matches_current_context", "supported", "aged_pending"}
    data = _exact_fields(value, counters | flags | {"samples", "attempt_window"}, "feedback measurement")
    for field in counters:
        _strict_uint(data[field], f"feedback measurement {field}")
    if any(type(data[flag]) is not bool for flag in flags):
        raise ValueError("invalid feedback measurement flag")
    for key in ("samples", "attempt_window"):
        if not isinstance(data[key], list) or len(data[key]) > 120:
            raise ValueError("feedback measurement exceeds the retained window")
        if data[key] and not (data["supported"] and data["matches_current_context"]):
            raise ValueError("feedback measurement contains stale samples")
        previous = -1
        for item in data[key]:
            if key == "samples":
                _exact_fields(item, {"frame", "update_ms", "draw_ms", "total_ms"}, "feedback timing pair")
                update, draw, total = (_finite_number(item[field], field)
                                       for field in ("update_ms", "draw_ms", "total_ms"))
                if min(update, draw) < 0 or not math.isclose(total, update + draw):
                    raise ValueError("feedback timing total disagrees with its components")
            else:
                _exact_fields(item, {"frame", "interval_ms", "update_submitted", "draw_submitted"},
                              "feedback timing attempt")
                if item["interval_ms"] is not None and _finite_number(item["interval_ms"], "interval_ms") < 0:
                    raise ValueError("negative feedback frame interval")
                if any(type(item[field]) is not bool for field in ("update_submitted", "draw_submitted")):
                    raise ValueError("invalid feedback submission state")
            frame = _strict_uint(item["frame"], "feedback originating frame")
            if not previous < frame <= outer_frame or frame + 119 < outer_frame:
                raise ValueError("feedback measurement frames are unordered or expired")
            previous = frame


def _validate_performance(result: Any, outer_frame: int, outer_revision: int, outer_epoch: int) -> None:
    measurement = result.get("feedback_measurement") if isinstance(result, dict) else None
    if isinstance(result, dict) and "feedback_measurement" in result:
        result = {key: item for key, item in result.items() if key != "feedback_measurement"}
        _validate_feedback_measurement(measurement, outer_frame)
    value = _exact_fields(result, {
        "version", "scope", "window_frames", "context", "matches_current_context",
        "target_fps", "frame_interval_ms", "cpu_submit_ms", "gpu_ms", "feedback",
    }, "performance result")
    if type(value["version"]) is not int or value["version"] != 1 or value["scope"] != "composed_scene":
        raise ValueError("invalid performance version or scope")
    if type(value["window_frames"]) is not int or value["window_frames"] != 120:
        raise ValueError("invalid performance window")
    context = _exact_fields(value["context"], {"revision", "external_epoch", "width", "height"},
                            "performance context")
    for field in ("revision", "external_epoch", "width", "height"):
        _strict_uint(context[field], f"performance context {field}")
    if type(value["matches_current_context"]) is not bool:
        raise ValueError("performance context match flag must be boolean")
    if value["matches_current_context"] and (
        context["revision"] != outer_revision or context["external_epoch"] != outer_epoch
    ):
        raise ValueError("matching performance context disagrees with the response envelope")
    if value["target_fps"] is not None and _finite_number(value["target_fps"], "target_fps") < 0:
        raise ValueError("target_fps must be nonnegative or null")
    _validate_timing_stats(value["frame_interval_ms"], "frame interval")
    _validate_timing_stats(value["cpu_submit_ms"], "CPU submission")
    gpu = _exact_fields(value["gpu_ms"], {
        "samples", "mean", "p50", "p95", "max", "supported", "state", "reason",
        "last_frame", "pending_queries", "dropped_samples",
    }, "GPU timing")
    samples = _validate_timing_stats({field: gpu[field] for field in ("samples", "mean", "p50", "p95", "max")},
                                     "GPU timing")
    if (type(gpu["supported"]) is not bool or not isinstance(gpu["state"], str) or
            gpu["state"] not in {"available", "warming_up", "unsupported"}):
        raise ValueError("invalid GPU support state")
    _strict_uint(gpu["pending_queries"], "pending GPU queries", maximum=8)
    _strict_uint(gpu["dropped_samples"], "dropped GPU samples")
    if gpu["last_frame"] is not None:
        _strict_uint(gpu["last_frame"], "last GPU frame")
        if gpu["last_frame"] > outer_frame or gpu["last_frame"] + 119 < outer_frame:
            raise ValueError("last GPU frame is outside the retained frame window")
    if gpu["supported"]:
        if gpu["reason"] is not None:
            raise ValueError("supported GPU timing must not have a reason")
        expected = "available" if samples else "warming_up"
        if gpu["state"] != expected or (samples == 0) != (gpu["last_frame"] is None):
            raise ValueError("GPU timing state disagrees with samples")
    else:
        if gpu["state"] != "unsupported" or samples != 0 or gpu["last_frame"] is not None or gpu["pending_queries"] != 0:
            raise ValueError("unsupported GPU timing has contradictory data")
        if not isinstance(gpu["reason"], str) or not gpu["reason"] or len(gpu["reason"]) > 512:
            raise ValueError("unsupported GPU timing requires a bounded reason")

    feedback = _exact_fields(value["feedback"], {
        "supported", "state", "reason", "feedback_generation", "matches_current_context",
        "update_gpu_ms", "paired_gpu_ms", "samples", "last_attempt_frame", "attempts",
        "draw_attempts", "pending_queries", "draw_pending_queries", "dropped_samples",
        "draw_dropped_samples",
    }, "feedback timing")
    for flag in ("supported", "matches_current_context"):
        if type(feedback[flag]) is not bool:
            raise ValueError(f"feedback {flag} must be boolean")
    for field in ("feedback_generation", "last_attempt_frame", "attempts", "draw_attempts", "dropped_samples", "draw_dropped_samples"):
        _strict_uint(feedback[field], f"feedback {field}")
    if feedback["last_attempt_frame"] > outer_frame + 1:
        raise ValueError("feedback attempt frame exceeds current update")
    for field in ("pending_queries", "draw_pending_queries"):
        _strict_uint(feedback[field], f"feedback {field}", maximum=8)
    _validate_timing_stats(feedback["update_gpu_ms"], "feedback update")
    count = _validate_timing_stats(feedback["paired_gpu_ms"], "feedback paired")
    pairs = feedback["samples"]
    if not isinstance(pairs, list) or len(pairs) != count:
        raise ValueError("feedback raw pairs disagree with statistics")
    previous = -1
    totals = []
    for pair in pairs:
        _exact_fields(pair, {"frame", "update_ms", "draw_ms"}, "feedback pair")
        frame = _strict_uint(pair["frame"], "feedback originating frame")
        if frame <= previous or frame > outer_frame or frame + 119 < outer_frame:
            raise ValueError("feedback frames must be unique, ordered and retained")
        previous = frame
        components = [_finite_number(pair[field], f"feedback {field}") for field in ("update_ms", "draw_ms")]
        if min(components) < 0:
            raise ValueError("feedback pair times must be nonnegative")
        totals.append(sum(components))
    if count > min(feedback["attempts"], feedback["draw_attempts"]):
        raise ValueError("feedback paired samples exceed attempts")
    if totals:
        totals.sort()
        expected = {"mean": sum(totals) / count, "p50": totals[max(1, math.ceil(.5 * count))-1],
                    "p95": totals[max(1, math.ceil(.95 * count))-1], "max": totals[-1]}
        if any(not math.isclose(feedback["paired_gpu_ms"][key], number, rel_tol=1e-9, abs_tol=1e-9)
               for key, number in expected.items()):
            raise ValueError("feedback paired statistics disagree with raw pairs")
    expected_state = ("unsupported" if not feedback["supported"] else "stale" if not feedback["matches_current_context"]
                      else "available" if count else "warming_up")
    if feedback["state"] != expected_state:
        raise ValueError("feedback timing state disagrees with context/samples")
    if not feedback["matches_current_context"] and pairs:
        raise ValueError("stale feedback must not expose matched pairs")
    if expected_state in {"unsupported", "stale"}:
        if not isinstance(feedback["reason"], str) or not feedback["reason"] or len(feedback["reason"]) > 512:
            raise ValueError("feedback timing requires a bounded reason")
    elif feedback["reason"] is not None:
        raise ValueError("available feedback timing must not have a reason")


def validate_query(reply: dict[str, Any], operation: str) -> dict[str, Any]:
    if operation == "export_scene":
        validate_export_reply(reply)
        return reply
    ok, status = _common(reply)
    if not ok:
        _failure(reply, allow_expired=False)
        return reply
    if status != "ok" or not isinstance(reply.get("result"), dict):
        raise ValueError("query success must have status ok and an object result")
    if operation == "get_capabilities":
        _validate_capabilities(reply["result"])
    elif operation == "get_scene":
        _validate_scene(reply["result"], reply["frame"], reply["revision"])
    elif operation == "get_performance":
        _validate_performance(reply["result"], reply["frame"], reply["revision"], reply["external_epoch"])
    elif operation == "get_diagnostics":
        _validate_diagnostics(reply["result"])
    return reply


def validate_receipt(reply: dict[str, Any], operation: str | None, target_id: str) -> dict[str, Any]:
    ok, status = _common(reply)
    if reply["id"] != target_id:
        raise ValueError("receipt ID does not match target request")
    method = reply.get("method")
    if method is not None and method not in {
        "configure_layers", "publish_shader", "set_uniform", "set_layer", "capture_preview",
        "trial_shader", "accept_trial", "cancel_trial"
    }:
        raise ValueError("receipt has an unknown method")
    if operation is not None and method is not None and method != operation:
        raise ValueError("receipt method does not match operation")
    known_operation = operation or method
    # Polling may lack an operation hint. Never project trial evidence through
    # the legacy failure path unless its originating operation is known.
    result = reply.get("result")
    if isinstance(result, dict) and "trial" in result and known_operation != "trial_shader":
        raise ValueError("trial metadata requires an identified protected-trial receipt")
    if not ok:
        allow_expired = known_operation == "capture_preview" or (operation is None and method is None)
        _failure(reply, allow_expired=allow_expired)
        if "reason" in reply["error"]:
            raise ValueError("unexpected failure reason")
        if known_operation == "trial_shader" and "result" in reply:
            result = reply["result"]
            if not isinstance(result, dict) or set(result) != {"trial"}:
                raise ValueError("failed trial receipt has invalid metadata")
            _validate_trial(result["trial"], original_id=target_id, outer_frame=reply["frame"],
                            outer_revision=reply["revision"])
            if result["trial"]["phase"] not in {"rolled_back", "interrupted"}:
                raise ValueError("failed trial receipt has a nonterminal phase")
        return reply
    if known_operation == "capture_preview":
        if status not in {"queued", "captured"}:
            raise ValueError("invalid capture receipt status")
        if status == "captured":
            _validate_capture_metadata(reply)
    elif status == "captured":
        if known_operation is not None:
            raise ValueError("non-capture mutation returned captured")
        _validate_capture_metadata(reply)
    elif known_operation == "trial_shader":
        if status not in {"evaluating", "ready", "committing", "rolling_back", "rendered"}:
            raise ValueError("invalid trial receipt status")
        result = reply.get("result")
        if not isinstance(result, dict) or set(result) != {"trial"}:
            raise ValueError("trial receipt is missing trial metadata")
        _validate_trial(result["trial"], original_id=target_id, outer_frame=reply["frame"],
                        outer_revision=reply["revision"])
        expected_phase = {"evaluating": "evaluating", "ready": "ready",
                          "committing": "committing", "rolling_back": "rolling_back",
                          "rendered": "accepted"}[status]
        if result["trial"]["phase"] != expected_phase:
            raise ValueError("trial receipt status and phase disagree")
    elif status not in {"queued", "applied", "rendered"}:
        raise ValueError("invalid mutation receipt status")
    if isinstance(result, dict) and "feedback_draw" in result:
        if known_operation not in {"accept_trial", "cancel_trial"} or status != "rendered":
            raise ValueError("feedback action draw requires a rendered trial action")
        _validate_feedback_draw(result["feedback_draw"], reply, "program")
    if status in {"applied", "rendered"} or known_operation == "trial_shader" and ok:
        _strict_uint(reply.get("applied_after_frame"), "applied_after_frame")
    return reply


def _validate_trial(value: Any, *, original_id: str | None = None,
                    outer_frame: int | None = None,
                    outer_revision: int | None = None) -> None:
    if not isinstance(value, dict):
        raise ValueError("trial metadata must be an object")
    required = {"trial_id", "layer", "phase", "gpu_budget_ms", "lease_remaining_ms",
                "evaluation_revision", "first_frame", "warmup_frames", "minimum_samples",
                "freshness_frames", "context", "samples", "p95_ms", "hard_max_ms",
                "last_gpu_frame"}
    optional = {"rollback_code", "restored_previous", "restoration_revision", "rendered_frame"}
    if "target" in value:
        if value["target"] != "default_feedback_texture":
            raise ValueError("invalid feedback trial target")
        required |= {"target", "candidate_program", "measurement_id", "pair_coverage"}
        optional.add("candidate_draw_frame")
        for field in ("candidate_program", "measurement_id"):
            _strict_uint(value.get(field), f"feedback trial {field}")
        coverage = _finite_number(value.get("pair_coverage"), "feedback trial pair coverage")
        if not 0 <= coverage <= 1:
            raise ValueError("feedback trial pair coverage outside range")
        if "candidate_draw_frame" in value:
            drawn = _strict_uint(value["candidate_draw_frame"], "feedback trial candidate draw")
            if (drawn < value.get("first_frame", drawn) or
                    outer_frame is not None and drawn > outer_frame):
                raise ValueError("feedback candidate draw outside trial progress")
        if value.get("phase") in {"ready", "committing", "accepted"} and (
                coverage < 0.95 or "candidate_draw_frame" not in value):
            raise ValueError("feedback admission lacks paired or composed evidence")
    if not required.issubset(value) or not set(value).issubset(required | optional):
        raise ValueError("trial metadata has invalid fields")
    if (not isinstance(value["trial_id"], str) or len(value["trial_id"]) > 64 or
            not ID_PATTERN.fullmatch(value["trial_id"])):
        raise ValueError("invalid trial identifier")
    if original_id is not None and value["trial_id"] != original_id:
        raise ValueError("trial metadata identifier disagrees with original receipt")
    if type(value["phase"]) is not str or value["phase"] not in {
        "evaluating", "ready", "committing", "rolling_back",
        "accepted", "rolled_back", "interrupted"
    }:
        raise ValueError("invalid trial phase")
    _strict_uint(value["layer"], "trial layer", maximum=31)
    budget = _finite_number(value["gpu_budget_ms"], "trial GPU budget")
    if not 0.05 <= budget <= 16:
        raise ValueError("trial GPU budget is outside range")
    _strict_uint(value["lease_remaining_ms"], "trial lease remaining", maximum=30000)
    for field in ("evaluation_revision", "first_frame"):
        _strict_uint(value[field], f"trial {field}")
    _strict_uint(value["samples"], "trial samples", maximum=120)
    if value["warmup_frames"] != 8 or value["minimum_samples"] != 30 or value["freshness_frames"] != 8:
        raise ValueError("trial measurement policy is invalid")
    context = _exact_fields(value["context"], {"revision", "external_epoch", "width", "height"}, "trial context")
    for field in context:
        _strict_uint(context[field], f"trial context {field}")
    if context["revision"] != value["evaluation_revision"]:
        raise ValueError("trial context revision disagrees with evaluation revision")
    if outer_revision is not None and value["evaluation_revision"] > outer_revision:
        raise ValueError("trial evaluation revision exceeds response revision")
    for field in ("p95_ms", "hard_max_ms"):
        if value[field] is not None and _finite_number(value[field], f"trial {field}") < 0:
            raise ValueError(f"trial {field} must be nonnegative")
    if value["last_gpu_frame"] is not None:
        _strict_uint(value["last_gpu_frame"], "trial last GPU frame")
        if outer_frame is not None and value["last_gpu_frame"] > outer_frame:
            raise ValueError("trial last GPU frame exceeds response frame")
    if outer_frame is not None and value["first_frame"] > outer_frame + 1:
        raise ValueError("trial first frame is outside response progress")
    if "rollback_code" in value and (type(value["rollback_code"]) is not str or
            value["rollback_code"] not in {
        "trial_budget_exceeded", "trial_expired", "trial_cancelled",
        "trial_gpu_unavailable", "trial_context_changed", "trial_conflict",
        "trial_feedback_update_failed", "trial_gpu_evidence_expired", "trial_not_ready"
    }):
        raise ValueError("invalid trial rollback code")
    if "restored_previous" in value and type(value["restored_previous"]) is not bool:
        raise ValueError("invalid restored_previous flag")
    for field in ("restoration_revision", "rendered_frame"):
        if field in value:
            _strict_uint(value[field], f"trial {field}")
            if field == "rendered_frame" and outer_frame is not None and value[field] > outer_frame:
                raise ValueError("trial rendered frame exceeds response frame")
    if "restoration_revision" in value:
        if value["restoration_revision"] < value["evaluation_revision"]:
            raise ValueError("trial restoration revision predates evaluation")
        if outer_revision is not None and value["restoration_revision"] > outer_revision:
            raise ValueError("trial restoration revision exceeds response revision")

    samples = value["samples"]
    if samples == 0:
        if value["p95_ms"] is not None:
            raise ValueError("empty trial sample set must have null p95")
    elif value["p95_ms"] is None or value["hard_max_ms"] is None or value["last_gpu_frame"] is None:
        raise ValueError("trial samples require p95, hard maximum, and last frame")
    if value["p95_ms"] is not None and value["hard_max_ms"] is not None and value["p95_ms"] > value["hard_max_ms"]:
        raise ValueError("trial p95 exceeds hard maximum")

    phase = value["phase"]
    admitted = phase in {"ready", "committing", "accepted"}
    if admitted:
        if (samples < 30 or value["p95_ms"] is None or value["p95_ms"] > budget or
                value["hard_max_ms"] is None or value["hard_max_ms"] > budget * 2 or
                value["last_gpu_frame"] is None):
            raise ValueError("admitted trial lacks valid GPU evidence")
        if phase in {"ready", "committing"} and value["lease_remaining_ms"] == 0:
            raise ValueError("pending admitted trial has an expired lease")
        if outer_frame is not None:
            maximum_age = 9 if phase == "accepted" else 8
            if outer_frame - value["last_gpu_frame"] > maximum_age:
                raise ValueError("admitted trial GPU evidence is stale")
    if phase in {"rolling_back", "rolled_back"} and "rollback_code" not in value:
        raise ValueError("rollback trial phase requires a rollback code")
    if phase == "accepted":
        if "rendered_frame" not in value or "rollback_code" in value or "restored_previous" in value:
            raise ValueError("accepted trial has invalid terminal evidence")
    elif phase == "rolled_back":
        if (value.get("restored_previous") is not True or "restoration_revision" not in value or
                "rendered_frame" not in value):
            raise ValueError("rolled-back trial lacks restoration evidence")
    elif phase == "interrupted" and "restored_previous" not in value:
        raise ValueError("interrupted trial lacks ownership evidence")
    if phase in {"accepted", "rolled_back"}:
        if (outer_frame is None or value["rendered_frame"] != outer_frame or
                value["rendered_frame"] < value["first_frame"]):
            raise ValueError("terminal trial render acknowledgement is contradictory")
    if phase == "interrupted" and "rendered_frame" in value:
        raise ValueError("interrupted trial must not claim a baseline-only rendered frame")


def _validate_feedback_draw(value: Any, receipt: dict[str, Any], program_field: str) -> None:
    draw = _exact_fields(value, {"trial_id", "evaluation_revision", "frame", program_field},
                         "feedback draw ownership")
    if (not isinstance(draw["trial_id"], str) or len(draw["trial_id"]) > 64 or
            not ID_PATTERN.fullmatch(draw["trial_id"])):
        raise ValueError("invalid feedback draw trial ID")
    if (_strict_uint(draw["frame"], "feedback draw frame") != receipt["frame"] or
            _strict_uint(draw["evaluation_revision"], "feedback evaluation revision") > receipt["revision"]):
        raise ValueError("feedback draw owner disagrees with receipt")
    if _strict_uint(draw[program_field], "feedback draw program") == 0:
        raise ValueError("feedback draw program must be linked")


def _validate_capture_metadata(receipt: dict[str, Any]) -> None:
    result = receipt.get("result")
    if not isinstance(result, dict) or result.get("mime_type") != "image/png" or not isinstance(
        result.get("data_base64"), str
    ):
        raise ValueError("captured receipt is missing PNG data")
    for field in ("width", "height"):
        value = result.get(field)
        if type(value) is not int or not 1 <= value <= 1024:
            raise ValueError(f"invalid capture {field}")
    for field in ("backing_width", "backing_height"):
        value = result.get(field)
        if type(value) is not int or value <= 0:
            raise ValueError(f"invalid capture {field}")
    if "feedback_trial" in result:
        _validate_feedback_draw(result["feedback_trial"], receipt, "candidate_program")
    minimum = _strict_uint(result.get("minimum_revision"), "minimum_revision")
    if minimum > receipt["revision"]:
        raise ValueError("capture minimum_revision exceeds receipt revision")


def _image(receipt: dict[str, Any]) -> tuple[dict[str, Any], str]:
    projected = deepcopy(receipt)
    encoded = projected["result"].pop("data_base64")
    try:
        data = base64.b64decode(encoded, validate=True)
    except (binascii.Error, ValueError) as error:
        raise ValueError("capture image is not valid base64") from error
    if len(data) > 8_388_608 or not data.startswith(b"\x89PNG\r\n\x1a\n") or len(data) < 33:
        raise ValueError("capture image is not a bounded PNG")
    length, kind = struct.unpack(">I4s", data[8:16])
    if length != 13 or kind != b"IHDR":
        raise ValueError("capture PNG has no valid IHDR")
    ihdr = data[16:29]
    expected_crc = struct.unpack(">I", data[29:33])[0]
    if (binascii.crc32(b"IHDR" + ihdr) & 0xFFFFFFFF) != expected_crc:
        raise ValueError("capture PNG has an invalid IHDR checksum")
    width, height, bit_depth, color_type, compression, filtering, interlace = struct.unpack(
        ">IIBBBBB", ihdr
    )
    legal_depths = {
        0: {1, 2, 4, 8, 16},
        2: {8, 16},
        3: {1, 2, 4, 8},
        4: {8, 16},
        6: {8, 16},
    }
    if (
        color_type not in legal_depths
        or bit_depth not in legal_depths[color_type]
        or compression != 0
        or filtering != 0
        or interlace not in {0, 1}
    ):
        raise ValueError("capture PNG has invalid IHDR encoding fields")
    if (width, height) != (receipt["result"]["width"], receipt["result"]["height"]):
        raise ValueError("capture PNG dimensions disagree with metadata")
    return projected, encoded


def _error_result(operation: Operation, request_id: str | None, delivery: str, code: str, message: str,
                  *, engine: dict[str, Any] | None = None, unknown: bool = False) -> CallToolResult:
    result = AdapterResult(
        adapter_version=1,
        operation=operation,
        request_id=request_id,
        outcome="unknown" if unknown else "failed",
        delivery=delivery,
        engine=engine,
        error=AdapterError(code=code, message=message),
    )
    return _call_result(result)


def _call_result(result: AdapterResult, image_data: str | None = None) -> CallToolResult:
    if result.engine is not None:
        result.engine = _sanitize_engine(result.engine)
    content: list[TextContent | ImageContent] = [
        TextContent(
            type="text",
            text=f"vosc {result.operation}: {result.outcome} ({result.delivery}).",
        )
    ]
    if image_data is not None:
        content.append(ImageContent(type="image", data=image_data, mime_type="image/png"))
    return CallToolResult(
        content=content,
        structured_content=result.model_dump(mode="json"),
        is_error=result.outcome in {"failed", "unknown"},
    )


def _sanitize_engine(engine: dict[str, Any]) -> dict[str, Any]:
    projected = deepcopy(engine)
    result = projected.get("result")
    if isinstance(result, dict):
        result.pop("data_base64", None)
    return projected


def _engine_error(reply: dict[str, Any]) -> AdapterError:
    error = reply["error"]
    message = error["message"]
    if error["code"] == "request_not_found":
        message += "; this does not prove the original request was not executed"
    return AdapterError(code=error["code"], message=message)


class _Admission:
    def __init__(self, maximum: int) -> None:
        self.maximum = maximum
        self.active = 0
        self.lock = asyncio.Lock()

    async def enter(self) -> bool:
        async with self.lock:
            if self.active >= self.maximum:
                return False
            self.active += 1
            return True

    async def leave(self) -> None:
        async with self.lock:
            self.active -= 1


class Adapter:
    def __init__(self, socket_path: Path) -> None:
        self.engine = EngineClient(socket_path)
        self.query_admission = _Admission(2)
        self.mutation_admission = _Admission(14)

    async def query(self, operation: Operation, params: dict[str, Any], request_id: str | None = None,
                    wait_ms: int = 0) -> CallToolResult:
        if not await self.query_admission.enter():
            return _error_result(operation, request_id, "query", "adapter_busy", "query admission is full")
        try:
            try:
                reply = await self.engine.query(operation, params)
                validate_query(reply, operation)
                if not reply["ok"]:
                    return _error_result(operation, request_id, "query", reply["error"]["code"],
                                         _engine_error(reply).message, engine=reply)
                if operation != "get_request":
                    if operation == "get_capabilities":
                        capabilities = reply["result"]
                        if capabilities["version"] != 1 or not REQUIRED_OPERATIONS.issubset(
                            capabilities["operations"]
                        ):
                            return _error_result(
                                operation, None, "query", "adapter_incompatible_engine",
                                "engine must expose API version 1 and all baseline operations",
                                engine=reply,
                            )
                    return _call_result(AdapterResult(adapter_version=1, operation=operation, request_id=None, outcome="observed",
                                                      delivery="query", engine=reply, error=None))
                receipt = validate_receipt(reply["result"], None, request_id or "")
                return await self._finish_receipt(operation, request_id or "", receipt, "query", wait_ms)
            except EngineClientError as error:
                return _error_result(operation, request_id, "query", error.code, error.message)
            except ValueError as error:
                return _error_result(operation, request_id, "query", "adapter_protocol_error", str(error))
        finally:
            await self.query_admission.leave()

    async def mutate(self, operation: Operation, request_id: str, params: dict[str, Any], wait_ms: int) -> CallToolResult:
        if not await self.mutation_admission.enter():
            return _error_result(operation, request_id, "not_submitted", "adapter_busy", "mutation admission is full")
        try:
            try:
                receipt = await self.engine.submit(request_id, operation, params)
                validate_receipt(receipt, operation, request_id)
            except EngineClientError as error:
                if error.wrote_mutation:
                    return _error_result(operation, request_id, "uncertain", "adapter_outcome_unknown",
                                         "mutation may have been submitted; inspect its receipt and scene", unknown=True)
                return _error_result(operation, request_id, "not_submitted", error.code, error.message)
            except ValueError as error:
                return _error_result(operation, request_id, "uncertain", "adapter_outcome_unknown",
                                     f"mutation reply was invalid after possible submission: {error}", unknown=True)
            return await self._finish_receipt(operation, request_id, receipt, "submitted", wait_ms)
        finally:
            await self.mutation_admission.leave()

    async def _finish_receipt(self, operation: Operation, request_id: str, receipt: dict[str, Any],
                              delivery: Literal["submitted", "query"], wait_ms: int) -> CallToolResult:
        deadline = time.monotonic() + wait_ms / 1000
        latest = receipt
        origin = receipt.get("method") if operation == "get_request" else operation
        while latest["ok"] and latest["status"] in PENDING and time.monotonic() < deadline:
            await asyncio.sleep(min(0.1, max(0.0, deadline - time.monotonic())))
            if time.monotonic() >= deadline:
                break
            try:
                outer = await self.engine.query(
                    "get_request", {"request_id": request_id},
                    timeout=max(0.0, deadline - time.monotonic()),
                )
                validate_query(outer, "get_request")
                if not outer["ok"]:
                    return _error_result(operation, request_id, delivery, "adapter_outcome_unknown",
                                         "receipt polling failed after valid submission", engine=latest, unknown=True)
                latest = validate_receipt(outer["result"], origin, request_id)
            except EngineClientError as error:
                if error.code == "adapter_wait_deadline":
                    break
                if error.code == "adapter_busy":
                    continue
                return _error_result(operation, request_id, delivery, "adapter_outcome_unknown",
                                     "receipt observation failed; inspect receipt and scene", engine=latest, unknown=True)
            except ValueError:
                return _error_result(operation, request_id, delivery, "adapter_outcome_unknown",
                                     "receipt observation returned invalid protocol data", engine=latest, unknown=True)
        if not latest["ok"]:
            return _call_result(AdapterResult(adapter_version=1, operation=operation, request_id=request_id, outcome="failed",
                                              delivery=delivery, engine=latest, error=_engine_error(latest)))
        if latest["status"] in PENDING:
            return _call_result(AdapterResult(adapter_version=1, operation=operation, request_id=request_id, outcome="pending",
                                              delivery=delivery, engine=latest, error=None))
        image_data = None
        projected = latest
        if latest["status"] == "captured":
            try:
                projected, image_data = _image(latest)
            except ValueError as error:
                clean = deepcopy(latest)
                if isinstance(clean.get("result"), dict):
                    clean["result"].pop("data_base64", None)
                return _error_result(operation, request_id, delivery, "adapter_protocol_error", str(error), engine=clean)
        return _call_result(AdapterResult(adapter_version=1, operation=operation, request_id=request_id, outcome="completed",
                                          delivery=delivery, engine=projected, error=None), image_data)


adapter: Adapter
mcp = MCPServer(
    "vosc-engine-adapter",
    instructions=("Typed local authoring tools for an operator-launched vosc renderer. "
                  "For compatible direct-source edits, prefer trial_shader, wait for ready, "
                  "inspect a preview, then explicitly accept_trial; cancel or expiry restores "
                  "the retained baseline. publish_shader, set_uniform, and OSC are unbudgeted "
                  "compatibility bypasses. Trial GPU timing covers the composed scene, not CPU "
                  "compilation, media/update work, presentation, or GPU-hang preemption. The "
                  "adapter never launches, resets, stops, or owns the renderer."),
)


@mcp.tool(annotations=QUERY_ANNOTATIONS)
async def get_capabilities() -> Annotated[CallToolResult, AdapterResult]:
    """Inspect the local engine API version, operations, features, and limits."""
    return await adapter.query("get_capabilities", {})


@mcp.tool(annotations=QUERY_ANNOTATIONS)
async def get_scene() -> Annotated[CallToolResult, AdapterResult]:
    """Inspect actual current layers, shaders, uniforms, receipts, frames, and revisions."""
    return await adapter.query("get_scene", {})


@mcp.tool(annotations=QUERY_ANNOTATIONS)
async def get_performance() -> Annotated[CallToolResult, AdapterResult]:
    """Inspect bounded composed-scene timings; context match excludes animation and file-watch identity."""
    return await adapter.query("get_performance", {})


@mcp.tool(annotations=QUERY_ANNOTATIONS)
async def get_diagnostics(
    after: Annotated[int, Field(strict=True, ge=0, le=UINT64_MAX)] = 0,
    limit: Annotated[int, Field(strict=True, ge=1, le=128)] = 128,
) -> Annotated[CallToolResult, AdapterResult]:
    """Read a bounded page of structured engine diagnostics after a sequence cursor."""
    return await adapter.query("get_diagnostics", {"after": after, "limit": limit})


@mcp.tool(annotations=QUERY_ANNOTATIONS)
async def get_request(
    request_id: RequestId,
    wait_ms: WaitMs = 0,
) -> Annotated[CallToolResult, AdapterResult]:
    """Look up a retained receipt without replaying or recreating engine work."""
    _validate_request_id(request_id)
    return await adapter.query("get_request", {"request_id": request_id}, request_id, wait_ms)


@mcp.tool(annotations=QUERY_ANNOTATIONS)
async def export_scene() -> Annotated[CallToolResult, AdapterResult]:
    """Export a strict editable OSC recipe descriptor without mutating the renderer."""
    return await adapter.query("export_scene", {})


@mcp.tool(annotations=RESET_ANNOTATIONS)
async def configure_layers(
    request_id: RequestId,
    count: Annotated[int, Field(strict=True, ge=0, le=32)],
    layout: Literal["stack", "grid", "row", "col"],
    timing: Literal["immediate", "onset"] = "immediate",
    wait_ms: WaitMs = 1000,
) -> Annotated[CallToolResult, AdapterResult]:
    """Reset all existing layers to a new bounded count and layout."""
    _validate_request_id(request_id)
    return await adapter.mutate("configure_layers", request_id, {"count": count, "layout": layout, "timing": timing}, wait_ms)


@mcp.tool(annotations=MUTATION_ANNOTATIONS)
async def publish_shader(
    request_id: RequestId,
    layer: Layer,
    source: Annotated[str, Field(strict=True, min_length=1, max_length=131_072)],
    timing: Literal["immediate", "onset"] = "immediate",
    wait_ms: WaitMs = 1000,
) -> Annotated[CallToolResult, AdapterResult]:
    """Publish GLSL directly without a GPU budget; prefer trial_shader for compatible edits."""
    _validate_request_id(request_id)
    if len(source.encode("utf-8")) > 131_072:
        raise ToolError("source exceeds 131072 UTF-8 bytes")
    return await adapter.mutate("publish_shader", request_id, {"layer": layer, "source": source, "timing": timing}, wait_ms)


@mcp.tool(annotations=MUTATION_ANNOTATIONS)
async def set_uniform(
    request_id: RequestId,
    layer: Layer,
    name: Annotated[str, Field(strict=True, min_length=1, max_length=128)],
    value: BoundedFloat | BoundedInt | UniformVector,
    timing: Literal["immediate", "onset"] = "immediate",
    wait_ms: WaitMs = 1000,
) -> Annotated[CallToolResult, AdapterResult]:
    """Set an active numeric uniform directly without GPU admission; protected overrides belong in trial_shader."""
    _validate_request_id(request_id)
    if len(name.encode("utf-8")) > 128:
        raise ToolError("name exceeds 128 UTF-8 bytes")
    _validate_uniform_value(value)
    return await adapter.mutate("set_uniform", request_id, {"layer": layer, "name": name, "value": value, "timing": timing}, wait_ms)


@mcp.tool(annotations=MUTATION_ANNOTATIONS)
async def set_layer(
    request_id: RequestId,
    layer: Layer,
    property: Literal["alpha", "visible"],
    value: AlphaFloat | AlphaInt | StrictBool,
    timing: Literal["immediate", "onset"] = "immediate",
    wait_ms: WaitMs = 1000,
) -> Annotated[CallToolResult, AdapterResult]:
    """Set alpha or visibility on an existing layer using the property's exact value type."""
    _validate_request_id(request_id)
    if property == "visible":
        if type(value) is not bool:
            raise ToolError("visible requires a strict boolean")
    elif isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or not 0 <= value <= 1:
        raise ToolError("alpha requires a finite number from 0 through 1")
    return await adapter.mutate("set_layer", request_id, {"layer": layer, "property": property, "value": value, "timing": timing}, wait_ms)


@mcp.tool(annotations=MUTATION_ANNOTATIONS)
async def capture_preview(
    request_id: RequestId,
    max_width: Annotated[int, Field(strict=True, ge=64, le=1024)] = 640,
    minimum_revision: Annotated[int, Field(strict=True, ge=0, le=UINT64_MAX)] = 0,
    max_readback_pixels: Annotated[int, Field(strict=True, ge=1, le=16_777_216)] = 16_777_216,
    wait_ms: WaitMs = 1000,
) -> Annotated[CallToolResult, AdapterResult]:
    """Request a bounded native PNG preview from an eligible rendered frame."""
    _validate_request_id(request_id)
    return await adapter.mutate("capture_preview", request_id, {"max_width": max_width,
                                "minimum_revision": minimum_revision,
                                "max_readback_pixels": max_readback_pixels}, wait_ms)


@mcp.tool(annotations=MUTATION_ANNOTATIONS)
async def trial_shader(
    request_id: RequestId,
    layer: Layer,
    source: Annotated[str, Field(strict=True, min_length=1, max_length=131_072)],
    uniforms: Annotated[dict[str, BoundedFloat | BoundedInt | UniformVector], Field(max_length=32)] | None = None,
    gpu_budget_ms: Annotated[StrictFloat, Field(ge=0.05, le=16.0)] = 8.0,
    ttl_ms: Annotated[int, Field(strict=True, ge=1000, le=30000)] = 10000,
    wait_ms: WaitMs = 1000,
    target: Literal["default_feedback_texture"] | None = None,
) -> Annotated[CallToolResult, AdapterResult]:
    """Install a bounded direct-source or default feedback candidate under GPU admission and rollback protection."""
    _validate_request_id(request_id)
    if len(source.encode("utf-8")) > 131_072:
        raise ToolError("source exceeds 131072 UTF-8 bytes")
    checked: dict[str, Any] = {}
    for name, value in (uniforms or {}).items():
        if not isinstance(name, str) or not name or len(name.encode("utf-8")) > 128:
            raise ToolError("uniform names must be nonempty and at most 128 UTF-8 bytes")
        _validate_uniform_value(value)
        checked[name] = value
    params = {"layer": layer, "source": source, "uniforms": checked,
              "gpu_budget_ms": gpu_budget_ms, "ttl_ms": ttl_ms}
    if target is not None:
        params["target"] = target
    return await adapter.mutate("trial_shader", request_id, params, wait_ms)


@mcp.tool(annotations=MUTATION_ANNOTATIONS)
async def accept_trial(
    request_id: RequestId,
    trial_id: RequestId,
    wait_ms: WaitMs = 1000,
) -> Annotated[CallToolResult, AdapterResult]:
    """Explicitly accept a ready protected trial after a fresh GPU check."""
    _validate_request_id(request_id)
    _validate_request_id(trial_id)
    return await adapter.mutate("accept_trial", request_id, {"trial_id": trial_id}, wait_ms)


@mcp.tool(annotations=MUTATION_ANNOTATIONS)
async def cancel_trial(
    request_id: RequestId,
    trial_id: RequestId,
    wait_ms: WaitMs = 1000,
) -> Annotated[CallToolResult, AdapterResult]:
    """Cancel a protected trial and restore its retained direct-source baseline."""
    _validate_request_id(request_id)
    _validate_request_id(trial_id)
    return await adapter.mutate("cancel_trial", request_id, {"trial_id": trial_id}, wait_ms)


def _validate_request_id(request_id: str) -> None:
    if request_id.startswith("_q."):
        raise ToolError("request_id prefix _q. is reserved for adapter queries")
    if len(request_id.encode("ascii")) > 64 or not ID_PATTERN.fullmatch(request_id):
        raise ToolError("request_id is not a bounded adapter identifier")


def _validate_uniform_value(value: Any) -> None:
    values = value if isinstance(value, list) else [value]
    if isinstance(value, list) and not 2 <= len(value) <= 4:
        raise ToolError("uniform vectors require 2 through 4 components")
    for item in values:
        if isinstance(item, bool) or not isinstance(item, (int, float)) or not math.isfinite(item) or abs(item) > 1_000_000:
            raise ToolError("uniform values must be finite numbers with absolute value at most 1000000")


def _seal_input_schemas() -> None:
    """Advertise the frozen closed-world contract with the pinned SDK."""
    for tool in mcp._tool_manager.list_tools():  # Pinned mcp==2.3.0 registry shape.
        if tool.parameters.get("additionalProperties") is not False:
            raise RuntimeError(f"tool {tool.name} did not inherit closed-world argument validation")


_seal_input_schemas()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--socket", required=True, type=Path, help="absolute private vosc Unix socket")
    args = parser.parse_args()
    if not args.socket.is_absolute():
        parser.error("--socket must be absolute")
    global adapter
    adapter = Adapter(args.socket)
    mcp.run(transport="stdio")


if __name__ == "__main__":
    main()
