"""Exercise reflected float-vector bindings through ordinary OSC and replay."""

from contextlib import closing
import json
import struct

from shader_ivec_acceptance import (
    configure, diagnostic_cursor, expect_binding, integer_shader, language_case,
    named_shared_case, observe, raw_replay, require_diagnostic, run_owned, typed_binding,
)
from scene_recipe import write_folder
from test_protected_authoring import PENDING, Session
from test_scene_recipe import applied, osc, run_sclang
from visuals_quark import checkout_path


def float32(value):
    return struct.unpack("!f", struct.pack("!f", value))[0]


def float_shader(expected, *, array_size=0):
    arity = len(expected)
    target = "probe[0]" if array_size else "probe"
    suffix = f"[{array_size}]" if array_size else ""
    vector = f"vec{arity}({','.join(str(float32(value)) for value in expected)})"
    second = f" && all(equal(probe[1], vec{arity}(0)))" if array_size == 2 else ""
    return (f"#version 330\nuniform vec{arity} probe{suffix};\nout vec4 color;\n"
            f"void main(){{bool ok=all(equal({target},{vector})){second};"
            "color=ok?vec4(0,1,0,1):vec4(1,0,0,1);}\n")


def send(renderer, components, layer=0, address="/shader/var"):
    osc(renderer.port, address, layer, "probe", json.dumps(components))


def load(client, source):
    applied(client, "publish_shader", {"layer": 0, "source": source})


def write_shader(output, label, source):
    path = output / f"{label}.frag"
    path.write_text(source)
    return path


def replay_setup(path):
    return [("/layers/reset", (1, "stack")), ("/shader", (0, str(path)))]


def replay_binding(components):
    return ("/shader/var", (0, "probe", *components))


def require_applied(result):
    if result["status"] != "applied" or result["frame"] < 0:
        raise RuntimeError(f"expected applied replay, got {result}")


def reject(renderer, client, payload, *diagnostics, layer=0):
    cursor = diagnostic_cursor(client)
    osc(renderer.port, "/shader/var", layer, "probe", payload)
    return require_diagnostic(client, cursor, f"layer {layer} uniform probe", *diagnostics)["message"]


def positive_cases(renderer, client, output, sclang, quark):
    cases = {}
    for arity in (2, 3, 4):
        for mixed in (False, True):
            expected = [-2.5, 7.0, 0.125, 1.75][:arity]
            components = list(expected)
            if mixed:
                expected[0] = 1.5
                expected[-1] = 4.0
                components[0] = "1.5"
                components[-1] = "fixtureOffset+i+total"

            label = f"vec{arity}-{'mixed' if mixed else 'constant'}"
            source = float_shader(expected)
            configure(renderer, client, source)
            osc(renderer.port, "/var", "fixtureOffset", 3.0)
            ordinary_components = list(components)
            if not mixed:
                ordinary_components[1] = 7
            send(renderer, ordinary_components, address="/shader/uniform")
            ordinary = observe(client, output, f"{label}-ordinary")[0]
            expect_binding(ordinary, components)
            path = write_shader(output, label, source)
            receipt = raw_replay(renderer, [
                ("/layers/reset", (1, "stack")), ("/var", ("fixtureOffset", 3.0)),
                ("/shader", (0, str(path))), replay_binding(components),
            ])
            require_applied(receipt)
            replay = observe(client, output, f"{label}-replay")[0]
            if ordinary != replay:
                raise RuntimeError(f"{label}: ordinary {ordinary} and replay {replay} differ")
            cases[label] = {"binding": ordinary, "replay": receipt}

    source = float_shader((2.0, -3.5, 2.25))
    configured = [2.0, -3.5, "fixtureOffset+i+total+0.25"]
    configure(renderer, client, source)
    osc(renderer.port, "/var", "fixtureOffset", 1.0)
    ordinary_send = language_case(renderer, sclang, quark, output, "fvec-quark-ordinary",
                                  source, configured, ordinary=True)
    ordinary = observe(client, output, "fvec-quark-ordinary")[0]
    expect_binding(ordinary, configured)
    replay_send = language_case(renderer, sclang, quark, output, "fvec-quark-replay",
                                source, configured, ordinary=False)
    replay = observe(client, output, "fvec-quark-replay")[0]
    if ordinary != replay:
        raise RuntimeError(f"Quark ordinary {ordinary} and replay {replay} differ")
    cases["quark"] = {"ordinary": ordinary_send, "replay": replay_send, "binding": replay}
    return cases


