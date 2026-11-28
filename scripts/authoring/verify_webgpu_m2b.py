"""Check real fixed-time WebGPU frames using a private recovery renderer."""

import argparse
import base64
import hashlib
from io import BytesIO
import json
from pathlib import Path
import sys
import uuid

from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

from control_client import Client, PENDING_STATES


FIXTURES = ROOT / "desktop/authoring/tests/fixtures"
WGSL = FIXTURES / "webgpu"


def digest(data):
    return hashlib.sha256(data).hexdigest()


def verify(client, hydra_path, evidence, fallback):
    def query(method, params=None):
        result = client.call(method, params)
        assert result["ok"], result
        return result["result"]

    def mutate(method, params, request_id=None):
        result = client.call(method, params, request_id)
        return client.wait(result["id"]) if result["status"] in PENDING_STATES else result

    def scene():
        return query("get_scene")

    def status():
        return query("draft_m1_status")

    def prepare(source, language="glsl", backend="auto", lease=None, token=None, artifact=None):
        context = {
            "source_base64": base64.b64encode(source).decode(),
            "source_digest": digest(source),
            "revision": scene()["revision"],
            "lease": lease or uuid.uuid4().hex,
            "token": token or uuid.uuid4().hex,
            "slots": {},
            "bindings": {},
            "language": language,
            "backend": backend,
            "recovery_time": 0.25,
        }
        if artifact:
            context.update(artifact)
        return context, mutate("draft_prepare", context)

    def commit(context):
        return mutate("draft_commit", {
            "revision": context["revision"],
            "lease": context["lease"],
            "token": context["token"],
            "control_digest": digest(b"{}"),
        })

    def run(source, language="glsl", backend="auto", artifact=None):
        context, prepared = prepare(source, language, backend, artifact=artifact)
        assert prepared["status"] == "prepared", prepared
        assert prepared["result"]["backend"] == "webgpu", prepared
        drawn = commit(context)
        assert drawn["status"] == "rendered", drawn
        result = drawn["result"]
        assert result["backend"] == "webgpu" and result["source_digest"] == digest(source), drawn
        assert result["first_frame"] == drawn["frame"], drawn
        assert result["scene"]["rendered_revision"] == drawn["revision"], drawn
        if language == "glsl":
            for field in ("compiler_identity", "artifact_digest"):
                assert len(result[field]) == 64 and result[field] == prepared["result"][field], drawn
        return drawn

    def capture(name, drawn):
        frame = query("draft_webgpu_test_frame", {"revision": drawn["revision"]})
        assert frame["frame"] >= drawn["frame"], frame
        png = base64.b64decode(frame["data_base64"], validate=True)
        (evidence / f"{name}.png").write_bytes(png)
        return Image.open(BytesIO(png)).convert("RGBA")

    def compare(a, b):
        assert a.size == b.size == (640, 480), (a.size, b.size)
        interior = (1, 1, 639, 479)
        return [maximum for _, maximum in ImageChops.difference(a.crop(interior), b.crop(interior)).getextrema()]

    features = query("get_capabilities")["features"]
    assert features["draft_webgpu_auto"], features
    if not fallback:
        assert features["draft_webgpu_translation"], features
    if not scene()["layers"]:
        configured = mutate("configure_layers", {"count": 1, "layout": "stack"})
        assert configured["status"] == "rendered", configured

    glow = (ROOT / "examples/shaders/assets/glow-ribbons.frag").read_bytes()
    glow_ref = (WGSL / "m2b-glow-reference.wgsl").read_bytes()
    if fallback:
        gl_context, prepared = prepare(glow)
        assert prepared["status"] == "prepared" and prepared["result"]["backend"] == "opengl", prepared
        gl = commit(gl_context)
        assert gl["status"] == "rendered" and gl["result"]["backend"] == "opengl", gl
        assert gl["result"]["first_frame"] == gl["frame"], gl
        wgsl = run(glow_ref, "wgsl")
        _, refused = prepare(glow)
        assert refused["error"]["code"] in ("backend_switch_required", "wgsl_translation_unavailable"), refused
        assert scene()["rendered_revision"] == wgsl["revision"]
        (evidence / "fallback.json").write_text(json.dumps([gl, wgsl, refused], indent=2) + "\n")
        print("PASS package without verified tools routes GLSL to OpenGL and still runs native WGSL")
        return

    callback = (FIXTURES / "callback.frag").read_bytes()
    callback_context, prepared = prepare(callback)
    assert prepared["status"] == "prepared" and prepared["result"]["backend"] == "opengl", prepared
    callback_draw = commit(callback_context)
    activation = callback_draw["result"]
    assert callback_draw["status"] == "rendered" and activation["backend"] == "opengl", callback_draw
    assert activation["callback_uniform"] == "u_vosc_callback", callback_draw
    sampled = mutate("draft_callback", {"lease": callback_context["lease"], "revision": callback_draw["revision"],
                                       "sequence": 1, "value": 0.5})
    assert sampled["status"] == "rendered", sampled
    uniform = sampled["result"]["scene"]["layers"][0]["shader"]["uniforms"]["u_vosc_callback"]
    assert float(uniform["rendered_value"]) == 0.5, uniform

    layer_alpha = b"#version 330\nuniform float alpha;\nout vec4 fragColor;\nvoid main() { fragColor = vec4(alpha); }\n"
    alpha_context, prepared = prepare(layer_alpha)
    assert prepared["status"] == "prepared" and prepared["result"]["backend"] == "opengl", prepared
    alpha_draw = commit(alpha_context)
    assert alpha_draw["status"] == "rendered" and alpha_draw["result"]["backend"] == "opengl", alpha_draw
    alpha = alpha_draw["result"]["scene"]["layers"][0]["shader"]["uniforms"]["alpha"]
    assert float(alpha["rendered_value"]) == 1.0, alpha

    hydra = hydra_path.read_bytes()
    hydra_ref = (WGSL / "m2b-hydra-reference.wgsl").read_bytes()
    first = run(glow_ref, "wgsl")
    baseline = status()
    glow_expected = capture("glow-wgsl", first)
    glsl = run(glow)
    glsl_status = status()
    glsl_pixels = capture("glow-translated", glsl)
    hydra_drawn = run(hydra)
    hydra_status = status()
    hydra_pixels = capture("hydra-translated", hydra_drawn)
    final = run(hydra_ref, "wgsl")
    expected_hydra = capture("hydra-wgsl", final)
    final_status = status()
    for state in (glsl_status, hydra_status, final_status):
        assert state["pid"] == baseline["pid"]
        assert state["webgpu_context"] > 0 and state["webgpu_context"] == baseline["webgpu_context"]
        assert state["webgpu_visible"] and not state["opengl_visible"]
    glow_error = compare(glow_expected, glsl_pixels)
    hydra_error = compare(expected_hydra, hydra_pixels)
    for error in (glow_error, hydra_error):
        assert max(error[:3]) <= 2 and error[3] == 0, error

    inverted = glow_ref.replace(b"frame.resolution.y - pixel.y", b"pixel.y")
    wrong_glow = glow_ref.replace(b"let glow = 1.0;", b"let glow = 0.0;")
    for name, negative in (("inverted-y", inverted), ("wrong-default", wrong_glow)):
        assert negative != glow_ref
        changed = capture(name, run(negative, "wgsl"))
        assert max(compare(glow_expected, changed)[:3]) > 2

    integer_defaults = b"""#version 330
uniform highp float signedHex = 0xffffffff;
uniform mediump float unsignedHex = 0xffffffffu;
uniform lowp float negatedUnsigned = -1u;
out vec4 fragColor;
void main() {
    fragColor = vec4(signedHex < 0.0 ? 1.0 : 0.0,
                     unsignedHex > 0.0 ? 1.0 : 0.0,
                     negatedUnsigned > 0.0 ? 1.0 : 0.0, 1.0);
}
"""
    defaults_draw = run(integer_defaults)
    assert [pixel["rgba"] for pixel in defaults_draw["result"]["pixels"]] == [[255, 255, 255, 255]] * 4

    encoded = json.loads((FIXTURES / "hydra/legacy-m4.vosc-draft").read_text())
    legacy = base64.b64decode(encoded["accepted"]["project"]["source"]["base64"], validate=True)
    legacy_drawn = run(legacy)

    saved = {field: glsl["result"][field] for field in ("artifact_digest", "compiler_identity")}
    restored = run(glow, backend="webgpu", artifact=saved)
    assert compare(capture("glow-restored", restored), glsl_pixels) == [0, 0, 0, 0]
    current_revision = restored["revision"]
    current_commit = scene()["draft_activation"]["commit_id"]

    invalid = b"#version 330\nout vec4 fragColor;\nvoid main() { fragColor = missing(); }\n"
    unsupported = b"#version 330\nuniform sampler2D image;\nout vec4 fragColor;\nvoid main() { fragColor = vec4(0); }\n"
    failures = []
    for params, expected in (
        ({"source": invalid}, "shader_failed"),
        ({"source": unsupported}, "backend_switch_required"),
        ({"source": callback}, "backend_switch_required"),
        ({"source": layer_alpha}, "backend_switch_required"),
        ({"source": glow, "backend": "webgpu", "artifact": {**saved, "artifact_digest": "0" * 64}},
         "shader_artifact_mismatch"),
        ({"source": glow, "backend": "webgpu", "artifact": {**saved, "compiler_identity": "0" * 64}},
         "shader_artifact_mismatch"),
        ({"source": glow, "token": "test-timeout-glsl-translation"}, "wgsl_prepare_timeout"),
    ):
        _, failure = prepare(**params)
        assert failure["error"]["code"] == expected, failure
        failures.append(failure)

    for kind in ("cancel", "fence", "supersede"):
        candidate, prepared = prepare(glow)
        assert prepared["status"] == "prepared", prepared
        if kind == "cancel":
            removed = mutate("draft_cancel", {"lease": candidate["lease"], "token": candidate["token"]})
        elif kind == "fence":
            removed = mutate("draft_fence", {"lease": candidate["lease"]})
        else:
            _, removed = prepare(hydra)
        assert removed["ok"], removed
        rejected = commit(candidate)
        assert rejected["error"]["code"] in ("stale_candidate", "stale_lease"), rejected
        failures.append(rejected)

    staged, prepared = prepare(hydra, token="test-fail-first-draft-draw")
    assert prepared["status"] == "prepared", prepared
    rollback = commit(staged)
    assert rollback["error"]["code"] == "draft_rolled_back" and rollback["result"]["restored_previous"], rollback
    failures.append(rollback)

    kept = scene()
    assert kept["revision"] == current_revision and kept["draft_activation"]["commit_id"] == current_commit
    assert kept["frame"] > restored["frame"]
    assert compare(capture("after-rejections", restored), glsl_pixels) == [0, 0, 0, 0]
    report = {
        "contexts": [baseline, glsl_status, hydra_status, final_status],
        "drawn": [first, glsl, hydra_drawn, final, legacy_drawn, restored],
        "max_error": {"glow_rgb_alpha": glow_error, "hydra_rgb_alpha": hydra_error}, "rejections": failures,
        "last_accepted_scene": kept,
    }
    (evidence / "frames.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"PASS {baseline['backend']} PID {baseline['pid']} and context {baseline['webgpu_context']}, "
          f"Glow Ribbons {glow_error}, Hydra {hydra_error}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--socket", required=True, type=Path)
    scenario = parser.add_mutually_exclusive_group(required=True)
    scenario.add_argument("--hydra", type=Path, help="current emitted animated Hydra GLSL")
    scenario.add_argument("--fallback", action="store_true", help="check a renderer with unavailable translation tools")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True)
    client = Client(args.socket)
    try:
        verify(client, args.hydra, args.output, args.fallback)
    finally:
        client.close()


if __name__ == "__main__":
    main()
