#!/usr/bin/env python3
"""Regenerate the SuperCollider setup for manifest-backed examples."""

import argparse
import json

from scene_recipe import ROOT, _sc_value, render_scene

EXAMPLES = ROOT / "examples"
SETUP_START = "// BEGIN GENERATED EXAMPLE SETUP"
SETUP_END = "// END GENERATED EXAMPLE SETUP"
V2_EXAMPLES = (
    "intro", "shaders", "sketches", "performance", "test", "camera", "geom",
    "buffer", "deferred", "clouds", "grass", "particles-gpu", "video",
    "webcam-delay", "webcam-looper", "ultralight",
)


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


def v2_value(argument, manifest):
    kind, value = argument["type"], argument["value"]
    if kind == "layer":
        return str(value) if isinstance(value, int) else _sc_value({"type": "s", "value": value})
    if kind == "ref":
        return _sc_value({"type": "s", "value": value})
    if kind == "vector":
        return "[" + ", ".join(v2_value(item, manifest) for item in value) + "]"
    if kind != "source":
        return _sc_value(argument)

    source = value["kind"]
    if source == "grid-plane":
        return '"2d:gridplane"'
    if source == "sketch":
        return _sc_value({"type": "s", "value": value["name"]})
    if source == "color":
        return _sc_value({"type": "s", "value": value["value"]})
    if source == "video":
        return "clip"
    if source in {"camera", "html"}:
        default = manifest["inputs"][value["input"]]["default"]
        if source == "camera":
            return f'"webcam:" ++ ((~cameraDevice ? {default}).asString)'
        return f'(~pageUrl ? {_sc_value({"type": "s", "value": default})})'
    if source == "stages":
        path = value["stages"]["fragment"]
    elif source in {"image", "fragment", "feedback", "model"}:
        path = value["path"]
    else:
        raise ValueError(f"unsupported example source {source!r}")
    path_value = '~exampleAssetRoot +/+ ' + _sc_value({"type": "s", "value": path})
    if source == "feedback":
        return f'("shader_pp:" ++ ({path_value}))'
    return path_value


def v2_send(command, manifest, name):
    address = command["address"]
    args = [v2_value(value, manifest) for value in command["args"]]
    if name == "deferred" and address == "/geom":
        # The native checkpoint fixes the red channel. The SC cue still
        # chooses it when the performer evaluates the setup.
        color = command["args"][4]["value"]
        args[4] = "[1.0.rand, " + ", ".join(v2_value(item, manifest) for item in color[1:]) + "]"
    if address == "/tex/set" and command["args"][1]["value"] == "aspectRatio":
        args[2] = "true" if command["args"][2]["value"] else "false"
    return f"~visuals.send('{address}', {', '.join(args)});"


def v2_connection(reset):
    return [
        '~exampleAssetRoot = ~rendererRoot ? PathName(thisProcess.nowExecutingPath).pathOnly;',
        '~visuals = OSCVisuals(~rendererHost ? "127.0.0.1",',
        '    ~rendererPort ? ("VOSC_OSC_PORT".getenv ? "33333").asInteger,',
        f'    reset: {"true" if reset else "false"});',
    ]


def v2_scene(name, manifest):
    width, height = manifest["size"]
    lines = [
        SETUP_START,
        "// This section is regenerated from scene.vosc.json.",
        "// After native launch, evaluate only this Attach block before the live cues.",
        "(",
        *v2_connection(False),
        ")",
        "",
        f"// To start from a blank owned renderer (vosc --size {width}x{height}), evaluate this Setup block instead.",
        "(",
    ]
    if name == "video":
        lines.extend([
            'var clip = ~clip ? "VOSC_VIDEO_CLIP".getenv;',
            'var validClip = clip.isString',
            '    and: {PathName(clip).isAbsolutePath}',
            '    and: {clip.toLower.endsWith(".mov") or: {clip.toLower.endsWith(".mp4")}}',
            '    and: {File.exists(clip)};',
            'if(validClip.not) {',
            '    Error("Set ~clip or VOSC_VIDEO_CLIP to an existing renderer-local MOV or MP4").throw;',
            '};',
        ])
    gate = manifest.get("onset_gate", -1)
    if gate != -1:
        lines.extend([
            '// An existing input might have enabled automatic onset gating.',
            '~visuals = OSCVisuals(~rendererHost ? "127.0.0.1",',
            '    ~rendererPort ? ("VOSC_OSC_PORT".getenv ? "33333").asInteger);',
            f"~visuals.send('/onset', {gate});",
        ])
    lines.extend(v2_connection(True))
    reset, *commands = manifest["commands"]
    count, layout = (v2_value(argument, manifest) for argument in reset["args"])
    if name == "intro":
        lines.extend([
            "// Try both layouts before loading the color source.",
            f"~visuals.send('/layers', {count});",
            "~visuals.send('/layers/layout', \"stack\");",
            f"~visuals.send('/layers/layout', {layout});",
        ])
    elif name not in {"buffer", "deferred"}:
        lines.append(f"~visuals.send('/layers', {count}, {layout});")
    if name in {"grass", "clouds"}:
        # Preserve the original ordinary OSC ordering. Native V2 reflects
        # bindings after loading the stages.
        stages = next(command for command in commands if command["address"] == "/shader")
        commands.remove(stages)
        commands.append(stages)
    after_capture = []
    if name == "webcam-delay":
        commands, after_capture = commands[:2], commands[2:]
    lines.extend(v2_send(command, manifest, name) for command in commands)
    lines.append(")")
    if after_capture:
        lines.extend([
            "",
            "// Blank renderer only. Evaluate once the live preview appears.",
            "(",
            *(v2_send(command, manifest, name) for command in after_capture),
            ")",
        ])
    return "\n".join([*lines, SETUP_END])


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
    for name in V2_EXAMPLES:
        folder = EXAMPLES / name
        manifest = json.loads((folder / "scene.vosc.json").read_text())
        path = folder / "scene.scd"
        content = path.read_text()
        before, start, tail = content.partition(SETUP_START)
        _, end, after = tail.partition(SETUP_END)
        if not (start and end):
            parser.error(f"missing setup markers in {path.relative_to(ROOT)}")
        generated = before + v2_scene(name, manifest) + after
        if args.check:
            if content != generated:
                stale.append(str(path.relative_to(ROOT)))
        else:
            path.write_text(generated)
    if stale:
        parser.error("regenerate stale companions: " + ", ".join(stale))


if __name__ == "__main__":
    main()
