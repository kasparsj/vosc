#!/usr/bin/env python3
"""Owned live feedback trial checks through the native socket, MCP and sclang."""
from __future__ import annotations

import argparse
import asyncio
import base64
import hashlib
import json
import math
import os
from pathlib import Path
import re
import socket
import subprocess
import time
import uuid

from mcp import Client as McpClient
from mcp.client.stdio import StdioServerParameters
from mcp.types import ImageContent

from test_feedback_composition import FIXTURES, export_folder, replay, setup_commands, updates
from test_scene_recipe import (
    ROOT, OwnedRenderer, arm_recovery_hook, collect_cleanup, encode_message, free_udp_port,
    osc, receive_results, run_sclang, wait_recovery_hook,
)
from test_engine_api import png_pixels
from visuals_quark import class_config, source_identity

TARGET = "default_feedback_texture"
STEP = 5
WHITE = (255, 255, 255)
BASELINE = (FIXTURES / "temporal.frag").read_text()
CANDIDATE = BASELINE.replace("decay*previous.r,max(0.0,previous.g-1.0/16.0),.25",
                             "decay*previous.r+.12,.92*previous.g+.07,.85")
DEMO = (FIXTURES / "demo.frag").read_text()
DEMO_CANDIDATE = DEMO.replace("vec3 seed=vec3(.5+.5*sin(v)", "vec3 seed=vec3(.65+.35*sin(v)")


def process_identity(pid):
    return subprocess.check_output(["ps", "-p", str(pid), "-o", "pid=,lstart=,command="],
                                   text=True).strip()


class Sender:
    def __init__(self, args, renderer, out):
        self.log = out / "sender.log"
        self.port = free_udp_port()
        assert self.port != renderer.port and renderer.port != 33333
        directory = out / "sender"
        config = class_config(directory, args.visuals_quark)
        runner = directory / "runner.scd"
        runner.write_text(f'''(
var target = NetAddr("127.0.0.1", {renderer.port});
["VOSC_QUARK_SOURCE", OSCVisuals.filenameSymbol.asString].postln;
Routine {{
    2400.do {{ |index|
        var amp = if((index div: 20).even, {{ 0.2 }}, {{ 0.8 }});
        target.sendMsg('/input/data', 0, amp.asFloat, 0.0, 0);
        Post << "SEND " << index << " " << Main.elapsedTime << " " << amp << Char.nl;
        0.05.wait;
    }};
    0.exit;
}}.play(SystemClock);
)
''')
        self.command = [str(args.sclang), "-a", "-D", "-u", str(self.port), "-i", "none",
                        "-l", str(config), str(runner)]
        env = {**os.environ, "XDG_CONFIG_HOME": str(directory / "config"),
               "XDG_DATA_HOME": str(directory / "data")}
        with self.log.open("wb") as stream:
            self.process = subprocess.Popen(self.command, env=env, stdout=stream, stderr=subprocess.STDOUT)
        self.identity = process_identity(self.process.pid)

    def trace(self):
        return [(int(index), float(stamp), float(amp)) for index, stamp, amp in
                re.findall(r"^SEND (\d+) ([\d.]+) ([\d.]+)$", self.log.read_text(), re.M)]

    def stop(self):
        live = self.process.poll() is None
        if live:
            self.process.terminate()
            try:
                self.process.wait(timeout=4)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=4)
        return {"command": self.command, "identity": self.identity, "pid": self.process.pid,
                "active_at_teardown": live, "exit": self.process.returncode, "send_trace": self.trace()}


def dump(path, data):
    path.write_text(json.dumps(data, indent=2) + "\n")


def hook(renderer, operation, **params):
    command = {"id": uuid.uuid4().hex, "operation": operation, **params}
    return wait_recovery_hook(renderer, command, arm_recovery_hook(renderer, command))


def query(client, method, **params):
    result = client.call(method, params)
    assert result["ok"], result
    return result["result"]


def err(reply, code):
    assert not reply["ok"] and reply["error"]["code"] == code, reply
    return reply


def until(probe, predicate, timeout=4):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = probe()
        if predicate(value):
            return value
        time.sleep(.014)
    raise AssertionError(f"expected condition was not observed: {str(value)[:1800]}")


def mixed_commands(source):
    commands = setup_commands(source, continuous=True, gradient=True)
    commands[1:1] = [("/input", (0, "", 0, 256)), ("/input/set", (0, "maxAmp", 1.)),
                     ("/var", ("base", .25)), ("/var", ("mod", 0.)),
                     ("/var", ("mod", "base+in_0*.5"))]
    return [(address, (1, "mod") if address == "/layer/alpha" else args) for address, args in commands]


def step(rgb, candidate=False):
    r, g, _ = rgb
    values = (r*.5 + .12*255, g*.92 + .07*255, .85*255) if candidate else (
        r*.5, max(0, g-255/16), .25*255)
    return tuple(round(channel) for channel in values)


