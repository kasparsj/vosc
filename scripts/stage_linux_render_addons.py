#!/usr/bin/env python3
"""Prepare the desktop Linux OF addon path without modifying its installation."""

import argparse
import errno
import hashlib
from pathlib import Path
import shutil
import tempfile

from addon_compat import RENDER_ADDON_PATCHES, linux_addon_updates


BUILD_DIRECTORIES = ("src", "libs")


def stage_render_addons(source, output):
    source = source.resolve()
    # openFrameworks writes addon objects under OF_ADDONS_PATH/obj.
    installed = sorted(path for path in source.iterdir() if path.is_dir() and path.name != "obj")
    updates = {Path(name): patch((source / name).read_bytes().decode("latin-1")).encode("latin-1")
               for name, patch in RENDER_ADDON_PATCHES.items()}
    for addon in installed:
        if addon.name != "ofxPostProcessing":  # The render patches already cover it.
            updates.update({Path(addon.name) / path: content
                            for path, content in linux_addon_updates(addon).items()})
    patched_names = {name.parts[0] for name in updates}
    fingerprint = hashlib.sha256(str(source).encode())
    for addon in installed:
        fingerprint.update(addon.name.encode() + b"\0")
        if addon.name in patched_names:
            for directory in BUILD_DIRECTORIES:
                for path in sorted((addon / directory).rglob("*")):
                    if path.is_file() and path.relative_to(source) not in updates:
                        fingerprint.update(str(path.relative_to(source)).encode() + b"\0")
                        fingerprint.update(path.read_bytes())
    for name, content in sorted(updates.items()):
        fingerprint.update(str(name).encode() + b"\0")
        fingerprint.update(content)
    destination = output / fingerprint.hexdigest()
    if destination.is_dir():
        return destination

    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=output) as temporary:
        staged = Path(temporary) / "addons"
        staged.mkdir()
        for addon in installed:
            target = staged / addon.name
            if addon.name not in patched_names:
                target.symlink_to(addon, target_is_directory=True)
                continue
            target.mkdir()
            for path in addon.iterdir():
                if path.name in BUILD_DIRECTORIES and path.is_dir():
                    shutil.copytree(path, target / path.name)
                elif not path.name.startswith(".") and path.relative_to(source) not in updates:
                    (target / path.name).symlink_to(path, target_is_directory=path.is_dir())
        for name, content in updates.items():
            (staged / name).write_bytes(content)
        try:
            staged.rename(destination)
        except OSError as error:
            if error.errno not in (errno.EEXIST, errno.ENOTEMPTY) or not destination.is_dir():
                raise
    return destination


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    print(stage_render_addons(args.source, args.output))
