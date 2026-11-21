#!/usr/bin/env python3
"""Verify native scene startup and SuperCollider replay using owned processes."""

from __future__ import annotations

import argparse
from contextlib import closing
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import time
import zlib

from generate_example_scd import sieve_scene
from test_scene_recipe import (
    ROOT, OwnedRenderer, arm_recovery_hook, capture, collect_cleanup, free_udp_port, osc, run_sclang,
)

SIEVE_IMAGE = "black_hole.jpg"
SIEVE_MARKER = "VOSC_SIEVE_SEND_COMPLETE"


def require(condition, detail):
    if not condition:
        raise RuntimeError(detail)


def redirect_sc(text, port, *, sieve):
    require(0 < port < 65536 and port != 33333, "expected owned private UDP port")
    original = "var rendererPort = 33333;" if sieve else "var rendererPort = 43333;"
    require(text.count(original) == 1, "original endpoint is ambiguous")
    require(text.count('var rendererHost = "127.0.0.1";') == 1, "unexpected example host")
    if sieve:
        require(text.rstrip().endswith(")"), "expected trailing Sieve block")
        text = text.rstrip()[:-1] + f'"{SIEVE_MARKER}".postln;\n)\n'
    return text.replace(original, f"var rendererPort = {port};", 1), "verified-owned-example"


def read_state(client):
    response = client.call("get_scene")
    require(response.get("ok"), response)
    return response["result"]


def comparable_state(state):
    return {
        "layout": state["layout"],
        "size": [state["width"], state["height"]],
        "onset_gate": state["onset_gate"],
        "layers": [
            {
                **{key: layer[key] for key in ("alpha", "visible", "size", "geometry_loaded")},
                "shader_loaded": layer["shader"]["loaded"],
                "uniforms": {
                    key: (value["gl_type"], value.get("rendered_value"))
                    for key, value in layer["shader"]["uniforms"].items()
                    if key in ("dim", "sievedim", "multiplier", "vColor", "hColor", "globalColor")
                },
                "samplers": {key: value["loaded"] for key, value in layer["shader"]["textures"].items()},
            }
            for layer in state["layers"]
        ],
    }


def observe(renderer, output, label, size, *, native=False, sieve=False):
    with closing(renderer.client()) as client:
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            if renderer.process.poll() is not None:
                raise RuntimeError(renderer.log.read_text(errors="replace"))
            lines = renderer.log.read_text(errors="replace").splitlines()
            ready = [line for line in lines if line.startswith("VOSC_SCENE_READY ")]
            state = read_state(client)
            layers = state.get("layers", [])
            if any(line.startswith("[ error ]") for line in lines):
                raise RuntimeError(f"{label} reported a renderer error. See {renderer.log}")
            if len(layers) == 1 and layers[0]["shader"]["loaded"] and (not native or len(ready) == 1):
                textures = layers[0]["shader"]["textures"]
                if not sieve or all(textures.get(name, {}).get("loaded") and textures[name]["source"]
                                    for name in ("sieve", "srctex")):
                    break
            time.sleep(0.05)
        else:
            raise RuntimeError(f"{label} did not settle")
        image = output / f"{label}.png"
        width, height, pixels, receipt = capture(client, image, max_width=size[0])
        require((width, height) == size, f"{label}: expected capture {size}, got {(width, height)}")
        state = read_state(client)
    receipt.pop("data_base64", None)
    observation = {"capture": receipt, "image": str(image), "ready": ready, "state": state}
    return pixels, observation