def recurrence(seed, count, candidate=False):
    for _ in range(count):
        seed = step(seed, candidate)
    return seed


def blend_oracle(png, rgb, alpha):
    width, height, pixels = png_pixels(png, include_alpha=True)
    assert (width, height) == (800, 600), (width, height)
    bad = maximum = 0
    output_alpha = 255 * (1 - alpha + alpha * alpha)
    for index, actual in enumerate(pixels):
        row, x = divmod(index, width)
        gradient = ((x+.5)/width, .2, (height-row-.5)/height)
        delta = max(abs(actual[c] - ((1-alpha)*rgb[c] + alpha*255*gradient[c])) for c in range(3))
        delta = max(delta, abs(actual[3] - output_alpha))
        maximum = max(maximum, delta)
        bad += delta > 2
    return {"bad_pixels": bad, "maximum_error": maximum, "pixels": width*height, "expected_rgb": rgb}


def compare(png, rgb, alpha):
    result = blend_oracle(png, rgb, alpha)
    assert result["bad_pixels"] == 0, result
    return result


def assert_plateau(scene, alpha):
    assert abs(scene["layers"][1]["alpha"] - alpha) < 1e-5, scene["layers"][1]["alpha"]
    return scene


def arm_capture(renderer, phase, **kwargs):
    hook(renderer, "feedback_trial_configure", capture=phase, **kwargs)


def exact_capture(renderer, out, label):
    capture = until(lambda: hook(renderer, "feedback_trial_capture")["result"], bool)
    path = Path(capture["path"])
    png = path.read_bytes()
    (out / f"{label}.png").write_bytes(png)
    capture["saved"] = str(out / f"{label}.png")
    return capture, png


def params(source=CANDIDATE, budget=8.0):
    return {"layer": 0, "target": TARGET, "source": source, "gpu_budget_ms": budget}


def start(client, source=CANDIDATE, budget=8.0):
    return client.call("trial_shader", params(source, budget))


def retained(client, trial_id):
    return query(client, "get_request", request_id=trial_id)


def terminal(client, trial_id, timeout=4):
    return until(lambda: retained(client, trial_id),
                 lambda r: r["status"] in {"failed", "rendered"}, timeout)


async def preview_cancel(renderer, client, sender, fixture, out, evidence):
    async with McpClient(StdioServerParameters(command=str(ROOT / "mcp/.venv/bin/python"),
                     args=[str(ROOT / "mcp/adapter.py"), "--socket", str(renderer.socket)],
                     cwd=str(ROOT))) as mcp:
        async def tool(name, **args):
            result = await mcp.call_tool(name, args)
            assert not result.is_error, (name, result.structured_content, result.content)
            return result

        capabilities = await tool("get_capabilities")
        assert capabilities.structured_content["engine"]["result"]["features"]["protected_feedback_shader_trials"]
        evidence["capabilities"] = capabilities.structured_content
        hook(renderer, "feedback_configure", enabled=True, reset_events=True)
        arm_capture(renderer, "candidate")
        request = {"id": "numbered-activation", "method": "trial_shader", "params": params()}
        command = {"id": uuid.uuid4().hex, "operation": "dispatch_control", "request": request,
                   "feedback_updates": STEP, "stage": "update"}
        existing = arm_recovery_hook(renderer, command)
        evidence["numbered_setup"] = replay(renderer, mixed_commands(fixture), final_gate=0)
        activation = wait_recovery_hook(renderer, command, existing)["result"]
        evidence["activation"] = activation
        assert activation["status"] == "evaluating", activation
        evaluation = activation["result"]["trial"]
        initial, png = exact_capture(renderer, out, "first-candidate")
        evidence["first_candidate"] = initial
        assert initial["frame"] == evaluation["first_frame"] and initial["baseline_updates"] == STEP
        initial["oracle"] = compare(png, recurrence(recurrence(WHITE, STEP), 1, True), initial["alpha"])
        snapshot = hook(renderer, "feedback_snapshot")["result"]
        evidence["candidate_trace"] = snapshot
        seeded = next(event for event in snapshot["events"] if event["event"] == "trial_seeded")
        assert seeded["baseline_updates"] == STEP

        for alpha in (.35, .65):
            before = until(lambda: query(client, "get_scene"),
                           lambda s: abs(s["layers"][1]["alpha"] - alpha) < 1e-5)
            before_trace = sender.trace()[-1]
            result = await tool("capture_preview", request_id=f"capture-{alpha}", max_width=800,
                                minimum_revision=evaluation["evaluation_revision"], wait_ms=1000)
            after = assert_plateau(query(client, "get_scene"), alpha)
            after_trace = sender.trace()[-1]
            meta = result.structured_content["engine"]
            owner = meta["result"]["feedback_trial"]
            assert owner["trial_id"] == activation["id"] and owner["frame"] == meta["frame"]
            candidate_steps = meta["frame"] - initial["frame"] + 1
            png = base64.b64decode(next(item.data for item in result.content if isinstance(item, ImageContent)))
            (out / f"preview-{alpha}.png").write_bytes(png)
            evidence[f"preview_{alpha}"] = {"receipt": meta, "before": before, "after": after,
                "observed_monotonic": time.monotonic(), "send_bracket": [before_trace, after_trace],
                "oracle": compare(png, recurrence(recurrence(WHITE, STEP), candidate_steps, True), alpha)}

        arm_capture(renderer, "restored")
        result = await tool("cancel_trial", request_id="cancel-first", trial_id=activation["id"], wait_ms=1000)
        evidence["cancel_action"] = result.structured_content
        restored, png = exact_capture(renderer, out, "first-restored")
        evidence["restored"] = restored
        receipt = err(terminal(client, activation["id"]), "trial_cancelled")
        evidence["cancel_trial"] = receipt
        assert restored["frame"] == receipt["result"]["trial"]["rendered_frame"]
        assert restored["updates"] == STEP + 1 and restored["program"] == seeded["baseline_program"]
        assert restored["source_fbo"] == seeded["baseline_destination_fbo"]
        assert restored["destination_fbo"] == seeded["baseline_source_fbo"]
        assert abs(restored["alpha"] - .35) < 1e-5 or abs(restored["alpha"] - .65) < 1e-5
        restored["oracle"] = compare(png, recurrence(WHITE, STEP + 1), restored["alpha"])
        wrong = {"white_seed": step(WHITE), "candidate_seed": step(recurrence(recurrence(WHITE, STEP),
                 restored["frame"] - initial["frame"], True)), "extra_update": recurrence(WHITE, STEP + 2)}
        restored["wrong_history_predictions"] = {
            key: blend_oracle(png, value, restored["alpha"]) for key, value in wrong.items()}
        assert all(value["bad_pixels"] > 0 for value in restored["wrong_history_predictions"].values())
        evidence["restoration_trace"] = hook(renderer, "feedback_snapshot")["result"]
        hook(renderer, "feedback_configure", enabled=False)
        evidence["verdicts"].update({"VAL-FT-01": "pass", "VAL-FT-03": "pass"})