def exact_bits_case(renderer, client, output):
    minimum = struct.unpack("!f", bytes.fromhex("00000001"))[0]
    normal = float32(0.123456789)
    components = [minimum, normal, "1+2", -minimum]
    expected_bits = {0: "00000001", 1: struct.pack("!f", normal).hex(), 3: "80000001"}
    source = ("#version 330\nuniform vec4 probe;\nout vec4 color;\n"
              f"void main(){{bool ok=abs(probe.x)<1e-30 && probe.y=={normal} && "
              "probe.z==3.0 && abs(probe.w)<1e-30;"
              "color=ok?vec4(0,1,0,1):vec4(1,0,0,1);}")
    configure(renderer, client, source)
    send(renderer, components)
    ordinary = observe(client, output, "exact-bits-ordinary")[0]
    receipt = raw_replay(renderer, replay_setup(write_shader(output, "exact-bits", source)) +
                         [replay_binding(components)])
    require_applied(receipt)
    replay = observe(client, output, "exact-bits-replay")[0]
    for observation in (ordinary, replay):
        expect_binding(observation, components)
        fields = observation["binding"][0][2:]
        bits = {index: struct.pack("!f", fields[index]["value"]).hex() for index in expected_bits}
        if bits != expected_bits:
            raise RuntimeError(f"expected {expected_bits}, got {bits}")
    return {"bits": expected_bits, "replay": receipt}


def wide_integer_case(renderer, client, output):
    # This number is one past the float32 halfway point. A double intermediate
    # would erase that bit and round to the lower float32 value.
    midpoint = (1 << 62) + (1 << 38)
    rounded = (1 << 62) + (1 << 39)
    configure(renderer, client, float_shader((rounded, -rounded)))
    send(renderer, [midpoint + 1, -(midpoint + 1)])
    return observe(client, output, "ordinary-wide-integer")[0]["pixel"]


def ordinary_rejections(renderer, client, output):
    configure(renderer, client, float_shader((0, 0)))
    empty = observe(client, output, "before-first-rejection")[0]
    expect_binding_failure = reject(renderer, client, '[1,"missingName"]', "initialize")
    if empty["binding"] or observe(client, output, "after-first-rejection")[0] != empty:
        raise RuntimeError("rejected first edit created a binding")

    expected = [2.25, -3.5]
    load(client, float_shader(expected))
    send(renderer, expected)
    before = observe(client, output, "before-ordinary-rejections")[0]
    expect_binding(before, expected)
    cases = {"first": expect_binding_failure}
    for index, (payload, diagnostic) in enumerate((
        ("[1,2", "syntax"), ("[1]", "arity"), ("[1,2,3]", "arity"),
        ("[true,2]", "expected float32"), ("[null,2]", "expected float32"),
        ("[[1],2]", "expected float32"), ('[{"x":1},2]', "expected float32"),
        ("[1e39,2]", "float32 range"), ("[1e400,2]", "float32 range"),
        ('["",2]', "expected float32"), (r'["1\u0000+2",2]', "initialize"),
        ('["1+",2]', "initialize"), ('["missingName",2]', "initialize"),
    )):
        cases[payload] = reject(renderer, client, payload, diagnostic)
        if observe(client, output, f"ordinary-reject-{index}")[0] != before:
            raise RuntimeError(f"ordinary {payload} changed the prior binding")
    load(client, float_shader((4.0, 5.0)))
    send(renderer, [4.0, 5.0])
    expect_binding(observe(client, output, "ordinary-recovered")[0], [4.0, 5.0])
    return cases


