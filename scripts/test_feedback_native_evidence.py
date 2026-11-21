#!/usr/bin/env python3
"""Private recovery-build feedback GL evidence; injections are explicitly test-only."""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import socket
import struct
import time
import uuid

from test_feedback_composition import FIXTURES, checked, replay, setup_commands
from test_scene_recipe import (OwnedRenderer, arm_recovery_hook, capture, collect_cleanup,
                              free_udp_port, osc, osc_bundle, recipe_messages,
                              receive_results, source_snapshot, wait_recovery_hook)


def hook(renderer, operation, **values):
    command = {"id": uuid.uuid4().hex, "operation": operation, **values}
    existing = arm_recovery_hook(renderer, command)
    event = wait_recovery_hook(renderer, command, existing)
    if event.get("event") != "command_complete":
        raise RuntimeError(f"hook did not complete: {event}")
    return event["result"]


def configure(renderer, **values):
    return hook(renderer, "feedback_configure", enabled=True, reset_events=True, **values)


def raw_replay(renderer, commands, *, expected=None, phase_hook=None):
    token = uuid.uuid4().hex
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as listener:
        listener.bind(("127.0.0.1", 0))
        if phase_hook:
            command = {"id": uuid.uuid4().hex, "require_scene_replay_ended": True, **phase_hook}
            existing = arm_recovery_hook(renderer, command)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
            sender.sendto(osc_bundle(recipe_messages(token, listener.getsockname()[1], commands,
                expected=expected, final_gate=-1)), ("127.0.0.1", renderer.port))
        receipt = receive_results(listener, 1)[0]
        if phase_hook:
            receipt["phase_hook"] = wait_recovery_hook(renderer, command, existing)
    if receipt["token"] != token:
        raise RuntimeError("foreign replay receipt")
    return receipt


def assert_storage(snapshot, width, height):
    if len(snapshot["instances"]) != 1:
        raise AssertionError("expected exactly one feedback instance")
    instance = snapshot["instances"][0]
    if not instance["bounded"] or not instance["allocated"]:
        raise AssertionError("feedback is not allocated and bounded")
    if len(instance["storage"]) != 2:
        raise AssertionError("expected exactly two FBOs")
    for fbo in instance["storage"]:
        if fbo["status"] != 36053 or fbo["samples"] or fbo["depth_attachment_type"] or fbo["stencil_attachment_type"]:
            raise AssertionError(f"invalid actual framebuffer: {fbo}")
        if len(fbo["color_attachments"]) != 1:
            raise AssertionError("actual attachment count differs from one")
        attachment = fbo["color_attachments"][0]
        expected = {"index": 0, "object_type": 5890, "width": width, "height": height,
                    "internalformat": 32856, "textureTarget": 3553, "minFilter": 9729,
                    "magFilter": 9729, "wrapS": 33071, "wrapT": 33071}
        for key, value in expected.items():
            if attachment[key] != value:
                raise AssertionError(f"actual GL {key}={attachment[key]} expected {value}")
    return instance


def inspect_client(renderer, method, params=None):
    client = renderer.client()
    try:
        return client.call(method, {} if params is None else params)
    finally:
        client.close()


def boundary(renderer, width, height):
    configure(renderer)
    commands = [(address, (0, float(width), float(height)) if address == "/tex/size" else args)
                for address, args in setup_commands(FIXTURES / "spatial.frag")]
    receipt = raw_replay(renderer, commands)
    if receipt["status"] != "applied":
        raise AssertionError(receipt)
    snapshot = hook(renderer, "feedback_snapshot")
    instance = assert_storage(snapshot, width, height)
    if snapshot["allocation_pairs"] != 1 or snapshot["color_allocations"] != 2:
        raise AssertionError("boundary allocated more than one pair")
    events = snapshot["events"]
    names = [event["event"] for event in events]
    required = ["compiled", "allocation_requested", "allocated", "white_seed_verified", "update_begin", "update_complete", "composed_draw"]
    cursor = -1
    for name in required:
        cursor = names.index(name, cursor + 1)
    white = next(event for event in events if event["event"] == "white_seed_verified")
    if any(buffer["bytes"] != width * height * 4 or buffer["nonwhite_bytes"] for buffer in white["buffers"]):
        raise AssertionError("both buffers were not white before first update")
    if snapshot["trace_dropped"] or instance["updates"] != 1:
        raise AssertionError("ordering evidence overflowed or static first update is not n1")
    return {"receipt": receipt, "snapshot": snapshot, "payload_bytes": 2 * width * height * 4}


