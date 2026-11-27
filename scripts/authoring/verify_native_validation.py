#!/usr/bin/env python3
"""Exercise native validation failure, timeout and separate output for IR-08."""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
from contextlib import closing
import json
from pathlib import Path
import platform
import shutil
import signal
import subprocess
import sys
import time

from native_validation import NativeValidator
from verify_26_round_trip import await_ready, capture, compare, scene, write_json
from test_engine_api import png_pixels
from test_native_scene_cli import require
from test_scene_recipe import OwnedRenderer, collect_cleanup, free_udp_port


SHOW = b"""#version 330
uniform vec2 resolution;
uniform float time;
out vec4 fragColor;
void main() {
    vec2 uv = gl_FragCoord.xy / resolution;
    fragColor = vec4(0.13 + uv.x * 0.45, 0.26 + uv.y * 0.30, 0.49 + 0.04 * sin(time), 1.0);
}
"""
CANDIDATE = b"""#version 330
uniform vec2 resolution;
out vec4 fragColor;
void main() {
    fragColor = vec4(gl_FragCoord.xy / resolution, 0.22, 1.0);
}
"""
FOLLOWING = b"""#version 330
uniform vec2 resolution;
out vec4 fragColor;
void main() {
    fragColor = vec4(0.25, 1.0 - gl_FragCoord.y / resolution.y, 0.79, 1.0);
}
"""
BROKEN = b"""#version 330
out vec4 fragColor;
void main() { fragColor = IR08_UNDECLARED; }
"""


def observe_network(pid):
    lsof = shutil.which("lsof")
    require(lsof is not None, "IR-08 requires lsof to inspect the actual listeners")
    command = [lsof, "-nP", "-a", "-p", str(pid), "-i"]
    result = subprocess.run(command, capture_output=True, text=True, timeout=5)
    require(result.returncode in (0, 1) and not result.stderr,
            f"could not inspect renderer network sockets: {result.stderr}")
    return {"command": command, "exit_status": result.returncode, "stdout": result.stdout}


def verify_private_child(binary, owner):
    root = Path(owner["data_root"])
    socket = Path(owner["control_socket"])
    log = Path(owner["log"]).read_text(encoding="utf-8", errors="replace")
    require(owner["arguments"][0] == str(binary)
            and f"VOSC_DRAFT_DATA_ROOT {root.resolve()}" in log.splitlines()
            and not any(line.startswith("VOSC_DATA_ROOT ") for line in log.splitlines()),
            "draft must launch the exact binary and select only its private data root")
    require(root.stat().st_mode & 0o077 == 0
            and socket.parent.stat().st_mode & 0o077 == 0
            and socket.stat().st_mode & 0o077 == 0,
            "draft data or control socket permits group or other access")
    listeners = observe_network(owner["pid"])
    require(listeners["exit_status"] == 1 and not listeners["stdout"],
            f"draft unexpectedly owns a network socket: {listeners}")
    return {"data_root_marker": f"VOSC_DRAFT_DATA_ROOT {root.resolve()}", "listeners": listeners}