def numbered_start(renderer, fixture, source=CANDIDATE, budget=8.0, *, first_write_fault=False):
    request = {"id": uuid.uuid4().hex, "method": "trial_shader", "params": params(source, budget)}
    command = {"id": uuid.uuid4().hex, "operation": "feedback_trial_dispatch", "request": request,
               "feedback_updates": STEP, "stage": "update", "incomplete_next_destination": first_write_fault}
    existing = arm_recovery_hook(renderer, command)
    setup = replay(renderer, mixed_commands(fixture), final_gate=0)
    return wait_recovery_hook(renderer, command, existing)["result"], setup


def ready(client, trial_id):
    reply = until(lambda: retained(client, trial_id),
                  lambda r: r["status"] == "ready" or not r["ok"], 7)
    assert reply["status"] == "ready", reply
    return reply


def action(client, name, trial_id):
    reply = client.call(name, {"trial_id": trial_id})
    assert reply["ok"], reply
    final = client.wait(reply["id"])
    assert final["status"] == "rendered", final
    return final


def verify_accept(renderer, client, fixture, out, result):
    receipt, result["setup"] = numbered_start(renderer, fixture)
    result["start"] = receipt
    assert receipt["status"] == "evaluating", receipt
    trial_id = receipt["id"]
    result["early"] = err(client.call("accept_trial", {"trial_id": trial_id}), "trial_not_ready")
    result["provisional_export"] = err(client.call("export_scene", {}), "trial_busy")
    first = client.call("get_performance", {})
    admitted = ready(client, trial_id)
    second = client.call("get_performance", {})
    initial = first["result"]["feedback_measurement"]
    measured = second["result"]["feedback_measurement"]
    meta = admitted["result"]["trial"]
    result["provisional_measurement"] = {"early": first, "ready": admitted, "later": second}
    assert second["external_epoch"] > first["external_epoch"]
    assert initial["id"] == measured["id"] == meta["measurement_id"]
    assert measured["program"] == meta["candidate_program"]
    assert meta["samples"] >= 30 and meta["pair_coverage"] >= .95
    assert meta["p95_ms"] <= 8 and meta["hard_max_ms"] <= 16
    arm_capture(renderer, "accepted")

    async def accept():
        async with McpClient(StdioServerParameters(command=str(ROOT / "mcp/.venv/bin/python"),
                         args=[str(ROOT / "mcp/adapter.py"), "--socket", str(renderer.socket)],
                         cwd=str(ROOT))) as mcp:
            perf = await mcp.call_tool("get_performance", {})
            assert not perf.is_error, perf.structured_content
            result["mcp_performance"] = perf.structured_content
            ack = await mcp.call_tool("accept_trial", {"trial_id": trial_id,
                "request_id": "accept-numbered", "wait_ms": 1000})
            assert not ack.is_error, ack.structured_content
            result["accept_action"] = ack.structured_content["engine"]

    asyncio.run(accept())
    accepted = terminal(client, trial_id)
    result["accepted"] = accepted
    draw, png = exact_capture(renderer, out, "first-accepted")
    result["accepted_draw"] = draw
    assert draw["frame"] == accepted["result"]["trial"]["rendered_frame"] == result["accept_action"]["frame"]
    assert draw["program"] == meta["candidate_program"] and draw["updates"] > 30
    assert draw["baseline_updates"] == STEP
    assert draw["updates"] == draw["frame"] - meta["first_frame"] + 1
    draw["oracle"] = compare(png, recurrence(recurrence(WHITE, STEP), draw["updates"], True), draw["alpha"])
    capabilities = query(client, "get_capabilities")
    assert "rollback_feedback" not in capabilities["operations"]
    assert "feedback_rollback" not in capabilities["features"]
    assert "feedback_rollback" not in query(client, "get_scene")
    result["removed_rollback"] = err(
        client.call("rollback_feedback", {"rollback_token": "0" * 32}), "unknown_method")
    result["wrong_trial_id"] = err(client.call("accept_trial", {"trial_id": "missing-id"}), "trial_not_found")
    folder = out / "Accepted feedback"
    saved_fixture = fixture.with_suffix(".saved")
    fixture.rename(saved_fixture)
    try:
        result["without_original_file"] = until(lambda: feedback(client),
            lambda state: state["updates"] > draw["updates"] + 2 and state["export_supported"])
        result["export"] = export_folder(renderer, folder)
    finally:
        saved_fixture.rename(fixture)
    exported = folder / "assets" / "layer-0-feedback.frag"
    assert exported.read_bytes() == CANDIDATE.encode()
    result["exported_fragment_sha256"] = hashlib.sha256(exported.read_bytes()).hexdigest()
    return folder


