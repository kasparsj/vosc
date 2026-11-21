#!/usr/bin/env python3
"""Run a protected shader acceptance/cancel session on an owned renderer.

The harness always launches the supplied binary with a private Unix socket and
OSC port 0. It intentionally resets that disposable renderer, so callers must
pass --allow-reset. It never attaches to an existing socket or process.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time
import uuid

from control_client import Client


BASELINE = """#version 330
uniform vec4 globalColor;
out vec4 fragColor;
void main() { fragColor = vec4(0.012, 0.018, 0.035, 1.0) * globalColor; }
"""

PENDING = {"queued", "applied", "evaluating", "ready", "committing", "rolling_back"}


class Session:
    def __init__(self, socket_path: Path, output: Path):
        self.socket_path = socket_path
        self.output = output
        self.prefix = f"protected-{uuid.uuid4().hex[:10]}"
        self.sequence = 0

    def call(self, method: str, params: dict | None = None, request_id: str | None = None) -> dict:
        self.sequence += 1
        request_id = request_id or f"{self.prefix}-{self.sequence}"
        client = Client(self.socket_path)
        try:
            response = client.call(method, params or {}, request_id)
        finally:
            client.close()
        return response

    def receipt(self, request_id: str) -> dict:
        response = self.call("get_request", {"request_id": request_id})
        assert response["ok"], response
        return response["result"]

    def wait_for(self, request_id: str, predicate, timeout: float = 10.0) -> dict:
        deadline = time.monotonic() + timeout
        latest = None
        while time.monotonic() < deadline:
            latest = self.receipt(request_id)
            if predicate(latest):
                return latest
            time.sleep(0.04)
        raise TimeoutError(f"request {request_id} did not reach expected state: {latest}")

    def mutate(self, method: str, params: dict) -> dict:
        response = self.call(method, params)
        assert response["ok"], response
        if response["status"] in PENDING:
            response = self.wait_for(response["id"], lambda item: item["status"] not in PENDING)
        assert response["ok"], response
        return response

    def scene(self) -> dict:
        response = self.call("get_scene")
        assert response["ok"], response
        return response["result"]

    def capture(self, name: str, minimum_revision: int) -> dict:
        response = self.call("capture_preview", {"max_width": 900,
            "minimum_revision": minimum_revision, "max_readback_pixels": 16_777_216})
        assert response["ok"], response
        if response["status"] in PENDING:
            response = self.wait_for(response["id"], lambda item: item["status"] not in PENDING)
        assert response["ok"] and response["status"] == "captured", response
        payload = response["result"].pop("data_base64")
        (self.output / name).write_bytes(base64.b64decode(payload))
        return response


def configured_controls(scene: dict, names: list[str]) -> dict:
    uniforms = scene["layers"][0]["shader"]["uniforms"]
    return {name: uniforms[name]["configured_value"] for name in names}


def wait_for_socket(process: subprocess.Popen, socket_path: Path, log_path: Path) -> None:
    for _ in range(150):
        if socket_path.is_socket():
            return
        if process.poll() is not None:
            raise RuntimeError(log_path.read_text(errors="replace"))
        time.sleep(0.1)
    raise TimeoutError("owned renderer socket did not appear")


def stop_owned(process: subprocess.Popen, binary: Path, socket_path: Path) -> dict:
    identity = subprocess.check_output(["ps", "-p", str(process.pid), "-o", "comm="], text=True).strip()
    if identity != str(binary):
        raise RuntimeError(f"refusing to stop unexpected process identity {identity!r}")
    os.kill(process.pid, signal.SIGTERM)
    try:
        return_code = process.wait(timeout=8)
    except subprocess.TimeoutExpired as error:
        raise RuntimeError("owned renderer did not exit after SIGTERM") from error
    return {"pid": process.pid, "identity": identity, "return_code": return_code,
            "socket_removed": not socket_path.exists()}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--shader", type=Path, required=True)
    parser.add_argument("--preset", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--allow-reset", action="store_true",
                        help="Acknowledge that the newly launched disposable renderer is reset")
    parser.add_argument("--review-ready", action="store_true",
                        help="Pause after the ready preview is saved, before explicit acceptance")
    args = parser.parse_args()
    if not args.allow_reset:
        parser.error("--allow-reset is required for this destructive disposable-renderer check")
    binary = args.binary.resolve()
    shader_path = args.shader.resolve()
    preset_path = args.preset.resolve()
    output = args.output.resolve()
    if not binary.is_file() or not shader_path.is_file() or not preset_path.is_file():
        parser.error("binary, shader, and preset must be existing files")
    output.mkdir(parents=True, exist_ok=False)
    output.chmod(0o700)
    runtime = Path(tempfile.mkdtemp(prefix="vosc-protected-authoring-", dir="/tmp"))
    runtime.chmod(0o700)
    socket_path = runtime / "control.sock"
    log_path = output / "renderer.log"
    log = log_path.open("wb")
    process = subprocess.Popen([str(binary)], cwd=binary.parents[3],
        env={**os.environ, "VOSC_CONTROL_SOCKET": str(socket_path), "VOSC_OSC_PORT": "0"},
        stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
    owner = {"pid": process.pid, "binary": str(binary),
        "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
        "socket": str(socket_path), "osc_port_requested": 0, "runtime": str(runtime)}
    (output / "owner.json").write_text(json.dumps(owner, indent=2) + "\n")
    cleanup = None
    try:
        wait_for_socket(process, socket_path, log_path)
        session = Session(socket_path, output)
        controls = json.loads(preset_path.read_text())["controls"]
        names = list(controls)
        shader = shader_path.read_text()
        session.mutate("configure_layers", {"count": 1, "layout": "stack"})
        session.mutate("publish_shader", {"layer": 0, "source": BASELINE})
        cheap_baseline = session.scene()

        started = session.call("trial_shader", {"layer": 0, "source": shader,
            "uniforms": controls, "gpu_budget_ms": 8.0, "ttl_ms": 30000})
        assert started["ok"], started
        trial_id = started["id"]
        ready = session.wait_for(trial_id, lambda item: item["status"] == "ready" or not item["ok"])
        assert ready["ok"] and ready["status"] == "ready", ready
        ready_scene = session.scene()
        ready_capture = session.capture("organic-ready.png", ready_scene["revision"])
        reviewed_before_accept = False
        if args.review_ready:
            print(f"READY_FOR_REVIEW {output / 'organic-ready.png'}", flush=True)
            input("Inspect the ready preview, then press Enter to accept: ")
            reviewed_before_accept = True
        accept = session.mutate("accept_trial", {"trial_id": trial_id})
        accepted_receipt = session.wait_for(trial_id, lambda item: item["status"] not in PENDING)
        assert accepted_receipt["ok"] and accepted_receipt["status"] == "rendered", accepted_receipt
        accepted_scene = session.scene()
        accepted_program = accepted_scene["layers"][0]["shader"]["program"]
        accepted_controls = configured_controls(accepted_scene, names)
        accepted_capture = session.capture("organic-accepted.png", accepted_scene["revision"])

        # Let preview readback and acceptance leave the 120-frame telemetry window
        # before recording composed-scene GPU observations.
        time.sleep(2.3)
        performance = []
        for _ in range(4):
            observed = session.call("get_performance")
            assert observed["ok"], observed
            metrics = observed["result"]
            assert metrics["matches_current_context"], metrics
            assert metrics["gpu_ms"]["supported"], metrics
            assert metrics["gpu_ms"]["state"] == "available", metrics
            assert metrics["gpu_ms"]["samples"] > 0, metrics
            performance.append(metrics)
            time.sleep(0.35)

        revised = dict(controls)
        revised["trailLength"] = 1.75
        cancel_started = session.call("trial_shader", {"layer": 0, "source": shader,
            "uniforms": revised, "gpu_budget_ms": 8.0, "ttl_ms": 30000})
        assert cancel_started["ok"], cancel_started
        cancel_trial_id = cancel_started["id"]
        cancel_ready = session.wait_for(cancel_trial_id,
            lambda item: item["status"] == "ready" or not item["ok"])
        assert cancel_ready["ok"] and cancel_ready["status"] == "ready", cancel_ready
        candidate_scene = session.scene()
        candidate_capture = session.capture("organic-cancel-candidate.png", candidate_scene["revision"])
        cancel = session.mutate("cancel_trial", {"trial_id": cancel_trial_id})
        cancelled_receipt = session.wait_for(cancel_trial_id, lambda item: item["status"] not in PENDING)
        assert not cancelled_receipt["ok"]
        assert cancelled_receipt["error"]["code"] == "trial_cancelled", cancelled_receipt
        restored_scene = session.scene()
        restored_program = restored_scene["layers"][0]["shader"]["program"]
        restored_controls = configured_controls(restored_scene, names)
        assert restored_program == accepted_program, (restored_program, accepted_program)
        assert restored_controls == accepted_controls, (restored_controls, accepted_controls)
        restored_capture = session.capture("organic-restored.png", restored_scene["revision"])

        evidence = {"status": "PASS", "owner": owner,
            "inputs": {"shader": str(shader_path), "shader_sha256": hashlib.sha256(shader.encode()).hexdigest(),
                       "preset": str(preset_path), "preset_sha256": hashlib.sha256(preset_path.read_bytes()).hexdigest(),
                       "controls": controls},
            "cheap_baseline": {"program": cheap_baseline["layers"][0]["shader"]["program"],
                               "revision": cheap_baseline["revision"]},
            "acceptance": {"trial_id": trial_id, "ready": ready, "action": accept,
                           "terminal": accepted_receipt, "scene": accepted_scene,
                           "ready_preview_reviewed_before_accept": reviewed_before_accept,
                           "capture_ready": ready_capture, "capture_accepted": accepted_capture,
                           "performance_after_settle": performance},
            "cancellation": {"trial_id": cancel_trial_id, "ready": cancel_ready,
                             "candidate_scene": candidate_scene, "action": cancel,
                             "terminal": cancelled_receipt, "restored_scene": restored_scene,
                             "accepted_program": accepted_program, "restored_program": restored_program,
                             "accepted_controls": accepted_controls, "restored_controls": restored_controls,
                             "capture_candidate": candidate_capture, "capture_restored": restored_capture}}
        (output / "evidence.json").write_text(json.dumps(evidence, indent=2) + "\n")
        print(f"PASS protected organic accept/cancel session; evidence: {output}")
        return 0
    finally:
        log.close()
        if process.poll() is None:
            cleanup = stop_owned(process, binary, socket_path)
        else:
            cleanup = {"pid": process.pid, "return_code": process.returncode,
                       "socket_removed": not socket_path.exists()}
        (output / "cleanup.json").write_text(json.dumps(cleanup, indent=2) + "\n")
        if not cleanup["socket_removed"]:
            raise RuntimeError("owned renderer socket remained after cleanup")


if __name__ == "__main__":
    raise SystemExit(main())
