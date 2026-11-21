#!/usr/bin/env python3
"""Exercise exported native scenes through real CLI startup on owned renderers."""

from __future__ import annotations

import argparse
from base64 import b64decode
from contextlib import closing
import json
from hashlib import sha256
from pathlib import Path
import shutil
import subprocess
import sys
import time

from test_feedback_composition import FIXTURES, capture_step, checked, replay, setup_commands
from test_native_scene_cli import compare, negative, require, run_owned
from test_scene_recipe import ROOT, capture, free_udp_port, normalize_descriptor, osc, png_rgba, run_sclang


def export_folder(renderer, folder, *, scd=False, rejection=None):
    command = [
        sys.executable, str(ROOT / "scripts/scene_recipe.py"), "--socket", str(renderer.socket),
        "export", str(folder),
    ]
    if scd:
        command.append("--scd")
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=30)
    if rejection is not None:
        require(result.returncode == 2 and rejection in result.stdout, result)
        require(not folder.exists() and not list(folder.parent.glob(f".{folder.name}.staging-*")),
                "failed export left output")
        return json.loads(result.stdout)
    require(result.returncode == 0, f"export failed: {result.stdout}{result.stderr}")
    require(Path(json.loads(result.stdout)["manifest"]) == folder / "scene.vosc.json", result.stdout)
    return result.stdout


def reject_unsupported_export(renderer, destination):
    osc(renderer.port, "/layer/delay", 0, 1)
    with closing(renderer.client()) as client:
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            reply = client.call("export_scene")
            if not reply["ok"]:
                require(reply["error"]["code"] == "scene_unsupported", reply)
                break
            time.sleep(0.03)
        else:
            raise RuntimeError("fixture did not reach the unsupported export state")
    result = subprocess.run([
        sys.executable, str(ROOT / "scripts/scene_recipe.py"), "--socket", str(renderer.socket),
        "export", str(destination),
    ], cwd=ROOT, capture_output=True, text=True, timeout=30)
    require(result.returncode == 2 and "scene_unsupported" in result.stdout, result)
    require(not destination.exists() and not list(destination.parent.glob(f".{destination.name}.staging-*")),
            "unsupported export left output")
    return json.loads(result.stdout)


def wait_ready(renderer):
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        log = renderer.log.read_text(errors="replace")
        if renderer.process.poll() is not None:
            raise RuntimeError(log)
        ready = [line for line in log.splitlines() if line.startswith("VOSC_SCENE_READY ")]
        if ready:
            require(len(ready) == 1, ready)
            return ready[0]
        time.sleep(0.03)
    raise RuntimeError(f"startup never became ready: {renderer.log}")


def live_recipe(client):
    return normalize_descriptor(checked(client, "export_scene")["recipe"])


def poll_gain(client, expected):
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        value = checked(client, "get_scene")["layers"][0]["shader"]["uniforms"]["gain"].get("rendered_value")
        if value is not None and abs(float(value) - expected) < 0.001:
            return value
        time.sleep(0.03)
    raise RuntimeError(f"expected gain {expected}, observed {value}")


def observe(renderer, node, output, label, recipe, *, gain=None, feedback=False):
    with closing(renderer.client()) as client:
        actual = live_recipe(client)
        node["descriptor"] = actual
        require(actual == recipe, f"{label} reexport differed")
        if feedback:
            samples = {}
            for step in (1, 2, 4, 8):
                samples[step], _ = capture_step(renderer, client, step, output / f"{label}-n{step}.png")
            return samples
        if gain is not None:
            osc(renderer.port, "/input/data", 0, 0.125, 0.0, 0)
            osc(renderer.port, "/input/data", "aux", 0.50, 0.0, 0)
            node["gain"] = poll_gain(client, gain)
        width, height, pixels, receipt = capture(client, output / f"{label}.png")
        require((width, height) == (800, 600), f"unexpected capture dimensions: {width}x{height}")
        receipt.pop("data_base64", None)
        node["capture"] = receipt
        if gain is not None:
            pixel = pixels[300 * width + 400]
            require(abs(pixel[0] - 255 * gain) <= 1 and pixel[1:] == (0, 0, 255),
                    f"unexpected configured pixel: {pixel}")
        return pixels