def verify_white_replay(args, out, folder, result):
    renderer = OwnedRenderer(args.binary.resolve(), out, "white-replay", free_udp_port(), [])
    result["replay_owner"] = {**renderer.owner, "process": process_identity(renderer.process.pid)}
    try:
        arm_capture(renderer, "white_replay")
        result["sc_replay"] = run_sclang(args.sclang, folder / "scene.scd", out, renderer.port,
                                          "accepted", args.visuals_quark)
        draw, png = exact_capture(renderer, out, "accepted-replay-white")
        result["white_draw"] = draw
        assert draw["frame"] == int(result["sc_replay"]["terminal"][1]) and draw["updates"] == 1
        draw["oracle"] = compare(png, step(WHITE, True), draw["alpha"])
    finally:
        result["replay_cleanup"], error = collect_cleanup(renderer)
        assert not error, error


def verify_faults(renderer, client, fixture, out, evidence):
    preflight_cases = (
        ("syntax", "#version 330\nsyntax failure;\n", {}, "shader_failed"),
        ("link", """#version 330
in VertexAttrib { vec2 position; vec4 color; vec3 normal; vec2 texcoord; } vertex;
out vec4 fragColor; void main() { fragColor = vec4(vertex.position, 0, 1); }
""", {}, "shader_failed"),
        ("allocation", CANDIDATE, {"fail_next_allocation": True}, "trial_feedback_allocation"),
        ("seed-copy", CANDIDATE, {"fail_next_seed_copy": True}, "trial_feedback_seed_copy"),
    )
    results = evidence["preflight_faults"] = {}
    for label, source, fault, code in preflight_cases:
        result = results[label] = {}
        replay(renderer, mixed_commands(fixture), final_gate=0)
        until(lambda: updates(client), lambda n: n > 3)
        before = query(client, "get_performance")["feedback_measurement"]
        hook(renderer, "feedback_configure", enabled=True, reset_events=True, **fault)
        arm_capture(renderer, "baseline")
        reply = start(client, source)
        result["rejection"] = err(reply, code)
        if code == "shader_failed":
            assert reply["error"]["diagnostics"], reply
        after = query(client, "get_performance")["feedback_measurement"]
        result["baseline_before"] = before
        result["baseline_after"] = after
        assert before["program"] == after["program"] and before["id"] == after["id"]
        draw, png = exact_capture(renderer, out, f"baseline-after-{label}")
        assert draw["program"] == before["program"] and draw["updates"] > 3
        draw["oracle"] = compare(png, recurrence(WHITE, draw["updates"]), draw["alpha"])
        result["baseline_capture"] = draw
        result["trace"] = hook(renderer, "feedback_snapshot")["result"]
    hook(renderer, "feedback_configure", enabled=False)
    retry = start(client)
    evidence["preflight_retry"] = {"start": retry, "ready": ready(client, retry["id"]),
                                  "cancel": action(client, "cancel_trial", retry["id"])}
    evidence["verdicts"]["VAL-FT-04"] = "pass"


