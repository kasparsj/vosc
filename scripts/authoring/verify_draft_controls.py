#!/usr/bin/env python3
"""Check native draft bindings, OSC samples and independent show isolation."""

from __future__ import annotations

import argparse
import base64
from contextlib import ExitStack, closing
import hashlib
import json
import math
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import uuid

from native_validation import MAX_SOURCE_BYTES, NativeValidator, stage_validation_assets
from verify_26_round_trip import await_ready, compare, scene, write_json
from test_engine_api import png_pixels
from test_native_scene_cli import read_state, require
from test_scene_recipe import OwnedRenderer, capture, collect_cleanup, free_udp_port


FIXTURE = b"""#version 330
uniform vec2 resolution;
uniform float gain = 0.6;
uniform vec3 tone = vec3(0.2, 0.5, 0.8);
out vec4 fragColor;
void main() {
    float x = 0.28 + 0.72 * gl_FragCoord.x / resolution.x;
    fragColor = vec4(tone * gain * x, 1.0);
}
"""


def own(stack, result, binary, output, name, options):
    renderer = OwnedRenderer(binary, output, name, free_udp_port(), [], arguments=options)
    node = result[name] = {"owner": renderer.owner}
    def finish():
        receipt, error = collect_cleanup(renderer)
        node["cleanup"] = receipt
        require(not error or (receipt["return_code"] == signal.SIGTERM and receipt["socket_removed"]
                             and not receipt["forced_kill"]), f"{name} did not close cleanly: {error}")
    stack.callback(finish)
    return renderer, stack.enter_context(closing(renderer.client()))


def update(client, method, params):
    initial = client.call(method, params)
    result = client.wait(initial["id"]) if initial["ok"] and initial["status"] in {"queued", "applied"} else initial
    require(result["ok"] and result["status"] == "rendered", result)
    return result


def observe(client, label, output, records):
    width, height, pixels, receipt = capture(client, output / f"{label}.png", max_width=320)
    receipt.pop("data_base64", None)
    state = read_state(client)
    records[label] = {"capture": receipt, "state": state}
    return width, height, pixels


def value(state, name):
    return json.loads(state["layers"][0]["shader"]["uniforms"][name]["rendered_value"])


def capture_mutation_frame(renderer, client, capture_clients, method, params, revision, path):
    # The server services one request per connection per update. Schedule two
    # independent capture requests while the owned renderer is paused. Even
    # when the pause falls inside socket service, one captures the mutation
    # frame and the other is an adjacent frame.
    calls = [(client, method, params)] + [
        (capture_client, "capture_preview", {"max_width": 320, "max_readback_pixels": 320 * 240})
        for capture_client in capture_clients
    ]
    requests = [{"id": uuid.uuid4().hex, "method": name, "params": values} for _, name, values in calls]
    renderer.send_signal(signal.SIGSTOP)
    try:
        _, stopped = os.waitpid(renderer.process.pid, os.WUNTRACED)
        require(os.WIFSTOPPED(stopped), "Owned renderer exited before frame capture")
        for (target, _, _), request in zip(calls, requests, strict=True):
            target.socket.sendall(json.dumps(request).encode() + b"\n")
    finally:
        renderer.send_signal(signal.SIGCONT)
    for (target, _, _), request in zip(calls, requests, strict=True):
        admitted = json.loads(target.stream.readline())
        require(admitted["ok"] and admitted["id"] == request["id"], admitted)
    drawn = client.wait(requests[0]["id"])
    require(drawn["ok"] and drawn["status"] == "rendered" and drawn["revision"] == revision, drawn)
    captures = [target.wait(request["id"]) for (target, _, _), request in
                zip(calls[1:], requests[1:], strict=True)]
    matching = [item for item in captures if item["ok"] and item["status"] == "captured"
                and item["frame"] == drawn["frame"] and item["revision"] == drawn["revision"]]
    require(len(matching) == 1, {"draw_frame": drawn["frame"],
                                  "capture_frames": [item.get("frame") for item in captures]})
    exact = matching[0]
    data = base64.b64decode(exact["result"].pop("data_base64"), validate=True)
    path.write_bytes(data)
    return drawn, exact, png_pixels(data, include_alpha=True)


