#!/usr/bin/env python3
"""Regenerate the SuperCollider setup for manifest-backed examples."""

import argparse
import json

from scene_recipe import ROOT, _sc_value, render_scene

EXAMPLES = ROOT / "examples"


def editable_scene(manifest):
    width, height = manifest["size"]
    descriptor = {
        "resolution": {"width": width, "height": height},
        "onset_gate": manifest.get("onset_gate", -1),
        "assets": [],
        "commands": manifest["commands"],
    }
    return render_scene(descriptor).replace(
        "var rendererPort = 33333;", "var rendererPort = 43333;", 1
    )


def sieve_scene(manifest):
    width, height = manifest["size"]
    gate = manifest.get("onset_gate", -1)
    if type(gate) is not int or gate not in (-1, 0, 1):
        raise ValueError("Sieve onset_gate must be -1, 0, or 1")
    lines = [
        f"// Blank renderer: vosc --size {width}x{height}",
        "// Use a fresh renderer with automatic onset gating or start with scene.vosc.json.",
        "(",
        "var sceneRoot = PathName(thisProcess.nowExecutingPath).pathOnly;",
        "var rendererHost = \"127.0.0.1\";",
        "var rendererPort = 33333; // change only to a renderer endpoint you own",
        "var rendererRoot = sceneRoot; // set to the scene folder on a remote renderer",
        "var visuals = OSCVisuals(rendererHost, rendererPort, reset: true);",
        "",
        "visuals.net.sendBundle(nil,",
    ]
    for command in manifest["commands"]:
        args = [
            "rendererRoot +/+ " + _sc_value(arg) if arg["type"] == "asset" else _sc_value(arg)
            for arg in command["args"]
        ]
        lines.append(f"    ['{command['address']}', {', '.join(args)}],")
    if gate != -1:
        lines.append(f"    ['/onset', {gate}],")
    # Drain queued setup commands on the same update that applies the final gate.
    lines.extend(["    ['/onset/force']", ");", "~visuals = visuals;", ")", ""])
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    stale = []
    for name, render in (("editable-scene", editable_scene), ("sieve", sieve_scene)):
        folder = EXAMPLES / name
        manifest = json.loads((folder / "scene.vosc.json").read_text())
        path = folder / "scene.scd"
        generated = "// Generated from scene.vosc.json by scripts/generate_example_scd.py.\n" + render(manifest)
        if args.check:
            if not path.is_file() or path.read_text() != generated:
                stale.append(str(path.relative_to(ROOT)))
        else:
            path.write_text(generated)
    if stale:
        parser.error("regenerate stale companions: " + ", ".join(stale))


if __name__ == "__main__":
    main()