def verify_recoverable(renderer, client, fixture, out, evidence):
    cases = evidence["recoverable"] = {}
    for label, code in (("first_write", "trial_feedback_update_failed"), ("low_budget", "trial_budget_exceeded"),
                        ("timer_loss", "trial_gpu_unavailable"), ("abandoned", "trial_expired")):
        arm_capture(renderer, "restored")
        receipt, _ = numbered_start(renderer, fixture, budget=.05 if label == "low_budget" else 8.,
                                    first_write_fault=label == "first_write")
        assert receipt["ok"], receipt
        if label == "timer_loss":
            arm_capture(renderer, "restored", timer_lost=True)
        finished = err(terminal(client, receipt["id"], timeout=13), code)
        restored, png = exact_capture(renderer, out, label)
        meta = finished["result"]["trial"]
        assert restored["frame"] == meta["rendered_frame"] and restored["updates"] == STEP + 1
        restored["oracle"] = compare(png, recurrence(WHITE, STEP + 1), restored["alpha"])
        if label == "first_write":
            assert "candidate_draw_frame" not in meta and meta["rendered_frame"] == meta["first_frame"]
        cases[label] = {"start": receipt, "terminal": finished, "capture": restored}
        if label == "first_write":
            cases[label]["trace"] = hook(renderer, "feedback_snapshot")["result"]
            hook(renderer, "feedback_configure", enabled=False)
        arm_capture(renderer, "")
    stale = start(client)
    ready(client, stale["id"])
    hook(renderer, "feedback_trial_configure", suppress_pairs=True)
    stale_receipt = until(lambda: retained(client, stale["id"]),
                          lambda r: r["status"] == "evaluating" and r["result"]["trial"]["samples"] >= 30)
    cases["stale"] = {"receipt": stale_receipt, "accept": err(client.call("accept_trial", {
        "trial_id": stale["id"]}), "trial_not_ready")}
    hook(renderer, "feedback_trial_configure")
    action(client, "cancel_trial", stale["id"])
    cases["abandoned"]["post_expiry_accept"] = err(client.call("accept_trial",
        {"trial_id": cases["abandoned"]["start"]["id"]}), "trial_not_found")
    retry = start(client)
    cases["retry"] = {"start": retry, "ready": ready(client, retry["id"]),
                      "cancel": action(client, "cancel_trial", retry["id"])}
    evidence["verdicts"]["VAL-FT-05"] = "pass"


def replay_opener(renderer):
    listener = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    listener.bind(("127.0.0.1", 0))
    sender = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    token = str(uuid.uuid4())
    sender.sendto(encode_message("/scene/reset", (token, listener.getsockname()[1], 800, 600)),
                  ("127.0.0.1", renderer.port))
    return sender, listener, token


def feedback(client):
    return query(client, "get_scene")["layers"][0]["shader"]["textures"]["tex"]["feedback"]


