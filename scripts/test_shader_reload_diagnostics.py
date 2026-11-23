#!/usr/bin/env python3
"""Verify failed explicit OSC and watched-file reloads on private renderers."""

import argparse
import base64
from contextlib import closing
import json
import os
from pathlib import Path
import shutil
import signal
import time

from test_engine_api import png_pixels
from test_scene_recipe import ROOT, OwnedRenderer, applied, collect_cleanup, free_udp_port, osc


def read(client, method, params=None):
    reply = client.call(method, params or {})
    assert reply["ok"], reply
    return reply["result"]


def organic_scene(output):
    destination = output / "organic-trails"
    shutil.copytree(ROOT / "examples/organic-trails", destination)
    return destination / "scene.vosc.json"


def capture_frame(client, path):
    requested = client.call("capture_preview", {"max_width": 320})
    assert requested["ok"], requested
    receipt = client.wait(requested["id"], timeout=15)
    assert receipt["ok"] and receipt["status"] == "captured", receipt
    data = base64.b64decode(receipt["result"].pop("data_base64"), validate=True)
    path.write_bytes(data)
    return png_pixels(data, include_alpha=True)[2], receipt


def verify_failed_reload(renderer, client, output):
    deadline = time.monotonic() + 10
    while "VOSC_SCENE_READY" not in renderer.log.read_text():
        assert renderer.process.poll() is None and time.monotonic() < deadline, renderer.log
        time.sleep(0.05)
    (layer,) = read(client, "get_scene")["layers"]
    baseline = layer["shader"]["program"]
    assert layer["index"] == 0 and layer["shader"]["loaded"] and baseline != 0, layer
    before, before_capture = capture_frame(client, output / "before-reload.png")

    cursor = read(client, "get_diagnostics")["latest_cursor"]
    candidate = output / "invalid-reload" / "organic-trails.frag"
    candidate.parent.mkdir()
    candidate.write_text("#version 330\nout vec4 fragColor;\nvoid main() { fragColor = vec4(vosc_invalid_identifier); }\n")
    osc(renderer.port, "/shader", 0, str(candidate))
    for _ in range(100):
        entries = read(client, "get_diagnostics", {"after": cursor})["entries"]
        rows = [entry for entry in entries if "Layer 0: shader reload failed" in entry["message"]]
        if rows:
            break
        time.sleep(0.05)
    else:
        raise AssertionError(f"Contextual reload diagnostic missing: {entries}")
    (row,) = rows
    assert str(candidate) in row["message"] and "Previous shader is still rendering." in row["message"], row
    assert any("vosc_invalid_identifier" in entry["message"] for entry in entries), entries
    terminal = renderer.log.read_text()
    assert "vosc_invalid_identifier" in terminal and "Layer 0: shader reload failed" in terminal, terminal

    first, first_capture = capture_frame(client, output / "after-reload-1.png")
    time.sleep(0.25)
    second, second_capture = capture_frame(client, output / "after-reload-2.png")
    assert any(max(pixel[:3]) > 0 for pixel in first), "first post-failure frame is black"
    assert any(max(pixel[:3]) > 0 for pixel in second), "second post-failure frame is black"
    assert before != first and first != second, "composed pixels did not animate"
    assert before_capture["frame"] < first_capture["frame"] < second_capture["frame"]
    loaded = read(client, "get_scene")["layers"][0]["shader"]
    assert loaded["loaded"] and loaded["program"] == baseline, loaded
    return {"program": baseline, "diagnostic": row, "entries": entries,
            "captures": [before_capture, first_capture, second_capture]}


def wait_for(fetch, matches):
    deadline = time.monotonic() + 8
    while True:
        value = fetch()
        if matches(value):
            return value
        assert time.monotonic() < deadline, value
        time.sleep(0.05)


