"""Explicit, isolated VisualsQuark class-library setup for owned SC checks."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import socket
import subprocess
import unicodedata


STOCK_LIBRARY = Path("/Applications/SuperCollider.app/Contents/Resources/SCClassLibrary")


def checkout_path(value: str | Path | None = None) -> Path:
    selected = value if value is not None else os.environ.get("VISUALS_QUARK_PATH")
    if not selected:
        raise ValueError("select the updated VisualsQuark checkout with --visuals-quark or VISUALS_QUARK_PATH")
    path = Path(selected).expanduser().resolve()
    if not path.is_dir() or not (path / "OSCVisuals.sc").is_file():
        raise ValueError(f"VisualsQuark checkout must contain OSCVisuals.sc: {path}")
    return path


def source_identity(value: str | Path) -> dict:
    path = checkout_path(value)
    sources = {
        item.relative_to(path).as_posix(): hashlib.sha256(item.read_bytes()).hexdigest()
        for item in sorted(path.rglob("*.sc"))
        if item.is_file() and ".git" not in item.relative_to(path).parts
    }
    digest = hashlib.sha256(json.dumps(sources, sort_keys=True).encode()).hexdigest()
    return {"path": str(path), "sources": sources, "sha256": digest}


def class_config(owned: Path, quark: str | Path | None, *, stock_only: bool = False) -> Path:
    path = None if stock_only else checkout_path(quark)
    overwrite = owned / "SystemOverwrites"
    overwrite.mkdir(parents=True)
    (overwrite / "extPlatform.sc").write_text("+ Platform { loadStartupFiles { ^this } }\n")
    config = owned / "sclang_conf.yaml"
    config.write_text("includePaths:\n" + "".join(
        "  - " + json.dumps(str(item), ensure_ascii=False) + "\n"
        for item in (STOCK_LIBRARY, path, overwrite) if item is not None
    ) + "excludePaths: []\npostInlineWarnings: false\n")
    return config


def probe(sclang: Path, quark: str | Path, owned: Path) -> dict:
    """Compile only the selected checkout and reject missing/old APIs before rendering."""
    before = source_identity(quark)
    config = class_config(owned, before["path"])
    script = owned / "probe.scd"
    script.write_text(
        "(\n"
        "if(OSCVisuals.respondsTo(\\sceneReplayVersion).not or: {\n"
        "    OSCVisuals.sceneReplayVersion != 1 or: { OSCVisuals.findMethod(\\replay).isNil }\n"
        "}) { \"VisualsQuark scene replay API 1 is required\".postln; 2.exit } {\n"
        "[\"VOSC_QUARK_SOURCE\", OSCVisuals.filenameSymbol.asString].postln;\n"
        "\"VOSC_QUARK_API_OK 1\".postln;\n"
        "0.exit;\n};\n)\n"
    )
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as listener:
        listener.bind(("127.0.0.1", 0))
        language_port = listener.getsockname()[1]
    if language_port == 33333:
        raise RuntimeError("refusing the default renderer port for a language probe")
    command = [str(sclang), "-a", "-l", str(config), "-D", "-u", str(language_port), "-i", "none", str(script)]
    environment = {**os.environ, "XDG_CONFIG_HOME": str(owned / "config"), "XDG_DATA_HOME": str(owned / "data")}
    process = subprocess.Popen(command, env=environment, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    timed_out = False
    try:
        output, _ = process.communicate(timeout=15)
    except subprocess.TimeoutExpired:
        timed_out = True
        process.terminate()
        try:
            output, _ = process.communicate(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            output, _ = process.communicate(timeout=3)
    (owned / "sclang.log").write_text(output)
    after = source_identity(quark)
    expected_source = str(Path(before["path"]) / "OSCVisuals.sc")
    source_marker = "[VOSC_QUARK_SOURCE, " + expected_source + "]"
    source_observed = unicodedata.normalize("NFD", source_marker) in unicodedata.normalize("NFD", output).splitlines()
    api_observed = "VOSC_QUARK_API_OK 1" in output.splitlines()
    result = {"pid": process.pid, "return_code": process.returncode, "timed_out": timed_out,
              "language_port": language_port, "command": command, "before": before, "after": after,
              "source_stable": before == after, "api_version": 1 if api_observed else None,
              "compiled_source": expected_source if source_observed else None}
    (owned / "receipt.json").write_text(json.dumps(result, indent=2) + "\n")
    if (timed_out or process.returncode != 0 or not api_observed or not source_observed
            or before != after):
        raise RuntimeError(f"selected VisualsQuark failed isolated replay-API probe; see {owned / 'sclang.log'}")
    return result