def manifest_matches(folder, recipe):
    manifest = json.loads((folder / "scene.vosc.json").read_text())
    require(manifest == {
        "version": 1, "title": folder.name, "size": [800, 600],
        "onset_gate": recipe["onset_gate"], "commands": recipe["commands"],
    }, "native manifest did not preserve the exported recipe")
    for asset in recipe["assets"]:
        data = (folder / asset["name"]).read_bytes()
        expected = asset["data"].encode() if asset["encoding"] == "utf8" else b64decode(asset["data"])
        require(data == expected, f"asset bytes changed: {asset['name']}")


def round_trip(args, output, evidence, label, commands, gate, *, gain=None, feedback=False, companion=False):
    folder = output / label
    companion_folder = output / "companion" / label
    moved = output / f"{label}: déplacé ü"
    cwd = output / "unrelated"

    def produce(renderer, node):
        node["setup"] = replay(renderer, commands, final_gate=gate)
        with closing(renderer.client()) as client:
            recipe = live_recipe(client)
            require(recipe["onset_gate"] == gate, recipe["onset_gate"])
            before = client.call("get_scene")
            node["cli"] = export_folder(renderer, folder)
            manifest_matches(folder, recipe)
            require(not (folder / "scene.scd").exists(), "default export wrote SuperCollider")
            if ":" in label:
                node["scd_path_rejection"] = export_folder(
                    renderer, output / f"{label}:companion", scd=True,
                    rejection="SuperCollider replay cannot use an asset folder containing a colon")
            if companion:
                node["cli_scd"] = export_folder(renderer, companion_folder, scd=True)
                manifest_matches(companion_folder, recipe)
                require((folder / "scene.vosc.json").read_bytes() ==
                        (companion_folder / "scene.vosc.json").read_bytes(), "companion changed native manifest")
            after = client.call("get_scene")
            require((before["revision"], before["external_epoch"]) ==
                    (after["revision"], after["external_epoch"]), "read-only export changed live renderer")
        pixels = observe(renderer, node, output, f"{label}-producer", recipe, gain=gain, feedback=feedback)
        if gain is not None:
            node["unsupported_export"] = reject_unsupported_export(renderer, output / "unsupported")
        return recipe, pixels

    recipe, expected = run_owned(args.binary, output, f"{label}-producer", free_udp_port(), produce, evidence)
    folder.rename(moved)
    require(not folder.exists(), "export origin still exists after relocation")

    def consume(renderer, node):
        node["ready"] = wait_ready(renderer)
        return observe(renderer, node, output, f"{label}-native", recipe, gain=gain, feedback=feedback)

    runtime_port = free_udp_port() if gain is not None or feedback else 0
    actual = run_owned(args.binary, output, f"{label}-native", runtime_port, consume, evidence,
                       cwd=cwd, arguments=("--scene", moved / "scene.vosc.json"))

    if companion:
        def consume_sc(renderer, node):
            node["sclang"] = run_sclang(args.sclang, companion_folder / "scene.scd", output, renderer.port,
                                       f"{label}-companion", args.visuals_quark)
            return observe(renderer, node, output, f"{label}-sc", recipe, gain=gain, feedback=feedback)

        sc = run_owned(args.binary, output, f"{label}-sc", free_udp_port(), consume_sc, evidence)
    else:
        sc = expected

    results = evidence.setdefault("comparisons", {})
    if feedback:
        results[label] = {
            str(step): {
                "native_sha256": sha256(actual[step]).hexdigest(),
                "producer_sha256": sha256(expected[step]).hexdigest(),
                "sc_sha256": sha256(sc[step]).hexdigest(),
            }
            for step in actual
        }
        require(actual == expected == sc, f"{label} feedback mismatch: {results[label]}")
    else:
        results[label] = compare(output, label, (800, 600), actual, expected)
        require(results[label]["changed_pixels"] == 0 and actual == sc, f"{label} pixels changed")
    return moved, recipe, actual


