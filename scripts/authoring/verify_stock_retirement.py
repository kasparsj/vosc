#!/usr/bin/env python3
"""Qualify packaged offline example cues and retained shader paths for IR-10/16."""

from __future__ import annotations

import argparse
from contextlib import closing, contextmanager
import errno
import json
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from test_macos_dmg import ReleaseRenderer, disposable_home
from test_native_scene_cli import read_state, require
from test_native_scene_v2 import assert_state, await_ready, folder_hashes, sha256
from test_scene_recipe import capture, free_udp_port, osc, sc_escape, wait_socket
from visuals_quark import class_config, probe


EXAMPLES = ("intro", "shaders", "performance", "test")
APP_BINARY = Path("Contents/MacOS/vosc")
STOCK = Path("Contents/Resources/data")
MANIFEST = "scene.vosc.json"
RETIRED = Path("assets/100fragments/RotStringsV.frag")


def verify_network_isolation():
    """Prove the enclosing offline policy still permits the SC loopback link."""
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as listener, socket.socket(
            socket.AF_INET, socket.SOCK_DGRAM) as client:
        listener.bind(("127.0.0.1", 0))
        listener.settimeout(1)
        client.sendto(b"offline-check", listener.getsockname())
        require(listener.recv(32) == b"offline-check", "loopback OSC transport failed")
        try:
            client.sendto(b"offline-check", ("192.0.2.1", 9))
        except OSError as error:
            require(error.errno in (errno.EPERM, errno.EACCES), f"unexpected network rejection: {error}")
            return {"loopback": "allowed", "nonloopback": "denied", "errno": error.errno}
    raise RuntimeError("run under the documented network-outbound sandbox profile")


def log_of(renderer):
    return renderer.log.read_text(encoding="utf-8", errors="replace")


@contextmanager
def ready_client(renderer, records, title):
    with renderer.run(records):
        records["ready_frame"] = await_ready(renderer, title)
        wait_socket(renderer.process, renderer.socket, renderer.log)
        with closing(renderer.client()) as client:
            yield client


def settle(client, predicate, detail, timeout=5):
    until = time.monotonic() + timeout
    while time.monotonic() < until:
        state = read_state(client)
        if predicate(state):
            return state
        time.sleep(0.06)
    raise RuntimeError(detail)


def source_is(state, layer, path):
    texture = state["layers"][layer]["shader"]["textures"].get("tex", {})
    return texture.get("loaded") and Path(texture["source"]).resolve() == path.resolve()


def frame(client, output, records, label):
    path = output / f"{label}.png"
    width, height, pixels, receipt = capture(client, path, max_width=400)
    require((width, height) == (400, 300) and max(max(pixel[:3]) for pixel in pixels) > 16,
            f"{label}: no visible frame at the expected dimensions")
    receipt.pop("data_base64", None)
    records.setdefault("frames", {})[label] = {"path": str(path), "sha256": sha256(path), "receipt": receipt}
    return pixels


def pixel_delta(records, label, before, after, minimum=400):
    changed = sum(max(abs(a - b) for a, b in zip(left[:3], right[:3])) >= 12
                  for left, right in zip(before, after, strict=True))
    records[label] = {"pixels_changed_at_least_12": changed, "minimum": minimum}
    require(changed >= minimum, f"{label}: insufficient visible change ({changed} pixels)")


def exact_cue(companion, prefix):
    lines = [line.strip() for line in companion.splitlines() if line.strip().startswith(prefix)]
    require(len(lines) == 1, f"expected one companion cue beginning {prefix!r}")
    return lines[0]


def run_sc(sclang, quark, scratch, output, label, renderer, assets, code, records, *, wait=0.3):
    owned = scratch / label
    library_config = class_config(owned, quark)
    runner = output / f"{label}.scd"
    log = output / f"{label}.log"
    runner.write_text(f'''(
~rendererRoot = "{sc_escape(str(assets))}";
~rendererPort = {renderer.port};
Routine({{
{code}
{wait}.wait;
"VOSC_CUES_SENT".postln;
0.exit;
}}).play(SystemClock);
)
''', encoding="utf-8")
    language_port = free_udp_port()
    command = [str(sclang), "-a", "-l", str(library_config), "-D", "-u", str(language_port),
               "-i", "none", str(runner)]
    env = {key: value for key, value in os.environ.items() if not key.startswith("VOSC_")}
    env.update(HOME=str(renderer.home), CFFIXED_USER_HOME=str(renderer.home),
               XDG_CONFIG_HOME=str(owned / "config"), XDG_DATA_HOME=str(owned / "data"))
    with log.open("wb") as stream:
        result = subprocess.run(command, env=env, stdout=stream, stderr=subprocess.STDOUT, timeout=wait + 15)
    records[label] = {"command": command, "returncode": result.returncode, "log": str(log),
                      "source": str(runner), "source_sha256": sha256(runner)}
    text = log.read_text(encoding="utf-8", errors="replace")
    require(result.returncode == 0 and text.splitlines().count("VOSC_CUES_SENT") == 1
            and not re.search(r"^(ERROR|FAILURE):", text, re.MULTILINE),
            f"{label}: sclang failed. See {log}")