def native_negatives(renderer, folder):
    base = setup_commands(FIXTURES / "spatial.frag")
    variants = {}
    for label, width, height in [("zero", 0., 600.), ("fractional", 1.5, 600.), ("wide", 801., 600.),
                                 ("tall", 800., 601.), ("overflow", 1e30, 1e30), ("nan", math.nan, 600.), ("infinity", math.inf, 600.)]:
        variants[label] = [(address, (0, width, height) if address == "/tex/size" else args) for address, args in base]
    variants["integer-dimensions"] = [(a, (0, 800, 600) if a == "/tex/size" else v) for a, v in base]
    for prop in ["internalformat", "textureTarget", "static", "filter", "aspectRatio", "blendMode"]:
        variants["missing-" + prop] = [c for c in base if not (c[0] == "/tex/set" and c[1][1] == prop)]
    variants["missing-size"] = [c for c in base if c[0] != "/tex/size"]
    variants["missing-speed"] = [c for c in base if c[0] != "/tex/speed"]
    variants["second-instance"] = base + [("/tex", (1, "shader_pp:" + str(FIXTURES / "spatial.frag")))]
    variants["duplicate-source"] = base + [base[1]]
    variants["wrong-prefix"] = [(a, (0, "shader:" + str(FIXTURES / "spatial.frag")) if a == "/tex" else v) for a, v in base]
    variants["unsupported-attachment"] = base + [("/tex/set", (0, "numColorbuffers", 2))]
    variants["history"] = base + [("/tex/set", (0, "numFrames", 2))]
    variants["builtin"] = base + [("/tex/var", (0, "time", 1.))]
    variants["filter-overlap"] = base + [("/tex/set", (0, "minFilter", 9729))]
    malformed = folder / "syntax.frag"
    malformed.write_text("#version 330\nnot valid shader;\n")
    variants["syntax"] = setup_commands(malformed)
    result = {}
    for label, commands in variants.items():
        configure(renderer)
        receipt = raw_replay(renderer, commands)
        snapshot = hook(renderer, "feedback_snapshot")
        if receipt["status"] != "failed" or snapshot["allocation_pairs"]:
            raise AssertionError(f"invalid setup allocated or applied ({label}): {receipt}, {snapshot}")
        result[label] = {"receipt": receipt, "snapshot": snapshot}
    configure(renderer)
    result["wrong-count"] = {"receipt": raw_replay(renderer, base, expected=len(base) + 1),
                             "snapshot": hook(renderer, "feedback_snapshot")}
    if result["wrong-count"]["receipt"]["status"] != "failed" or result["wrong-count"]["snapshot"]["allocation_pairs"]:
        raise AssertionError("wrong wrapper count allocated or applied")
    return result