def edit_manifest(args, output, evidence, original, recipe, before):
    edited = output / "edited"
    shutil.copytree(original, edited)
    file = edited / "scene.vosc.json"
    manifest = json.loads(file.read_text())
    expected = normalize_descriptor(recipe)
    for commands in (manifest["commands"], expected["commands"]):
        gain = next(command for command in commands
                    if command["address"] == "/var" and command["args"][0]["value"] == "brightness")
        gain["args"][1]["value"] = 0.5
    file.write_text(json.dumps(manifest))

    def consume(renderer, node):
        node["ready"] = wait_ready(renderer)
        return observe(renderer, node, output, "edited-native", expected, gain=0.375)

    pixels = run_owned(args.binary, output, "edited-native", free_udp_port(), consume, evidence,
                       arguments=("--scene", file), cwd=output / "unrelated")
    evidence["comparisons"]["edit"] = compare(output, "edit", (800, 600), before, pixels)
    require(evidence["comparisons"]["edit"]["changed_pixels"] > 0, "manifest edit did not change the draw")


def feedback_negatives(args, output, evidence, folder):
    manifest = json.loads((folder / "scene.vosc.json").read_text())
    controls = next(command for command in manifest["commands"] if command["address"] == "/tex/var")
    controls["args"].append({"type": "f", "value": 0.25})
    file = folder / "invalid-control.vosc.json"
    file.write_text(json.dumps(manifest))
    evidence["feedback_invalid_control"] = negative(
        args.binary, output, "feedback-invalid-control", ["--scene", file],
        "feedback controls require exact reflected float scalar/vector arity",
        trace=("window", "preflight") if args.trace else ())

    block_source = (FIXTURES / "temporal.frag").read_text().replace(
        "uniform float decay;", "layout(std140) uniform FeedbackBlock { float decay; };")
    source = output / "feedback-block.frag"
    source.write_text(block_source)
    shutil.copy2(source, folder / source.name)
    block_manifest = json.loads((folder / "scene.vosc.json").read_text())
    next(command for command in block_manifest["commands"]
         if command["address"] == "/tex")["args"][1]["value"] = source.name
    block_file = folder / "invalid-block.vosc.json"
    block_file.write_text(json.dumps(block_manifest))
    expected = "feedback control has no writable location: decay"
    evidence["feedback_invalid_block"] = negative(
        args.binary, output, "feedback-invalid-block", ["--scene", block_file], expected,
        trace=("window", "preflight") if args.trace else ())

    owned_source = folder / "feedback-owned-name.frag"
    owned_source.write_text((FIXTURES / "temporal.frag").read_text().replace("decay", "numLights"))
    owned_manifest = json.loads((folder / "scene.vosc.json").read_text())
    next(command for command in owned_manifest["commands"]
         if command["address"] == "/tex")["args"][1]["value"] = owned_source.name
    next(command for command in owned_manifest["commands"]
         if command["address"] == "/tex/var")["args"][1]["value"] = "numLights"
    owned_file = folder / "invalid-owned-name.vosc.json"
    owned_file.write_text(json.dumps(owned_manifest))
    evidence["feedback_invalid_owned_name"] = negative(
        args.binary, output, "feedback-invalid-owned-name", ["--scene", owned_file],
        "feedback control overrides a built-in",
        trace=("window", "preflight") if args.trace else ())

    def produce(renderer, node):
        replay(renderer, setup_commands(FIXTURES / "temporal.frag"), final_gate=0)
        osc(renderer.port, "/tex", 0, "shader_pp:" + str(source))
        deadline = time.monotonic() + 5
        with closing(renderer.client()) as client:
            while time.monotonic() < deadline:
                reply = client.call("export_scene")
                if not reply["ok"] and expected in reply["error"]["message"]:
                    break
                time.sleep(0.03)
            else:
                raise RuntimeError(f"invalid feedback control did not reach export validation: {reply}")
        node["rejection"] = export_folder(renderer, output / "feedback-block-export", rejection=expected)

    run_owned(args.binary, output, "feedback-block-producer", free_udp_port(), produce, evidence)

    manifest["commands"] = [
        command for command in manifest["commands"] if
        not (command["address"] == "/tex/set" and command["args"][1]["value"] == "filter")
    ]
    file = folder / "invalid-settings.vosc.json"
    file.write_text(json.dumps(manifest))
    evidence["feedback_invalid_settings"] = negative(
        args.binary, output, "feedback-invalid-settings", ["--scene", file],
        "feedback setup requires every bounded storage and holder setting")