def verify_conflicts(renderer, client, fixture, evidence):
    cases = evidence["conflicts"] = {}
    for gated in (False, True):
        replay(renderer, mixed_commands(fixture), final_gate=0)
        osc(renderer.port, "/onset", int(gated))
        until(lambda: query(client, "get_scene"), lambda s: s["onset_gate"] == gated)
        trial = start(client)
        assert trial["ok"], trial
        node = cases["queued" if gated else "texture_var"] = {"start": trial}
        node["api_busy"] = err(client.call("configure_layers", {"count": 1, "layout": "stack"}), "trial_busy")
        sender, listener, token = replay_opener(renderer)
        with sender, listener:
            node["replay_busy"] = receive_results(listener, 1)[0]
        assert node["replay_busy"]["token"] == token and "busy" in node["replay_busy"]["detail"]
        osc(renderer.port, "/tex/var", 0, "decay", .75)
        node["interrupted"] = err(terminal(client, trial["id"]), "trial_conflict")
        assert node["interrupted"]["result"]["trial"]["restored_previous"]
        scene = query(client, "get_scene")
        assert scene["legacy_pending"] == int(gated)
        node["after_edit"] = scene
        if gated:
            assert feedback(client)["controls"]["decay"] == [{"type": "f", "value": .5}]
            osc(renderer.port, "/onset", 0)
            node["after_gate"] = until(lambda: query(client, "get_scene"), lambda s: s["legacy_pending"] == 0)
        assert feedback(client)["controls"]["decay"] == [{"type": "f", "value": .75}], feedback(client)

    replay(renderer, mixed_commands(fixture), final_gate=0)
    trial = start(client)
    assert trial["ok"], trial
    watcher_source = BASELINE.replace(".25,1", ".31,1")
    fixture.write_text(watcher_source)
    try:
        node = cases["watcher"] = {"start": trial, "terminal": err(terminal(client, trial["id"]), "trial_conflict")}
        assert node["terminal"]["result"]["trial"]["restored_previous"]
        node["reloaded"] = until(lambda: feedback(client),
                                 lambda f: f["retained_fragment"] == watcher_source, 6)
    finally:
        fixture.write_text(BASELINE)

    replay(renderer, mixed_commands(fixture), final_gate=0)
    trial = start(client)
    assert trial["ok"], trial
    cases["resize"] = {"action": hook(renderer, "native_resize", width=780, height=580),
                       "terminal": err(terminal(client, trial["id"]), "trial_conflict")}
    assert cases["resize"]["terminal"]["result"]["trial"]["restored_previous"]
    cases["resize"]["scene"] = until(lambda: query(client, "get_scene"),
                                   lambda s: s["width"] == 780 and s["height"] == 580)

    for operation in ("accept_trial", "cancel_trial"):
        replay(renderer, mixed_commands(fixture), final_gate=0)
        trial = start(client)
        ready(client, trial["id"])
        action_id = f"superseded-{operation}"
        event = hook(renderer, "feedback_trial_conflict", stage="draw",
                      action={"id": action_id, "method": operation, "params": {"trial_id": trial["id"]}})
        assert event["result"]["action"]["status"] == "applied", event
        original = err(terminal(client, trial["id"]), "trial_conflict")
        cases[operation] = {"hook": event, "action": err(retained(client, action_id), "trial_conflict"),
                            "original": original}
        assert "rendered_frame" not in original["result"]["trial"]

    replay(renderer, mixed_commands(fixture), final_gate=0)
    trial = start(client)
    assert trial["ok"], trial
    foreign = CANDIDATE.replace(".85,1", ".65,1")
    event = hook(renderer, "feedback_trial_conflict", stage="draw", foreign_source=foreign)
    assert event["result"]["foreign_installed"]
    skipped = err(terminal(client, trial["id"]), "trial_conflict")
    assert not skipped["result"]["trial"]["restored_previous"]
    assert feedback(client)["retained_fragment"] == foreign
    cases["foreign_program"] = {"hook": event, "terminal": skipped, "feedback": feedback(client)}
    evidence["verdicts"]["VAL-FT-06"] = "pass"


def verify_preflight(renderer, client, fixture, evidence):
    out = evidence["preflight"] = {}
    replay(renderer, mixed_commands(fixture), final_gate=0)
    original = query(client, "get_scene")
    out["incompatible_bindings"] = err(start(client, CANDIDATE.replace("uniform float decay;", "")
        .replace("decay*previous.r", ".5*previous.r")), "invalid_uniform")
    out["before"] = original
    out["after_incompatible"] = query(client, "get_scene")
    assert original["revision"] == out["after_incompatible"]["revision"]
    assert out["after_incompatible"]["trial"] is None
    active = start(client)
    assert active["ok"], active
    out["second_active"] = err(start(client), "trial_busy")
    action(client, "cancel_trial", active["id"])
    sender, listener, token = replay_opener(renderer)
    with sender, listener:
        until(lambda: query(client, "get_scene"), lambda s: s["layers"] == [])
        out["replay_active"] = err(start(client), "trial_busy")
        sender.sendto(encode_message("/scene/replay/end", (token, listener.getsockname()[1], 0, 0)),
                      ("127.0.0.1", renderer.port))
        out["settled_replay"] = receive_results(listener, 1)[0]
    out["unsupported_shape"] = err(start(client), "trial_unsupported")
    out["after_unsupported"] = query(client, "get_scene")
    assert out["after_unsupported"]["trial"] is None
    evidence["verdicts"]["VAL-FT-09"] = "pass"


def measurement(client):
    return client.call("get_performance", {})


def collect_fixed_window(client, sender, destination):
    started = time.monotonic()
    first = until(lambda: sender.trace()[-1], lambda sample: sample[0] % 40 == 0, 3)
    opening = measurement(client)
    initial = opening["result"]["feedback_measurement"]
    start = initial["attempt_window"][-1]["frame"]
    lower, upper = start + 121, start + 750
    pairs, attempts, polls = {}, {}, []
    completed = None
    try:
        while time.monotonic()-started < 30:
            status = measurement(client)
            current = status["result"]["feedback_measurement"]
            assert current["supported"] and current["matches_current_context"] and current["id"] == initial["id"]
            for sample in current["samples"]:
                if lower <= sample["frame"] <= upper:
                    assert pairs.setdefault(sample["frame"], sample) == sample
            for attempt in current["attempt_window"]:
                if lower <= attempt["frame"] <= upper:
                    assert attempts.setdefault(attempt["frame"], attempt) == attempt
            last = current["attempt_window"][-1]["frame"]
            polls.append({"frame": last, "external_epoch": status["external_epoch"],
                          "monotonic_seconds": time.monotonic(), "pairs": len(pairs),
                          "last_send": sender.trace()[-1],
                          "update_drops": current["update_drops"], "draw_drops": current["draw_drops"]})
            if last >= upper and completed is None:
                completed = time.monotonic()
            if completed is not None and (len(pairs) == 630 or time.monotonic() - completed >= 1.):
                break
            time.sleep(.045)
    finally:
        ordered = sorted(pair["total_ms"] for pair in pairs.values())
        result = {"identity": {k: v for k, v in initial.items() if k not in {"samples", "attempt_window"}},
                  "sender_start": first, "range": [lower, upper], "polls": polls,
                  "pairs": pairs, "attempts": attempts,
                  "missing_frames": [frame for frame in range(lower, upper+1) if frame not in pairs],
                  "coverage": len(pairs)/630, "p95_ms": ordered[math.ceil(.95*len(ordered))-1] if ordered else None,
                  "max_ms": max(ordered) if ordered else None}
        dump(destination, result)
    assert completed is not None and len(attempts) == 630
    assert len(pairs) >= 600 and result["coverage"] >= .95
    assert result["p95_ms"] <= 8 and result["max_ms"] <= 16
    assert polls[-1]["external_epoch"] > opening["external_epoch"]
    return {k: v for k, v in result.items() if k not in {"pairs", "attempts", "polls"}}


