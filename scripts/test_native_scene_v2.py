#!/usr/bin/env python3
"""Exercise V2 startup via the CLI and an owned renderer, retaining per-case evidence."""

from __future__ import annotations

import argparse
from contextlib import closing
import hashlib
import json
import math
from pathlib import Path
import re
import shutil
import subprocess
import time
from urllib.parse import urlsplit

from test_native_scene_cli import negative, read_state, require, run_owned
from test_scene_recipe import ROOT, capture

BUILT_INS = ("intro", "shaders", "sketches", "performance", "test")
GEOMETRY = ("camera", "geom", "buffer", "deferred", "clouds", "grass", "particles-gpu")
MEDIA = ("video", "webcam-delay", "webcam-looper", "ultralight")
ANIMATED = {"shaders", "sketches", "buffer", "grass", "particles-gpu", "video"}
READY = re.compile(r"^VOSC_SCENE_READY frame=(\d+) title=(.+)$", re.MULTILINE)
MANIFEST = "scene.vosc.json"


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def folder_hashes(folder):
    return {path.relative_to(folder).as_posix(): sha256(path)
            for path in sorted(folder.rglob("*")) if path.is_file() and path.name != ".DS_Store"}


def relocate(name, output):
    source = ROOT / "examples" / name
    staged = output / f"{name}-staged"
    relocated = output / f"{name} déplacé"
    expected = folder_hashes(source)
    shutil.copytree(source, staged)
    staged.rename(relocated)
    require(not staged.exists(), f"{name}: staging origin still exists")
    require(expected == folder_hashes(relocated), f"{name}: relocated content changed")
    return source, relocated, expected


def await_ready(renderer, title):
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        log = renderer.log.read_text(errors="replace")
        require(renderer.process.poll() is None, f"renderer exited before ready: {renderer.log}")
        ready = READY.findall(log)
        if ready:
            require(len(ready) == 1 and ready[0][1] == title,
                    f"wrong ready marker: {ready!r}; see {renderer.log}")
            return int(ready[0][0])
        time.sleep(0.05)
    raise RuntimeError(f"{title} did not report readiness: {renderer.log}")


def scene_expectations(manifest):
    """Project inventory bindings onto fields the renderer exposes."""
    reset, *commands = manifest["commands"]
    require(reset["address"] == "/layers/reset", "expected initial layer reset")
    count, layout = (arg["value"] for arg in reset["args"])
    layers = [{"alpha": 1, "geometry": False, "uniforms": {}} for _ in range(count)]
    named = {}
    for command in commands:
        address = command["address"]
        arguments = command["args"]
        values = [arg["value"] for arg in arguments]
        if address == "/tex" and arguments[0]["type"] == "s":
            named[values[0]] = values[1]
        if arguments[0]["type"] != "layer":
            continue
        targets = layers if values[0] == "*" else [layers[values[0]]]
        for layer in targets:
            if address == "/tex":
                layer["texture"] = named[values[1]] if arguments[1]["type"] == "ref" else values[1]
            elif address == "/geom":
                layer["geometry"] = True
            elif address == "/shader":
                layer["geometry"] = True  # Draw shaders install a canonical quad when needed.
                layer["fragment"] = values[1]["stages"]["fragment"]
            elif address == "/layer/alpha":
                layer["alpha"] = values[1]
            elif address == "/shader/var":
                name, value = values[1:]
                if isinstance(value, list):
                    value = [item["value"] for item in value]
                layer["uniforms"][name] = value if isinstance(value, list) else [value]
    return layout, layers, any(command["address"] == "/cam" for command in commands)


def same_path(observed, expected):
    return bool(observed) and Path(observed).is_absolute() and Path(observed).resolve() == expected.resolve()