def injected_failures(renderer):
    result = {}
    for label, options in [("preallocation-denial", {"fail_next_allocation": True}),
                           ("actual-incomplete-fbo", {"incomplete_next_destination": True}),
                           ("after-update-invalidation", {"invalidate_after_update": True})]:
        configure(renderer, **options)
        receipt = raw_replay(renderer, setup_commands(FIXTURES / "spatial.frag"))
        snapshot = hook(renderer, "feedback_snapshot")
        if receipt["status"] != "failed":
            raise AssertionError(f"injected failure reported applied: {label}: {receipt}")
        if label == "preallocation-denial" and snapshot["allocation_pairs"]:
            raise AssertionError("simulated preallocation denial allocated")
        if label == "actual-incomplete-fbo":
            detach = [e for e in snapshot["events"] if e["event"] == "injected_destination_detachment"]
            if len(detach) != 1 or detach[0]["actual_status"] == 36053:
                raise AssertionError("incomplete injection did not observe actual incomplete FBO")
        result[label] = {"receipt": receipt, "snapshot": snapshot}
        result[label]["recovery"] = boundary(renderer, 800, 600)
    for label, phase in [("ui-supersession", {"operation": "external_ui_change"}),
                         ("resize-supersession", {"operation": "native_resize", "width": 799, "height": 600}),
                         ("api-supersession", {"operation": "dispatch_control", "request": {"id": uuid.uuid4().hex,
                             "method": "configure_layers", "params": {"count": 1, "layout": "stack", "timing": "onset"}}}),
                         ("diagnostic-overflow", {"operation": "emit_diagnostics", "count": 513})]:
        configure(renderer)
        receipt = raw_replay(renderer, setup_commands(FIXTURES / "spatial.frag"), phase_hook=phase)
        if receipt["status"] != "failed":
            raise AssertionError(f"phase interference reported applied: {label}: {receipt}")
        result[label] = {"receipt": receipt, "snapshot": hook(renderer, "feedback_snapshot"),
                         "recovery": boundary(renderer, 800, 600)}
    return result


CONTROL_SOURCE = """#version 330
uniform float scalarValue;
uniform vec2 pairValue;
uniform vec3 tripleValue;
uniform vec4 quadValue;
out vec4 fragColor;
void main() {
 bool correct = scalarValue == .5 && all(equal(pairValue,vec2(.25,.75)))
  && all(equal(tripleValue,vec3(.125,.25,.625)))
  && all(equal(quadValue,vec4(.125,.25,.375,.5)));
 fragColor = correct ? vec4(.125,.5,.875,1) : vec4(1,0,0,1);
}
"""
CONTROLS = {"scalarValue": [.5], "pairValue": [.25, .75], "tripleValue": [.125, .25, .625], "quadValue": [.125, .25, .375, .5]}


def controls(renderer, folder):
    path = folder / "controls.frag"
    path.write_text(CONTROL_SOURCE)
    commands = setup_commands(path) + [("/tex/var", (0, name, *values)) for name, values in CONTROLS.items()]
    configure(renderer)
    receipt = raw_replay(renderer, commands)
    if receipt["status"] != "applied":
        raise AssertionError(receipt)
    snapshot = hook(renderer, "feedback_snapshot")
    uniforms = snapshot["instances"][0]["uniforms"]
    for name, expected in CONTROLS.items():
        if uniforms[name]["values"] != expected or uniforms[name]["array_size"] != 1:
            raise AssertionError(f"actual reflected control differs: {name}: {uniforms[name]}")
    client = renderer.client()
    try:
        width, height, pixels, capture_receipt = capture(client, folder / "controls.png")
        export = checked(client, "export_scene")["recipe"]
    finally:
        client.close()
    expected = (32, 128, 223, 255)
    bad = sum(max(abs(a - b) for a, b in zip(pixel, expected)) > 2 for pixel in pixels)
    if (width, height) != (800, 600) or bad:
        raise AssertionError(f"rendered controls failed {bad}")
    exported = {c["args"][1]["value"]: [a["value"] for a in c["args"][2:]] for c in export["commands"] if c["address"] == "/tex/var"}
    if exported != CONTROLS:
        raise AssertionError(f"exported controls differ: {exported}")
    negatives = {}
    for label, altered in [("wrong-arity", (0, "pairValue", .25)), ("integer", (0, "scalarValue", 1)),
                           ("expression", (0, "scalarValue", "t")), ("nan", (0, "scalarValue", math.nan)),
                           ("infinity", (0, "scalarValue", math.inf)), ("magnitude", (0, "scalarValue", 1000001.))]:
        configure(renderer)
        variant = [c for c in commands if not (c[0] == "/tex/var" and c[1][1] == altered[1])]
        variant.append(("/tex/var", altered))
        response = raw_replay(renderer, variant)
        trace = hook(renderer, "feedback_snapshot")
        if response["status"] != "failed" or trace["allocation_pairs"]:
            raise AssertionError(f"malformed control allocated/applied: {label}")
        negatives[label] = {"receipt": response, "snapshot": trace}
    return {"receipt": receipt, "snapshot": snapshot, "capture": capture_receipt,
            "bad_pixels": bad, "expected_rgba": expected, "export": export, "negatives": negatives}


