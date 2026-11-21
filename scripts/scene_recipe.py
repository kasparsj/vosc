#!/usr/bin/env python3
"""Export an editable VOSC OSC/SuperCollider scene folder."""

from __future__ import annotations

import argparse
import asyncio
import ctypes
import errno
import json
import os
from pathlib import Path
import shutil
import struct
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "mcp"))
from engine_client import EngineClient, EngineClientError  # noqa: E402
from scene_recipe_schema import (RecipeValidationError, validate_descriptor,
                                 validate_export_reply)  # noqa: E402


def _sc_string(value: str) -> str:
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n").replace("\r", "\\r") + '"'


def _sc_value(arg: dict) -> str:
    if arg["type"] in {"s", "asset"}:
        return _sc_string(arg["value"])
    if arg["type"] == "f":
        return _sc_float32(arg["value"])
    return str(arg["value"])


def _sc_float32(value: float) -> str:
    """Return the shortest deterministic SC Float spelling with identical float32 bits."""
    expected = struct.pack("!f", value)
    if expected == b"\x80\x00\x00\x00":
        return "-0.0"
    candidates: set[str] = set()
    for precision in range(1, 10):
        for kind in ("g", "e", "f"):
            candidate = format(value, f".{precision}{kind}").lower()
            if "e" in candidate:
                mantissa, exponent = candidate.split("e")
                mantissa = mantissa.rstrip("0").rstrip(".")
                exponent = str(int(exponent))
                candidate = mantissa + "e" + exponent
            elif "." in candidate:
                candidate = candidate.rstrip("0").rstrip(".")
            if "." not in candidate and "e" not in candidate:
                candidate += ".0"
            try:
                if struct.pack("!f", float(candidate)) == expected:
                    candidates.add(candidate)
            except (OverflowError, ValueError):
                pass
    if not candidates:
        raise ValueError(f"cannot encode float32 for SuperCollider: {value!r}")
    return min(candidates, key=lambda item: (len(item), "e" in item, item))


_VECTOR_ADDRESSES = {
    "/layer/pos", "/layer/rot", "/layer/scale", "/layer/pivot", "/layer/size", "/layer/tint",
}


def _render_send(command: dict) -> str:
    address = command["address"]
    args = command["args"]
    rendered: list[str]
    if address in _VECTOR_ADDRESSES:
        rendered = [_sc_value(args[0]), "[" + ", ".join(_sc_value(arg) for arg in args[1:]) + "]"]
    elif address in {"/shader/var", "/tex/var"} and len(args) > 3:
        rendered = [_sc_value(args[0]), _sc_value(args[1]),
                    "[" + ", ".join(_sc_value(arg) for arg in args[2:]) + "]"]
    else:
        rendered = [_sc_value(arg) for arg in args]
    return f"    visuals.send('{address}'{', ' if rendered else ''}{', '.join(rendered)});"




