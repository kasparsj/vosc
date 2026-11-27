#!/usr/bin/env python3
"""Run the native pinned 26.frag round trip and retain evidence for IR-01–03."""

from __future__ import annotations

import argparse
import base64
from contextlib import closing
import hashlib
import json
import math
from pathlib import Path
import platform
import shutil
import signal
import subprocess
import sys
import time
from urllib.request import urlopen

from glsl import convert_legacy_fragment

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from test_engine_api import png_pixels  # noqa: E402
from test_native_scene_cli import read_state, require  # noqa: E402
from test_native_scene_v2 import await_ready, folder_hashes  # noqa: E402
from test_scene_recipe import OwnedRenderer, collect_cleanup, free_udp_port, osc  # noqa: E402

REPOSITORY = "https://github.com/tado/100fragments"
COMMIT = "c3bb9905f927de5be1b2357ee7522408cf7946d5"
SOURCE_PATH = "26.frag"
SOURCE_URL = f"https://raw.githubusercontent.com/tado/100fragments/{COMMIT}/{SOURCE_PATH}"
SOURCE_SHA256 = "d38439b1000ecc4581ccbb84ad34372baa34953698956a601c2bc12e9fa8da9e"
SIZE = (320, 240)
GLOW_EXPRESSION = "in_0*2.0"
SAMPLES = (0.2, 0.8)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def source_revisions(output, evidence):
    with urlopen(SOURCE_URL, timeout=15) as response:
        original = response.read(128 * 1024 + 1)
    require(digest(original) == SOURCE_SHA256, "pinned upstream source digest mismatch")
    conversion = convert_legacy_fragment(original)
    require(conversion == convert_legacy_fragment(original), "conversion is nondeterministic")
    # The creative edit applies only to the exact pinned fixture and remains
    # separate from format conversion.
    exponent = b"pow(abs(st.x), 0.03)"
    uniform_anchor = b"uniform vec2 resolution;"
    require(conversion.source.count(exponent) == conversion.source.count(uniform_anchor) == 1,
            "pinned creative revision anchors changed")
    edited = conversion.source.replace(exponent, b"pow(abs(st.x), 0.03 * glow)").replace(
        uniform_anchor, uniform_anchor + b"\nuniform float glow = 1.0;")
    revisions = output / "revisions"
    revisions.mkdir()
    evidence["source"] = {"repository": REPOSITORY, "ref": "master", "commit": COMMIT,
                          "path": SOURCE_PATH, "url": SOURCE_URL,
                          "conversion_version": conversion.version,
                          "conversion_diagnostic": conversion.diagnostic, "revisions": {}}
    for label, source in (("upstream", original), ("converted", conversion.source), ("glow", edited)):
        path = revisions / f"{label}.frag"
        path.write_bytes(source)
        evidence["source"]["revisions"][label] = {"path": str(path), "sha256": digest(source)}
    return conversion.source, edited


def command(address, *args):
    return {"address": address, "args": [{"type": kind, "value": value} for kind, value in args]}


def scene(output, label, source, *, glow=False):
    folder = output / label
    folder.mkdir()
    fragment = folder / f"{label}.frag"
    fragment.write_bytes(source)
    title = f"26.frag — {label}"
    commands = [
        command("/layers/reset", ("i", 1), ("s", "stack")),
        command("/input", ("i", 0), ("s", ""), ("i", 0), ("i", 512)),
        command("/input/set", ("i", 0), ("s", "maxAmp"), ("f", 1.0)),
        command("/layer/speed", ("i", 0), ("f", 0.0)),
        command("/shader", ("i", 0), ("asset", fragment.name)),
    ]
    if glow:
        commands.append(command("/shader/var", ("i", 0), ("s", "glow"), ("f", 1.0)))
    manifest = folder / "scene.vosc.json"
    write_json(manifest, {"version": 1, "title": title, "size": SIZE, "onset_gate": 0,
                          "commands": commands})
    return manifest


def observed_uniform(shader, name, expected, *, binding=None):
    uniform = shader["uniforms"].get(name)
    if not uniform or "rendered_value" not in uniform or uniform["gl_type"] != 0x1406:
        return False
    value = float(uniform["rendered_value"])
    matches = value == 0.0 if expected == 0.0 else math.isclose(value, expected, abs_tol=1e-6)
    return matches and (binding is None or uniform.get("binding") == binding)


def check_scene(state, glow=None, binding=None):
    require(len(state["layers"]) == 1 and state["layout"] == "stack"
            and state["onset_gate"] is False and (state["width"], state["height"]) == SIZE,
            "unexpected draft scene layout, gate or dimensions")
    layer = state["layers"][0]
    shader = layer["shader"]
    # V1 startup compiles the manifest's exact local bytes as direct source,
    # which get_scene labels "source" regardless of the asset's filename.
    require(layer["geometry_loaded"] and shader["loaded"] and shader["program"] > 0
            and shader["path"] == "source" and not shader["textures"],
            f"wrong active draw shader: {shader}")
    require(observed_uniform(shader, "time", 0.0) and not shader["uniforms"]["time"]["writable"],
            f"native time was not a renderer-managed zero: {shader['uniforms']}")
    if glow is not None:
        return observed_uniform(shader, "glow", glow, binding=binding) and shader["uniforms"]["glow"]["writable"]
    require("glow" not in shader["uniforms"], "converted baseline unexpectedly reflects glow")
    return True