def control_boundaries(renderer, folder):
    results = {}
    for count in (64, 65):
        path = folder / (f"controls-{count}.frag")
        names = [f"u{i}" for i in range(count)]
        path.write_text("#version 330\n" + "\n".join(f"uniform float {name};" for name in names)
            + "\nout vec4 fragColor;void main(){float v=" + "+".join(names) + ";fragColor=vec4(v,v,v,1); }\n")
        commands = setup_commands(path) + [("/tex/var", (0, name, .015625)) for name in names]
        configure(renderer)
        receipt = raw_replay(renderer, commands)
        snapshot = hook(renderer, "feedback_snapshot")
        if count == 65:
            if receipt["status"] != "failed" or snapshot["allocation_pairs"]:
                raise AssertionError("65 controls allocated or applied")
            results["65"] = {"receipt": receipt, "snapshot": snapshot}
            continue
        if receipt["status"] != "applied":
            raise AssertionError(receipt)
        uniforms = snapshot["instances"][0]["uniforms"]
        if any(uniforms[name]["values"] != [.015625] for name in names):
            raise AssertionError("64 active scalar controls did not reach the GL program")
        client = renderer.client()
        try:
            width, height, pixels, capture_receipt = capture(client, folder / "controls-64.png")
        finally:
            client.close()
        bad = sum(max(abs(a-b) for a,b in zip(pixel,(255,255,255,255))) > 2 for pixel in pixels)
        if bad or (width,height) != (800,600):
            raise AssertionError("64-control independent sum oracle failed")
        results["64"] = {"receipt": receipt, "snapshot": snapshot, "capture": capture_receipt, "bad_pixels": bad}
    variants = {
        "array": ("uniform float controlValue[2];", "vec4(controlValue[0]+controlValue[1])", (.25,.75)),
        "integer-uniform": ("uniform int controlValue;", "vec4(float(controlValue))", (.5,)),
        "bool-uniform": ("uniform bool controlValue;", "controlValue?vec4(1):vec4(0)", (.5,)),
        "sampler-uniform": ("uniform sampler2D controlValue;", "texture(controlValue,vec2(.5))", (.5,)),
        "wrong-builtin": ("uniform vec3 resolution;", "vec4(resolution,1)", None),
    }
    for label,(declaration,expression,values) in variants.items():
        path = folder / (label+".frag")
        path.write_text("#version 330\n"+declaration+"\nout vec4 fragColor;void main(){fragColor="+expression+";}\n")
        commands = setup_commands(path)
        if values is not None:
            commands.append(("/tex/var",(0,"controlValue",*values)))
        configure(renderer)
        receipt = raw_replay(renderer,commands)
        snapshot = hook(renderer,"feedback_snapshot")
        if receipt["status"] != "failed" or snapshot["allocation_pairs"]:
            raise AssertionError(f"unsupported reflected type applied or allocated: {label}")
        results[label] = {"receipt":receipt,"snapshot":snapshot}
    path = folder / "custom-stage.frag"
    path.write_text("#version 330\nout vec4 fragColor;void main(){fragColor=vec4(.5);}")
    path.with_suffix('.vert').write_text("#version 330\nuniform mat4 modelViewProjectionMatrix;in vec4 position;void main(){gl_Position=modelViewProjectionMatrix*position;}")
    for label, source in [("custom-vertex",path),("missing-source",folder/'missing.frag')]:
        configure(renderer)
        receipt = raw_replay(renderer,setup_commands(source))
        snapshot = hook(renderer,'feedback_snapshot')
        if receipt['status'] != 'failed' or snapshot['allocation_pairs']:
            raise AssertionError(f"invalid source allocated/applied: {label}")
        results[label]={'receipt':receipt,'snapshot':snapshot}
    return results


