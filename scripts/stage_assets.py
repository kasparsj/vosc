#!/usr/bin/env python3
"""Refresh shared assets in a writable VOSC runtime data directory."""

import argparse
import filecmp
import os
from pathlib import Path
import shutil
import stat
import tempfile


ROOT = Path(__file__).resolve().parents[1]
DEFERRED_SHADERS = "addons/ofxDeferredShading/data/shaders/deferred"


def stage_assets(destination, *, addon_sdk=None, include_videos=False):
    sources = [
        (ROOT / "assets", Path(".")),
        (ROOT / "examples/editable-scene", Path("examples/editable-scene")),
    ]
    if addon_sdk is not None:
        sources.append((addon_sdk / DEFERRED_SHADERS, Path("shaders/deferred")))
    media = ROOT / "media/videos"
    if include_videos and media.exists():
        sources.append((media, Path("videos")))

    files = []
    directories = {destination, destination / "videos"}
    resolved = destination.resolve()
    for source, relative in sources:
        if not source.is_dir():
            raise ValueError(f"Missing asset source directory: {source}")
        source_root = source.resolve()
        if resolved.is_relative_to(source_root) or source_root.is_relative_to(resolved):
            raise ValueError(f"Runtime directory overlaps asset source: {destination}")
        for path in sorted(source.rglob("*")):
            name = path.relative_to(source)
            if any(part.startswith(".") for part in name.parts):
                continue
            target = destination / relative / name
            if path.is_symlink():
                raise ValueError(f"Refusing a linked source asset: {path}")
            if path.is_dir():
                directories.add(target)
            else:
                files.append((path, target))
                directories.update(parent for parent in target.parents if parent.is_relative_to(destination))

    for target in sorted(directories):
        if target.is_symlink() or (target.exists() and not target.is_dir()):
            raise ValueError(f"Expected an ordinary runtime directory: {target}")
    for _, target in files:
        if target.is_symlink() or (target.exists() and not target.is_file()):
            raise ValueError(f"Expected an ordinary runtime file: {target}")
    for directory in sorted(directories):
        directory.mkdir(parents=True, exist_ok=True)
    for source, target in files:
        if target.exists() and not target.samefile(source) and filecmp.cmp(source, target, shallow=False):
            continue
        descriptor, name = tempfile.mkstemp(dir=target.parent, prefix=".vosc-asset-")
        os.close(descriptor)
        temporary = Path(name)
        try:
            shutil.copyfile(source, temporary)
            temporary.chmod(stat.S_IMODE(source.stat().st_mode) | stat.S_IWUSR)
            # Publish a complete file and leave a fresh mtime for shader reload.
            temporary.replace(target)
        finally:
            temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--addon-sdk", type=Path, help="Include the macOS deferred shaders")
    parser.add_argument("--include-videos", action="store_true", help="Copy clips from media/videos")
    args = parser.parse_args()
    try:
        stage_assets(args.destination, addon_sdk=args.addon_sdk, include_videos=args.include_videos)
    except (OSError, ValueError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