def exercise_cues(name, companion, renderer, client, moved, output, scratch, sclang, quark, records):
    generated = companion.split("// BEGIN GENERATED EXAMPLE SETUP", 1)[1].split(
        "// END GENERATED EXAMPLE SETUP", 1)[0]
    attach, setup = re.findall(r"^\(\n(.+?)^\)", generated, re.DOTALL | re.MULTILINE)

    def send(label, code, *, wait=0.3):
        run_sc(sclang, quark, scratch, output, label, renderer, moved, code, records, wait=wait)

    def shot(label):
        return frame(client, output, records, label)

    send(f"{name}-sc-setup", setup)
    scene = json.loads((moved / MANIFEST).read_text(encoding="utf-8"))
    def setup_arrived(state):
        try:
            assert_state(name, moved, scene, state, {})
        except RuntimeError:
            return False
        return True
    records["setup_state"] = settle(client, setup_arrived, f"{name}: packaged SC setup did not reconstruct scene")
    # NoisySpirals initializes first, then accumulates strokes across frames.
    if name == "performance":
        time.sleep(1)
    first = shot(f"{name}-sc-setup")

    if name == "intro":
        line = exact_cue(companion, "~visuals.send('/tex', 2, \"shader:\"")
        send("intro-prism", attach + line)
        prism = moved / "assets/prism-bands.frag"
        records["prism_state"] = settle(client, lambda state: source_is(state, 2, prism),
                                         "Intro did not load the packaged live shader cue")
        cue = shot("intro-prism")
        pixel_delta(records, "prism_appears", first, cue)
        time.sleep(0.5)
        pixel_delta(records, "prism_animates", cue, shot("intro-prism-later"))
    elif name == "shaders":
        sine = exact_cue(companion, "~visuals.send('/tex/var', 0, \"glow\", \"sin")
        blue = exact_cue(companion, "~visuals.send('/tex/color', 0, [0, 0, 1.0]")
        send("shaders-sine-blue", attach + sine + "\n" + blue)
        blue_frame = shot("shaders-sine-blue")
        pixel_delta(records, "sine_and_color", first, blue_frame)
        require(sum(p[2] > 60 and p[1] <= 4 for p in blue_frame) > 1000,
                "Shaders color control did not produce blue ribbons")
        input_cues = [
            exact_cue(companion, "~visuals.send('/input', 0)"),
            exact_cue(companion, "~visuals.send('/input/data', 0, 0.65)"),
            exact_cue(companion, "~visuals.send('/tex/var', 0, \"glow\", \"in_0\""),
        ]
        osc(renderer.port, "/tex/speed", 0, 0.0)
        send("shaders-map-input", attach + "\n".join(input_cues))
        samples = []
        for label, amplitude in (("low", 0.2), ("high", 0.8)):
            osc(renderer.port, "/input/data", 0, amplitude)
            time.sleep(0.15)
            samples.append(shot(f"shaders-glow-{label}"))
        pixel_delta(records, "glow_amplitude_response", *samples)
        grid = [exact_cue(companion, prefix) for prefix in (
            "~visuals.send('/tex', \"*\", \"\")", "~visuals.send('/layers', 9, \"grid\")",
            "~visuals.send('/tex', \"*\", \"shader:\"", "~visuals.send('/tex/var', \"*\", \"glow\",")]
        send("shaders-grid-reload", attach + "\n".join(grid))
        ribbons = moved / "assets/glow-ribbons.frag"
        records["grid_state"] = settle(client, lambda state: state["layout"] == "grid"
                                       and len(state["layers"]) == 9
                                       and all(source_is(state, i, ribbons) for i in range(9)),
                                       "Shaders grid cue did not load the local ribbon on all layers")
        shot("shaders-grid-reload")
    elif name == "performance":
        fades = re.findall(r"^\(\n(~visuals\.send\('/layer/alpha'.+?)^\)",
                           companion.split("// END GENERATED EXAMPLE SETUP", 1)[1], re.DOTALL | re.MULTILINE)
        require(len(fades) == 2, "expected two packaged crossfade cues")
        previous = first
        for index, cue in enumerate(fades, start=1):
            send(f"performance-fade-{index}", attach + cue, wait=10.4)
            # A full fade outlasts the engine's idle control connection.
            with closing(renderer.client()) as observation:
                state = settle(observation, lambda s: s["layers"][index]["alpha"] >= 0.99
                               and s["layers"][index - 1]["alpha"] <= 0.01,
                               f"Performance cue {index} did not complete the crossfade")
                current = frame(observation, output, records, f"performance-fade-{index}")
            records[f"fade_{index}_state"] = state
            pixel_delta(records, f"fade_{index}_pixels", previous, current, minimum=16 if index == 1 else 400)
            previous = current
        require(source_is(state, 2, moved / "assets/pulse-rings.frag"),
                "Performance rings are not the active packaged cue")
        time.sleep(0.5)
        with closing(renderer.client()) as observation:
            later = frame(observation, output, records, "performance-rings-later")
            pixel_delta(records, "pulse_rings_animate", previous, later)
    else:
        transforms = companion.split("// END GENERATED EXAMPLE SETUP", 1)[1]
        first_cue = re.search(r"^\(\n(.+?)^\)", transforms, re.DOTALL | re.MULTILINE)[1]
        osc(renderer.port, "/tex/speed", 1, 0.0)
        time.sleep(0.15)
        before_tint = shot("test-before-tint")
        send("test-transforms", attach + first_cue)
        edited = shot("test-transforms")
        def grid_panel(pixels):
            return [pixel for row in range(150) for pixel in pixels[row * 400 + 200:(row + 1) * 400]]
        pixel_delta(records, "transforms_visible", grid_panel(before_tint), grid_panel(edited))
        require(source_is(read_state(client), 1, moved / "assets/motion-grid.frag"),
                "Test cue did not keep the packaged motion-grid source")