def typed_command(address, *args):
    return {"address": address, "args": [{"type": kind, "value": value} for kind, value in args]}


def invalid_uniforms(args, output, evidence):
    cases = (
        ("amount", "layout(std140) uniform Block { float amount; };", "amount",
         (0.75,), ("f",), "shader uniform has no writable location"),
        ("layer", "uniform int layer;", "float(layer)", (17,), ("i",), "renderer-owned shader uniform"),
        ("offset", "uniform vec2 offset;", "offset.x", (1.0, 2.0), ("f", "f"), "renderer-owned shader uniform"),
        ("numLights", "uniform int numLights;", "float(numLights)", (7,), ("i",), "renderer-owned shader uniform"),
        ("alpha", "uniform float alpha;", "alpha", (0.25,), ("f",), "layer-owned shader uniform"),
        ("settings", "struct Item { float amount; }; uniform Item settings[1];", "settings[0].amount",
         (0.75,), ("f",), "shader uniform has no writable location"),
    )

    def produce(renderer, node):
        for name, declaration, expression, values, kinds, expected in cases:
            source = output / f"invalid-{name}.frag"
            source.write_text(f"#version 330\n{declaration}\nout vec4 color;\n"
                              f"void main(){{color=vec4({expression},0,0,1);}}\n")
            replay(renderer, [
                ("/layers/reset", (1, "stack")),
                ("/shader", (0, str(source))),
                ("/shader/var", (0, name, *values)),
            ])
            node[name] = export_folder(renderer, output / f"rejected-{name}", rejection=expected)
            manifest = {
                "version": 1, "title": name, "size": [800, 600],
                "commands": [
                    typed_command("/layers/reset", ("i", 1), ("s", "stack")),
                    typed_command("/shader", ("i", 0), ("asset", source.name)),
                    typed_command("/shader/var", ("i", 0), ("s", name), *zip(kinds, values)),
                ],
            }
            file = output / f"invalid-{name}.json"
            file.write_text(json.dumps(manifest))
            evidence["invalid_" + name] = negative(
                args.binary, output, f"invalid-{name}", ["--scene", file], expected,
                trace=("window", "preflight") if args.trace else ())

    run_owned(args.binary, output, "invalid-uniform", free_udp_port(), produce, evidence)