def verify_sustained(renderer, client, sender, out, evidence):
    source = out / "demo.frag"
    source.write_text(DEMO)
    (out / "demo-candidate.frag").write_text(DEMO_CANDIDATE)
    cases = evidence["sustained"] = {"setup": replay(renderer, mixed_commands(source), final_gate=0)}
    until(lambda: measurement(client)["result"]["feedback_measurement"],
          lambda v: v["matches_current_context"] and bool(v["samples"]))
    cases["baseline"] = collect_fixed_window(client, sender, out / "baseline-timing.json")
    trial = start(client, DEMO_CANDIDATE)
    cases["trial"] = ready(client, trial["id"])
    cases["accept"] = action(client, "accept_trial", trial["id"])
    until(lambda: measurement(client)["result"]["feedback_measurement"],
          lambda v: v["matches_current_context"] and bool(v["samples"]))
    cases["accepted"] = collect_fixed_window(client, sender, out / "accepted-timing.json")
    cases["delta_ms"] = {field: cases["accepted"][field] - cases["baseline"][field]
                         for field in ("p95_ms", "max_ms")}
    evidence["verdicts"]["VAL-FT-11"] = "pass"


def verify_crossing(renderer, client, evidence):
    before = measurement(client)
    original = before["result"]["feedback_measurement"]
    request = {"id": "cross-configuration", "method": "set_layer",
               "params": {"layer": 1, "property": "alpha", "value": .42}}
    dispatched = hook(renderer, "dispatch_control", stage="draw", request=request)
    changed_frame = dispatched["result"]["frame"] + 1
    changed = until(lambda: measurement(client),
                    lambda r: r["result"]["feedback_measurement"]["id"] != original["id"] and
                    len(r["result"]["feedback_measurement"]["samples"]) >= 5)
    fresh = changed["result"]["feedback_measurement"]
    evidence["crossing"] = {"before": before, "mutation": dispatched, "after": changed}
    assert fresh["crossing_frames"] == original["crossing_frames"] + 1
    assert fresh["last_crossing_frame"] == changed_frame
    assert all(item["frame"] > changed_frame for item in fresh["samples"])
    if evidence.get("accept_export_replay", {}).get("provisional_measurement"):
        evidence["verdicts"]["VAL-FT-07"] = "pass"


