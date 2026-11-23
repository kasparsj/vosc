#!/usr/bin/env python3
"""Verify layer and named shared local-image replacement through OSC."""

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


def replace_with_failed_image(renderer, client, target, path):
    cursor = diagnostics(client)["latest_cursor"]
    osc(renderer.port, "/tex", target, str(path))
    deadline = time.monotonic() + 5
    while True:
        entries = diagnostics(client, after=cursor)["entries"]
        if any(row["message"] == f"could not load image: {path}" for row in entries):
            return True, entries
        if time.monotonic() >= deadline:
            return False, entries
        time.sleep(0.05)


def has_colors(frame, colors):
    return all(frame.count(color) > len(frame) // 5 for color in colors)


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

    applied(client, "configure_layers", {"count": 1, "layout": "stack"})
    empty = pixels("empty")
    assert empty and all(pixel[:3] == (0, 0, 0) for pixel in empty)
    osc(renderer.port, "/tex", 0, str(image_a))
    before = pixels("image-a")
    assert has_colors(before, colors_a), "image A did not render"

    evidence["failures"] = {}
    for path in (missing, corrupt):
        reported, entries = replace_with_failed_image(renderer, client, 0, path)
        evidence["failures"][path.name] = {
            "reported_source": reported,
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


def verify_shared_replacement(renderer, client, fixtures, output, evidence):
    shared = "namedSharedImage"
    image_a, image_b = (fixtures / name for name in ("shared-a.png", "shared-b.png"))
    missing, corrupt = (fixtures / name for name in ("shared-missing.png", "shared-corrupt.png"))
    colors_a = ((240, 20, 30, 255), (175, 35, 110, 255))
    colors_b = ((10, 235, 45, 255), (25, 140, 215, 255))
    image_a.write_bytes(png_rgba(160, 240, *colors_a))
    image_b.write_bytes(png_rgba(160, 240, *colors_b))
    corrupt.write_bytes(b"\x89PNG\r\n\x1a\ntruncated PNG fixture")

    def layer_pixels(name):
        width, height, frame, _ = capture(client, output / f"shared-{name}.png", max_width=320)
        assert (width, height) == (320, 240), (width, height)
        half = width // 2
        return tuple([pixel for row in range(height)
                      for pixel in frame[row * width + start:row * width + start + half]]
                     for start in (0, half))

    applied(client, "configure_layers", {"count": 2, "layout": "row"})
    osc(renderer.port, "/tex", shared, str(image_a))
    osc(renderer.port, "/tex", 0, shared)
    left, right = layer_pixels("left-only")
    assert has_colors(left, colors_a) and all(pixel[:3] == (0, 0, 0) for pixel in right)
    osc(renderer.port, "/tex", 1, shared)
    before = layer_pixels("baseline")
    assert before[0] == left and has_colors(before[1], colors_a), "both layers must render image A"

    evidence["failures"] = {}
    for path in (missing, corrupt):
        reported, entries = replace_with_failed_image(renderer, client, shared, path)
        after = layer_pixels(f"after-{path.stem}")
        evidence["failures"][path.name] = {
            "reported_source": reported,
            "retained_a_by_layer": [current == baseline for current, baseline in zip(after, before)],
            "diagnostics": entries,
        }

    osc(renderer.port, "/tex", shared, str(image_b))
    after = layer_pixels("image-b")
    evidence["rendered_b_by_layer"] = [has_colors(layer, colors_b) for layer in after]
    assert all(evidence["rendered_b_by_layer"]), "image B must reach both existing bindings"
    for name, outcome in evidence["failures"].items():
        assert outcome["reported_source"], f"diagnostics did not identify {name}"
        assert all(outcome["retained_a_by_layer"]), f"{name} removed image A: {outcome['retained_a_by_layer']}"


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
                evidence["shared"] = {}
                verify_shared_replacement(renderer, client, Path(fixtures), output, evidence["shared"])
    finally:
        evidence["cleanup"], cleanup_error = collect_cleanup(renderer)
        (output / "evidence.json").write_text(json.dumps(evidence, indent=2) + "\n")
    assert cleanup_error is None, cleanup_error
    print(f"PASS layer and named shared local-image replacement, and layer unload. Evidence in {output}")


if __name__ == "__main__":
    main()