def replay_rejections(renderer, client, output):
    known = [2.25, -3.5, 4.0]
    path = write_shader(output, "replay-rejections", float_shader(known))
    setup = replay_setup(path) + [replay_binding(known)]
    cases = {}
    for label, invalid in (("arity", [2.0, 3.0]), ("int32", [2, 3, 4]),
                           ("initialization", [2.0, "1+", 4.0])):
        cursor = diagnostic_cursor(client)
        receipt = raw_replay(renderer, setup + [replay_binding(invalid)], finish=False)
        if (receipt["status"], receipt["frame"], receipt["detail"]) != (
            "failed", -1, "invalid, out-of-order, or unroutable setup command"
        ):
            raise RuntimeError(f"unexpected {label} replay rejection: {receipt}")
        if label == "initialization":
            receipt["initializer_diagnostic"] = require_diagnostic(
                client, cursor, "layer 0 uniform probe", "initialize float uniform expression"
            )["message"]
        expect_binding(observe(client, output, f"replay-reject-{label}")[0], known)
        cases[label] = receipt
    if len({receipt["token"] for receipt in cases.values()}) != len(cases):
        raise RuntimeError("replay rejection cases reused a token")
    cases["recovered"] = raw_replay(renderer, setup)
    require_applied(cases["recovered"])
    expect_binding(observe(client, output, "replay-recovered")[0], known)
    return cases


def cross_arity(renderer, client, output, label):
    cases = {}
    for arity in (2, 3):
        oversized = [0.25, 0.5, 0.75, 1.0][:arity + 1]
        old = [-0.25, 0.25, -0.5][:arity]
        configure(renderer, client, float_shader(old))
        send(renderer, old)
        reject(renderer, client, json.dumps(oversized), "arity")
        state = observe(client, output, f"cross-arity-{label}-{arity}")[0]
        expect_binding(state, old)
        cases[str(arity)] = state
    return cases


def rgb_shorthand(renderer, client, output, label, *, baseline):
    cases = {}
    for kind, fields, expected in (
        ("numeric", [255, 127.5, 0], [1.0, 0.5, 0.0, 1.0]),
        ("unit", [1, 0.5, 0], [1.0, 0.5, 0.0, 1.0]),
        ("mixed", [3.0, "1/2", 0], [3.0, 0.5, 0.0, 1.0]),
    ):
        for marker in (False, True):
            suffix = f"{label}-{kind}-{'marker' if marker else 'color'}"
            source = float_shader(expected)
            integers = [1, 2, 3, 4]
            if marker:
                configure(renderer, client, integer_shader(integers))
                send(renderer, integers)
                load(client, source)
            else:
                configure(renderer, client, source)
                setup = [0.25, "1/2" if kind == "mixed" else 0.5, 0.0, 1.0]
                send(renderer, setup)

            if not baseline:
                prior = observe(client, output, suffix + "-prior", green=False)[0]
                for index, (payload, reason) in enumerate((
                    ('[1,2,"missingName"]', "initialize"), ("[true,2,3]", "expected float32"),
                )):
                    reject(renderer, client, payload, reason)
                    if observe(client, output, f"{suffix}-rejected-{index}", green=False)[0] != prior:
                        raise RuntimeError(f"{suffix}: invalid shorthand replaced prior binding")
                if marker:
                    # The int binding is not recipe-exportable under float reflection.
                    load(client, integer_shader(integers))
                    expect_binding(observe(client, output, suffix + "-restored-int")[0], integers)
                    load(client, source)
            send(renderer, fields)
            result = observe(client, output, suffix)[0]
            if not baseline:
                binding = [3.0, "1/2", 0.0, 1.0] if kind == "mixed" else expected
                expect_binding(result, binding)
            cases[f"{kind}-{marker}"] = result["pixel"]

    # Only a color already present in the pool can handle this three-field object.
    configure(renderer, client, float_shader((0.25, 0.5, 0.0, 1.0)))
    send(renderer, [0.0, 0.0, 0.0, 1.0])
    send(renderer, {"r": 0.25, "g": 0.5, "b": 0.0})
    expect_binding(observe(client, output, label + "-legacy-pool")[0], [0.25, 0.5, 0.0, 1.0])
    return cases