def verify_watched_reload(renderer, client, output, texture=False):
    path = output / "watched.frag"

    def save(source):
        previous = int(path.stat().st_mtime) if path.exists() else 0
        path.write_text(source)
        if int(path.stat().st_mtime) <= previous:
            os.utime(path, (previous + 1, previous + 1))

    def layer_shader():
        (layer,) = read(client, "get_scene")["layers"]
        return layer["shader"]

    def animated_frames(label, channel):
        frames, receipts = [], []
        for index in range(1, 4):
            if receipts:
                time.sleep(0.27)
            pixels, receipt = capture_frame(client, output / f"{label}-{index}.png")
            assert not receipts or receipt["frame"] > receipts[-1]["frame"]
            assert any(pixel[channel] > 25 for pixel in pixels), f"{label}: expected visible color"
            assert not any(pixel[1 - channel] for pixel in pixels), f"{label}: unexpected color"
            frames.append(pixels)
            receipts.append(receipt)
        assert any(frame != frames[0] for frame in frames[1:]), f"{label}: output stopped animating"
        return receipts

    save("""#version 330
uniform float time;
uniform float baselineGain;
out vec4 color;
void main() { color = vec4(0.55 + 0.25 * sin(time * 4.0) + baselineGain, 0, 0, 1); }
""")
    applied(client, "configure_layers", {"count": 1, "layout": "stack"})
    osc(renderer.port, "/onset", 0)
    osc(renderer.port, "/tex" if texture else "/shader", 0, str(path))
    baseline = None
    if texture:
        # Wait until the lazy texture shader has rendered before editing its file.
        wait_for(lambda: capture_frame(client, output / "texture-ready.png")[0],
                 lambda pixels: any(p[0] > 25 and p[1] == 0 for p in pixels))
    else:
        baseline = wait_for(
            layer_shader, lambda shader: shader["uniforms"].get("baselineGain", {}).get("writable"))
        assert baseline["loaded"] and baseline["program"], baseline
    frames = animated_frames("baseline", 0)

    cursor = read(client, "get_diagnostics")["latest_cursor"]
    save("#version 330\nout vec4 color;\nvoid main() { color = vec4(vosc_invalid_watched_identifier); }\n")

    def diagnostics():
        return read(client, "get_diagnostics", {"after": cursor})["entries"]

    entries = wait_for(diagnostics, lambda rows: any(
        "vosc_invalid_watched_identifier" in row["message"] for row in rows))
    if baseline is not None:
        after_failure = layer_shader()
        assert after_failure["loaded"] and after_failure["program"] == baseline["program"], after_failure
        assert after_failure["uniforms"].keys() == baseline["uniforms"].keys(), after_failure
    assert any(str(path) in row["message"] and "Previous shader is still rendering." in row["message"]
               for row in entries), entries
    frames += animated_frames("invalid", 0)

    save("""#version 330
uniform float time;
uniform vec3 correctedBias;
out vec4 color;
void main() { color = vec4(vec3(0, 0.55 + 0.25 * sin(time * 4.0), 0) + correctedBias, 1); }
""")
    if baseline is not None:
        corrected = wait_for(layer_shader, lambda shader: shader["uniforms"].get("correctedBias", {}).get("writable"))
        assert corrected["loaded"] and corrected["program"] != baseline["program"], corrected
        assert "baselineGain" not in corrected["uniforms"], corrected
    else:
        corrected = None
        wait_for(lambda: capture_frame(client, output / "texture-corrected.png")[0],
                 lambda pixels: any(p[1] > 25 and p[0] == 0 for p in pixels))
    frames += animated_frames("corrected", 1)
    return {"baseline": baseline, "corrected": corrected, "entries": entries, "captures": frames}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--cwd", type=Path, required=True, help="Staged runtime working directory")
    parser.add_argument("--output", type=Path, required=True, help="New directory for test artifacts")
    parser.add_argument("--case", choices=("explicit", "watcher", "texture"), default="explicit")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(mode=0o700)
    arguments = ("--scene", organic_scene(output)) if args.case == "explicit" else ()
    renderer = OwnedRenderer(args.binary.resolve(), output, "reload", free_udp_port(), [],
                             arguments=arguments, cwd=args.cwd.resolve())
    evidence = {"owner": renderer.owner}
    try:
        with closing(renderer.client()) as client:
            if args.case == "explicit":
                evidence.update(verify_failed_reload(renderer, client, output))
            else:
                evidence.update(verify_watched_reload(renderer, client, output, texture=args.case == "texture"))
    finally:
        evidence["cleanup"], cleanup_error = collect_cleanup(renderer)
        (output / "evidence.json").write_text(json.dumps(evidence, indent=2) + "\n")
    # In --scene mode the app returns the openFrameworks signal status after shutdown.
    cleanup = evidence["cleanup"]
    if cleanup_error:
        assert cleanup["return_code"] == signal.SIGTERM and cleanup["socket_removed"] and not cleanup["forced_kill"], cleanup_error
        assert cleanup["cleanup_errors"] == [f"owned renderer exited nonzero: {signal.SIGTERM}"], cleanup_error
    print(f"PASS {args.case} shader reload diagnostics and animation. Evidence in {output}")


if __name__ == "__main__":
    main()