def verify_examples(app, scratch, output, sclang, quark, records):
    for name in EXAMPLES:
        home, root = disposable_home(scratch, f"{name}-home")
        stock = app / STOCK / "examples" / name
        native = output / f"{name} relocated"
        moved = output / f"{name} companion"
        shutil.copytree(stock, native)
        shutil.copytree(stock, moved)
        hashes = folder_hashes(stock)
        require(folder_hashes(native) == folder_hashes(moved) == hashes, "relocation changed packaged files")
        manifest = json.loads((native / MANIFEST).read_text(encoding="utf-8"))
        renderer = ReleaseRenderer(app / APP_BINARY, native / MANIFEST, scratch, output / f"{name}-native.log", home)
        node = records[name] = {"stock": str(stock), "hashes": hashes, "selected_root": str(root)}
        require(not root.exists(), f"{name}: expected fresh managed data")
        with ready_client(renderer, node, manifest["title"]) as client:
            require(f"VOSC_DATA_ROOT {root}" in log_of(renderer).splitlines(), f"{name}: wrong managed root")
            node["native_state"] = state = read_state(client)
            assert_state(name, native, manifest, state, {})
            if name == "performance":
                time.sleep(1)
            first = frame(client, output, node, name + "-native")
            if name != "intro":
                time.sleep(0.5)
                pixel_delta(node, "native_animation", first, frame(client, output, node, name + "-native-later"),
                            minimum=16 if name == "performance" else 400)
            exercise_cues(name, (moved / "scene.scd").read_text(encoding="utf-8"), renderer, client,
                          moved, output, scratch, sclang, quark, node)
        require(folder_hashes(native) == folder_hashes(moved) == hashes, f"{name}: launch changed example files")