def texture_slots(args, output, evidence, folder):
    original = json.loads((folder / "scene.vosc.json").read_text())
    named = next(cmd for cmd in original["commands"] if cmd["address"] == "/shader/texture")
    path = named["args"][2]["value"]
    shader = folder / "slot.frag"
    shader.write_text("""#version 330
uniform sampler2D tex;
out vec4 fragColor;
void main() { fragColor = texture(tex, vec2(0.5)); }
""")
    setup = [
        typed_command("/layers/reset", ("i", 1), ("s", "stack")),
        typed_command("/shader", ("i", 0), ("asset", shader.name)),
    ]
    default = typed_command("/tex", ("i", 0), ("asset", path))
    named_default = typed_command("/shader/texture", ("i", 0), ("s", "tex"), ("asset", path))
    for index, declarations in enumerate(((default, named_default), (named_default, default))):
        fixture = folder / f"slot-collision-{index}.json"
        fixture.write_text(json.dumps({**original, "commands": [*setup, *declarations]}))
        evidence[f"slot_collision_{index}"] = negative(
            args.binary, output, f"slot-collision-{index}", ["--scene", fixture],
            "/tex" if index else "/shader/texture")

    def consume(renderer, node):
        node["ready"] = wait_ready(renderer)
        with closing(renderer.client()) as client:
            recipe = live_recipe(client)
            width, height, pixels, _ = capture(client, output / f"{renderer.log.stem}.png")
        center = pixels[height // 2 * width + width // 2]
        require(center == (255, 0, 0, 255), f"default-name sampler drew {center}")
        setting = next(command for command in recipe["commands"] if command["address"] == "/tex/size")
        require([arg["value"] for arg in setting["args"]] == [0, 32.0, 24.0],
                f"default-name sampler size changed: {setting}")
        node["center_pixel"], node["setting"] = center, setting

    for label, command, address, selector in (
        ("default-slot", default, "/tex/size", (("i", 0),)),
        ("named-default-slot", named_default, "/shader/texture/size", (("i", 0), ("s", "tex"))),
    ):
        fixture = folder / f"{label}.json"
        size = typed_command(address, *selector, ("f", 32.0), ("f", 24.0))
        fixture.write_text(json.dumps({**original, "commands": [*setup, command, size]}))
        run_owned(args.binary, output, label, 0, consume, evidence, arguments=("--scene", fixture))

    green = (0, 255, 0, 255)
    (folder / "other.png").write_bytes(png_rgba(4, 4, green, green))
    dual = folder / "slot-pair.frag"
    dual.write_text("""#version 330
uniform sampler2D tex;
uniform sampler2D other;
out vec4 fragColor;
void main() {
    fragColor = gl_FragCoord.x < 400.0 ? texture(tex, vec2(0.5)) : texture(other, vec2(0.5));
}
""")
    fixture = folder / "slot-pair.json"
    fixture.write_text(json.dumps({**original, "commands": [
        setup[0], typed_command("/shader", ("i", 0), ("asset", dual.name)), default,
        typed_command("/shader/texture", ("i", 0), ("s", "other"), ("asset", "other.png")),
    ]}))

    def consume_pair(renderer, node):
        node["ready"] = wait_ready(renderer)
        with closing(renderer.client()) as client:
            width, height, pixels, _ = capture(client, output / "slot-pair.png")
        row = height // 2 * width
        node["left"], node["right"] = pixels[row + width // 4], pixels[row + 3 * width // 4]
        require((node["left"], node["right"]) == ((255, 0, 0, 255), green), "texture slots were swapped")

    run_owned(args.binary, output, "slot-pair", 0, consume_pair, evidence, arguments=("--scene", fixture))


def large_manifest(args, output, evidence):
    folder = output / "large"

    def produce(renderer, node):
        replay(renderer, [("/layers/reset", (32, "grid"))], final_gate=0)
        for key, values in (("pos", (0, 0, 0)), ("rot", (0, 0, 0)), ("scale", (1, 1, 1)),
                            ("pivot", (0, 0, 0)), ("tint", (1, 1, 1, 1))):
            expressions = [f"t*0+{v}".ljust(2047) + "\t" for v in values]
            osc(renderer.port, f"/layer/{key}", "*", json.dumps(expressions))
        # Wait for the queued OSC edits to be applied before issuing the query.
        time.sleep(0.6)
        node["cli"] = export_folder(renderer, folder)
        file = folder / "scene.vosc.json"
        require(file.stat().st_size > 1024 * 1024, "fixture did not exceed the old manifest limit")
        node["manifest_bytes"] = file.stat().st_size
        node["scd_rejection"] = export_folder(
            renderer, output / "large-companion", scd=True,
            rejection="SuperCollider replay bundle exceeds")
        with closing(renderer.client()) as client:
            expected = live_recipe(client)
        manifest_matches(folder, expected)
        return expected

    expected = run_owned(args.binary, output, "large-producer", free_udp_port(), produce, evidence)

    def consume(renderer, node):
        node["ready"] = wait_ready(renderer)
        with closing(renderer.client()) as client:
            require(live_recipe(client) == expected, "large JSON changed retained expression data")

    run_owned(args.binary, output, "large-native", 0, consume, evidence,
              arguments=("--scene", folder / "scene.vosc.json"))
    oversize = output / "oversize.vosc.json"
    with oversize.open("wb") as stream:
        stream.truncate(9 * 1024 * 1024 + 1)
    evidence["oversize_manifest"] = negative(args.binary, output, "oversize-manifest",
        ["--scene", oversize], "file size is outside 1..9437184")


def image_budget(args, output, evidence):
    image = output / "flat.png"
    color = (64, 128, 192, 255)
    image.write_bytes(png_rgba(2048, 2048, color, color))
    bindings = [("/tex", (i, str(image))) for i in range(5)]
    round_trip(args, output, evidence, "decoded-budget", [
        ("/layers/reset", (4, "stack")), *bindings[:4],
    ], -1)

    def produce(renderer, node):
        replay(renderer, [("/layers/reset", (5, "stack")), *bindings])
        node["rejection"] = export_folder(
            renderer, output / "decoded-over-budget", rejection="64 MiB decoded image budget")

    run_owned(args.binary, output, "decoded-over-budget", free_udp_port(), produce, evidence)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("binary", "sclang", "visuals-quark", "output"):
        parser.add_argument("--" + key, type=Path, required=True)
    parser.add_argument("--trace", action="store_true", help="expect recovery build startup traces")
    args = parser.parse_args()
    args.binary, args.output = args.binary.resolve(), args.output.resolve()
    output = args.output
    output.mkdir(parents=True)
    (output / "unrelated").mkdir()
    evidence = {}
    try:
        shader = output / "input.frag"
        shader.write_text("""#version 330
uniform sampler2D imageTex;
uniform float imageTexSize;
uniform float gain;
uniform float singleton[1];
uniform ivec2 probe;
out vec4 fragColor;
void main() {
    bool configured = singleton[0] == 0.5 && probe == ivec2(2, 3) && imageTexSize == 6.0;
    fragColor = configured ? vec4(texture(imageTex, vec2(0.5)).rgb * gain, 1.0) : vec4(1, 0, 1, 1);
}
""")
        image = output / "image.png"
        image.write_bytes(png_rgba(4, 4, (255, 0, 0, 255), (255, 0, 0, 255)))
        commands = [
            ("/layers/reset", (2, "stack")),
            ("/input", (0, "", 0, 256)),
            ("/input", ("aux", "", 0, 256)),
            ("/input/set", (0, "maxAmp", 0.5)),
            ("/input/set", ("aux", "maxLoud", 0.125)),
            ("/var", ("brightness", 1.0)),
            ("/var", ("offsetProbe", 1.0)),
            ("/var", ("combinedGain", 0.0)),
            ("/var", ("combinedGain", "(in_0+in_aux)*brightness")),
            ("/shader", (0, str(shader))),
            ("/shader/var", (0, "gain", "combinedGain")),
            ("/shader/var", (0, "singleton", 0.5)),
            ("/shader/var", (0, "probe", "offsetProbe+1", 3)),
            ("/shader/texture", (0, "imageTex", str(image))),
            ("/shader/texture", (0, "unusedSampler", str(image))),
            ("/shader/var", (0, "imageTexSize", 6.0)),
            ("/tex", (1, str(image))),
            ("/tex/size", (1, 64.0, 64.0)),
            ("/tex/set", (1, "static", 1)),
            ("/tex/set", (1, "aspectRatio", 0)),
            ("/layer/rot", (1, 0.0, 0.0, 5.0)),
            ("/layer/visible", (1, 0)),
        ]
        moved, recipe, pixels = round_trip(
            args, output, evidence, "input-images", commands, 0, gain=0.75, companion=True)
        edit_manifest(args, output, evidence, moved, recipe, pixels)
        round_trip(args, output, evidence, "colon:default-image", [
            ("/layers/reset", (1, "stack")),
            ("/tex", (0, str(image))),
            ("/tex/set", (0, "aspectRatio", 0)),
            ("/tex/set", (0, "static", 1)),
        ], -1)
        invalid_uniforms(args, output, evidence)
        texture_slots(args, output, evidence, moved)
        large_manifest(args, output, evidence)
        image_budget(args, output, evidence)
        for layout, count, gate in (("grid", 32, 1), ("col", 2, 0), ("row", 0, -1)):
            round_trip(args, output, evidence, layout, [("/layers/reset", (count, layout))], gate)
        for name in ("spatial", "temporal"):
            moved, _, _ = round_trip(
                args, output, evidence, name,
                setup_commands(FIXTURES / f"{name}.frag", gradient=name == "spatial"),
                -1, feedback=True, companion=True)
            if name == "temporal":
                feedback_negatives(args, output, evidence, moved)
        evidence["status"] = "passed"
        print(f"PASS scene JSON export and replay. Evidence: {output}")
    except Exception as error:
        evidence.update(status="failed", error=str(error))
        raise
    finally:
        (output / "evidence.json").write_text(json.dumps(evidence, indent=2) + "\n")


if __name__ == "__main__":
    main()
