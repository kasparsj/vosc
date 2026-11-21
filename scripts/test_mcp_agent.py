#!/usr/bin/env python3
"""Run one ephemeral image-capable Codex episode against an owned MCP renderer.

The adapter and renderer are supplied by absolute path. This harness changes the
scene but never launches, stops, or restarts the renderer and never edits global
Codex configuration.
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import signal
import subprocess


ROOT = Path(__file__).resolve().parents[1]
ADAPTER = ROOT / "mcp" / "adapter.py"
PYTHON = ROOT / "mcp" / ".venv" / "bin" / "python"
TOOLS = ["get_capabilities", "get_scene", "get_diagnostics", "get_request", "configure_layers",
         "publish_shader", "set_uniform", "set_layer", "capture_preview"]


PROMPT = """This is a live vosc MCP authoring acceptance episode against a disposable renderer.
Use only the nine vosc MCP tools; do not use shell, file-reading, browsing, or other tools.
Inspect capabilities and scene, including which uniforms are writable. Reuse existing layer 0 so
any pre-seeded texture remains attached; configure one stack layer only if the scene is empty.
Author an animated GLSL shader with a writable vec3 uniform named uTint, publish it, wait for
truthful rendering, and capture a preview. If a texture is present, sample it in the shader.
Describe a detail you actually observe in the returned image, then revise the uniform for an
obvious visible color change and capture again. Next publish invalid GLSL, report the returned
compile diagnostics and verify by preview that the prior working visual remains. Correct the
source in the same scene and capture the recovered output. Include every request_id and state
which observations came from native MCP images. Do not infer appearance from metadata.
"""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--socket", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--allow-reset", action="store_true")
    parser.add_argument("--timeout", type=int, default=600, help="maximum host episode seconds")
    args = parser.parse_args()
    if not args.allow_reset:
        parser.error("--allow-reset is required because the agent replaces the disposable scene")
    if not args.socket.is_absolute() or not args.output.is_absolute():
        parser.error("--socket and --output must be absolute paths")
    args.output.mkdir(parents=True, exist_ok=True)
    final_path = args.output / "host-final.txt"
    events_path = args.output / "host-events.jsonl"
    server = (
        "mcp_servers = { vosc = { command = " + json.dumps(str(PYTHON)) + ", args = [" +
        json.dumps(str(ADAPTER)) + ", \"--socket\", " + json.dumps(str(args.socket)) + "] } }"
    )
    command = ["codex", "exec", "--ignore-user-config", "--ephemeral", "--json", "-C",
               str(args.output), "-s", "read-only", "--skip-git-repo-check", "-c", server,
               "-o", str(final_path)]
    for tool in TOOLS:
        command += ["-c", f'mcp_servers.vosc.tools.{tool}.approval_mode="approve"']
    command.append(PROMPT)
    invocation = {"args": command, "global_config_modified": False, "socket": str(args.socket),
                  "scope": "ephemeral host and caller-owned disposable renderer"}
    (args.output / "host-invocation.json").write_text(json.dumps(invocation, indent=2) + "\n", encoding="utf-8")
    stderr_path = args.output / "host-stderr.txt"
    timed_out = False
    with events_path.open("wb") as events, stderr_path.open("wb") as stderr:
        process = subprocess.Popen(command, cwd=args.output, stdout=events, stderr=stderr, start_new_session=True)
        try:
            returncode = process.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            try:
                os.killpg(process.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                returncode = process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.wait(timeout=3)
                returncode = 124
    (args.output / "host-exit.json").write_text(
        json.dumps({"returncode": returncode, "timed_out": timed_out,
                    "timeout_seconds": args.timeout}, indent=2) + "\n", encoding="utf-8")
    return 124 if timed_out else returncode


if __name__ == "__main__":
    raise SystemExit(main())