def validate(binary, output, evidence):
    manifest = scene(output, "independent-output", SHOW)
    show = OwnedRenderer(binary, output, "scheduled-output", free_udp_port(), [],
                         arguments=("--scene", manifest))
    observations = evidence["scheduled_output"] = {"owner": show.owner, "observations": {}}
    try:
        ready_frame = await_ready(show, json.loads(manifest.read_text(encoding="utf-8"))["title"])
        observations["ready_frame"] = ready_frame
        sockets = observations["network"] = observe_network(show.process.pid)
        require(sockets["exit_status"] == 0 and f"UDP *:{show.port}" in sockets["stdout"],
                f"ordinary scheduled output lost its general OSC listener: {sockets}")
        with closing(show.client()) as client, NativeValidator(binary) as validator:
            def observe_output(label):
                frame, observation = capture(client, output / f"show-{label}.png", ready_frame)
                observations["observations"][label] = observation
                return frame, observation["before"]

            def observe_candidate(path, candidate):
                result = validator.capture(path, expected_source_sha256=candidate["source_sha256"])
                require(result["ok"], result)
                return result, png_pixels(path.read_bytes(), include_alpha=True)

            baseline_frame, baseline_state = observe_output("baseline")
            require(observations["observations"]["baseline"]["interior_luminance"]["range"] >= 16,
                    "scheduled output lacked a visible independent image")

            candidate = evidence["link"] = validator.validate(CANDIDATE)
            require(candidate["ok"], candidate)
            process = validator.process
            owner = validator.owner
            require(owner["pid"] != show.process.pid and owner["control_socket"] != str(show.socket)
                    and Path(owner["data_root"]).resolve() != (binary.parents[3] / "data").resolve(),
                    "validation process, socket and data root must be independent")
            evidence["private_child"] = verify_private_child(binary, owner)
            preview, candidate_frame = observe_candidate(output / "candidate.png", candidate)
            evidence["capture"] = preview

            failed = evidence["failed_compile"] = validator.validate(BROKEN)
            require(not failed["ok"] and failed["code"] == "shader_failed" and any(
                "IR08_UNDECLARED" in entry["message"] for entry in failed["receipt"]["error"]["diagnostics"]),
                f"expected request-correlated native driver compile diagnostics: {failed}")
            require(failed["receipt"]["revision"] == candidate["receipt"]["revision"]
                    and validator.process is process and process.poll() is None,
                    "a compile error replaced the working revision or lost the validation renderer")
            require(validator.capture(output / "stale.png")["code"] == "no_active_candidate",
                    "a failed candidate inherited a prior image")

            failed_frame, failed_state = observe_output("after-compile-failure")
            started = time.monotonic()
            process.send_signal(signal.SIGSTOP)
            process_status = subprocess.check_output(["ps", "-p", str(process.pid), "-o", "stat="],
                                                     text=True).strip()
            require("T" in process_status, f"owned validation renderer did not stop: {process_status!r}")
            with ThreadPoolExecutor(max_workers=1) as pool:
                pending = pool.submit(validator.validate, FOLLOWING)
                stalled_frame, stalled_state = observe_output("during-validation-stall")
                require(not pending.done() and process.poll() is None, "show frame did not land during validation wait")
                timeout = pending.result(timeout=5)
            elapsed = time.monotonic() - started
            evidence["timed_out_validation"] = {
                "signal": "SIGSTOP", "stopped_process_status": process_status,
                "simulated_fault": "process stop at native validation request boundary",
                "elapsed_seconds": elapsed, "deadline_seconds": validator.timeout, **timeout,
            }
            require(not timeout["ok"] and timeout["code"] == "validation_timeout"
                    and timeout["cleanup"]["pid"] == owner["pid"] and timeout["cleanup"]["forced"]
                    and timeout["cleanup"]["socket_removed"] and process.poll() == -signal.SIGKILL
                    and validator.timeout <= elapsed < 5,
                    f"owned stalled renderer was not recovered within the deadline: {timeout}")
            require(validator.capture(output / "stale.png")["code"] == "no_active_candidate",
                    "a stopped validation process retained an active image")

            recovered = evidence["recovered"] = validator.validate(FOLLOWING)
            require(recovered["ok"] and recovered["owner"]["epoch"] > owner["epoch"]
                    and recovered["owner"]["pid"] != owner["pid"]
                    and recovered["owner"]["control_socket"] != owner["control_socket"],
                    f"next valid shader did not render in a fresh validation process: {recovered}")
            evidence["recovered_private_child"] = verify_private_child(binary, recovered["owner"])
            preview, following_frame = observe_candidate(output / "recovered.png", recovered)
            evidence["recovered_capture"] = preview
            recovery_difference = compare(candidate_frame, following_frame)
            require(recovery_difference["pixels_changed_by_at_least_16"] > 0,
                    "recovered candidate did not produce its own native image")
            evidence["recovery_image_difference"] = recovery_difference
            recovered_frame, recovered_state = observe_output("after-validation-recovery")

            previous_state = baseline_state
            for frame, state in ((failed_frame, failed_state), (stalled_frame, stalled_state),
                                 (recovered_frame, recovered_state)):
                require(state["frame"] > previous_state["frame"], "scheduled output stopped drawing")
                require(all(state[key] == baseline_state[key] for key in
                            ("layers", "revision", "rendered_revision", "external_epoch")),
                        "scheduled output state changed through isolated validation")
                comparison = compare(baseline_frame, frame)
                require(comparison["max_channel_error"] == 0, f"scheduled output pixels changed: {comparison}")
                previous_state = state
    finally:
        cleanup, error = collect_cleanup(show)
        observations["cleanup"] = cleanup
        ordinary_exit = cleanup["return_code"] == signal.SIGTERM and cleanup["socket_removed"] and (
            cleanup["identity"] == str(binary) and not cleanup["forced_kill"] and
            cleanup["cleanup_errors"] == [f"owned renderer exited nonzero: {signal.SIGTERM.value}"])
        require(not error or ordinary_exit, f"scheduled output cleanup failed: {error}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path, help="new evidence directory")
    arguments = parser.parse_args()
    output = arguments.output.resolve()
    output.mkdir(mode=0o700, parents=True)
    evidence = {"command": [sys.executable, *sys.argv], "platform": platform.platform(), "IR-08": "open"}
    try:
        validate(arguments.binary.resolve(strict=True), output, evidence)
        evidence["IR-08"] = "pass"
    except Exception as error:
        evidence["error"] = str(error)
        raise
    finally:
        write_json(output / "report.json", evidence)
    print(output / "report.json")


if __name__ == "__main__":
    main()