def wildcard_and_replacement(renderer, client, output):
    applied(client, "configure_layers", {"count": 3, "layout": "col"})
    osc(renderer.port, "/onset", 0)
    for index, (source, old) in enumerate((
        (float_shader((2.0, -3.0)), [0.0, 0.0]),
        (float_shader((0.25, 0.5, 0.75)), [0.25, 0.5, 0.75]),
        (integer_shader((1, 2)), [1, 2]),
    )):
        applied(client, "publish_shader", {"layer": index, "source": source})
        send(renderer, old, layer=index)
    cursor = diagnostic_cursor(client)
    send(renderer, [2.0, -3.0], layer="*")
    require_diagnostic(client, cursor, "layer 1 uniform probe", "arity")
    require_diagnostic(client, cursor, "layer 2 uniform probe", "expected int32")
    states = []
    for index, expected in enumerate(([2.0, -3.0], [0.25, 0.5, 0.75], [1, 2])):
        state = observe(client, output, f"wildcard-{index}", point=(400, 100 + 200 * index), layer=index)[0]
        if state.get("binding") != typed_binding(expected, index):
            raise RuntimeError(f"layer {index}: expected {expected}, got {state}")
        states.append(state)

    old_values = [2.0, 3.0, 4.0]
    old_source = float_shader(old_values)
    new_values = [0.25, 0.5]
    new_source = float_shader(new_values)
    configure(renderer, client, old_source)
    send(renderer, old_values)
    expect_binding(observe(client, output, "replacement-original")[0], old_values)
    load(client, new_source)
    before = observe(client, output, "replacement-prior", green=False)[0]
    reject(renderer, client, "[7.0,8.0,9.0]", "arity")
    if observe(client, output, "replacement-rejected", green=False)[0] != before:
        raise RuntimeError("old arity changed the prior binding")
    load(client, old_source)
    expect_binding(observe(client, output, "replacement-retained-binding")[0], old_values)
    load(client, new_source)
    send(renderer, new_values)
    expect_binding(observe(client, output, "replacement-current-arity")[0], new_values)
    return states


def legacy_paths(renderer, client, output, label):
    cases = {}
    applied(client, "configure_layers", {"count": 1, "layout": "stack"})
    osc(renderer.port, "/onset", 0)
    send(renderer, [0.25, 0.5, 0.75])
    load(client, float_shader((0.25, 0.5)))
    cases["preload"] = observe(client, output, f"{label}-preload")[0]
    for length in (1, 2):
        for mixed in (False, True):
            key = f"array-{length}-{mixed}"
            source = float_shader((2.0, -3.0, 0.5, 1.25), array_size=length)
            path = write_shader(output, f"{label}-{key}", source)
            fields = [2.0, -3.0, "1/2" if mixed else 0.5, 1.25]
            receipt = raw_replay(renderer, replay_setup(path) + [replay_binding(fields)])
            require_applied(receipt)
            reflected = client.call("get_scene", {})["result"]["layers"][0]["shader"]["uniforms"]["probe"]
            if not reflected["array"]:
                raise RuntimeError(f"{key}: expected array reflection, got {reflected}")
            state = observe(client, output, f"{label}-{key}")[0]
            expect_binding(state, fields)
            cases[key] = state
    cases["shared"] = named_shared_case(renderer, client)["message"]
    return cases


def scalar_cases(renderer, client, output):
    cases = {}
    for arity in (2, 3, 4):
        for address, scalar in (("/shader/var", 0.375), ("/shader/uniform", -7)):
            label = f"vec{arity}-scalar-{address.rsplit('/', 1)[1]}"
            expected = [float32(scalar)] * arity
            configure(renderer, client, float_shader(expected))
            osc(renderer.port, address, 0, "probe", scalar)
            state, _ = observe(client, output, label)
            expect_binding(state, expected)
            cases[label] = state

    before = cases["vec4-scalar-uniform"]
    cases["short_array_diagnostic"] = reject(renderer, client, "[-7,-7,-7]", "arity")
    cases["nonfinite_diagnostics"] = [
        reject(renderer, client, value, "expected finite float32 scalar")
        for value in (float("inf"), float("nan"))
    ]
    retained, _ = observe(client, output, "vec4-scalar-retained")
    if retained != before:
        raise RuntimeError(f"rejected scalar and array writes changed vec4: {before}, {retained}")
    cases["retained"] = retained
    return cases