def capture(client, path, ready_frame, glow=None, binding=None):
    deadline = time.monotonic() + 5
    while True:
        before = read_state(client)
        if check_scene(before, glow, binding):
            break
        require(time.monotonic() < deadline,
                f"wrong reflected glow for {path.name}: {before['layers'][0]['shader']['uniforms']}")
        time.sleep(0.05)

    request = client.call("capture_preview", {"max_width": 1024})
    receipt = client.wait(request["id"], timeout=15)
    require(receipt.get("ok") and receipt.get("status") == "captured", f"capture failed: {receipt}")
    captured = receipt["result"]
    data = base64.b64decode(captured.pop("data_base64"), validate=True)
    width, height, pixels = png_pixels(data, include_alpha=True)
    require((width, height) == (captured["width"], captured["height"])
            == (captured["backing_width"], captured["backing_height"]),
            "capture was rescaled or its metadata does not match the PNG")
    # The renderer reads GL_RGBA. The PNG encoder may omit a fully opaque
    # alpha channel, which the decoder then restores losslessly as 255.
    png_mode = {2: "RGB (implicit opaque alpha)", 6: "RGBA"}[data[25]]
    after = read_state(client)
    require(check_scene(after, glow, binding), "reflected shader controls changed during capture")
    require(after["layers"][0]["shader"]["program"] == before["layers"][0]["shader"]["program"]
            and after["external_epoch"] == receipt["external_epoch"] == before["external_epoch"]
            and ready_frame <= before["frame"] <= receipt["frame"] <= after["frame"],
            "frame receipt did not match observed shader state")
    path.write_bytes(data)
    luminance = [int(0.2126 * r + 0.7152 * g + 0.0722 * b + 0.5)
                 for y in range(height // 10, height - height // 10)
                 for r, g, b, _ in pixels[y * width + width // 10:(y + 1) * width - width // 10]]
    low, high = min(luminance), max(luminance)
    return (width, height, pixels), {"png": str(path), "png_sha256": digest(data), "png_mode": png_mode,
                                    "native_readback": "GL_RGBA", "receipt": receipt,
                                    "before": before, "after": after,
                                    "interior_luminance": {"min": low, "max": high, "range": high - low}}


def compare(first, second):
    require(first[:2] == second[:2], "native frame dimensions differ")
    deltas = [max(abs(a - b) for a, b in zip(left, right))
              for left, right in zip(first[2], second[2], strict=True)]
    return {"width": first[0], "height": first[1], "max_channel_error": max(deltas),
            "pixels_changed": sum(delta > 0 for delta in deltas),
            "pixels_changed_by_at_least_16": sum(delta >= 16 for delta in deltas),
            "total_pixels": len(deltas)}


def samples(renderer, client, output, observations, ready_frame, prefix):
    frames = []
    for label, amplitude in zip(("low", "high"), SAMPLES, strict=True):
        osc(renderer.port, "/input/data", 0, amplitude, 0.0, 0)
        frame, observation = capture(client, output / f"{prefix}-{label}.png", ready_frame,
                                     amplitude * 2.0, "expression")
        observation["injection"] = {"transport": "loopback UDP", "port": renderer.port,
                                    "address": "/input/data", "args": [0, amplitude, 0.0, 0],
                                    "expected_glow": amplitude * 2.0}
        observations[label] = observation
        frames.append(frame)
    response = compare(*frames)
    observations["response"] = response
    require(response["pixels_changed_by_at_least_16"] > response["total_pixels"] // 100,
            f"{prefix}: native OSC response was not materially visible: {response}")
    return frames


def replay_setup(manifest, edited):
    recipe = json.loads(manifest.read_text(encoding="utf-8"))
    commands = recipe["commands"]
    speeds = [index for index, item in enumerate(commands) if
              item == command("/layer/speed", ("i", 0), ("f", 0.0))]
    shaders = [item for item in commands if item["address"] == "/shader"]
    require(len(speeds) == len(shaders) == 1, "replay must preserve one frozen draw shader")
    fragment = manifest.parent / shaders[0]["args"][1]["value"]
    require(fragment.resolve().is_relative_to(manifest.parent.resolve()) and fragment.read_bytes() == edited,
            "replay is missing the exact local glow source")
    expected_mapping = command("/shader/var", ("i", 0), ("s", "glow"), ("s", GLOW_EXPRESSION))
    require(expected_mapping in commands and any(item["address"] == "/input" and
            item["args"][0] == {"type": "i", "value": 0} for item in commands),
            f"replay lost OSC input 0 or glow {GLOW_EXPRESSION!r}: {commands}")
    return fragment, recipe["title"]


def run(binary, output, evidence):
    baseline, edited = source_revisions(output, evidence)
    baseline_scene = scene(output, "converted-baseline", baseline)
    glow_scene = scene(output, "edited-default", edited, glow=True)
    staged_export = output / "exported"
    renders = evidence["renders"] = {}

    def launch(label, manifest, callback):
        def observe(renderer, node):
            config = json.loads(manifest.read_text(encoding="utf-8"))
            fragment = manifest.parent / next(item["args"][1]["value"] for item in config["commands"]
                                               if item["address"] == "/shader")
            ready_frame = await_ready(renderer, config["title"])
            node.update({"ready_frame": ready_frame, "log": str(renderer.log), "manifest": str(manifest),
                         "manifest_sha256": digest(manifest.read_bytes()), "fragment": str(fragment),
                         "fragment_sha256": digest(fragment.read_bytes())})
            with closing(renderer.client()) as client:
                return callback(renderer, client, node, ready_frame, fragment)

        renderer = OwnedRenderer(binary, output, label, free_udp_port(), [],
                                 arguments=("--scene", manifest))
        node = renders[label] = {"owner": renderer.owner}
        try:
            return observe(renderer, node)
        finally:
            node["cleanup"], error = collect_cleanup(renderer)
            # Ordinary openFrameworks handles SIGTERM, closes the window and
            # returns the signal number from its main loop after teardown.
            cleanup = node["cleanup"]
            ordinary_exit = cleanup["return_code"] == signal.SIGTERM and cleanup["socket_removed"] and (
                cleanup["identity"] == str(binary) and not cleanup["forced_kill"] and
                cleanup["cleanup_errors"] == [f"owned renderer exited nonzero: {signal.SIGTERM.value}"])
            require(not error or ordinary_exit, f"{label} cleanup failed: {error}")

    def baseline_observation(_renderer, client, node, ready_frame, _fragment):
        frame, node["baseline"] = capture(client, output / "converted-baseline.png", ready_frame)
        require(node["baseline"]["interior_luminance"]["range"] >= 16,
                "converted baseline lacks visible interior luminance variation")
        return frame

    baseline_frame = launch("converted", baseline_scene, baseline_observation)
    evidence["IR-01"] = "pass"

    def edited_observation(renderer, client, node, ready_frame, _fragment):
        frame, node["default"] = capture(client, output / "edited-default.png",
                                        ready_frame, 1.0, "constant")
        comparison = compare(baseline_frame, frame)
        evidence["default_comparison"] = comparison
        require(node["default"]["interior_luminance"]["range"] >= 16
                and comparison["max_channel_error"] <= 1,
                f"default glow did not preserve the native frame: {comparison}")
        osc(renderer.port, "/shader/var", 0, "glow", GLOW_EXPRESSION)
        node["mapping"] = {"address": "/shader/var", "args": [0, "glow", GLOW_EXPRESSION]}
        original_frames = samples(renderer, client, output, node, ready_frame, "edited")
        evidence["IR-02"] = "pass"
        export_command = [sys.executable, str(ROOT / "scripts" / "scene_recipe.py"), "--socket",
                          str(renderer.socket), "export", str(staged_export)]
        result = subprocess.run(export_command, capture_output=True, text=True, timeout=25)
        evidence["export"] = {"command": export_command, "stdout": result.stdout,
                              "stderr": result.stderr, "returncode": result.returncode}
        require(result.returncode == 0, f"scene recipe CLI failed: {result.stdout}\n{result.stderr}")
        return original_frames

    original_frames = launch("edited", glow_scene, edited_observation)
    original_hashes = folder_hashes(staged_export)
    relocated = output / "relocated scene é"
    shutil.move(staged_export, relocated)
    require(not staged_export.exists() and original_hashes == folder_hashes(relocated),
            "relocation changed exported files")
    manifest = relocated / "scene.vosc.json"
    fragment, title = replay_setup(manifest, edited)
    evidence["export"].update({"relocated": str(relocated), "file_hashes": original_hashes,
                               "fragment": str(fragment), "title": title})

    def replay_observation(renderer, client, node, ready_frame, shader):
        require(shader == fragment, "replay resolved a different shader")
        return samples(renderer, client, output, node, ready_frame, "replayed")

    replayed_frames = launch("replayed", manifest, replay_observation)
    comparisons = [compare(first, second) for first, second in
                   zip(original_frames, replayed_frames, strict=True)]
    evidence["replay_comparisons"] = comparisons
    require(all(item["max_channel_error"] <= 1 for item in comparisons),
            f"replayed native OSC endpoints did not preserve appearance: {comparisons}")
    evidence["IR-03"] = "pass"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True, help="new evidence directory")
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    output = args.output.resolve()
    output.mkdir(mode=0o700, parents=True)
    evidence = {"command": [sys.executable, *sys.argv], "platform": platform.platform(),
                "architecture": platform.machine(), "logical_scene_size": SIZE,
                "input": "deterministic OSC sample injection", "IR-01": "open",
                "IR-02": "open", "IR-03": "open"}
    try:
        run(binary, output, evidence)
    except Exception as error:
        evidence["error"] = str(error)
        raise
    finally:
        write_json(output / "report.json", evidence)
    print(output / "report.json")


if __name__ == "__main__":
    main()