async def ordinary_mcp(renderer, out, evidence):
    async with McpClient(StdioServerParameters(command=str(ROOT / "mcp/.venv/bin/python"),
                     args=[str(ROOT / "mcp/adapter.py"), "--socket", str(renderer.socket)],
                     cwd=str(ROOT))) as mcp:
        async def submit(name, request, **arguments):
            response = await mcp.call_tool(name, {"request_id": request, "wait_ms": 1000, **arguments})
            return response, response.structured_content["engine"]

        for label, source, code in (
            ("syntax", "#version 330\nsyntax failure;\n", "shader_failed"),
            ("link", """#version 330
in VertexAttrib { vec2 position; vec4 color; vec3 normal; vec2 texcoord; } vertex;
out vec4 fragColor; void main() { fragColor=vec4(vertex.position,0,1); }
""", "shader_failed"),
            ("bindings", CANDIDATE.replace("uniform float decay;", "")
             .replace("decay*previous.r", ".5*previous.r"), "invalid_uniform"),
        ):
            response, rejected = await submit("trial_shader", f"ordinary-{label}", **params(source))
            assert response.is_error, response.structured_content
            evidence[label] = err(rejected, code)
            if code == "shader_failed":
                assert rejected["error"]["diagnostics"], rejected
        response, candidate = await submit("trial_shader", "ordinary-start", **params())
        assert not response.is_error, response.structured_content
        evidence["started"] = candidate
        rejected, busy = await submit("trial_shader", "ordinary-busy", **params())
        assert rejected.is_error
        evidence["busy"] = err(busy, "trial_busy")
        response, preview = await submit("capture_preview", "ordinary-preview", max_width=800)
        assert not response.is_error, response.structured_content
        evidence["preview"] = preview
        assert preview["result"]["feedback_trial"]["trial_id"] == candidate["id"]
        (out / "ordinary-preview.png").write_bytes(base64.b64decode(next(
            block.data for block in response.content if isinstance(block, ImageContent))))
        response, cancelled = await submit("cancel_trial", "ordinary-cancel", trial_id=candidate["id"])
        assert not response.is_error, response.structured_content
        assert cancelled["result"]["feedback_draw"]["trial_id"] == candidate["id"]
        evidence["cancelled"] = cancelled


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("binary", "sclang", "visuals-quark", "output"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--preview-only", action="store_true")
    mode.add_argument("--ordinary", action="store_true")
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(mode=0o700)
    quark = source_identity(args.visuals_quark)
    evidence = {"verdicts": {}, "quark": quark,
        "quark_revision": subprocess.check_output(
            ["git", "-C", str(args.visuals_quark), "rev-parse", "HEAD"], text=True).strip(),
        "source": subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip(),
        "collector_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
    renderer = sender = client = None
    errors = evidence["errors"] = {}
    try:
        fixture = out / "temporal.frag"
        fixture.write_text(BASELINE)
        (out / "candidate.frag").write_text(CANDIDATE)
        evidence["fixture_sha256"] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                                       for p in (fixture, out / "candidate.frag", FIXTURES / "gradient.frag")}
        renderer = OwnedRenderer(args.binary.resolve(), out, "trial", free_udp_port(), [])
        endpoint = subprocess.check_output(["lsof", "-a", "-nP", "-p", str(renderer.process.pid), "-iUDP"],
                                           text=True)
        assert f":{renderer.port}" in endpoint, endpoint
        evidence["renderer"] = {**renderer.owner, "process": process_identity(renderer.process.pid),
                                "owned_endpoint": endpoint}
        client = renderer.client()
        evidence["setup"] = replay(renderer, mixed_commands(fixture), final_gate=0)
        sender = Sender(args, renderer, out)
        until(sender.trace, lambda events: len(events) >= 10, 12)
        assert str(args.visuals_quark.resolve()) in sender.log.read_text()
        if args.ordinary:
            assert not renderer.hooks_enabled
            asyncio.run(ordinary_mcp(renderer, out, evidence))
            evidence["ordinary_mcp_passed"] = True
        else:
            asyncio.run(preview_cancel(renderer, client, sender, fixture, out, evidence))
        if not args.preview_only and not args.ordinary:
            def check(label, operation):
                try:
                    return operation()
                except Exception as error:
                    errors[label] = repr(error)
                    current = query(client, "get_scene").get("trial")
                    if current:
                        client.call("cancel_trial", {"trial_id": current["trial_id"]})
                        terminal(client, current["trial_id"], timeout=13)
                    return None
                finally:
                    dump(out / "evidence.json", evidence)

            accepted = evidence["accept_export_replay"] = {}
            folder = check("accept", lambda: verify_accept(renderer, client, fixture, out, accepted))
            if folder:
                evidence["verdicts"]["VAL-FT-02"] = "partial (awaiting stale and expired acceptance checks)"
                check("white replay", lambda: verify_white_replay(args, out, folder, accepted))
                if "white replay" not in errors:
                    evidence["verdicts"]["VAL-FT-10"] = "pass"
                client.close()
                client = renderer.client()
            check("preactivation failures", lambda: verify_faults(renderer, client, fixture, out, evidence))
            check("recovery", lambda: verify_recoverable(renderer, client, fixture, out, evidence))
            if folder and evidence.get("recoverable", {}).get("stale") and "recovery" not in errors:
                evidence["verdicts"]["VAL-FT-02"] = "pass"
            check("conflicts", lambda: verify_conflicts(renderer, client, fixture, evidence))
            check("preflight", lambda: verify_preflight(renderer, client, fixture, evidence))
            check("sustained", lambda: verify_sustained(renderer, client, sender, out, evidence))
            if "sustained" not in errors:
                check("crossing", lambda: verify_crossing(renderer, client, evidence))
            assert not errors, errors
    finally:
        if client:
            client.close()
        if sender:
            evidence["sender"] = sender.stop()
        if renderer:
            evidence["driver"] = [line for line in renderer.log.read_text().splitlines()
                                  if any(key in line for key in ("Vendor :", "GPU :", "OpenGL ver.", "GLSL ver."))]
            evidence["renderer_cleanup"], error = collect_cleanup(renderer)
            if error:
                evidence["cleanup_error"] = error
        evidence["quark_source_stable"] = source_identity(args.visuals_quark) == quark
        dump(out / "evidence.json", evidence)


if __name__ == "__main__":
    main()
