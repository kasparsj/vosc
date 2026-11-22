#!/usr/bin/env python3
"""Verify an explicit failed OSC reload on a disposable Organic Trails renderer."""

import argparse
import base64
from contextlib import closing
import json
from pathlib import Path
import shutil
import signal
import time

from test_engine_api import png_pixels
from test_scene_recipe import ROOT, OwnedRenderer, collect_cleanup, free_udp_port, osc


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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--cwd", type=Path, required=True, help="Staged runtime working directory")
    parser.add_argument("--output", type=Path, required=True, help="New directory for test artifacts")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(mode=0o700)
    manifest = organic_scene(output)
    renderer = OwnedRenderer(args.binary.resolve(), output, "reload", free_udp_port(), [],
                             arguments=("--scene", manifest), cwd=args.cwd.resolve())
    evidence = {"owner": renderer.owner}
    try:
        with closing(renderer.client()) as client:
            evidence.update(verify_failed_reload(renderer, client, output))
    finally:
        evidence["cleanup"], cleanup_error = collect_cleanup(renderer)
        (output / "evidence.json").write_text(json.dumps(evidence, indent=2) + "\n")
    # In --scene mode the app returns the openFrameworks signal status after shutdown.
    cleanup = evidence["cleanup"]
    if cleanup_error:
        assert cleanup["return_code"] == signal.SIGTERM and cleanup["socket_removed"] and not cleanup["forced_kill"], cleanup_error
        assert cleanup["cleanup_errors"] == [f"owned renderer exited nonzero: {signal.SIGTERM}"], cleanup_error
    print(f"PASS previous Layer 0 program and animation survived failed reload. Evidence in {output}")


if __name__ == "__main__":
    main()