def render_scene(descriptor: dict) -> str:
    lines = [
        "(",
        "var sceneRoot = PathName(thisProcess.nowExecutingPath).pathOnly;",
        "var rendererHost = \"127.0.0.1\"; // editable",
        "var rendererPort = 33333; // editable; use an owned private port for tests",
        "var rendererRoot = sceneRoot; // editable for remote/shared filesystems",
        f"var width = {descriptor['resolution']['width']};",
        f"var height = {descriptor['resolution']['height']};",
        f"var finalGate = {descriptor['onset_gate']};",
        "var replyTimeout = 15.0;",
        "var visuals;",
        "",
        "if(OSCVisuals.respondsTo(\\sceneReplayVersion).not or: { OSCVisuals.sceneReplayVersion != 1 }) {",
        "    Error(\"VisualsQuark scene replay API 1 is required; update OSCVisuals before evaluating this scene\").throw",
        "};",
        "visuals = OSCVisuals(rendererHost, rendererPort);",
        "",
        "visuals.replay({ |visuals|",
    ]
    feedback_names = {asset["name"] for asset in descriptor["assets"] if asset["kind"] == "feedback_fragment"}
    if feedback_names:
        insertion = lines.index("visuals = OSCVisuals(rendererHost, rendererPort);")
        lines[insertion:insertion] = [
            "if(OSCVisuals.respondsTo(\\sceneFeedbackReplayVersion).not or: { OSCVisuals.sceneFeedbackReplayVersion != 1 }) {",
            '    Error("VisualsQuark feedback replay API 1 is required").throw',
            "};",
        ]
    previous_section = None
    for command in descriptor["commands"]:
        address = command["address"]
        if address in {"/layers/reset", "/input", "/input/set", "/var"}:
            section = "Layout, inputs and shared values"
        elif address in {"/shader", "/tex", "/shader/texture"}:
            section = "Layer resources"
        elif address.startswith("/shader/") or address.startswith("/tex/"):
            section = "Shader and texture controls"
        else:
            section = "Layer controls"
        if section != previous_section:
            lines.append(f"    // {section}")
            previous_section = section
        if address == "/tex" and command["args"][1]["value"] in feedback_names:
            command = {**command, "args": [command["args"][0], {"type": "s", "value": "shader_pp:" + command["args"][1]["value"]}]}
        lines.append(_render_send(command))
    lines += [
        "}, sceneRoot: sceneRoot, width: width, height: height, rendererRoot: rendererRoot,",
        "   finalGate: finalGate, replyTimeout: replyTimeout);",
        "~exportedScene = visuals; // use a different environment variable if preferred",
        ")",
        "",
        "// Evaluate live adjustments separately, after replay reports applied:",
        "// ~exportedScene.send('/layer/alpha', 0, 0.5);",
        "",
    ]
    return "\n".join(lines)


def render_readme(descriptor: dict) -> str:
    return f"""# Editable VOSC scene

Edit `scene.scd` and the files in `assets/`, then evaluate the whole saved file in SuperCollider 3.14.1.
This scene uses `OSCVisuals` from VisualsQuark with scene replay API 1:
https://github.com/kasparsj/VisualsQuark
The running language must have the updated class loaded before evaluating the scene. Local development
changes to a checkout are not automatically installed or published. No class installation or reload is
performed by this file. An absent or older class fails before any scene commands are sent.

`OSCVisuals.replay` records the visible `visuals.send(...)` calls, preflights all of them, sends one ordered immediate OSC
bundle, and reports `applied`, `failed`, or `unknown`.
Runtime shader/image failures can leave earlier commands applied; there is no rollback or automatic retry.

The default endpoint is local port 33333. For a private or remote renderer, edit `rendererHost`,
`rendererPort`, and `rendererRoot` near the top of `scene.scd`. Remote assets must already be copied
to a path visible to the renderer. No JSON snapshot is required. After replay reports `applied`, use
`~exportedScene.send(...)` for ordinary immediate live changes. Live Array arguments use the ordinary
OSCVisuals JSON-string representation; replay keeps typed flattened OSC components.

The Quark and scene body are trusted executable SuperCollider source. A retained callback recorder
expires when its callback returns. The ordinary OSCVisuals instance remains usable after success,
failure or timeout and never retries automatically. Shader/image files remain in the exported folder;
no separate sender helper or JSON snapshot is required.

Recipe: {len(descriptor['commands'])} setup commands, {len(descriptor['assets'])} assets.
"""


def _rename_exclusive(source: Path, destination: Path, *, parent_fd: int | None = None) -> None:
    """Atomically publish a directory without replacing a concurrent target."""
    libc = ctypes.CDLL(None, use_errno=True)
    renameatx_np = getattr(libc, "renameatx_np", None)
    if renameatx_np is None:
        raise OSError(errno.ENOTSUP, "atomic no-replace directory rename is unavailable")
    renameatx_np.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
    renameatx_np.restype = ctypes.c_int
    source_fd = destination_fd = -2
    source_name = os.fsencode(source)
    destination_name = os.fsencode(destination)
    if parent_fd is not None:
        source_fd = destination_fd = parent_fd
        source_name = os.fsencode(source.name)
        destination_name = os.fsencode(destination.name)
    if renameatx_np(source_fd, source_name, destination_fd, destination_name, 0x00000004) != 0:
        code = ctypes.get_errno()
        raise OSError(code, os.strerror(code), str(destination))


