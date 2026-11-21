#!/usr/bin/env python3
"""Exercise OSC reset scope with isolated native renderers."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import socket
import uuid

from test_feedback_composition import FIXTURES, setup_commands
from test_feedback_export_rejections import ordinary, resource_snapshot, wait_settled
from test_feedback_native_evidence import assert_storage, configure, hook
from test_scene_recipe import (
    OwnedRenderer, applied, capture, collect_cleanup, encode_message, free_udp_port, osc_bundle,
    recipe_messages, receive_results,
)

NOTE = ("/dirt/play", ("cycle", 0.25, "n", 0, "s", "reset-fixture", "cps", 1.0,
                       "amp", 1.0, "gain", 1.0))
GATE = ("/onset", (1,))
FORCE = ("/onset/force", ())
DEFAULT_CAMERA = {"camPos": [0, 0, -870], "camLook": [0, 0, 0], "camOrbit": 0}
DEFAULT_PIPELINE = {"deferred": False, "post_passes": 0, "deferred_passes": 0,
                    "point_light_pass": False, "shadow_light_pass": False}


def scene(renderer):
    client = renderer.client()
    try:
        reply = client.call("get_scene", {})
        assert reply["ok"], reply
        return reply["result"]
    finally:
        client.close()


def send(renderer, commands):
    epoch = scene(renderer)["external_epoch"]
    ordinary(renderer, commands)
    return wait_settled(renderer, epoch)


def replay(renderer, commands=(), *, gate=0, prefix=(), refusal=None):
    token = uuid.uuid4().hex
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as listener:
        listener.bind(("127.0.0.1", 0))
        port = listener.getsockname()[1]
        messages = recipe_messages(token, port, commands, final_gate=gate)
        if refusal == "invalid replay opener":
            messages[0] = encode_message("/scene/reset", (token, port, 63, 600))
        packet = [encode_message(address, args) for address, args in prefix] + messages
        listener.sendto(osc_bundle(packet), ("127.0.0.1", renderer.port))
        results = receive_results(listener, 2 if refusal else 1)
    for result in results:
        assert result["token"] == token, results
        assert result["status"] == ("failed" if refusal else "applied"), results
        assert (result["frame"] < 0) == bool(refusal), results
    if refusal:
        assert refusal in results[0]["detail"] and "unknown or refused" in results[1]["detail"], results
    return results[0]


def gate_case(renderer, retained_note):
    setup = [("/layers/reset", (1, "stack"))]
    # This command and the force flag are pending in the same input batch as the opener.
    reply = replay(renderer, setup, gate=1, prefix=(GATE, ("/var", ("obsolete", 1.0)), NOTE, FORCE))
    reset = scene(renderer)
    assert reset["onset_gate"] and reset["legacy_pending"] == 0, reset
    queued = send(renderer, [GATE, ("/layer/alpha", (0, 0.25))])
    assert queued["legacy_pending"] == (0 if retained_note else 1), queued
    assert queued["layers"][0]["alpha"] == (0.25 if retained_note else 1.0), queued
    released = send(renderer, [NOTE])
    assert released["legacy_pending"] == 0 and released["layers"][0]["alpha"] == 0.25, released
    for gate in (0, -1):
        replay(renderer, setup, gate=gate)
        assert not scene(renderer)["onset_gate"]
    return {"replay": reply, "after_new_work": queued, "after_fresh_note": released}


def pixels(renderer, path):
    client = renderer.client()
    try:
        width, height, rgba, result = capture(client, path)
        assert (width, height) == (800, 600), result
        assert (result["backing_width"], result["backing_height"]) == (800, 600), result
        return rgba
    finally:
        client.close()


def seed(renderer, shader, sample=0.625):
    rendered = send(renderer, [
        ("/onset", (0,)), ("/layers/reset", (2, "row")),
        ("/input", (0, "", 0, 256)), ("/input/data", (0, sample, 0.0, 0)),
        ("/var", ("resetValue", 0.25)),
        ("/shader", (0, str(shader))), ("/shader/var", (0, "red", "in_0+resetValue")),
        ("/shader", ("resetShader", str(shader))), ("/geom", ("resetGeom", "box", 1., 1., 1.)),
        ("/geom", (1, "box", 1., 1., 1.)), ("/tex/size", ("resetTex", 2., 2.)),
        ("/layer/alpha", (0, 0.75)),
    ])
    assert rendered["layers"][0]["shader"]["program"], rendered
    assert rendered["layers"][1]["geometry_loaded"], rendered
    return rendered


def globals_on(renderer):
    send(renderer, [
        ("/cam", ("camera",)), ("/cam/pos", (1., 2., -900.)), ("/cam/look", (1., 2., 3.)),
        ("/var", ("camOrbit", 0.25)), ("/light", ("resetLight", 3., 4., 5.)),
        ("/shading/passes", ("fxaa", "pointlight", "shadowlight")), ("/shading", ("deferred",)),
    ])
    snapshot = resource_snapshot(renderer)
    assert snapshot["lights"] == 1, snapshot
    assert snapshot["camera_values"]["camOrbit"] == 0.25, snapshot
    assert all(snapshot["camera_values"][key] != DEFAULT_CAMERA[key] for key in DEFAULT_CAMERA), snapshot
    pipeline = snapshot["render_pipeline"]
    assert pipeline["deferred"] and pipeline["post_passes"] and pipeline["deferred_passes"], snapshot
    assert pipeline["point_light_pass"] and pipeline["shadow_light_pass"], snapshot
    return snapshot


def retired(renderer, program):
    report = hook(renderer, "inspect_programs", programs=[program])
    assert report["programs"] == [{"id": program, "gl_is_program": False}], report
    return report["programs"]


def layer_case(renderer, output, shader):
    replay(renderer)
    seeded = seed(renderer, shader)
    # The expression affects a rendered pixel before the old shader is removed.
    before_pixels = pixels(renderer, output / "layer-dirty.png")
    assert abs(before_pixels[300 * 800 + 200][0] - 223) <= 1
    dirty = globals_on(renderer)
    pools = dirty["resources"]
    for key in ("texture_shared", "geometry_shared", "shader_shared", "geometry_layer", "layer_bindings"):
        assert pools[key] > 0, dirty
    queued = send(renderer, [GATE, ("/layer/alpha", (0, 0.25))])
    assert queued["legacy_pending"] == 1, queued
    rebuilt = send(renderer, [("/layers/reset", (1, "grid"))])
    assert rebuilt["layout"] == "grid" and len(rebuilt["layers"]) == 1, rebuilt
    assert rebuilt["camera_enabled"] and rebuilt["legacy_pending"] == 1 and rebuilt["onset_gate"], rebuilt
    layer = rebuilt["layers"][0]
    assert layer["visible"] and layer["alpha"] == 1.0 and layer["size"] == [800, 600], layer
    assert not layer["shader"]["loaded"] and not layer["geometry_loaded"], layer
    assert (rebuilt["width"], rebuilt["height"]) == (800, 600), rebuilt
    after = resource_snapshot(renderer)
    for key in ("camera_values", "render_pipeline", "lights", "inputs"):
        assert after[key] == dirty[key], after
    for key in ("texture_shared", "geometry_shared", "shader_shared", "variable_shared"):
        assert after["resources"][key] == pools[key], after
    assert after["resources"]["geometry_layer"] == 0, after
    gl = retired(renderer, seeded["layers"][0]["shader"]["program"])
    released = send(renderer, [FORCE, GATE])
    assert released["layers"][0]["alpha"] == 0.25 and released["legacy_pending"] == 0, released
    zero = send(renderer, [("/layers/reset", (0,))])
    assert zero["layers"] == [] and zero["layout"] == "grid", zero
    default = send(renderer, [("/layers/reset", ())])
    assert len(default["layers"]) == 9 and default["layout"] == "grid", default
    send(renderer, [NOTE, ("/layers/reset", (1, "stack"))])
    assert resource_snapshot(renderer)["tidal_notes"] == 1
    return {"dirty": dirty, "rebuilt": rebuilt, "after": after, "old_program": gl}


def scene_case(renderer, output, shader):
    replay(renderer)
    clean = resource_snapshot(renderer)
    blank = pixels(renderer, output / "scene-clean.png")
    evidence = []
    for cycle in range(2):
        seeded = seed(renderer, shader, sample=0.625 - cycle * 0.5)
        red = pixels(renderer, output / f"scene-seed-{cycle}.png")[300 * 800 + 200][0]
        expected_red = (223, 96)[cycle]
        assert abs(red - expected_red) <= 1, red
        dirty = globals_on(renderer)
        before = send(renderer, [GATE, ("/var", ("queuedBeforeReset", 2.0))])
        assert before["legacy_pending"] == 1, before
        refusal = replay(renderer, [("/layers/reset", (0, "grid"))], refusal="invalid replay opener")
        after_refusal = scene(renderer)
        for key in ("layers", "layout", "camera_enabled", "width", "height", "external_epoch", "legacy_pending"):
            assert after_refusal[key] == before[key], (key, after_refusal, before)
        reply = replay(renderer)
        reset = scene(renderer)
        assert reset["layout"] == "stack" and reset["layers"] == [], reset
        assert not reset["camera_enabled"] and not reset["onset_gate"] and reset["legacy_pending"] == 0, reset
        assert (reset["width"], reset["height"]) == (800, 600), reset
        after = resource_snapshot(renderer)
        assert after["resources"] == clean["resources"], (after, clean)
        assert after["camera_values"] == DEFAULT_CAMERA and after["render_pipeline"] == DEFAULT_PIPELINE, after
        assert all(item["matches"] for item in after["managed_camera"].values()), after
        assert after["lights"] == after["inputs"] == after["tidal_notes"] == 0, after
        gl = retired(renderer, seeded["layers"][0]["shader"]["program"])
        assert pixels(renderer, output / f"scene-reset-{cycle}.png") == blank
        epoch = scene(renderer)["external_epoch"]
        ordinary(renderer, [("/input/data", (0, 0.5, 0.0, 0)), GATE])
        sampled = wait_settled(renderer, epoch)
        assert sampled["external_epoch"] == epoch + 1, sampled
        assert sampled["onset_gate"], sampled
        evidence.append({"dirty": dirty, "reset": after, "reply": reply, "refusal": refusal, "old_program": gl})
    return evidence


def empty_camera_case(renderer):
    replay(renderer)
    # Legacy targeted variable updates can leave a typed binding empty.
    send(renderer, [("/var", ("camPos",)), ("/var", ("camOrbit",))])
    empty = resource_snapshot(renderer)
    assert all(item["matches"] for item in empty["managed_camera"].values()), empty
    assert empty["camera_values"] == {"camLook": DEFAULT_CAMERA["camLook"]}, empty
    replay(renderer)
    restored = resource_snapshot(renderer)
    assert restored["camera_values"] == DEFAULT_CAMERA, restored
    return {"empty": empty, "restored": restored}


def feedback_case(renderer):
    commands = [(address, (0, 4.0, 4.0) if address == "/tex/size" else args)
                for address, args in setup_commands(FIXTURES / "temporal.frag")]
    evidence = []
    for reset in ("layers", "scene"):
        configure(renderer)
        replay(renderer, commands)
        seeded = hook(renderer, "feedback_snapshot")
        assert_storage(seeded, 4, 4)
        assert any(event["event"] == "white_seed_verified" for event in seeded["events"]), seeded
        if reset == "layers":
            send(renderer, [("/layers/reset", (0, "stack"))])
        else:
            # Drop the already-stepped instance at the whole-scene boundary.
            replay(renderer)
        assert hook(renderer, "feedback_snapshot")["instances"] == []
        evidence.append({"reset": reset, "seeded": seeded})
    return evidence


def busy_case(renderer, shader):
    client = renderer.client()
    try:
        applied(client, "configure_layers", {"count": 1, "layout": "stack"})
        applied(client, "publish_shader", {"layer": 0, "source": shader.read_text()})
        trial = client.call("trial_shader", {"layer": 0, "source": shader.read_text(),
                                           "uniforms": {"red": [0.25]}, "ttl_ms": 30000,
                                           "gpu_budget_ms": 16.0})
        assert trial["ok"], trial
        ready = client.wait(trial["id"], timeout=15)
        assert ready["status"] == "ready", ready
        before = scene(renderer)
        refusal = replay(renderer, [("/layers/reset", (0, "grid"))], refusal="renderer replay surface is busy")
        after = scene(renderer)
        for key in ("layers", "layout", "width", "height", "external_epoch"):
            assert before[key] == after[key], (key, before, after)
        assert after["trial"]["trial_id"] == before["trial"]["trial_id"] == trial["id"], after
        return {"refusal": refusal, "trial_id": trial["id"], "scene": after}
    finally:
        client.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--expect-retained-note", action="store_true", help="Run only the baseline gate probe.")
    parser.add_argument("--expect-sanitizer", action="append", choices=("address", "undefined"), default=[])
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    shader = args.output.resolve() / "reset-fixture.frag"
    shader.write_text("#version 330\nuniform float red;out vec4 fragColor;"
                      "void main(){fragColor=vec4(red,0.25,0.5,1.0);}\n")
    cases = {"gate": lambda r: gate_case(r, args.expect_retained_note)}
    if not args.expect_retained_note:
        cases["layers"] = lambda r: layer_case(r, args.output, shader)
        cases["scene"] = lambda r: scene_case(r, args.output, shader)
        cases["empty_camera"] = empty_camera_case
        cases["feedback"] = feedback_case
        cases["busy"] = lambda r: busy_case(r, shader)
    evidence = {}
    try:
        for name, run in cases.items():
            renderer = OwnedRenderer(args.binary.resolve(), args.output, name, free_udp_port(), args.expect_sanitizer)
            case = evidence[name] = {"owner": renderer.owner}
            try:
                case["result"] = run(renderer)
            except BaseException as error:
                case["error"] = repr(error)
                raise
            finally:
                case["cleanup"], cleanup_error = collect_cleanup(renderer)
            if cleanup_error:
                raise RuntimeError(cleanup_error)
            print(f"PASS {name}", flush=True)
    finally:
        (args.output / "evidence.json").write_text(json.dumps(evidence, indent=2) + "\n")


if __name__ == "__main__":
    main()