def verify_retired_paths(app, baseline, scratch, output):
    home, root = disposable_home(scratch, "retired-home")
    retained_scene = output / "legacy scene retained"
    original = baseline / STOCK / "examples/shaders"
    shutil.copytree(original, retained_scene, ignore=lambda folder, _: {"100fragments"} if Path(folder).name == "assets" else set())
    original_hashes = folder_hashes(original)
    unrelated = retained_scene / "assets/user-reference.txt"
    unrelated.write_text("Retained scene notes.\n", encoding="utf-8")
    snapshot = folder_hashes(retained_scene)
    missing = retained_scene / RETIRED
    records = {}
    renderer = ReleaseRenderer(app / APP_BINARY, retained_scene / MANIFEST, scratch, output / "legacy-missing.log", home)
    with renderer.run(records.setdefault("manifest_missing", {}), negative=True):
        require(renderer.process.wait(timeout=20) != 0 and not renderer.socket.exists()
                and f"Missing shader '{RETIRED}' at {missing}" in log_of(renderer),
                "legacy native scene did not report its missing shader and exact path")
    require(folder_hashes(retained_scene) == snapshot, "manifest rejection rewrote caller data")

    missing.parent.mkdir()
    shutil.copyfile(original / RETIRED, missing)
    renderer = ReleaseRenderer(app / APP_BINARY, retained_scene / MANIFEST, scratch, output / "legacy-retained.log", home)
    with ready_client(renderer, records.setdefault("manifest_retained", {}), "Shaders") as client:
        require(source_is(read_state(client), 0, missing), "legacy native manifest used a different shader")
        frame(client, output, records, "legacy-retained-native")
        path = "100fragments/RotStringsV.frag"
        candidates = [root / candidate for candidate in (path, path + ".frag", "shaders/" + path, "shaders/" + path + ".frag")]
        before = read_state(client)["layers"][0]["shader"]["program"]
        osc(renderer.port, "/shader", 0, path)
        until = time.monotonic() + 5
        while "Missing fragment shader" not in log_of(renderer) and time.monotonic() < until:
            time.sleep(0.05)
        log = log_of(renderer)
        require(f"Missing fragment shader '{path}'" in log and all(f"'{p}'" in log for p in candidates),
                "ordinary load did not report each searched fragment path")
        require(read_state(client)["layers"][0]["shader"]["program"] == before, "missing shader replaced active program")
        records["ordinary_missing"] = {"requested": path, "searched": list(map(str, candidates)), "program": before}

        restored = candidates[2]
        restored.parent.mkdir()
        shutil.copyfile(original / RETIRED, restored)
        osc(renderer.port, "/shader", 0, path)
        osc(renderer.port, "/shader/var", 0, "glow", 1.0)
        state = settle(client, lambda s: s["layers"][0]["shader"]["program"] not in {0, before}
                       and s["layers"][0]["shader"]["path"] == restored.name
                       and "glow" in s["layers"][0]["shader"]["uniforms"],
                       "retained ordinary shader path did not load")
        records["ordinary_retained"] = {"path": str(restored), "sha256": sha256(restored), "state": state}
        frame(client, output, records, "legacy-retained-ordinary")
        require(sha256(restored) == original_hashes[RETIRED.as_posix()], "ordinary restore rewrote retained shader")
    retained_hashes = {**snapshot, RETIRED.as_posix(): original_hashes[RETIRED.as_posix()]}
    require(folder_hashes(retained_scene) == retained_hashes, "renderer rewrote or substituted retained example files")
    records["preserved_original_scene_and_assets"] = retained_hashes
    return records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("app", "baseline-app", "sclang", "visuals-quark", "output"):
        parser.add_argument("--" + name, required=True, type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(mode=0o700, parents=True)
    app, baseline = args.app.resolve(strict=True), args.baseline_app.resolve(strict=True)
    evidence = {"command": [sys.executable, *sys.argv], "app": str(app), "baseline_app": str(baseline),
                "binary_sha256": sha256(app / APP_BINARY), "stock_sha256": sha256(app / "Contents/Resources/vosc-stock.json"),
                "IR-10": "open", "IR-16": "open"}
    try:
        evidence["offline_policy"] = verify_network_isolation()
        with tempfile.TemporaryDirectory(prefix="vosc-stock-", dir="/tmp") as directory:
            scratch = Path(directory).resolve()
            evidence["visuals_quark"] = probe(args.sclang, args.visuals_quark, output / "quark-probe")
            verify_examples(app, scratch, output, args.sclang, args.visuals_quark.resolve(),
                            evidence.setdefault("examples", {}))
            evidence["IR-10"] = "pass"
            evidence["legacy_paths"] = verify_retired_paths(app, baseline, scratch, output)
            evidence["IR-16"] = "pass"
    except Exception as error:
        evidence["error"] = str(error)
        raise
    finally:
        report = output / "report.json"
        report.write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    print(report)


if __name__ == "__main__":
    main()