def write_folder(destination: Path, descriptor: dict) -> None:
    validate_descriptor(descriptor)
    if destination.name in {"", ".", ".."}:
        raise ValueError("destination must name a new directory")
    parent = destination.parent.resolve(strict=False)
    parent.mkdir(parents=True, exist_ok=True)
    parent = parent.resolve(strict=True)
    destination = parent / destination.name
    parent_info = parent.stat()
    if parent_info.st_uid != os.getuid() or parent_info.st_mode & 0o022:
        raise PermissionError("destination parent must be owned by this user and not group/other writable")
    if destination.exists() or destination.is_symlink():
        raise FileExistsError(f"destination already exists: {destination}")
    parent_fd = os.open(parent, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    staging: Path | None = None
    staging_identity: tuple[int, int] | None = None
    try:
        staging = Path(tempfile.mkdtemp(prefix=f".{destination.name}.staging-", dir=parent))
        staging_info = staging.stat(follow_symlinks=False)
        staging_identity = (staging_info.st_dev, staging_info.st_ino)
        for asset in descriptor["assets"]:
            path = staging / asset["name"]
            path.parent.mkdir(parents=True, exist_ok=True)
            data = asset["data"].encode("utf-8") if asset["encoding"] == "utf8" else __import__("base64").b64decode(asset["data"], validate=True)
            with path.open("xb") as stream:
                stream.write(data); stream.flush(); os.fsync(stream.fileno())
        (staging / "scene.scd").write_text(render_scene(descriptor), encoding="utf-8", newline="\n")
        (staging / "README.md").write_text(render_readme(descriptor), encoding="utf-8", newline="\n")
        parent_info = os.fstat(parent_fd)
        current_parent = parent.stat()
        if (parent_info.st_dev, parent_info.st_ino) != (current_parent.st_dev, current_parent.st_ino):
            raise OSError(errno.ESTALE, "destination parent changed during export", str(parent))
        current_staging = staging.stat(follow_symlinks=False)
        if (staging.parent != parent or not staging.is_dir() or staging.is_symlink() or
                (current_staging.st_dev, current_staging.st_ino) != staging_identity):
            raise OSError(errno.ESTALE, "owned staging directory changed during export", str(staging))
        _rename_exclusive(staging, destination, parent_fd=parent_fd)
    except BaseException:
        if staging is not None and staging_identity is not None:
            try:
                current_parent = parent.stat()
                current_staging = staging.stat(follow_symlinks=False)
                if ((current_parent.st_dev, current_parent.st_ino) ==
                        (os.fstat(parent_fd).st_dev, os.fstat(parent_fd).st_ino) and
                        (current_staging.st_dev, current_staging.st_ino) == staging_identity and
                        not staging.is_symlink()):
                    shutil.rmtree(staging)
            except FileNotFoundError:
                pass
        raise
    finally:
        os.close(parent_fd)


async def run(args: argparse.Namespace) -> int:
    reply = await EngineClient(args.socket).query("export_scene", {})
    validate_export_reply(reply)
    if reply["ok"] is False:
        print(json.dumps(reply, ensure_ascii=False, sort_keys=True))
        return 2
    descriptor = reply["result"]["recipe"]
    write_folder(args.directory, descriptor)
    print(json.dumps({"status": "exported", "directory": str(args.directory),
                      "commands": len(descriptor["commands"]), "assets": len(descriptor["assets"])},
                     ensure_ascii=False, sort_keys=True))
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--socket", required=True, type=Path)
    sub = parser.add_subparsers(dest="command", required=True)
    export = sub.add_parser("export"); export.add_argument("directory", type=Path)
    args = parser.parse_args()
    if not args.socket.is_absolute(): parser.error("--socket must be absolute")
    if args.command != "export": parser.error("unsupported command")
    try:
        return asyncio.run(run(args))
    except (OSError, ValueError, EngineClientError) as error:
        print(json.dumps({"status": "failed", "error": str(error)}, ensure_ascii=False, sort_keys=True)); return 2


if __name__ == "__main__":
    raise SystemExit(main())
