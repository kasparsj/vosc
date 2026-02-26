#!/usr/bin/env python3
"""Generate canonical OSC reference from parser/handlers + status metadata."""

from __future__ import annotations

import argparse
import difflib
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Set, Tuple

ROOT = Path(__file__).resolve().parents[1]
PARSER_FILE = ROOT / "src/osc/CommandParser.cpp"
OUTPUT_FILE = ROOT / "docs/OSC-reference.md"
METADATA_FILE = ROOT / "docs/osc-command-status.json"

HANDLER_FILES: List[Path] = [
    ROOT / "src/Texture.cpp",
    ROOT / "src/tex/TexData.cpp",
    ROOT / "src/Geom.cpp",
    ROOT / "src/shader/Shader.cpp",
    ROOT / "src/layer/Layer.cpp",
    ROOT / "src/Material.cpp",
    ROOT / "src/Camera.cpp",
    ROOT / "src/input/OSCInput.cpp",
    ROOT / "src/input/Inputs.cpp",
    ROOT / "src/light/Lights.cpp",
    ROOT / "src/var/BaseVar.cpp",
]

STATUS_VALUES = {
    "supported",
    "alias",
    "deprecated",
    "disabled",
    "feature-flagged",
}

DOMAIN_ORDER = {
    "setup": 0,
    "input": 1,
    "camera": 2,
    "light": 3,
    "render": 4,
    "tex": 5,
    "geom": 6,
    "shader": 7,
    "layer": 8,
    "material": 9,
    "var": 10,
}

RESOURCE_PREFIXES = {"/tex", "/var", "/geom", "/shader", "/layer", "/mat"}
TOPLEVEL_PREFIXES = {"/input", "/midi", "/cam"}


@dataclass(frozen=True)
class CommandRow:
    command: str
    domain: str
    targeting: str
    status: str
    canonical: str
    feature_flag: str
    owner: str
    notes: str


def _extract_command_literals(text: str) -> Set[str]:
    values = set(re.findall(r'"(/[^"\\\n]+)"', text))
    cleaned: Set[str] = set()
    for value in values:
        if "..." in value:
            continue
        if re.match(r"^/[A-Za-z0-9_\-/*.]+$", value):
            cleaned.add(value)
    return cleaned


def _read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def discover_commands() -> Set[str]:
    parser_text = _read(PARSER_FILE)

    top_level_exact = set(re.findall(r'address\s*==\s*"(/[^"\n]+)"', parser_text))
    parser_prefixes = set(re.findall(r'isPrefixedCommand\(address,\s*"(/[^"\n]+)"\)', parser_text))
    parser_literals = _extract_command_literals(parser_text)

    handler_literals: Set[str] = set()
    for file in HANDLER_FILES:
        handler_literals |= _extract_command_literals(_read(file))

    commands: Set[str] = set(top_level_exact)

    # Commands explicitly referenced in parser helper logic (e.g. /midi/list, /layer/solo)
    for literal in parser_literals:
        if literal in parser_prefixes:
            continue
        if any(literal == prefix or literal.startswith(prefix + "/") for prefix in parser_prefixes):
            commands.add(literal)

    # Include all handler command literals that are under parser-recognized prefixes.
    for literal in handler_literals:
        if any(literal == prefix or literal.startswith(prefix + "/") for prefix in parser_prefixes):
            commands.add(literal)

    # Include top-level prefix roots that parser accepts directly.
    commands |= TOPLEVEL_PREFIXES

    return commands


def load_metadata() -> Dict[str, Dict[str, str]]:
    try:
        raw = json.loads(_read(METADATA_FILE))
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"Invalid JSON in {METADATA_FILE}: {exc}") from exc

    if not isinstance(raw, dict):
        raise RuntimeError("Metadata root must be a JSON object mapping command -> metadata")

    required = {"status", "canonical", "notes", "feature_flag", "replacement", "owner"}
    metadata: Dict[str, Dict[str, str]] = {}

    for command, value in raw.items():
        if not isinstance(command, str) or not command.startswith("/"):
            raise RuntimeError(f"Invalid command key in metadata: {command!r}")
        if not isinstance(value, dict):
            raise RuntimeError(f"Metadata entry for {command} must be an object")

        missing_keys = required - set(value.keys())
        if missing_keys:
            raise RuntimeError(f"Metadata entry {command} missing required keys: {sorted(missing_keys)}")

        entry = {key: str(value.get(key, "")) for key in required}

        status = entry["status"]
        if status not in STATUS_VALUES:
            raise RuntimeError(f"Metadata entry {command} has invalid status: {status}")
        if status == "alias" and not entry["canonical"]:
            raise RuntimeError(f"Metadata entry {command}: alias status requires canonical")
        if status == "deprecated" and not entry["replacement"]:
            raise RuntimeError(f"Metadata entry {command}: deprecated status requires replacement")
        if not entry["owner"]:
            raise RuntimeError(f"Metadata entry {command}: owner is required")

        metadata[command] = entry

    return metadata


def domain_for(command: str) -> str:
    if command.startswith("/layers"):
        return "setup"
    if command.startswith("/input") or command == "/mic" or command.startswith("/sound/"):
        return "input"
    if command.startswith("/cam"):
        return "camera"
    if command.startswith("/light"):
        return "light"
    if command.startswith("/tex"):
        return "tex"
    if command.startswith("/geom"):
        return "geom"
    if command.startswith("/shader"):
        return "shader"
    if command.startswith("/layer"):
        return "layer"
    if command.startswith("/mat"):
        return "material"
    if command.startswith("/var"):
        return "var"
    return "render"