def check_oracle(frame, offset, gain):
    width, height, pixels = frame
    x = width // 2
    shading = offset + 0.72 * (x + 0.5) / width
    expected = [round(channel * gain * shading * 255) for channel in (0.2, 0.723456789, 0.8)] + [255]
    observed = pixels[(height // 2) * width + x]
    require(all(abs(a - b) <= 2 for a, b in zip(observed, expected, strict=True)),
            {"expected": expected, "observed": observed})
    return {"expected": expected, "observed": observed, "tolerance_per_channel": 2}


def cadence(client):
    start = time.monotonic()
    first = read_state(client)["frame"]
    time.sleep(1.2)
    final = read_state(client)["frame"]
    elapsed = time.monotonic() - start
    performance = client.call("get_performance")["result"]
    return {"elapsed_seconds": elapsed, "frames": final - first, "observed_fps": (final - first) / elapsed,
            "renderer_performance": performance}


def linux_network_sockets(renderer):
    renderer.assert_linux_identity()
    proc = Path("/proc") / str(renderer.process.pid)
    owned_inodes = set()
    for descriptor in (proc / "fd").iterdir():
        target = os.readlink(descriptor)
        if target.startswith("socket:["):
            owned_inodes.add(target[8:-1])
    sockets = []
    for protocol in ("tcp", "tcp6", "udp", "udp6"):
        rows = (proc / "net" / protocol).read_text().splitlines()
        for row in rows[1:]:
            fields = row.split()
            if fields[9] in owned_inodes:
                sockets.append({"protocol": protocol, "local_address": fields[1],
                                "local_port": int(fields[1].rsplit(":", 1)[1], 16),
                                "state": fields[3], "inode": int(fields[9])})
    renderer.assert_linux_identity()
    return sockets


def verify(args, records):
    output = args.output.resolve()
    binary = args.binary.resolve(strict=True)
    show_manifest = scene(output, "scheduled-output", FIXTURE)
    with tempfile.TemporaryDirectory(prefix="vosc-draft-control-", dir="/tmp") as disposable, ExitStack() as stack:
        stock = Path(disposable) / "data"
        records["vertex_assets"] = stage_validation_assets(binary, stock)
        show, show_client = own(stack, records, binary, output, "show", ["--scene", show_manifest])
        await_ready(show, "26.frag — scheduled-output")
        show_first = observe(show_client, "show-first", output, records)
        records["show_baseline_cadence"] = cadence(show_client)
        draft, client = own(stack, records, binary, output, "draft",
                            ["--draft-data-root", stock, "--draft-preview", "--size", "320x240", "--fps", 30])
        capture_clients = [stack.enter_context(closing(draft.client())) for _ in range(2)]
        if sys.platform == "linux":
            show_sockets = linux_network_sockets(show)
            require(any(entry["protocol"] in ("udp", "udp6") and entry["local_port"] == show.port
                        for entry in show_sockets), f"Independent show OSC socket was not attributed: {show_sockets}")
            records["show"]["network"] = show_sockets
            draft_sockets = linux_network_sockets(draft)
            require(not draft_sockets, f"Unexpected private draft TCP/UDP socket: {draft_sockets}")
            records["draft"]["network"] = draft_sockets
        else:
            listeners = subprocess.run(["/usr/sbin/lsof", "-nP", "-a", "-p", str(draft.process.pid), "-i"],
                                       text=True, capture_output=True, check=False)
            require(listeners.returncode == 1 and not listeners.stdout, f"Unexpected private draft listener: {listeners.stdout}")
            records["draft"]["network"] = listeners.stdout
        slots = {"sTest": 0.1}
        bindings = {
            "gain": {"type": "float", "components": [{"slot": "sTest", "low": 0.2, "high": 0.8}]},
            "tone": {"type": "vec3", "components": [{"fixed": n} for n in (0.2, 0.723456789, 0.8)]}
        }
        initial = update(client, "configure_layers", {"count": 1, "layout": "stack"})

        def source_digest(source):
            return hashlib.sha256(source).hexdigest()

        def prepare(target, label, source, lease, revision, values=bindings, candidate_slots=slots):
            request = {"source_base64": base64.b64encode(source).decode("ascii"),
                       "source_digest": source_digest(source), "lease": lease, "token": label,
                       "revision": revision, "slots": candidate_slots, "bindings": values}
            return target.call("draft_prepare", request)

        def commit_params(candidate, lease, revision, candidate_slots=slots):
            controls = json.dumps({"slots": candidate_slots, "bindings": candidate["result"]["bindings"]}, sort_keys=True)
            return {"token": candidate["result"]["token"], "lease": lease, "revision": revision,
                    "control_digest": source_digest(controls.encode())}

        def commit(candidate, lease, revision, source, offset, gain, candidate_slots=slots):
            require(candidate["ok"] and candidate["status"] == "prepared", candidate)
            params = commit_params(candidate, lease, revision, candidate_slots)
            token = params["token"]
            drawn, exact, pixels = capture_mutation_frame(
                draft, client, capture_clients, "draft_commit", params, revision + 1, output / f"{token}-first.png"
            )
            result = drawn["result"]
            require(result["commit_id"] == drawn["id"] and result["source_digest"] == source_digest(source)
                    and result["control_digest"] == params["control_digest"] and result["scene"]["frame"] == drawn["frame"]
                    and result["scene"]["rendered_revision"] == drawn["revision"], drawn)
            records[f"{token}_first_pixels"] = {"capture": exact, "oracle": check_oracle(pixels, offset, gain)}
            return drawn

        def sample(target, lease, revision, sequence, amplitude, slot="sTest"):
            return target.call("draft_sample", {"slot": slot, "value": amplitude, "lease": lease,
                                                "sequence": sequence, "revision": revision})

        def confirm_sample(label, lease, revision, sequence, amplitude, expected, offset=0.28):
            receipt, exact, pixels = capture_mutation_frame(
                draft, client, capture_clients, "draft_sample",
                {"slot": "sTest", "value": amplitude, "lease": lease, "sequence": sequence, "revision": revision},
                revision, output / f"{label}.png"
            )
            require(receipt["ok"] and receipt["status"] == "rendered" and receipt["revision"] == revision
                    and receipt["result"]["sequence"] == sequence, receipt)
            state = receipt["result"]["scene"]
            require(state["frame"] == receipt["frame"] and state["draft_slots"]["sTest"] == amplitude
                    and math.isclose(value(state, "gain"), expected, abs_tol=1e-6), state)
            records[label + "_sample"] = receipt
            records[label + "_pixels"] = {"capture": exact, "oracle": check_oracle(pixels, offset, expected)}
            return pixels

        denied_methods = [
            ("draft_prepare", {"source_base64": base64.b64encode(FIXTURE).decode("ascii"),
                               "source_digest": source_digest(FIXTURE), "lease": "other",
                               "token": "x", "revision": 0, "slots": {}, "bindings": {}}),
            ("draft_commit", {"lease": "other", "token": "x", "revision": 0, "control_digest": "0" * 64}),
            ("draft_fence", {"lease": "other"}),
            ("draft_cancel", {"lease": "other", "token": "x"}),
            ("draft_sample", {"lease": "other", "revision": 0, "sequence": 1, "slot": "sTest", "value": 1}),
            ("draft_configure_slot", {"slot": "sTest", "initial": 0, "lease": "other", "revision": 0}),
            ("draft_bind_uniform", {"layer": 0, "name": "gain", "components": [{"fixed": 1}],
                                    "lease": "other", "revision": 0})
        ]
        for method, params in denied_methods:
            rejected = show_client.call(method, params)
            require(not rejected["ok"] and rejected["error"]["code"] == "unknown_method", rejected)
        source_payload = base64.b64encode(FIXTURE).decode("ascii")
        rejected = show_client.call("publish_shader", {"layer": 0, "source_base64": source_payload})
        require(not rejected["ok"] and rejected["error"]["code"] == "invalid_params", rejected)

        records["prepare_A"] = prepare(client, "A", FIXTURE, "original", initial["revision"])
        require(read_state(client)["revision"] == initial["revision"], "Prepare mutated the ongoing scene")
        accepted_a = records["commit_A"] = commit(records["prepare_A"], "original", initial["revision"], FIXTURE, 0.28, 0.26)
        revision_a = accepted_a["revision"]
        require(math.isclose(value(accepted_a["result"]["scene"], "gain"), 0.26, abs_tol=1e-6), accepted_a)
        frames = [confirm_sample(label, "original", revision_a, i, amplitude, expected)
                  for i, (label, amplitude, expected) in enumerate(
                      (("low", 0.0, 0.2), ("middle", 0.5, 0.5), ("high", 1.0, 0.8)), 1)]
        require(all(item["pixels_changed_by_at_least_16"] > 15_000
                    for item in (compare(left, right) for left, right in zip(frames[:-1], frames[1:], strict=True))),
                "Sample endpoints did not change the preview pixels")

        good = FIXTURE.replace(b"0.28", b"0.42")
        broken = good.replace(b"vec4(tone", b"vec4(unknown")
        failed = records["compile_rejected"] = prepare(client, "X", broken, "original", revision_a)
        require(not failed["ok"] and failed["error"]["code"] == "shader_failed" and failed["error"]["diagnostics"],
                failed)
        require(read_state(client)["draft_activation"]["token"] == "A", "Broken source replaced the preview")
        confirm_sample("failed-low", "original", revision_a, 4, 0.0, 0.2)
        records["prepare_B"] = prepare(client, "B", good, "original", revision_a)
        confirm_sample("sample-during-prepare", "original", revision_a, 5, 1.0, 0.8)
        accepted_b = records["commit_B"] = commit(records["prepare_B"], "original", revision_a, good, 0.42, 0.8)
        revision_b = accepted_b["revision"]
        require(math.isclose(value(accepted_b["result"]["scene"], "gain"), 0.8, abs_tol=1e-6)
                and accepted_b["result"]["scene"]["draft_slots"]["sTest"] == 1, accepted_b)
        require(math.isclose(value(accepted_b["result"]["scene"], "tone")[1], 0.723456789, abs_tol=1e-7), accepted_b)
        records["rejected_samples"] = {
            "prior_revision_same_slot": sample(client, "original", revision_a, 6, 0.0),
            "prior_sequence": sample(client, "original", revision_b, 5, 0.0),
            "unknown_slot": sample(client, "original", revision_b, 6, 0.0, "unknown"),
        }
        for stale in records["rejected_samples"].values():
            require(not stale["ok"], stale)
        require(read_state(client)["draft_slots"]["sTest"] == 1, "Stale or unconfigured sample changed the active slot")
        confirm_sample("recovered-low", "original", revision_b, 6, 0.0, 0.2, 0.42)
        confirm_sample("recovered-high", "original", revision_b, 7, 1.0, 0.8, 0.42)

        retyped = FIXTURE.replace(b"uniform float gain = 0.6;", b"uniform vec2 gain = vec2(0.4, 0.7);").replace(b"tone * gain *", b"tone * gain.x *")
        prepared = records["retyped"] = prepare(client, "retyped", retyped, "original", revision_b)
        issue = prepared["result"]["conflicts"][0]
        require(issue["reason"] == "retyped" and issue["actual_type"] == "vec2" and
                math.isclose(issue["default_values"][0], 0.4, abs_tol=1e-6), prepared)
        cancelled = client.call("draft_cancel", {"lease": "original", "token": "retyped"})
        require(cancelled["ok"] and read_state(client)["draft_activation"]["token"] == "B", cancelled)
        old = client.call("draft_commit", commit_params(prepared, "original", revision_b))
        if old["status"] == "queued": old = client.wait(old["id"])
        require(not old["ok"] and old["error"]["code"] == "stale_candidate", old)

        without_gain = FIXTURE.replace(b"uniform float gain = 0.6;\n", b"").replace(b"tone * gain *", b"tone *")
        inactive = records["inactive"] = prepare(client, "inactive", without_gain, "original", revision_b)
        require(inactive["ok"] and inactive["result"]["conflicts"] == [
            {"name": "gain", "previous_type": "float", "reason": "inactive"}], inactive)
        owned = {**bindings, "resolution": {"type": "vec2", "components": [{"fixed": 4}, {"fixed": 4}]}}
        managed = records["renderer_owned"] = prepare(client, "managed", FIXTURE, "original", revision_b, owned)
        require(managed["ok"] and managed["result"]["conflicts"] == [
            {"name": "resolution", "previous_type": "vec2", "actual_type": "vec2", "reason": "renderer_owned"}],
            managed)

        if args.recovery_hooks:
            reject = prepare(client, "test-fail-first-draft-draw", FIXTURE, "original", revision_b)
            rolled = client.call("draft_commit", commit_params(reject, "original", revision_b))
            if rolled["status"] in ("queued", "applied"): rolled = client.wait(rolled["id"])
            records["rollback"] = rolled
            require(not rolled["ok"] and rolled["error"]["code"] == "draft_rolled_back"
                    and rolled["result"]["restored_previous"], rolled)
            baseline = rolled["result"]["scene"]
            require(baseline["frame"] == rolled["frame"] and baseline["rendered_revision"] == revision_b
                    and baseline["draft_activation"]["commit_id"] == accepted_b["id"] and baseline["draft_slots"]["sTest"] == 1
                    and math.isclose(value(baseline, "gain"), 0.8, abs_tol=1e-6)
                    and math.isclose(value(baseline, "tone")[1], 0.723456789, abs_tol=1e-7), rolled)
            confirm_sample("rolled-back-low", "original", revision_b, 8, 0.0, 0.2, 0.42)
            confirm_sample("rolled-back-high", "original", revision_b, 9, 1.0, 0.8, 0.42)

        before_fence = prepare(client, "fenced", good, "original", revision_b)
        require(client.call("draft_fence", {"lease": "original"})["ok"], "Lease fence rejected")
        fenced = client.call("draft_commit", commit_params(before_fence, "original", revision_b))
        if fenced["status"] == "queued": fenced = client.wait(fenced["id"])
        records["fence_before_commit"] = fenced
        require(not fenced["ok"] and fenced["error"]["code"] == "stale_lease"
                and read_state(client)["draft_activation"]["token"] == "B", fenced)
        fresh = prepare(client, "fresh", good, "new-document", revision_b)
        accepted_new = records["new_lease"] = commit(fresh, "new-document", revision_b, good, 0.42, 0.26)
        require(math.isclose(accepted_new["result"]["scene"]["draft_slots"]["sTest"], 0.1, abs_tol=1e-6),
                "A different document inherited the previous sample")

        new_revision = accepted_new["revision"]
        slot_params = {"slot": "sPrecision", "initial": 0.5, "lease": "new-document", "revision": new_revision}
        records["stale_typed"] = {
            "foreign_lease": client.call("draft_configure_slot", {**slot_params, "lease": "foreign"}),
            "prior_revision": client.call("draft_configure_slot", {**slot_params, "revision": new_revision - 1}),
        }
        for response, expected in zip(records["stale_typed"].values(), ("stale_lease", "stale_revision"), strict=True):
            require(not response["ok"] and response["error"]["code"] == expected, response)

        slot = records["configured_slot"] = update(client, "draft_configure_slot", slot_params)
        require(slot["result"]["lease"] == "new-document" and slot["result"]["scene"]["frame"] == slot["frame"]
                and slot["result"]["scene"]["draft_slots"]["sPrecision"] == 0.5, slot)
        precision = {"slot": "sPrecision", "low": 999999.99, "high": -999999.98}
        bind = records["typed_range"] = update(client, "draft_bind_uniform", {
            "layer": 0, "name": "gain", "components": [precision], "lease": "new-document", "revision": slot["revision"]
        })
        typed_scene = bind["result"]["scene"]
        require(bind["result"]["lease"] == "new-document" and typed_scene["frame"] == bind["frame"]
                and typed_scene["rendered_revision"] == bind["revision"] and value(typed_scene, "gain") == 0.0, bind)

        # Quotes make the plain JSON request exceed the socket's 256 KiB limit.
        padding = (b'"' * 255 + b'\n') * 512
        prefix = good + b"/*\n"
        max_source = prefix + padding[:MAX_SOURCE_BYTES - len(prefix) - 2] + b"*/"
        extended_slots = {**slots, "sPrecision": 0.5}
        extended_bindings = {**bindings, "gain": {"type": "float", "components": [precision]}}
        rejected = records["over_limit"] = prepare(client, "too-long", max_source + b"\n", "new-document", bind["revision"])
        require(not rejected["ok"] and rejected["error"]["code"] == "invalid_params", rejected)
        range_candidate = prepare(client, "precision", max_source, "new-document", bind["revision"],
                                  extended_bindings, extended_slots)
        drawn = records["commit_precision"] = commit(range_candidate, "new-document", bind["revision"], max_source,
                                                     0.42, 0, extended_slots)
        range_scene = drawn["result"]["scene"]
        require(range_scene["draft_slots"]["sPrecision"] == 0.5 and value(range_scene, "gain") == 0.0, drawn)

        # Churn real retained, unrelated operations past the native 64-entry LRU.
        for index in range(70):
            churn = client.call("draft_cancel", {"lease": "new-document", "token": f"irrelevant-{index}"})
            require(churn["ok"], churn)
        for expected in (drawn, bind):
            retained = client.call("get_request", {"request_id": expected["id"]})
            require(retained["ok"] and retained["result"] == expected, retained)
        original_params = commit_params(range_candidate, "new-document", bind["revision"], extended_slots)
        replay = records["commit_replayed_after_churn"] = client.call("draft_commit", original_params, drawn["id"])
        require(replay == drawn, replay)
        conflict = records["replay_conflict"] = client.call(
            "draft_commit", {**original_params, "token": "different"}, drawn["id"]
        )
        require(not conflict["ok"] and conflict["error"]["code"] == "id_conflict", conflict)
        late = records["fence_after_commit"] = client.call("draft_fence", {"lease": "new-document"})
        fenced_slot = records["fence_after_typed"] = client.call(
            "draft_configure_slot", {**slot_params, "slot": "sBlocked", "revision": drawn["revision"]}
        )
        require(late["ok"] and not fenced_slot["ok"] and fenced_slot["error"]["code"] == "stale_lease"
                and read_state(client)["draft_activation"]["token"] == "precision", "A late fence changed the drawn scene")

        with NativeValidator(binary) as validator:
            checked = records["private_validation_128k"] = validator.validate(max_source)
            require(checked["ok"], checked)
        # The show stays idle during the private draft journey. Its local
        # client can expire independently of the running show renderer.
        with closing(show.client()) as final_show_client:
            show_last = observe(final_show_client, "show-last", output, records)
        first, last = (records[name]["state"] for name in ("show-first", "show-last"))
        require(compare(show_first, show_last)["max_channel_error"] == 0 and last["frame"] > first["frame"]
                and all(first[key] == last[key] for key in ("layers", "revision", "external_epoch")),
                "Independent show was changed")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--recovery-hooks", action="store_true", help="Exercise only on the recovery-enabled renderer")
    args = parser.parse_args()
    args.output.mkdir(parents=True)
    result = {}
    try:
        verify(args, result)
        result["status"] = "pass"
    except Exception as error:
        result["status"] = "fail"
        result["error"] = str(error)
        raise
    finally:
        write_json(args.output / "report.json", result)