def assert_state(name, folder, manifest, state, inputs):
    layout, layers, has_camera = scene_expectations(manifest)
    require([state["width"], state["height"]] == manifest["size"], f"{name}: viewport mismatch")
    require(state["onset_gate"] is False, f"{name}: startup gate should apply immediately")
    require(state["camera_enabled"] == has_camera, f"{name}: camera state mismatch")
    require(state["layout"] == layout and len(state["layers"]) == len(layers), f"{name}: layout mismatch")
    for index, (actual, expected) in enumerate(zip(state["layers"], layers)):
        label = f"{name} layer {index}"
        require(actual["geometry_loaded"] == expected["geometry"], f"{label}: geometry mismatch")
        require(actual["alpha"] == expected["alpha"] and actual["visible"], f"{label}: alpha/visibility mismatch")
        shader = actual["shader"]
        if "fragment" in expected:
            require(shader["loaded"] and shader["program"] != 0, f"{label}: shader did not link")
            # get_scene exposes a draw shader's basename. Source validation
            # and the negative missing-stage check cover its local path.
            require(Path(shader["path"]).name == Path(expected["fragment"]).name,
                    f"{label}: expected shader {expected['fragment']}, saw {shader['path']!r}")
        for uniform, values in expected["uniforms"].items():
            current = shader["uniforms"].get(uniform)
            raw = current and current.get("rendered_value")
            require(raw is not None, f"{label}: no rendered value for {uniform}")
            rendered = json.loads(raw) if isinstance(raw, str) else raw
            rendered = rendered if isinstance(rendered, list) else [rendered]
            require(len(rendered) == len(values) and all(math.isclose(got, want, abs_tol=1e-6, rel_tol=1e-5)
                    for got, want in zip(rendered, values)), f"{label}: {uniform} was {rendered}, expected {values}")
        if "texture" not in expected:
            continue
        descriptor = expected["texture"]
        texture = shader["textures"].get("tex", {})
        require(texture.get("loaded"), f"{label}: default texture is missing")
        source = texture["source"]
        kind = descriptor["kind"]
        if kind in ("fragment", "image", "feedback"):
            require(same_path(source, folder / descriptor["path"]),
                    f"{label}: expected local texture {descriptor['path']}, saw {source!r}")
        elif kind == "video":
            require(same_path(source, Path(inputs["clip"])), f"{label}: video path mismatch")
        else:
            target = {"color": "color", "grid-plane": "gridplane"}.get(kind)
            if kind == "sketch":
                target = descriptor["name"]
            elif kind in ("camera", "html"):
                target = inputs[descriptor["input"]]
            require(source == target, f"{label}: expected {kind} source {target!r}, saw {source!r}")
        if kind == "feedback":
            feedback = texture.get("feedback", {})
            require([feedback.get(key) for key in ("width", "height", "internalformat", "textureTarget")] ==
                    [128, 128, 34836, 3553] and feedback.get("updates", 0) >= 1,
                    f"{label}: expected updated 128×128 RGBA32F texture")


def observe(renderer, node, output, label, name, folder, manifest, inputs):
    ready = await_ready(renderer, manifest["title"])
    node["ready_frame"] = ready
    node["log"] = str(renderer.log)
    width, height = manifest["size"]

    with closing(renderer.client()) as client:
        state = read_state(client)
        node["state"] = state
        assert_state(name, folder, manifest, state, inputs)
        node["captures"] = []

        def frame(index):
            image = output / f"{label}-{index}.png"
            actual_width, actual_height, rgba, receipt = capture(client, image, max_width=width)
            require((actual_width, actual_height) == (width, height), f"{label}: capture dimensions mismatch")
            require((receipt["backing_width"], receipt["backing_height"]) == (width, height),
                    f"{label}: capture backing dimensions mismatch")
            receipt.pop("data_base64", None)
            node["captures"].append({"file": str(image), "sha256": sha256(image), "receipt": receipt})
            return rgba

        first = frame(0)
        require(any(max(pixel[:3]) > 8 for pixel in first), f"{label}: blank composed frame")
        if name in ANIMATED:
            time.sleep(0.4)
            later = frame(1)
            changed = sum(before[:3] != after[:3] for before, after in zip(first, later))
            node["changed_pixels"] = changed
            require(changed > 0, f"{label}: no visible frame progression")
        capabilities = client.call("get_capabilities")
        require(capabilities.get("ok"), f"{label}: could not read renderer capabilities")
        node["features"] = capabilities["result"]["features"]

    log = renderer.log.read_text(errors="replace")
    require(len(READY.findall(log)) == 1, f"{label}: ready marker repeated")
    require(not any(line.startswith("[ error ]") for line in log.splitlines()),
            f"{label}: renderer logged an error: {renderer.log}")
    return hashlib.sha256(bytes(channel for pixel in first for channel in pixel)).hexdigest()