def watcher(renderer, folder):
    path = folder / "watched.frag"
    path.write_bytes((FIXTURES / "temporal.frag").read_bytes())
    commands = setup_commands(path) + [("/tex/var", (0, "decay", .5))]
    configure(renderer)
    receipt = raw_replay(renderer, commands)
    if receipt["status"] != "applied":
        raise AssertionError(receipt)
    before = hook(renderer, "feedback_snapshot")
    source = path.read_text().replace("previous.g-1.0/16.0", "previous.g-1.0/32.0")
    if source == path.read_text():
        raise AssertionError("watcher edit did not alter shader algorithm")
    # Ensure the watcher sees an actual mtime tick even on second-granularity filesystems.
    time.sleep(1.1)
    path.write_text(source)
    deadline = time.monotonic() + 10
    while True:
        after = hook(renderer, "feedback_snapshot")
        if after["generation"] > before["generation"]:
            break
        if time.monotonic() > deadline:
            raise AssertionError("actual successful static watcher reload was not observed")
        time.sleep(.1)
    if after["instances"][0]["updates"] != 1:
        raise AssertionError("watcher query stepped static feedback")
    export = inspect_client(renderer, "export_scene")
    fragments = [a["data"] for a in export["result"]["recipe"]["assets"] if a["kind"] == "feedback_fragment"]
    if len(fragments) != 1 or "previous.g-1.0/32.0" not in fragments[0]:
        raise AssertionError("watcher retained source export is stale")
    performance = inspect_client(renderer, "get_performance")
    if performance["result"]["feedback"]["feedback_generation"] != after["generation"]:
        raise AssertionError("watcher generation not exposed in telemetry")
    osc(renderer.port, "/tex/set", 0, "needsUpdate", 1)
    stepped = hook(renderer, "feedback_snapshot")
    if stepped["instances"][0]["updates"] != 2 or stepped["instances"][0]["successful_generation"] != stepped["generation"]:
        raise AssertionError("postwatcher execution generation was stale")
    return {"receipt": receipt, "before": before, "after": after, "export": export,
            "performance": performance, "stepped": stepped}


def source_limits(renderer, folder):
    result = {}
    ordinary = "#version 330\nout vec4 fragColor;void main(){fragColor=vec4(.5);}\n"
    for label, payload in [("oversize", (ordinary + "//" + "x" * (128 * 1024)).encode()),
                           ("invalid-utf8", ordinary.encode() + b"//\xff\n")]:
        path = folder / (label + ".frag")
        path.write_bytes(payload)
        configure(renderer)
        receipt = raw_replay(renderer, setup_commands(path))
        snapshot = hook(renderer, "feedback_snapshot")
        export = inspect_client(renderer, "export_scene")
        result[label] = {"receipt": receipt, "snapshot": snapshot, "export": export,
                         "passed": receipt["status"] == "failed" and snapshot["allocation_pairs"] == 0}
    return result


def loop_execution(renderer, folder):
    results = {}
    for label in ("a", "b"):
        configure(renderer)
        receipt = raw_replay(renderer, setup_commands(FIXTURES / ("attribution-" + label + ".frag")))
        if receipt["status"] != "applied":
            raise AssertionError(receipt)
        snapshot = hook(renderer, "feedback_snapshot")
        client = renderer.client()
        try:
            width, height, _, receipt = capture(client, folder / ("loop-" + label + "-n1.png"))
        finally:
            client.close()
        if (width, height) != (800, 600) or snapshot["instances"][0]["updates"] != 1:
            raise AssertionError("loop execution fixture was not static full-size n1")
        results[label] = {"snapshot": snapshot, "capture": receipt}
    return results


