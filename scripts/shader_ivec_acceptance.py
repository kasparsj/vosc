"""Focused real-renderer checks for layer integer-vector uniform assignment."""

from contextlib import closing
import json
from pathlib import Path
import socket
import time
import uuid

from test_scene_recipe import (
    OwnedRenderer, applied, capture, collect_cleanup, free_udp_port, osc, osc_bundle,
    recipe_messages, receive_results, resize_scene, run_sclang,
)
from visuals_quark import checkout_path


def integer_literal(value):
    return "(-2147483647 - 1)" if value == -2147483648 else str(value)


def integer_shader(expected, *, array_size=0):
    arity = len(expected)
    target = "probe[0]" if array_size else "probe"
    suffix = f"[{array_size}]" if array_size else ""
    vector = f"ivec{arity}({','.join(map(integer_literal, expected))})"
    second = " && all(equal(probe[1],ivec2(0)))" if array_size == 2 else ""
    return (f"#version 330\nuniform ivec{arity} probe{suffix};\nout vec4 color;\n"
            f"void main(){{bool ok=all(equal({target},{vector})){second};"
            "color=ok?vec4(0,1,0,1):vec4(1,0,0,1);}\n")


def observe(client, directory, label, *, green=True, point=None, layer=0):
    response = client.call("export_scene", {})
    if response["ok"]:
        scene = response["result"]["recipe"]
        binding = [item["args"] for item in scene["commands"]
                   if item["address"] == "/shader/var"
                   and item["args"][0]["value"] == layer and item["args"][1]["value"] == "probe"]
        state = {"binding": binding}
    else:
        state = {"export_error": response["error"]["code"]}
    width, height, pixels, _ = capture(client, directory / f"{label}.png")
    x, y = point or (width // 2, height // 2)
    pixel = pixels[y * width + x]
    if green and not (pixel[1] > 245 and pixel[0] < 10 and pixel[2] < 10 and pixel[3] > 245):
        raise RuntimeError(f"{label}: expected shader success, got {pixel} and {state}")
    state["pixel"] = pixel
    return state, pixels


def configure(renderer, client, shader):
    applied(client, "configure_layers", {"count": 1, "layout": "stack"})
    osc(renderer.port, "/onset", 0)
    applied(client, "publish_shader", {"layer": 0, "source": shader})


def raw_replay(renderer, commands, *, finish=True):
    token = uuid.uuid4().hex
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as wire:
        wire.bind(("127.0.0.1", 0))
        packets = recipe_messages(token, wire.getsockname()[1], commands)
        wire.sendto(osc_bundle(packets if finish else packets[:-1]), ("127.0.0.1", renderer.port))
        result = receive_results(wire, 1, timeout=5)[0]
    if result["token"] != token:
        raise RuntimeError(f"unexpected replay token: {result}")
    return result


def array_replay(renderer, client, output, label):
    cases = {}
    for length in (1, 2):
        for mixed in (False, True):
            name = f"{label}-array-{length}-{'mixed' if mixed else 'constant'}"
            path = output / f"{name}.frag"
            path.write_text(integer_shader((16777217, 3), array_size=length))
            result = raw_replay(renderer, [
                ("/layers/reset", (1, "stack")), ("/shader", (0, str(path))),
                ("/shader/var", (0, "probe", 16777217, "1+2" if mixed else 3)),
            ])
            if result["status"] != "applied" or result["frame"] < 0:
                raise RuntimeError(f"{name}: expected applied receipt, got {result}")
            scene = client.call("get_scene", {})["result"]
            uniform = scene["layers"][0]["shader"]["uniforms"].get("probe", {})
            if not uniform.get("array") or uniform.get("writable") or uniform.get("gl_type") != 0x8B53:
                raise RuntimeError(f"{name}: expected an active GLSL array, got {uniform}")
            state, _ = observe(client, output, name)
            expect_binding(state, [16777217, "1+2" if mixed else 3])
            key = f"{length}-{mixed}"
            cases[key] = {"result": state, "reflection": uniform}
            (output / f"{name}-receipt.json").write_text(json.dumps(result, indent=2) + "\n")
    return cases


def ordinary_baseline(renderer, client, output, label):
    scalar = ("#version 330\nuniform int probe;\nout vec4 color;\n"
              "void main(){color=probe==17?vec4(0,1,0,1):vec4(1,0,0,1);}\n")
    configure(renderer, client, scalar)
    osc(renderer.port, "/shader/var", 0, "probe", 17)
    scalar_result, _ = observe(client, output, f"{label}-scalar")
    configure(renderer, client, integer_shader((16777217, 31)))
    osc(renderer.port, "/shader/var", 0, "probe", 16777217, 31)
    flat_result, _ = observe(client, output, f"{label}-flat")
    expect_binding(scalar_result, [17])
    expect_binding(flat_result, [16777217, 31])
    return {"scalar": scalar_result, "flat": flat_result}


def run_owned(binary, output, label, action):
    renderer = OwnedRenderer(binary, output, label, free_udp_port(), [])
    receipt = {"owner": renderer.owner, "status": "FAILED"}
    try:
        resize_scene(renderer.port, 800, 600)
        with closing(renderer.client()) as client:
            value = action(renderer, client)
        receipt["status"] = "PASS"
        return value
    finally:
        receipt["cleanup"], error = collect_cleanup(renderer)
        if error is not None:
            receipt["status"] = "FAILED"
        (output / f"{label}-owner.json").write_text(json.dumps(receipt, indent=2) + "\n")
        if error is not None:
            raise RuntimeError(error)


def baseline_cases(binary, output):
    def run(renderer, client):
        preserved = ordinary_baseline(renderer, client, output, "baseline")
        configure(renderer, client, integer_shader((16777217, 31)))
        osc(renderer.port, "/shader/var", 0, "probe", "[16777217,31]")
        ordinary_array, _ = observe(client, output, "baseline-ordinary-array", green=False)
        return {"legacy": preserved, "ordinary_array": ordinary_array,
                "arrays": array_replay(renderer, client, output, "baseline")}
    return run_owned(binary, output, "baseline", run)


def typed_binding(components, layer=0):
    return [[{"type": "i", "value": layer}, {"type": "s", "value": "probe"}] + [
        {"type": "s" if isinstance(value, str) else "f" if isinstance(value, float) else "i", "value": value}
        for value in components
    ]]


def expect_binding(state, components):
    if state.get("binding") != typed_binding(components):
        raise RuntimeError(f"expected typed binding {components}, got {state}")


def language_case(renderer, sclang, quark, output, label, source, components, *, ordinary):
    shader_path = output / "assets" / f"{label}.frag"
    shader_path.write_text(source)
    scene = output / f"{label}.scd"
    payload = json.dumps(components)
    if ordinary:
        statements = f"""
visuals.send('/shader/uniform', 0, "probe", {payload});
"VOSC_IVEC_SEND_COMPLETE".postln;
SystemClock.sched(0.25, {{ 0.exit; nil }});
"""
    else:
        statements = f"""
visuals.replay({{ |scene|
    scene.send('/layers/reset', 1, "stack");
    scene.send('/var', "fixtureOffset", 1.0);
    scene.send('/shader', 0, "assets/{label}.frag");
    scene.send('/shader/var', 0, "probe", {payload});
}}, sceneRoot: thisProcess.nowExecutingPath.dirname, width: 800, height: 600,
    finalGate: 0, onComplete: {{ |status|
        SystemClock.sched(0.25, {{ 0.exit; nil }});
    }});
"""
    scene.write_text(
        '(\nvar rendererPort = 33333;\nvar visuals = OSCVisuals("127.0.0.1", rendererPort);\n'
        f'{statements})\n'
    )
    return run_sclang(sclang, scene, output, renderer.port, label, quark,
                      ordinary=ordinary, ordinary_marker="VOSC_IVEC_SEND_COMPLETE")


def positive_cases(renderer, client, sclang, quark, output):
    results = {}
    vectors = ((16777217, -2147483648), (-16777217, 2147483647, 9),
               (2147483647, -2147483648, 16777217, -16777217))
    for vector in vectors:
        arity = len(vector)
        for mixed in (False, True):
            components = list(vector)
            expected = list(vector)
            if mixed:
                components[-1] = "fixtureOffset+i+total"
                expected[-1] = arity * 2
            label = f"ivec{arity}-{'mixed' if mixed else 'constant'}"
            source = integer_shader(expected)
            configure(renderer, client, source)
            osc(renderer.port, "/var", "fixtureOffset", 1.0)
            ordinary_send = language_case(renderer, sclang, quark, output, label + "-osc",
                                          source, components, ordinary=True)
            ordinary = observe(client, output, label + "-osc")
            replay_send = language_case(renderer, sclang, quark, output, label + "-replay",
                                        source, components, ordinary=False)
            replay = observe(client, output, label + "-replay")
            expect_binding(ordinary[0], components)
            if ordinary != replay:
                raise RuntimeError(f"{label}: ordinary and replay evidence differed")
            results[label] = {"result": ordinary[0], "ordinary": ordinary_send, "replay": replay_send}
    return results


def scalar_cases(renderer, client, output):
    results = {}
    for arity in (2, 3, 4):
        for address, scalar in (("/shader/var", 16777217), ("/shader/uniform", -16777217)):
            label = f"ivec{arity}-scalar-{address.rsplit('/', 1)[1]}"
            expected = [scalar] * arity
            configure(renderer, client, integer_shader(expected))
            osc(renderer.port, address, 0, "probe", scalar)
            state, _ = observe(client, output, label)
            expect_binding(state, expected)
            results[label] = state
    return results


def diagnostic_cursor(client):
    return client.call("get_diagnostics", {})["result"]["latest_cursor"]


def require_diagnostic(client, cursor, *fragments):
    deadline = time.monotonic() + 3
    while time.monotonic() < deadline:
        page = client.call("get_diagnostics", {"after": cursor})["result"]
        for entry in page["entries"]:
            if all(fragment in entry["message"] for fragment in fragments):
                return entry
        cursor = page["next_cursor"]
        time.sleep(0.025)
    raise RuntimeError(f"missing diagnostic for {fragments}")


def range_case(renderer, client, output):
    # Green proves the post-update -1 result, blue proves the distinct initial value.
    source = ("#version 330\nuniform ivec2 probe;\nout vec4 color;\n"
              "void main(){color=probe.x!=16777217?vec4(1,0,0,1):"
              "probe.y==-1?vec4(0,1,0,1):probe.y==1?vec4(0,0,1,1):vec4(1,0,0,1);}\n")
    configure(renderer, client, source)
    osc(renderer.port, "/var", "fixtureOffset", 1.0 / 4096)
    components = [16777217, "fixtureOffset*4096"]
    osc(renderer.port, "/shader/var", 0, "probe", json.dumps(components))
    initial, _ = observe(client, output, "range-initial", green=False)
    if initial["pixel"] != (0, 0, 255, 255):
        raise RuntimeError(f"expected initial integer result of one: {initial}")
    osc(renderer.port, "/var", "fixtureOffset", -0.00042724609375)
    valid = observe(client, output, "range-truncated")
    expect_binding(valid[0], components)
    cursor = diagnostic_cursor(client)
    osc(renderer.port, "/var", "fixtureOffset", 524288.0)
    diagnostic = require_diagnostic(client, cursor, "integer expression result is outside int32 range")
    held = observe(client, output, "range-held")
    if held != valid:
        raise RuntimeError("integer overflow changed the configured binding or rendered result")
    osc(renderer.port, "/var", "fixtureOffset", 1.0)
    return {"initial": initial, "held": held[0], "diagnostic": diagnostic}


def ordinary_negatives(renderer, client, output):
    configure(renderer, client, integer_shader((16777217, -9)))
    osc(renderer.port, "/shader/var", 0, "probe", "[16777217,-9]")
    prior = observe(client, output, "before-invalid")
    diagnostics = []
    cases = (
        ("[2]", "arity"), ("[2,3,4]", "arity"), ("[2.0,3.0]", "expected int32"),
        ("[2147483648,3]", "int32 range"), ("[-2147483649,3]", "int32 range"),
        ("[true,3]", "expected int32"), ("[null,3]", "expected int32"),
        ("[2,", "array syntax"), ('[2,"1+"]', "initialize"),
        (r'[2,"2\u0000+"]', "initialize"), ('[2,"missingFixtureOffset"]', "initialize"),
    )
    for index, (payload, reason) in enumerate(cases):
        address = "/shader/var" if index % 2 else "/shader/uniform"
        cursor = diagnostic_cursor(client)
        osc(renderer.port, address, 0, "probe", payload)
        diagnostics.append(require_diagnostic(client, cursor, "layer 0 uniform probe", reason))
        if observe(client, output, f"invalid-{index}") != prior:
            raise RuntimeError(f"rejected uniform changed output: {payload}")
    osc(renderer.port, "/shader/uniform", 0, "probe", "[16777217,-8]")
    state, _ = observe(client, output, "valid-after-invalid", green=False)
    expect_binding(state, [16777217, -8])
    if state["pixel"] != (255, 0, 0, 255):
        raise RuntimeError("valid follow-up did not reach the shader")
    return diagnostics


def replay_negatives(renderer, client, output):
    path = output / "raw-negative.frag"
    path.write_text(integer_shader((16777217, 9, -7)))
    setup = [("/layers/reset", (1, "stack")), ("/shader", (0, str(path))),
             ("/shader/var", (0, "probe", 16777217, 9, -7))]
    failures = []
    for index, components in enumerate(((7, 8), (7.0, 8.0, 9.0), (7, 8, "1+"))):
        result = raw_replay(renderer, setup + [("/shader/var", (0, "probe", *components))], finish=False)
        if result["status"] != "failed" or result["detail"] != "invalid, out-of-order, or unroutable setup command":
            raise RuntimeError(f"expected correlated command rejection, got {result}")
        state, _ = observe(client, output, f"replay-invalid-{index}")
        expect_binding(state, [16777217, 9, -7])
        failures.append(result)
    return failures


def float_shader(expected=(19, 31)):
    x, y = expected
    return ("#version 330\nuniform vec2 probe;\nout vec4 color;\n"
            f"void main(){{color=all(equal(probe,vec2({x},{y})))?vec4(0,1,0,1):vec4(1,0,0,1);}}\n")


def replace_with_float(renderer, client, output, label):
    # An intervening flat update may change the live integer storage length.
    osc(renderer.port, "/shader/var", 0, "probe", 17)
    applied(client, "publish_shader", {"layer": 0, "source": float_shader((29, 47))})
    osc(renderer.port, "/shader/uniform", 0, "probe", "[29.0,47.0]")
    result, _ = observe(client, output, label)
    expect_binding(result, [29.0, 47.0])
    return result


def wildcard_case(renderer, client, output, label, *, baseline=False):
    applied(client, "configure_layers", {"count": 3, "layout": "col"})
    osc(renderer.port, "/onset", 0)
    for index, source in enumerate((integer_shader((19, 31)), integer_shader((7, 8, 9)), float_shader())):
        applied(client, "publish_shader", {"layer": index, "source": source})
    osc(renderer.port, "/shader/var", 0, "probe", 7, 8)
    osc(renderer.port, "/shader/var", 1, "probe", 7, 8, 9)
    cursor = diagnostic_cursor(client)
    osc(renderer.port, "/shader/var", "*", "probe", "[19,31]")
    float_state, _ = observe(client, output, label + "-float", point=(400, 500), layer=2)
    if float_state.get("binding") != typed_binding([19.0, 31.0], 2):
        raise RuntimeError(f"expected typed float wildcard binding, got {float_state}")
    result = {"float": float_state}
    if not baseline:
        result["rejection"] = require_diagnostic(client, cursor, "layer 1 uniform probe", "arity")
        updated, _ = observe(client, output, label + "-updated", point=(400, 100))
        expect_binding(updated, [19, 31])
        preserved, _ = observe(client, output, label + "-retained", point=(400, 300), layer=1)
        if preserved.get("binding") != typed_binding([7, 8, 9], 1):
            raise RuntimeError(f"arity mismatch replaced second layer: {preserved}")
        result.update({"updated": updated, "preserved": preserved})
    return result


def named_shared_case(renderer, client):
    osc(renderer.port, "/shader", "namedShader", integer_shader((1, 2)))
    osc(renderer.port, "/shader/var", "namedShader", "probe", "[1,2]")
    cursor = diagnostic_cursor(client)
    osc(renderer.port, "/shader/var", "namedShader", "probe", "[1,2,3]")
    return require_diagnostic(client, cursor, "VariablePool::create not implemented for size: 3")


def run_shader_ivec(args, binary, sclang, output):
    baseline = args.baseline_binary.resolve() if args.baseline_binary else None
    if baseline and (baseline == binary or not baseline.is_file()):
        raise ValueError("shader ivec acceptance requires a distinct baseline app")
    quark = checkout_path(args.visuals_quark)
    (output / "assets").mkdir()
    evidence = {"status": "FAILED"}

    def run_baseline(renderer, client):
        configure(renderer, client, integer_shader((19, 31)))
        osc(renderer.port, "/shader/var", 0, "probe", "[19,31]")
        replaced = replace_with_float(renderer, client, output, "baseline-float-replacement")
        return {"wildcard": wildcard_case(renderer, client, output, "baseline-wildcard", baseline=True),
                "float_replacement": replaced, "shared": named_shared_case(renderer, client)["message"]}

    def run_candidate(renderer, client):
        evidence["candidate_legacy"] = ordinary_baseline(renderer, client, output, "candidate")
        evidence["candidate_arrays"] = array_replay(renderer, client, output, "candidate")
        evidence["positive"] = positive_cases(renderer, client, sclang, quark, output)
        evidence["scalars"] = scalar_cases(renderer, client, output)
        evidence["range"] = range_case(renderer, client, output)
        evidence["ordinary_rejections"] = ordinary_negatives(renderer, client, output)
        evidence["replay_rejections"] = replay_negatives(renderer, client, output)
        evidence["candidate_wildcard"] = wildcard_case(renderer, client, output, "candidate-wildcard")

        configure(renderer, client, float_shader())
        osc(renderer.port, "/shader/var", 0, "probe", "[19.0,31.0]")
        observe(client, output, "before-type-replacement")
        applied(client, "publish_shader", {"layer": 0, "source": integer_shader((19, 31))})
        osc(renderer.port, "/shader/var", 0, "probe", "[19,31]")
        replaced, _ = observe(client, output, "after-type-replacement")
        expect_binding(replaced, [19, 31])
        evidence["replacement_uniform"] = replaced
        evidence["float_replacement"] = replace_with_float(renderer, client, output, "candidate-float-replacement")
        evidence["candidate_shared"] = named_shared_case(renderer, client)["message"]

    try:
        if baseline:
            evidence["baseline"] = baseline_cases(baseline, output)
            evidence["legacy_paths"] = run_owned(baseline, output, "legacy", run_baseline)
        run_owned(binary, output, "candidate", run_candidate)
        if baseline:
            for case, expected in {
                "candidate_legacy": evidence["baseline"]["legacy"],
                "candidate_arrays": evidence["baseline"]["arrays"],
                "float_replacement": evidence["legacy_paths"]["float_replacement"],
                "candidate_shared": evidence["legacy_paths"]["shared"],
            }.items():
                if evidence[case] != expected:
                    raise RuntimeError(f"{case} differs from the historical binary")
            if evidence["candidate_wildcard"]["float"] != evidence["legacy_paths"]["wildcard"]["float"]:
                raise RuntimeError("float wildcard differs from the historical binary")
        evidence["status"] = "PASS"
    finally:
        (output / "shader-ivec-evidence.json").write_text(json.dumps(evidence, indent=2) + "\n")
    print(f"PASS shader ivec acceptance; evidence: {output}")
    return 0