def run_scene(args, output, cwd, name, node, evidence, inputs=None):
    inputs = inputs or {}
    original, moved, hashes = relocate(name, output)
    node["files"] = hashes
    renders = []
    for label, folder in ((f"{name}-original", original), (f"{name}-relocated", moved)):
        path = folder / MANIFEST
        manifest = json.loads(path.read_text())
        options = ["--scene", path]
        for key, value in inputs.items():
            options.extend(("--scene-input", f"{key}={value}"))
        renders.append(label)
        node["renders"] = renders

        def collect(renderer, sample):
            return observe(renderer, sample, output, label, name, folder, manifest, inputs)

        result = run_owned(args.binary, output, label, 0, collect, evidence, arguments=options, cwd=cwd)
        evidence["renders"][label]["decoded_rgba_sha256"] = result
    if name == "intro":
        actual = [evidence["renders"][label]["decoded_rgba_sha256"] for label in renders]
        require(actual[0] == actual[1], f"{name}: relocated static pixels changed")


def run_negatives(args, output, execute):
    work = output / "invalid-fixtures"
    work.mkdir()
    folders = {}
    for name in ("intro", "test", "geom", "particles-gpu", "grass", "camera", "webcam-delay"):
        folders[name] = work / name
        shutil.copytree(ROOT / "examples" / name, folders[name])

    def reject(label, argv, expected):
        execute("negative", label, lambda node: node.update(
            negative(args.binary, output, label, argv, expected)))

    def fixture(label, name, edit, expected):
        folder = folders[name]
        value = json.loads((folder / MANIFEST).read_text())
        edit(value)
        path = folder / f"{label}.json"
        path.write_text(json.dumps(value))
        reject(label, ["--scene", path], expected)

    def update_source(scene, address, **updates):
        descriptor = next(c["args"][1]["value"] for c in scene["commands"]
                          if c["address"] == address and c["args"][1]["type"] == "source")
        descriptor.update(updates)

    def setting(scene, address, prop, value):
        command = next(c for c in scene["commands"] if c["address"] == address and c["args"][1]["value"] == prop)
        command["args"][2]["value"] = value

    fixture("unknown-version", "intro", lambda d: d.update(version=3), "unsupported scene version")
    fixture("v2-into-v1", "intro", lambda d: d.update(version=1), "invalid type or value for argument 1")
    fixture("unknown-field", "intro", lambda d: d.update(unexpected=True), "expected scene metadata")
    fixture("invalid-source-tag", "intro", lambda d: update_source(d, "/tex", kind="raw"), "invalid source descriptor")
    fixture("unknown-address", "intro", lambda d: d["commands"][-1].update(address="/not-startup"),
            "outside native scene v2")
    fixture("invalid-argument-type", "intro", lambda d: d["commands"][0]["args"][0].update(type="f"),
            "invalid scalar argument")
    fixture("over-layer-limit", "intro", lambda d: d["commands"][0]["args"][0].update(value=33),
            "expected integer in 1..32")
    fixture("undefined-alias", "geom", lambda d: d["commands"].pop(2), "undefined texture reference")
    fixture("over-instance-limit", "particles-gpu", lambda d: setting(d, "/geom/set", "drawInstanced", 16385),
            "unsupported geometry setting")
    fixture("over-history-limit", "webcam-delay", lambda d: setting(d, "/tex/set", "numFrames", 121),
            "unsupported texture setting")
    def move_stage_after_uniforms(scene):
        commands = scene["commands"]
        index = next(i for i, command in enumerate(commands) if command["address"] == "/shader")
        commands.append(commands.pop(index))

    fixture("uniform-before-shader", "grass", move_stage_after_uniforms, "uniform requires a preceding shader")
    fixture("missing-image-vs-bundled", "test", lambda d: update_source(d, "/tex", path="black_hole.jpg"),
            "black_hole.jpg")
    fixture("asset-traversal", "intro", lambda d: update_source(d, "/tex", path="../color.frag"),
            "scene-relative asset path")
    (folders["intro"] / "assets" / "escape.frag").symlink_to(ROOT / "examples/intro/assets/color.frag")
    fixture("escaped-symlink", "intro", lambda d: update_source(d, "/tex", path="assets/escape.frag"),
            "inside the scene folder")
    def remove_stage(scene):
        shader = next(c for c in scene["commands"] if c["address"] == "/shader")
        shader["args"][1]["value"]["stages"].pop("fragment")
    fixture("missing-fragment-stage", "grass", remove_stage, "explicit fragment stage")
    (folders["camera"] / "assets" / "penguin" / "penguin.jpg").unlink()
    fixture("missing-model-sidecar", "camera", lambda d: None, "penguin.jpg")
    grass = folders["grass"] / "assets"
    (grass / "grass" / "Grass.geom").write_text('#pragma include "../include/cycle.glsl"\n')
    (grass / "include" / "cycle.glsl").write_text('#pragma include "cycle.glsl"\n')
    fixture("include-cycle", "grass", lambda d: None, "include cycle")
    (grass / "include" / "cycle.glsl").unlink()
    fixture("missing-shader-include", "grass", lambda d: None, "cycle.glsl")

    video = ROOT / "examples/video" / MANIFEST
    intro = ROOT / "examples/intro" / MANIFEST
    reject("missing-video-input", ["--scene", video], "requires --scene-input clip")
    reject("undeclared-input", ["--scene", intro, "--scene-input", "clip=a.mp4"], "unknown scene input")
    reject("duplicate-input", ["--scene", video, "--scene-input", "clip=a.mp4", "--scene-input", "clip=b.mp4"],
           "duplicate scene input")
    reject("invalid-camera-index", ["--scene", ROOT / "examples/webcam-looper" / MANIFEST,
                                    "--scene-input", "camera=64"], "expected camera device index")
    reject("invalid-html-url", ["--scene", ROOT / "examples/ultralight" / MANIFEST,
                                "--scene-input", "url=file:///tmp/test.html"], "expected HTTP(S) URL")


