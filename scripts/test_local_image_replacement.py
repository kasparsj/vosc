#!/usr/bin/env python3
"""Verify local-image replacement and explicit unload through layer OSC."""

import argparse
from contextlib import closing
import json
from pathlib import Path
import tempfile
import time

from test_scene_recipe import OwnedRenderer, applied, capture, collect_cleanup, free_udp_port, osc, png_rgba


def diagnostics(client, **params):
    reply = client.call("get_diagnostics", params)
    assert reply["ok"], reply
    return reply["result"]


def verify_replacement(renderer, client, fixtures, output, evidence):
    image_a = fixtures / "image-a.png"
    image_b = fixtures / "image-b.png"
    missing = fixtures / "missing.png"
    corrupt = fixtures / "corrupt.png"
    colors_a = ((240, 20, 30, 255), (175, 35, 110, 255))
    colors_b = ((10, 235, 45, 255), (25, 140, 215, 255))
    image_a.write_bytes(png_rgba(320, 240, *colors_a))
    image_b.write_bytes(png_rgba(320, 240, *colors_b))
    corrupt.write_bytes(b"\x89PNG\r\n\x1a\ntruncated PNG fixture")

    def pixels(name):
        return capture(client, output / f"{name}.png", max_width=320)[2]

    def has_colors(frame, colors):
        return all(frame.count(color) > len(frame) // 5 for color in colors)

    applied(client, "configure_layers", {"count": 1, "layout": "stack"})
    empty = pixels("empty")
    assert empty and all(pixel[:3] == (0, 0, 0) for pixel in empty)
    osc(renderer.port, "/tex", 0, str(image_a))
    before = pixels("image-a")
    assert has_colors(before, colors_a), "image A did not render"

    evidence["failures"] = {}
    for path in (missing, corrupt):
        cursor = diagnostics(client)["latest_cursor"]
        osc(renderer.port, "/tex", 0, str(path))
        deadline = time.monotonic() + 5
        while True:
            entries = diagnostics(client, after=cursor)["entries"]
            matching = [row for row in entries if f"could not load image: {path}" == row["message"]]
            if matching or time.monotonic() >= deadline:
                break
            time.sleep(0.05)
        evidence["failures"][path.name] = {
            "reported_source": bool(matching),
            "retained_a": pixels(f"after-{path.stem}") == before,
            "diagnostics": entries,
        }

    osc(renderer.port, "/tex", 0, str(image_b))
    after = pixels("image-b")
    evidence["rendered_b"] = after != before and has_colors(after, colors_b)
    osc(renderer.port, "/tex/unload", 0)
    evidence["cleared"] = pixels("after-unload") == empty

    assert evidence["rendered_b"], "image B did not replace the previous texture"
    assert evidence["cleared"], "explicit /tex/unload did not clear layer 0"
    for name, outcome in evidence["failures"].items():
        assert outcome["reported_source"], f"diagnostics did not identify {name}"
        assert outcome["retained_a"], f"{name} removed image A"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--cwd", type=Path, required=True, help="Staged runtime working directory")
    parser.add_argument("--output", type=Path, required=True, help="New directory for test artifacts")
    args = parser.parse_args()
    binary, cwd, output = args.binary.resolve(), args.cwd.resolve(), args.output.resolve()
    if not binary.is_file() or not cwd.is_dir():
        parser.error("--binary and --cwd must exist")
    output.mkdir(mode=0o700)
    renderer = OwnedRenderer(binary, output, "local-image", free_udp_port(), [],
                             arguments=("--size", "320x240", "--fps", "60"), cwd=cwd)
    evidence = {"owner": renderer.owner}
    try:
        with tempfile.TemporaryDirectory(prefix="fixtures-", dir=output) as fixtures:
            with closing(renderer.client()) as client:
                verify_replacement(renderer, client, Path(fixtures), output, evidence)
    finally:
        evidence["cleanup"], cleanup_error = collect_cleanup(renderer)
        (output / "evidence.json").write_text(json.dumps(evidence, indent=2) + "\n")
    assert cleanup_error is None, cleanup_error
    print(f"PASS local-image replacement and unload. Evidence in {output}")


if __name__ == "__main__":
    main()