def trial_rejection(renderer):
    commands = [(address, (1, "stack") if address == "/layers/reset" else values)
                for address, values in setup_commands(FIXTURES / "spatial.frag")
                if address == "/layers/reset" or values[0] == 0]
    replay(renderer, commands)
    client = renderer.client()
    try:
        source = "#version 330\nout vec4 fragColor;void main(){fragColor=vec4(1);}"
        baseline = client.call("publish_shader", {"layer":0,"source":source})
        if baseline.get("status") in ("queued", "applied"):
            baseline = client.wait(baseline["id"])
        if not baseline.get("ok"):
            raise AssertionError(f"direct-source trial baseline failed: {baseline}")
        inspection = checked(client,"get_scene")
        if len(inspection['layers']) != 1 or not inspection['layers'][0]['shader']['textures']['tex'].get('feedback'):
            raise AssertionError("trial test did not retain one-layer feedback")
        response = client.call("trial_shader", {"layer":0,"source":source})
        if response.get("status") in ("queued", "applied", "evaluating"):
            response = client.wait(response["id"])
    finally:
        client.close()
    error = response.get('error',{})
    if response.get("ok") or error.get('code') != 'trial_unsupported' or 'texture or buffer bindings' not in error.get('message',''):
        raise AssertionError(f"feedback texture-binding trial rejection absent after valid cardinality/source: {response}")
    return {'baseline':baseline,'inspection':inspection,'trial':response}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--expect-sanitizer", choices=("address", "undefined"), action="append", default=[])
    parser.add_argument("--only", choices=("all", "boundaries", "negatives", "failures", "controls", "control_boundaries", "watcher", "source_limits", "loops", "trial"), default="all")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(mode=0o700, parents=True, exist_ok=False)
    evidence = {"status": "FAILED", "source_before": source_snapshot(), "cases": {}}
    renderer = None
    try:
        renderer = OwnedRenderer(args.binary.resolve(), output, "native-evidence", free_udp_port(), args.expect_sanitizer)
        evidence["owner"] = renderer.owner
        if not renderer.hooks_enabled:
            raise RuntimeError("requires a recovery-test-hook build")
        cases = {"boundaries": lambda: {"1x1": boundary(renderer, 1, 1), "800x600": boundary(renderer, 800, 600)},
                 "negatives": lambda: native_negatives(renderer, output), "failures": lambda: injected_failures(renderer),
                 "controls": lambda: controls(renderer, output), "control_boundaries": lambda: control_boundaries(renderer, output),
                 "watcher": lambda: watcher(renderer, output),
                 "source_limits": lambda: source_limits(renderer, output), "loops": lambda: loop_execution(renderer, output),
                 "trial": lambda: trial_rejection(renderer)}
        for label, case in cases.items():
            if args.only not in ("all", label):
                continue
            try:
                value = case()
                passed = all(item["passed"] for item in value.values()) if label == "source_limits" else True
                evidence["cases"][label] = {"passed": passed, "result": value}
            except Exception as error:
                evidence["cases"][label] = {"passed": False, "error": repr(error)}
        evidence["status"] = "PASS" if all(case["passed"] for case in evidence["cases"].values()) else "FAILED"
    except Exception as error:
        evidence["error"] = repr(error)
    finally:
        if renderer:
            evidence["cleanup"], error = collect_cleanup(renderer)
            if error:
                evidence["cleanup_error"] = error
                evidence["status"] = "FAILED"
        evidence["source_after"] = source_snapshot()
        evidence["source_stable"] = evidence["source_before"] == evidence["source_after"]
        if not evidence["source_stable"]:
            evidence["status"] = "FAILED"
        (output / "evidence.json").write_text(json.dumps(evidence, indent=2) + "\n")
    print(evidence["status"], output)
    return 0 if evidence["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