def targeting_for_domain(domain: str) -> str:
    if domain in {"tex", "geom", "shader", "var"}:
        return "index|wildcard|shared"
    if domain in {"layer", "material"}:
        return "index|wildcard"
    return "none"


def build_rows(discovered: Set[str], metadata: Dict[str, Dict[str, str]]) -> List[CommandRow]:
    missing = sorted(discovered - set(metadata.keys()))
    if missing:
        formatted = "\n  ".join(missing)
        raise RuntimeError(
            "Metadata missing entries for discovered commands:\n"
            f"  {formatted}\n"
            "Add these keys to docs/osc-command-status.json"
        )

    extras = sorted(set(metadata.keys()) - discovered)
    unsupported_extras = [cmd for cmd in extras if metadata[cmd]["status"] == "supported"]
    if unsupported_extras:
        formatted = "\n  ".join(unsupported_extras)
        raise RuntimeError(
            "Metadata contains supported commands not discovered in parser/handlers:\n"
            f"  {formatted}\n"
            "Mark as deprecated/feature-flagged/disabled or remove the entry."
        )

    rows: List[CommandRow] = []
    for command in metadata:
        item = metadata[command]
        status = item["status"]

        canonical = item["canonical"].strip()
        if status == "deprecated":
            canonical = item["replacement"].strip()
        elif not canonical:
            canonical = command if status == "supported" else ""

        rows.append(
            CommandRow(
                command=command,
                domain=domain_for(command),
                targeting=targeting_for_domain(domain_for(command)),
                status=status,
                canonical=canonical,
                feature_flag=item["feature_flag"].strip(),
                owner=item["owner"].strip(),
                notes=item["notes"].strip(),
            )
        )

    rows.sort(key=lambda row: (DOMAIN_ORDER[row.domain], row.command))
    return rows


def _md(value: str) -> str:
    return value.replace("|", "\\|") if value else "-"


def render(rows: List[CommandRow]) -> str:
    aliases = [row for row in rows if row.status == "alias"]
    feature_rows = [row for row in rows if row.status == "feature-flagged"]

    lines: List[str] = []
    lines.append("# OSC Reference")
    lines.append("")
    lines.append("Generated by `scripts/generate_osc_reference.py`. Do not edit manually.")
    lines.append("")
    lines.append("See [Architecture](Architecture.md) for subsystem/lifecycle ownership.")
    lines.append("")

    lines.append("## Runtime Behavior")
    lines.append("")
    lines.append("- Onset queueing: `/shading`, `/shading/passes`, and targeted resource commands are queued and flushed when onset conditions pass (`/onset`, `/onset/force`, audio onset, or Tidal note activity).")
    lines.append("- Shading: `/shading deferred` enables deferred path; `/shading/passes` resets and rebuilds pass chains by name or id.")
    lines.append("- MIDI: `/midi` forwards messages to MIDI input handling; `/midi/list` lists available input ports.")
    lines.append("- Target selector semantics:")
    lines.append("")
    lines.append("| Family | Targeting |")
    lines.append("|---|---|")
    lines.append("| `/tex*`, `/var*`, `/geom*`, `/shader*` | `index` / `wildcard` / `shared-name` |")
    lines.append("| `/layer*`, `/mat*` | `index` / `wildcard` |")
    lines.append("| top-level commands (`/layers`, `/onset`, `/midi`, `/shading`, etc.) | no target arg |")
    lines.append("")

    if aliases:
        lines.append("## Aliases")
        lines.append("")
        for row in aliases:
            lines.append(f"- `{row.command}` -> `{row.canonical}`")
        lines.append("")

    if feature_rows:
        lines.append("## Feature-Flagged Behavior")
        lines.append("")
        for row in feature_rows:
            flag = row.feature_flag or "(flag not set)"
            note = f" - {row.notes}" if row.notes else ""
            lines.append(f"- `{row.command}` ({flag}){note}")
        lines.append("")

    lines.append("## Canonical Command Table")
    lines.append("")
    lines.append("| Command | Domain | Targeting | Status | Canonical | Feature Flag | Owner | Notes |")
    lines.append("|---|---|---|---|---|---|---|---|")
    for row in rows:
        lines.append(
            "| "
            + " | ".join(
                [
                    _md(row.command),
                    _md(row.domain),
                    _md(row.targeting),
                    _md(row.status),
                    _md(row.canonical),
                    _md(row.feature_flag),
                    _md(row.owner),
                    _md(row.notes),
                ]
            )
            + " |"
        )

    lines.append("")
    return "\n".join(lines)


def write_or_check(content: str, check: bool) -> int:
    current = OUTPUT_FILE.read_text(encoding="utf-8") if OUTPUT_FILE.exists() else ""
    if current == content:
        print(f"OSC reference is up to date: {OUTPUT_FILE}")
        return 0

    if check:
        print(f"OSC reference is out of date: {OUTPUT_FILE}")
        diff = difflib.unified_diff(
            current.splitlines(),
            content.splitlines(),
            fromfile=str(OUTPUT_FILE),
            tofile=str(OUTPUT_FILE) + " (generated)",
            lineterm="",
        )
        for line in diff:
            print(line)
        print("\nRun: python3 scripts/generate_osc_reference.py")
        return 1

    OUTPUT_FILE.write_text(content, encoding="utf-8")
    print(f"Wrote {OUTPUT_FILE}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Fail if generated output differs from committed file")
    args = parser.parse_args()

    try:
        discovered = discover_commands()
        metadata = load_metadata()
        rows = build_rows(discovered, metadata)
        content = render(rows)
        return write_or_check(content, check=args.check)
    except RuntimeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
