#!/usr/bin/env python3
"""Verify layer, named shared, and shader sampler local-image replacement through OSC."""

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


def replace_with_failed_image(renderer, client, address, args, path):
    cursor = diagnostics(client)["latest_cursor"]
    osc(renderer.port, address, *args, str(path))
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


def capture_row(client, path):
    width, height, frame, _ = capture(client, path, max_width=320)
    assert (width, height) == (320, 240), (width, height)
    half = width // 2
    return tuple([pixel for row in range(height)
                  for pixel in frame[row * width + start:row * width + start + half]]
                 for start in (0, half))


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
        reported, entries = replace_with_failed_image(renderer, client, "/tex", (0,), path)
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
        return capture_row(client, output / f"shared-{name}.png")

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
        reported, entries = replace_with_failed_image(renderer, client, "/tex", (shared,), path)
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


SAMPLER_FRAGMENT = """#version 330
uniform sampler2D imageTex;
out vec4 fragColor;
void main() {
    fragColor = vec4(texture(imageTex, vec2(0.25, 0.25)).rgb, 1.0);
}
"""


def verify_sampler_replacement(renderer, client, fixtures, output, evidence, *, shared):
    shared_name = "sharedSamplerImage"
    image_a, image_b, missing, corrupt = (fixtures / f"sampler-{name}.png" for name in
                                         ("a", "b", "missing", "corrupt"))
    color_a, color_b = (240, 20, 30, 255), (10, 235, 45, 255)
    image_a.write_bytes(png_rgba(32, 32, color_a, color_a))
    image_b.write_bytes(png_rgba(32, 32, color_b, color_b))
    corrupt.write_bytes(b"\x89PNG\r\n\x1a\ntruncated PNG fixture")

    def bind(layer, source):
        osc(renderer.port, "/shader/texture", layer, "imageTex", str(source))

    def pixels(name):
        return capture_row(client, output / f"sampler-{name}.png")

    applied(client, "configure_layers", {"count": 0, "layout": "row"})
    applied(client, "configure_layers", {"count": 2, "layout": "row"})
    for layer in (0, 1):
        applied(client, "publish_shader", {"layer": layer, "source": SAMPLER_FRAGMENT})
    source_a = shared_name if shared else image_a
    if shared:
        osc(renderer.port, "/tex", shared_name, str(image_a))
    bind(1, source_a)
    bind(0, source_a)
    baseline = pixels("baseline")
    expected_a = tuple([color_a] * len(region) for region in baseline)
    assert baseline == expected_a, "both samplers must render image A"
    expected_b = tuple([color_b] * len(region) for region in baseline)

    failures = evidence["failures"] = {}
    for path in (missing, corrupt):
        reported, entries = replace_with_failed_image(
            renderer, client, "/shader/texture", (0, "imageTex"), path)
        after = pixels(f"after-{path.stem}")
        outcome = failures[path.name] = {
            "reported_source": reported,
            "retained_a_by_layer": [current == baseline[index] for index, current in enumerate(after)],
            "diagnostics": entries,
        }
        if shared:
            # Updating the named source without rebinding detects a detached sampler.
            osc(renderer.port, "/tex", shared_name, str(image_b))
            updated = pixels(f"shared-update-after-{path.stem}")
            outcome["followed_shared_update_by_layer"] = [
                current == expected_b[index] for index, current in enumerate(updated)
            ]
            osc(renderer.port, "/tex", shared_name, str(image_a))
        bind(0, source_a)
        assert pixels(f"restored-{path.stem}") == baseline, "baseline was not restored"

    bind(0, image_b)
    after = pixels("image-b")
    evidence["rendered_b_on_addressed_sampler"] = after[0] == expected_b[0]
    evidence["retained_a_on_second_sampler"] = after[1] == baseline[1]


def assert_sampler_replacement(evidence):
    failures = []
    for scope, case in evidence.items():
        for name, outcome in case["failures"].items():
            if not outcome["reported_source"]:
                failures.append(f"{scope}: diagnostics did not identify {name}")
            if not all(outcome["retained_a_by_layer"]):
                failures.append(f"{scope}: {name} removed image A: {outcome['retained_a_by_layer']}")
            if scope == "shared" and not all(outcome["followed_shared_update_by_layer"]):
                failures.append(f"{scope}: {name} detached a sampler from the named texture")
        if not case["rendered_b_on_addressed_sampler"] or not case["retained_a_on_second_sampler"]:
            failures.append(f"{scope}: image B must reach only the addressed sampler")
    assert not failures, "\n".join(failures)


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
                samplers = evidence["samplers"] = {}
                for scope in ("local", "shared"):
                    case_output = output / f"sampler-{scope}"
                    case_output.mkdir()
                    samplers[scope] = {}
                    verify_sampler_replacement(renderer, client, Path(fixtures), case_output,
                                               samplers[scope], shared=(scope == "shared"))
    finally:
        evidence["cleanup"], cleanup_error = collect_cleanup(renderer)
        (output / "evidence.json").write_text(json.dumps(evidence, indent=2) + "\n")
    assert cleanup_error is None, cleanup_error
    assert_sampler_replacement(evidence["samplers"])
    print(f"PASS layer, named shared, and shader sampler local-image replacement. Evidence in {output}")


if __name__ == "__main__":
    main()