def scalar_wildcard_and_replacement(renderer, client, output):
    applied(client, "configure_layers", {"count": 3, "layout": "col"})
    osc(renderer.port, "/onset", 0)
    for layer, source in enumerate((float_shader((3.0, 3.0)), float_shader((3.0,) * 4),
                                    integer_shader((3,) * 3))):
        applied(client, "publish_shader", {"layer": layer, "source": source})
        send(renderer, [0] * (2, 4, 3)[layer], layer=layer)

    def observe_layers(label, components):
        states = []
        for layer, expected in enumerate(components):
            state, _ = observe(client, output, f"{label}-{layer}", point=(400, 100 + 200 * layer),
                               layer=layer)
            if state["binding"] != typed_binding(expected, layer):
                raise RuntimeError(f"{label} layer {layer}: expected {expected}, got {state}")
            states.append(state)
        return states

    osc(renderer.port, "/shader/var", "*", "probe", 3)
    broadcast = observe_layers("scalar-wildcard-int", ([3.0] * 2, [3.0] * 4, [3] * 3))

    for layer, arity in enumerate((2, 4)):
        applied(client, "publish_shader", {"layer": layer, "source": float_shader((0.625,) * arity)})
    cursor = diagnostic_cursor(client)
    osc(renderer.port, "/shader/uniform", "*", "probe", 0.625)
    rejection = require_diagnostic(client, cursor, "/shader/uniform", "layer 2 uniform probe",
                                   "expected int32 scalar")
    mixed = observe_layers("scalar-wildcard-float", ([0.625] * 2, [0.625] * 4, [3] * 3))

    configure(renderer, client, float_shader((0.625,) * 2))
    osc(renderer.port, "/shader/uniform", 0, "probe", 0.625)
    original, _ = observe(client, output, "scalar-before-replacement")
    expect_binding(original, [0.625] * 2)
    load(client, float_shader((0.625,) * 3))
    prior, _ = observe(client, output, "scalar-replacement-before-resend", green=False)
    if prior["pixel"] == original["pixel"]:
        raise RuntimeError("vec3 probe matched before the scalar was resent")
    osc(renderer.port, "/shader/uniform", 0, "probe", 0.625)
    resent, _ = observe(client, output, "scalar-replacement-after-resend")
    expect_binding(resent, [0.625] * 3)
    return {"broadcast": broadcast, "mixed": mixed, "rejection": rejection,
            "replacement": {"original": original, "before_resend": prior, "after_resend": resent}}


def scalar_export_and_trial(renderer, client, output):
    expected = [0.375] * 4
    configure(renderer, client, float_shader(expected))
    osc(renderer.port, "/shader/var", 0, "probe", 0.375)
    original, _ = observe(client, output, "scalar-export-original")
    expect_binding(original, expected)
    exported = client.call("export_scene", {})
    write_folder(output / "scalar-export", exported["result"]["recipe"])

    session = Session(renderer.socket, output)
    baseline = session.capture("scalar-trial-baseline.png", session.scene()["revision"])
    started = session.call("trial_shader", {
        "layer": 0, "source": float_shader([0.5] * 4),
        "uniforms": {"probe": [0.375] * 4}, "gpu_budget_ms": 8.0, "ttl_ms": 30000,
    })
    if not started["ok"]:
        raise RuntimeError(f"scalar binding was ineligible for protected trial: {started}")
    trial_id = started["id"]
    ready = session.wait_for(trial_id, lambda item: item["status"] == "ready" or not item["ok"])
    if not ready["ok"]:
        raise RuntimeError(f"scalar trial did not become ready: {ready}")
    candidate = session.capture("scalar-trial-candidate.png", session.scene()["revision"])
    action = session.mutate("cancel_trial", {"trial_id": trial_id})
    cancelled = session.wait_for(trial_id, lambda item: item["status"] not in PENDING)
    if cancelled.get("error", {}).get("code") != "trial_cancelled":
        raise RuntimeError(f"trial did not cancel: {cancelled}")
    restored = session.capture("scalar-trial-restored.png", session.scene()["revision"])
    original_png = (output / "scalar-trial-baseline.png").read_bytes()
    if (original_png == (output / "scalar-trial-candidate.png").read_bytes() or
            original_png != (output / "scalar-trial-restored.png").read_bytes()):
        raise RuntimeError("expected distinct trial preview and identical restored baseline")
    restored_binding, _ = observe(client, output, "scalar-trial-restored-binding")
    if restored_binding != original:
        raise RuntimeError(f"restored binding differs from OSC baseline: {restored_binding}")
    return {"original": original, "export_frame": exported["frame"], "started": started,
            "ready": ready, "cancel": action, "terminal": cancelled,
            "captures": {"baseline": baseline, "candidate": candidate, "restored": restored}}


