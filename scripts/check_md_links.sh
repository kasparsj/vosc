#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

python3 - "$ROOT_DIR" <<'PY'
import re
import sys
from pathlib import Path

root = Path(sys.argv[1])

files = [root / "README.md"]
files.extend(sorted((root / "docs").glob("*.md")))

link_re = re.compile(r"\[[^\]]+\]\(([^)]+)\)")
head_re = re.compile(r"^#{1,6}\s+(.+?)\s*$")


def slugify(text: str) -> str:
    text = text.strip().lower()
    text = re.sub(r"`", "", text)
    text = re.sub(r"\[([^\]]+)\]\([^)]*\)", r"\1", text)
    text = re.sub(r"[^a-z0-9\s\-]", "", text)
    text = re.sub(r"\s+", "-", text)
    text = re.sub(r"-+", "-", text)
    return text.strip("-")


def anchors_for(path: Path):
    anchors = set()
    counts = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        m = head_re.match(line)
        if not m:
            continue
        base = slugify(m.group(1))
        if not base:
            continue
        idx = counts.get(base, 0)
        counts[base] = idx + 1
        anchor = base if idx == 0 else f"{base}-{idx}"
        anchors.add(anchor)
    return anchors


def fail(msg: str):
    print(msg)
    sys.exit(1)

for path in files:
    text = path.read_text(encoding="utf-8")
    for target in link_re.findall(text):
        if not target:
            continue
        if target.startswith(("http://", "https://", "mailto:")):
            continue

        file_part = target
        anchor_part = ""
        if "#" in target:
            file_part, anchor_part = target.split("#", 1)

        if file_part == "":
            # local anchor on the same file
            target_path = path
        else:
            target_path = (path.parent / file_part).resolve()
            if not target_path.exists():
                fail(f"BROKEN_FILE: {path.relative_to(root)} -> {target}")
            if anchor_part and target_path.is_dir():
                fail(f"BROKEN_ANCHOR: {path.relative_to(root)} -> {target}")

        if anchor_part:
            want = anchor_part.strip().lower()
            anchors = anchors_for(target_path)
            if want not in anchors:
                fail(f"BROKEN_ANCHOR: {path.relative_to(root)} -> {target}")

print("Markdown links OK")
PY