def watchdog(args, output, mode, hook, node):
    started = time.monotonic()
    node.update(negative(args.binary, output, mode, ["--scene", ROOT / "examples/video" / MANIFEST,
                                                   "--scene-input", f"clip={args.video_fixture}"],
                         "clip: startup timed out before composition",
                         trace=("window", "preflight", "receiver", "reset"), inject=hook))
    node["duration_seconds"] = time.monotonic() - started
    require(f"VOSC_TEST_NATIVE {mode}" in node["stderr"], f"{mode}: test hook was not reached")
    require(14 <= node["duration_seconds"] < 20, f"{mode}: watchdog deadline outside tolerance")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True, help="new evidence directory")
    parser.add_argument("--scene", action="append", choices=BUILT_INS + GEOMETRY + MEDIA,
                        help="limit positive cases to these names")
    parser.add_argument("--video-fixture", type=Path, help="readable timecoded local MOV or MP4")
    parser.add_argument("--camera-index", type=int, choices=range(64), metavar="0..63",
                        help="opt in to physical camera cases on this device")
    parser.add_argument("--html-loopback-url", help="run against an HTML-enabled build and a local fixture URL")
    parser.add_argument("--watchdog", action="store_true", help="run both 15-second faults using a recovery-hook build")
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument("--negative-only", action="store_true")
    selection.add_argument("--skip-negatives", action="store_true")
    args = parser.parse_args()

    args.binary = args.binary.resolve()
    if not args.binary.is_file():
        parser.error("--binary must refer to the owned renderer executable")
    if args.video_fixture is not None:
        args.video_fixture = args.video_fixture.resolve()
        if not args.video_fixture.is_file() or args.video_fixture.suffix not in (".mov", ".mp4"):
            parser.error("--video-fixture must refer to a readable .mov or .mp4")
    if args.watchdog and args.video_fixture is None:
        parser.error("--watchdog needs --video-fixture")
    if args.html_loopback_url:
        url = urlsplit(args.html_loopback_url)
        if url.scheme not in {"http", "https"} or url.hostname not in {"127.0.0.1", "::1", "localhost"}:
            parser.error("--html-loopback-url needs an explicit HTTP(S) loopback URL")

    output = args.output.resolve()
    output.mkdir(parents=True)
    cwd = output / "unrelated cwd"
    cwd.mkdir()
    evidence = {
        "binary": str(args.binary),
        "binary_sha256": sha256(args.binary),
        "source_head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "git_status": subprocess.check_output(["git", "status", "--short"], cwd=ROOT, text=True),
        "video_fixture": str(args.video_fixture) if args.video_fixture else None,
        "video_fixture_sha256": sha256(args.video_fixture) if args.video_fixture else None,
        "camera_index": args.camera_index,
        "native": {}, "negative": {}, "renders": {},
        "coverage": "Native startup and relocation. SC cues, model/reference parity, instance counts and hardware qualification have separate evidence.",
        "external_apod": {"status": "OPEN", "detail": "external site not exercised by this harness"},
    }

    def execute(group, label, callback):
        node = evidence[group][label] = {"status": "FAIL"}
        try:
            callback(node)
        except Exception as error:
            node["detail"] = str(error)
        else:
            node["status"] = "PASS"
        suffix = f": {node['detail']}" if "detail" in node else ""
        print(f"{node['status']} {label}{suffix}", flush=True)

    def external_open(name, reason):
        evidence["native"][name] = {"status": "OPEN", "detail": reason}
        print(f"OPEN {name}: {reason}", flush=True)

    try:
        if not args.negative_only:
            for name in dict.fromkeys(args.scene or BUILT_INS + GEOMETRY + MEDIA):
                inputs = {}
                if name == "video":
                    if args.video_fixture is None:
                        external_open(name, "requires a timecoded --video-fixture")
                        continue
                    inputs = {"clip": str(args.video_fixture)}
                elif name.startswith("webcam-"):
                    if args.camera_index is None:
                        external_open(name, "requires an explicit --camera-index and permission")
                        continue
                    inputs = {"camera": str(args.camera_index)}
                elif name == "ultralight":
                    if not args.html_loopback_url:
                        external_open(name, "requires an HTML-enabled binary and --html-loopback-url")
                        continue
                    inputs = {"url": args.html_loopback_url}
                execute("native", name, lambda node: run_scene(args, output, cwd, name, node, evidence, inputs))

        if not args.skip_negatives:
            run_negatives(args, output, execute)
            if not args.html_loopback_url:
                execute("negative", "html-disabled", lambda node: node.update(negative(
                    args.binary, output, "html-disabled", ["--scene", ROOT / "examples/ultralight" / MANIFEST],
                    "HTML textures require a build with USE_OFX_ULTRALIGHT")))
        if args.watchdog:
            for mode, hook in (("stall-open", "VOSC_TEST_NATIVE_STALL_OPEN"),
                               ("no-frame", "VOSC_TEST_NATIVE_NO_FRAME")):
                execute("negative", mode, lambda node: watchdog(args, output, mode, hook, node))
    except Exception as error:
        evidence["harness_error"] = str(error)
        raise
    finally:
        all_cases = list(evidence["native"].values()) + list(evidence["negative"].values())
        failed = "harness_error" in evidence or any(case["status"] == "FAIL" for case in all_cases)
        evidence["status"] = "FAIL" if failed else (
            "OPEN" if any(case["status"] == "OPEN" for case in all_cases) else "PASS")
        path = output / "native-v2-evidence.json"
        path.write_text(json.dumps(evidence, indent=2) + "\n")
        print(f"Evidence: {path}", flush=True)
    return int(evidence["status"] == "FAIL")


if __name__ == "__main__":
    raise SystemExit(main())