def run_shader_fvec(args, binary, sclang, output):
    baseline = args.baseline_binary.resolve()
    if baseline == binary or not baseline.is_file():
        raise ValueError("shader float-vector acceptance requires a distinct baseline app")
    quark = checkout_path(args.visuals_quark)
    (output / "assets").mkdir()
    evidence = {"status": "FAILED"}

    def run_baseline(renderer, client):
        evidence["baseline_cross_arity"] = cross_arity(renderer, client, output, "baseline")
        evidence["baseline_rgb"] = rgb_shorthand(renderer, client, output, "baseline", baseline=True)
        evidence["baseline_legacy"] = legacy_paths(renderer, client, output, "baseline")

    def run_candidate(renderer, client):
        evidence["candidate_cross_arity"] = cross_arity(renderer, client, output, "candidate")
        evidence["candidate_rgb"] = rgb_shorthand(renderer, client, output, "candidate", baseline=False)
        evidence["positive"] = positive_cases(renderer, client, output, sclang, quark)
        evidence["exact_bits"] = exact_bits_case(renderer, client, output)
        evidence["wide_integer"] = wide_integer_case(renderer, client, output)
        evidence["ordinary_rejections"] = ordinary_rejections(renderer, client, output)
        evidence["replay_rejections"] = replay_rejections(renderer, client, output)
        evidence["wildcard"] = wildcard_and_replacement(renderer, client, output)
        evidence["scalars"] = scalar_cases(renderer, client, output)
        evidence["scalar_wildcard"] = scalar_wildcard_and_replacement(renderer, client, output)
        evidence["scalar_export_trial"] = scalar_export_and_trial(renderer, client, output)
        # The legacy fixture ends by installing a named shader, which cannot be exported.
        evidence["candidate_legacy"] = legacy_paths(renderer, client, output, "candidate")
        if (evidence["baseline_cross_arity"] != evidence["candidate_cross_arity"] or
                evidence["baseline_rgb"] != evidence["candidate_rgb"] or
                evidence["baseline_legacy"] != evidence["candidate_legacy"]):
            raise RuntimeError("one or more preserved paths differed from baseline")

    def run_scalar_replay(renderer, _client):
        replay = run_sclang(sclang, output / "scalar-export/scene.scd", output, renderer.port,
                            "scalar-export", quark)
        with closing(renderer.client()) as client:
            state, _ = observe(client, output, "scalar-export-replay")
        if state != evidence["scalar_export_trial"]["original"]:
            raise RuntimeError(f"replayed scalar binding differs from export: {state}")
        if ((output / "scalar-export-original.png").read_bytes() !=
                (output / "scalar-export-replay.png").read_bytes()):
            raise RuntimeError("replayed scalar frame differs from original")
        return {"receipt": replay, "render": state}

    try:
        run_owned(baseline, output, "baseline", run_baseline)
        run_owned(binary, output, "candidate", run_candidate)
        evidence["scalar_replay"] = run_owned(binary, output, "scalar-replay", run_scalar_replay)
        evidence["status"] = "PASS"
    finally:
        (output / "shader-fvec-evidence.json").write_text(json.dumps(evidence, indent=2) + "\n")
    print(f"PASS shader float-vector acceptance; evidence: {output}")
    return 0