def compare(output, label, size, left, right):
    deltas = [tuple(abs(a - b) for a, b in zip(first, second)) for first, second in zip(left, right)]
    width, height = size
    require(len(left) == len(right) == len(deltas) == width * height, f"{label}: bad dimensions")
    changed = sum(any(pixel) for pixel in deltas)
    stats = {
        "changed_pixels": changed, "total_pixels": len(deltas),
        "mean_channel_error": sum(map(sum, deltas)) / (4 * len(deltas)),
        "max_channel_error": max(map(max, deltas)),
    }
    if changed:
        def chunk(kind, payload):
            return (struct.pack(">I", len(payload)) + kind + payload +
                    struct.pack(">I", zlib.crc32(kind + payload)))

        pixels = bytes(channel for pixel in deltas
                       for channel in (min(255, 8 * max(pixel)),) * 3 + (255,))
        stride = width * 4
        raw = b"".join(b"\0" + pixels[start:start + stride] for start in range(0, len(pixels), stride))
        diff = output / f"{label}-diff.png"
        diff.write_bytes(b"\x89PNG\r\n\x1a\n" +
                         chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) +
                         chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))
        stats["diff_image"] = str(diff)
        # The lookup repeats every six pixels. A residue never changing anywhere
        # in the frame indicates a lookup/tint branch unaffected by resampling.
        residues = {(index % width % 6, (index // width) % 6)
                    for index, delta in enumerate(deltas) if any(delta)}
        stats["changed_lookup_residues"] = sorted(residues)
    return stats


def run_owned(binary, output, label, port, callback, evidence, **options):
    renderer = OwnedRenderer(binary, output, label, port, [], **options)
    node = evidence.setdefault("renders", {})[label] = {"owner": renderer.owner}
    try:
        return callback(renderer, node)
    finally:
        if renderer.hooks_enabled and renderer.process.poll() is None:
            arm_recovery_hook(renderer, {"id": "native-scene-cleanup", "operation": "shutdown"})
            try:
                renderer.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                pass
        node["cleanup"], error = collect_cleanup(renderer)
        require(not error, f"{label} cleanup failed: {error}")


def run_port(args, output, file, label, size, cwd, evidence, *, sieve):
    def run(renderer, node):
        pixels, observation = observe(renderer, output, label, size, native=True, sieve=sieve)
        node.update(observation)
        return pixels, comparable_state(observation["state"])

    return run_owned(args.binary, output, label, 0, run, evidence, arguments=("--scene", file), cwd=cwd)


def run_animated_examples(args, output, cwd, evidence):
    size = (1024, 768)
    for name in ("organic-trails", "sound-data"):
        staged = output / f"{name}-staged"
        relocated = output / f"{name} déplacé"
        shutil.copytree(ROOT / "examples" / name, staged)
        staged.rename(relocated)
        manifest = relocated / "scene.vosc.json"

        def run(renderer, node):
            pixels, observation = observe(renderer, output, name, size, native=True)
            node.update(observation)
            layer, = observation["state"]["layers"]
            if name == "organic-trails":
                uniforms = layer["shader"]["uniforms"]
                manifest_data = json.loads(manifest.read_text())
                for command in manifest_data["commands"]:
                    if command["address"] != "/shader/var":
                        continue
                    _, control, *components = (arg["value"] for arg in command["args"])
                    format_string = f"!{len(components)}f"
                    expected = list(struct.unpack(format_string, struct.pack(format_string, *components)))
                    uniform = uniforms[control]
                    rendered = json.loads(uniform["rendered_value"])
                    if not isinstance(rendered, list):
                        rendered = [rendered]
                    require(len(rendered) == len(expected) and all(
                        math.isclose(actual, target, rel_tol=5e-6)
                        for actual, target in zip(rendered, expected)
                    ), f"{control} rendered value")
                next_pixels, _ = observe(renderer, output, "organic-trails-next", size, native=True)
                require(pixels != next_pixels and any(max(pixel[:3]) > 80 for pixel in pixels),
                        "Organic Trails did not render moving particles")
                return

            require(layer["alpha"] == 0.0, "Sound Data alpha without a sample")
            osc(renderer.port, "/input/data", "analysis", 0.7, 1.0, 0)
            active, sample = observe(renderer, output, "sound-data-sample", size, native=True)
            require(sample["state"]["layers"][0]["alpha"] > 0.0 and active != pixels,
                    "Sound Data sample did not drive the rendered opacity")
            osc(renderer.port, "/input/data", "analysis", 0.0)
            cleared, sample = observe(renderer, output, "sound-data-cleared", size, native=True)
            require(sample["state"]["layers"][0]["alpha"] == 0.0 and cleared == pixels,
                    "Sound Data cleanup did not clear the rendered opacity")

        run_owned(args.binary, output, name, free_udp_port(), run, evidence,
                  arguments=("--scene", manifest), cwd=cwd)


def run_companion(args, output, folder, label, size, cwd, evidence, *, sieve, native=False):
    original = folder / "scene.scd"
    file = folder / f"{label}.scd"

    def run(renderer, node):
        node["sclang"] = run_sclang(
            args.sclang, file, output, renderer.port, label, args.visuals_quark,
            ordinary=sieve, ordinary_marker=SIEVE_MARKER,
            prepare_endpoint=lambda text, port: redirect_sc(text, port, sieve=sieve),
        )
        pixels, observation = observe(renderer, output, label, size, native=native, sieve=sieve)
        node.update(observation)
        return pixels, comparable_state(observation["state"])

    arguments = ()
    if native:
        arguments = ("--scene", folder / "scene.vosc.json")
    elif sieve:
        launch_size = re.search(r"^// Blank renderer: vosc --size (\d+x\d+)$",
                                original.read_text(), re.MULTILINE)
        require(launch_size and launch_size[1] == f"{size[0]}x{size[1]}",
                "Sieve's generated blank-renderer command does not match the manifest dimensions")
        arguments = ("--size", launch_size[1])
    file.write_bytes(original.read_bytes())
    try:
        return run_owned(args.binary, output, label, free_udp_port(), run, evidence, arguments=arguments, cwd=cwd)
    finally:
        file.unlink()


def run_sieve_manifest_variant(args, output, cwd, evidence):
    folder = output / "sieve custom déplacé"
    shutil.copytree(ROOT / "examples/sieve", folder)
    manifest_file = folder / "scene.vosc.json"
    manifest = json.loads(manifest_file.read_text())
    size = (800, 600)
    manifest.update(size=list(size), onset_gate=1)
    manifest_file.write_text(json.dumps(manifest))
    (folder / "scene.scd").write_text(sieve_scene(manifest))
    native = run_port(args, output, manifest_file, "sieve-custom-native", size, cwd, evidence, sieve=True)
    replay = run_companion(args, output, folder, "sieve-custom-sc", size, cwd, evidence, sieve=True)
    key = "sieve-custom-sc-parity"
    comparison = compare(output, key, size, native[0], replay[0])
    evidence["comparisons"][key] = comparison
    require(native[1]["onset_gate"] is True and replay[1]["onset_gate"] is True,
            "Sieve custom onset gate did not take effect in both renderers")
    require(comparison["changed_pixels"] == 0 and native[1] == replay[1], key)


def negative(binary, output, label, argv, expected, *, trace=(), inject=None):
    with tempfile.TemporaryDirectory(prefix="vosc-cli-negative-", dir="/tmp") as runtime:
        socket = Path(runtime) / "control.sock"
        environment = {**os.environ, "VOSC_OSC_PORT": "0",
                       "VOSC_CONTROL_SOCKET": str(socket),
                       "VOSC_RECOVERY_TEST_DIR": runtime, "VOSC_TEST_TRACE_SCENE": "1"}
        if inject:
            environment[inject] = "1"
        command = [str(binary), *map(str, argv)]
        log = output / f"negative-{label}.log"
        try:
            run = subprocess.run(command, cwd=binary.parents[3], env=environment,
                                 capture_output=True, text=True, timeout=20, start_new_session=True)
        except subprocess.TimeoutExpired as error:
            log.write_bytes((error.stdout or b"") + (error.stderr or b""))
            raise
        log.write_text(run.stdout + run.stderr)
        require(run.returncode > 0 and expected in run.stderr and "VOSC_SCENE_READY" not in run.stdout,
                f"{label}: wanted nonzero exit with {expected!r}. See {log}")
        observed = tuple(line.removeprefix("VOSC_TEST_SCENE ") for line in run.stderr.splitlines()
                         if line.startswith("VOSC_TEST_SCENE "))
        require(observed == trace, f"{label}: expected scene boundaries {trace}, saw {observed}. See {log}")
        require(not socket.exists() and not (Path(runtime) / "events.jsonl").exists(),
                f"{label}: initialized control endpoint or left a socket for invalid input")
        require(trace or "GPU :" not in run.stdout, f"{label}: initialized renderer during CPU preflight failure")
        return {"argv": command, "exit": run.returncode, "stderr": run.stderr, "log": str(log),
                "scene_boundaries": observed, "control_socket_exists": socket.exists()}


def run_negatives(args, output, evidence):
    results = evidence.setdefault("negative", {})
    work = output / "invalid-fixtures"
    shutil.copytree(ROOT / "examples/sieve", work)
    base = work / "scene.vosc.json"

    for label, argv in (("missing-argument", ["--scene"]), ("repeated-argument", ["--scene", base, "--scene", base])):
        results[label] = negative(args.binary, output, label, argv, "Usage:")
    help_run = subprocess.run([str(args.binary), "--help"], capture_output=True, text=True, timeout=10)
    require(help_run.returncode == 0 and "--scene" in help_run.stdout, "wrong --help output")
    evidence["help"] = help_run.stdout

    def fixture(label, change, expected, *, trace=()):
        file = work / f"{label}.json"
        value = json.loads(base.read_text())
        change(value)
        file.write_text(json.dumps(value))
        results[label] = negative(args.binary, output, label, ["--scene", file], expected, trace=trace)

    malformed = work / "malformed.json"
    malformed.write_text("{ broken")
    results["malformed"] = negative(args.binary, output, "malformed", ["--scene", malformed], "parse error")
    raw_cases = {
        "duplicate-key": ('{"version":1,"version":1}', "duplicate JSON key: version"),
        "raw-nul": ('{}\0{}', "scene file contains NUL"),
        "double-underflow": ('{"value":1e-999}', "nonzero JSON number underflows float32"),
    }
    for label, (content, expected) in raw_cases.items():
        file = work / f"{label}.json"
        file.write_bytes(content.encode())
        results[label] = negative(args.binary, output, label, ["--scene", file], expected)
    fixture("version", lambda data: data.update(version=2), "unsupported scene version")
    fixture("onset-gate", lambda data: data.update(onset_gate=2), "onset_gate must be an integer in -1..1")
    fixture("address", lambda data: data["commands"][-1].update(address="/bogus"), "command 9 /bogus")
    fixture("arity", lambda data: data["commands"][-1]["args"].clear(), "command 9 /shader/var")
    fixture("order", lambda data: data["commands"].reverse(), "command 1 /shader/var")
    fixture("integer-overflow", lambda data: data["commands"][4]["args"][2].update(value=2147483648),
            "command 5 /shader/var")
    require((args.binary.parents[3] / "data/images/black_hole.jpg").is_file(),
            "private build data must contain a matching basename for the missing-asset case")
    fixture("missing", lambda data: data["commands"][2]["args"][2].update(value="black_hole.jpg"), "black_hole.jpg")
    fixture("traversal", lambda data: data["commands"][2]["args"][2].update(value="../outside.png"),
            "scene-relative asset path")
    (work / "assets/escape.png").symlink_to(ROOT / "bin/data/images/square_3x3.png")
    fixture("symlink", lambda data: data["commands"][2]["args"][2].update(value="assets/escape.png"),
            "inside the scene folder")
    (work / "assets/corrupt.png").write_bytes(b"not an image")
    fixture("corrupt", lambda data: data["commands"][2]["args"][2].update(value="assets/corrupt.png"),
            "invalid image header")
    def write_png(path, width, height, *, rgba16=False):
        def chunk(kind, payload):
            return (struct.pack(">I", len(payload)) + kind + payload +
                    struct.pack(">I", zlib.crc32(kind + payload)))

        depth, color_type = (16, 6) if rgba16 else (8, 0)
        header = struct.pack(">IIBBBBB", width, height, depth, color_type, 0, 0, 0)
        row = bytes(width * (8 if rgba16 else 1) + 1)
        compressor = zlib.compressobj()
        pixels = b"".join(compressor.compress(row) for _ in range(height)) + compressor.flush()
        path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) +
                         chunk(b"IDAT", pixels) + chunk(b"IEND", b""))

    write_png(work / "assets/over-side.png", 4097, 1)
    fixture("over-side", lambda data: data["commands"][2]["args"][2].update(value="assets/over-side.png"),
            "4096-pixel per-side")
    write_png(work / "assets/over-area.png", 4096, 2442)
    fixture("over-area", lambda data: data["commands"][2]["args"][2].update(value="assets/over-area.png"),
            "10000000-pixel limit")
    fixture("decoded-budget", lambda data: data["commands"].insert(3, data["commands"][2]),
            "64 MiB decoded image budget")
    write_png(work / "assets/rgba16.png", 4000, 2500, rgba16=True)
    fixture("wide-decoded-rows", lambda data: data["commands"][2]["args"][2].update(value="assets/rgba16.png"),
            "64 MiB decoded image budget")
    write_png(work / "assets/at-side.png", 4096, 1)
    write_png(work / "assets/at-area.png", 4000, 2500)
    dimensions = json.loads(base.read_text())
    dimensions["commands"][2]["args"][2]["value"] = "assets/at-side.png"
    dimensions["commands"][3]["args"][2]["value"] = "assets/at-area.png"
    dimension_manifest = work / "at-dimension-limits.json"
    dimension_manifest.write_text(json.dumps(dimensions))
    run_port(args, output, dimension_manifest, "at-dimension-limits", (1024, 768),
             args.binary.parents[3], evidence, sieve=True)
    (work / "assets/include.frag").write_text('#version 400\n#pragma include "foreign.glsl"\n')
    fixture("shader-include", lambda data: data["commands"][1]["args"][1].update(value="assets/include.frag"),
            "shader includes are outside scene v1")
    (work / "assets/broken.frag").write_text("#version 400\nvoid main(){BROKEN}\n")
    fixture("bad-glsl", lambda data: data["commands"][1]["args"][1].update(value="assets/broken.frag"),
            "command 2 /shader: shader compilation", trace=("window", "preflight"))

    def bad_uniform(data):
        command = data["commands"].pop(4)
        for arg in command["args"][2:]:
            arg.update(type="f", value=float(arg["value"]))
        data["commands"].append(command)

    fixture("bad-uniform", bad_uniform, "command 9 /shader/var: shader uniform dim requires int32",
            trace=("window", "preflight"))

    (work / "assets/managed.frag").write_text(
        "#version 400\nuniform int layer;\nout vec4 color;\n"
        "void main(){color = layer == 17 ? vec4(0,1,0,1) : vec4(1,0,0,1);}\n")

    def uniform_probe(data, shader_path, name, kind, value):
        reset, shader = data["commands"][:2]
        shader["args"][1]["value"] = shader_path
        data["commands"] = [reset, shader, {"address": "/shader/var", "args": [
            {"type": "i", "value": 0}, {"type": "s", "value": name}, {"type": kind, "value": value}]}]

    fixture("managed-uniform", lambda data: uniform_probe(data, "assets/managed.frag", "layer", "i", 17),
            "command 3 /shader/var: renderer-managed shader uniform: layer",
            trace=("window", "preflight"))

    (work / "assets/uniform-block.frag").write_text(
        "#version 400\nlayout(std140) uniform Config { float amount; };\nout vec4 color;\n"
        "void main(){color = vec4(amount,0,0,1);}\n")
    fixture("uniform-block", lambda data: uniform_probe(data, "assets/uniform-block.frag", "amount", "f", 0.75),
            "command 3 /shader/var: shader uniform has no writable location: amount",
            trace=("window", "preflight"))

    (work / "assets/managed-size.frag").write_text(
        "#version 400\nuniform sampler2D srctex;\nuniform vec2 srctexSize;\nout vec4 color;\n"
        "void main(){color = texture(srctex, gl_FragCoord.xy / srctexSize);}\n")

    def managed_texture_size(data):
        reset, shader, texture = data["commands"][:3]
        shader["args"][1]["value"] = "assets/managed-size.frag"
        size = {"address": "/shader/var", "args": [
            {"type": "i", "value": 0}, {"type": "s", "value": "srctexSize"},
            {"type": "f", "value": 8.0}, {"type": "f", "value": 8.0}]}
        data["commands"] = [reset, shader, size, texture]

    fixture("managed-texture-size", managed_texture_size,
            "command 3 /shader/var: renderer-managed shader uniform: srctexSize", trace=("window", "preflight"))

    asset = work / "assets" / SIEVE_IMAGE
    results["upload-failure"] = negative(
        args.binary, output, "upload-failure", ["--scene", base],
        f"command 3 /shader/texture: image upload failed: {asset}: OpenGL error 1282",
        trace=("window", "preflight", "receiver", "reset", "command", "command", "command"), inject="VOSC_TEST_FAIL_SCENE_UPLOAD")

    (work / "assets/invalid-texture-size.frag").write_text(
        "#version 400\nuniform sampler2D srctex;\nuniform int srctexSize;\nout vec4 color;\n"
        "void main(){color = texture(srctex, gl_FragCoord.xy / float(max(srctexSize, 1)));}\n")

    def bad_texture_size_type(data):
        data["commands"] = data["commands"][:3]
        data["commands"][1]["args"][1]["value"] = "assets/invalid-texture-size.frag"

    fixture("invalid-texture-size-type", bad_texture_size_type,
            "command 3 /shader/texture: renderer-managed texture size requires a writable vec2: srctexSize",
            trace=("window", "preflight"))

    results["no-arg-setup-failure"] = negative(
        args.binary, output, "no-arg-setup-failure", [], "forced no-argument setup failure",
        trace=("window",), inject="VOSC_TEST_FAIL_NO_ARG_SETUP")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--sclang", type=Path, required=True)
    parser.add_argument("--visuals-quark", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.binary, args.sclang, output = (path.resolve() for path in (args.binary, args.sclang, args.output))
    output.mkdir(parents=True, exist_ok=False)
    evidence = {"binary_sha256": hashlib.sha256(args.binary.read_bytes()).hexdigest()}
    try:
        run_negatives(args, output, evidence)
        run_owned(args.binary, output, "no-arg-shutdown", 0, lambda renderer, node: None, evidence)
        records = evidence["comparisons"] = {}
        cwd = output / "unrelated"
        cwd.mkdir()
        run_animated_examples(args, output, cwd, evidence)
        for folder, size in (("editable-scene", (640, 480)), ("sieve", (1024, 768))):
            sieve = folder == "sieve"
            repo_example = ROOT / "examples" / folder
            origin = output / f"{folder}-staged"
            relocated = output / f"{folder} déplacé"
            shutil.copytree(repo_example, origin)
            native = run_port(args, output, origin / "scene.vosc.json", f"{folder}-native", size,
                              args.binary.parents[3], evidence, sieve=sieve)
            origin.rename(relocated)
            require(not origin.exists(), "staged origin remained after relocation")
            moved = run_port(args, output, relocated / "scene.vosc.json", f"{folder}-relocated", size,
                             cwd, evidence, sieve=sieve)
            key = f"{folder}-relocation"
            records[key] = compare(output, key, size, native[0], moved[0])
            require(records[key]["changed_pixels"] == 0 and native[1] == moved[1], key)
            sc = run_companion(args, output, relocated, f"{folder}-sc", size, cwd, evidence, sieve=sieve)
            if sieve:
                replay = run_companion(args, output, relocated, "sieve-sc-after-native", size, cwd, evidence,
                                       sieve=True, native=True)
                key = "sieve-sc-after-native"
                records[key] = compare(output, key, size, native[0], replay[0])
                require(records[key]["changed_pixels"] == 0 and native[1] == replay[1], key)
                uniforms = native[1]["layers"][0]["uniforms"]
                require(uniforms["dim"] == (0x8B53, "[1024, 768]") and
                        uniforms["sievedim"] == (0x8B53, "[6, 6]") and uniforms["multiplier"][0] == 0x8B50,
                        f"incorrect Sieve uniform types or values: {uniforms}")
            key = f"{folder}-sc-parity"
            records[key] = compare(output, key, size, sc[0], native[0])
            require(records[key]["changed_pixels"] == 0 and sc[1] == native[1],
                    f"{key} differs in pixels or state. See {output / 'evidence.json'}")
        run_sieve_manifest_variant(args, output, cwd, evidence)
        evidence["status"] = "passed"
        print(f"PASS native scene CLI; evidence: {output}")
        return 0
    except Exception as error:
        evidence.update(status="failed", error=str(error))
        raise
    finally:
        (output / "evidence.json").write_text(json.dumps(evidence, indent=2) + "\n")


if __name__ == "__main__":
    raise SystemExit(main())
