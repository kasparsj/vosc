#!/usr/bin/env python3
"""Prepare pinned openFrameworks and VOSC addons for a macOS Xcode build."""

import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shlex
import stat
import subprocess
import sys
import tempfile

from macos_build import ROOT, path_within, receiver_inputs, validate_output_path
from build_macos_profile import SDK_VERSION, sdk_version


MANIFEST = ROOT / "scripts/setup_macos_dependencies.json"
RECEIPT = ".vosc-setup.json"
SDK_PROJECT = "libs/openFrameworksCompiled/project/osx/openFrameworksLib.xcodeproj/project.pbxproj"


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def raise_walk_error(error):
    raise error


def tree_digest(root):
    """Fingerprint the payload, including extra paths, modes and symlink targets."""
    digest = hashlib.sha256()
    for directory, subdirectories, files in os.walk(root, onerror=raise_walk_error):
        subdirectories.sort()
        for name in sorted(subdirectories + files):
            path = Path(directory) / name
            relative = path.relative_to(root).as_posix()
            if relative == RECEIPT:
                continue
            mode = path.lstat().st_mode
            value = None
            if stat.S_ISLNK(mode):
                value = os.readlink(path)
                if Path(value).is_absolute() or not path.resolve().is_relative_to(root):
                    raise ValueError(f"Symlink leaves dependency tree: {path} -> {value}")
            elif stat.S_ISREG(mode):
                value = sha256(path)
            elif not stat.S_ISDIR(mode):
                raise ValueError(f"Unexpected file type in dependency tree: {path}")
            digest.update(json.dumps([relative, mode, value]).encode() + b"\n")
    return digest.hexdigest()


def require_directory(path):
    if path.is_symlink() or (path.exists() and not path.is_dir()):
        raise ValueError(f"Expected a real directory at {path}. Choose another --dest.")


def reuse(path, inputs):
    require_directory(path)
    if not path.exists():
        return False
    try:
        receipt = path / RECEIPT
        if not stat.S_ISREG(receipt.lstat().st_mode):
            raise ValueError("Expected a regular setup receipt")
        previous = json.loads(receipt.read_text())
        if previous == {"inputs": inputs, "tree_sha256": tree_digest(path)}:
            print(f"Reusing {path}", file=sys.stderr)
            return True
    except (OSError, ValueError, RuntimeError):
        pass
    raise ValueError(f"Existing dependency tree conflicts at {path}. Choose another --dest.")


def cached_archive(pin, cache, staging):
    archive = cache / pin["sha256"]
    if archive.is_symlink() or (archive.exists() and not archive.is_file()):
        raise ValueError(f"Expected an archive file at {archive}. Choose another --dest.")
    if archive.is_file() and sha256(archive) == pin["sha256"]:
        return archive
    print(f"Downloading {pin['url']}", file=sys.stderr)
    partial = staging / "download"
    subprocess.run([
        "curl", "--fail", "--location", "--silent", "--show-error",
        "--retry", "2", "--connect-timeout", "20", "--max-time", "600",
        "--output", str(partial), pin["url"],
    ], check=True)
    if sha256(partial) != pin["sha256"]:
        raise ValueError(f"SHA-256 mismatch for {pin['url']}")
    partial.replace(archive)
    return archive


def unpack(archive, target, *members):
    target.mkdir(parents=True)
    # macOS bsdtar handles both release formats and rejects paths through symlinks
    # and traversal outside its extraction root by default. Payload digests are
    # checked before extraction, and tree_digest checks remaining link targets.
    subprocess.run([
        "/usr/bin/tar", "-xf", str(archive), "-C", str(target), "--strip-components", "1",
        "--no-same-owner", "--no-same-permissions", "--no-mac-metadata",
        "--exclude", "._*", *members,
    ], check=True)


def require_files(root, paths):
    for relative in paths:
        if not (root / relative).is_file():
            raise ValueError(f"Missing required build input: {root / relative}")


def require_arm64(path):
    if subprocess.run(["xcrun", "lipo", "-verify_arch", "arm64", str(path)]).returncode:
        raise ValueError(f"Missing arm64 architecture: {path}")


def validate_sdk(root):
    sdk_version(root)
    require_files(root, [SDK_PROJECT])


def addon_inputs(manifest):
    bundled = manifest["bundled_addons"]
    expected = set((ROOT / "addons.make").read_text().split())
    actual = set(bundled["names"]) | manifest["addons"].keys()
    if actual != expected:
        raise ValueError("Setup dependency manifest does not match addons.make")
    return {
        "bundled": bundled,
        "addons": manifest["addons"],
        "muparser": manifest["muparser_source"],
    }


def prepare_addons(target, inputs, manifest, cache, staging):
    bundled = inputs["bundled"]
    pin = bundled["archive"]
    unpack(cached_archive(pin, cache, staging), target,
           *(f"{pin['root']}/addons/{name}" for name in bundled["names"]))
    addons = target / "addons"
    for name, pin in inputs["addons"].items():
        unpack(cached_archive(pin, cache, staging), addons / name)
    muparser = inputs["muparser"]
    source = addons / muparser["path"]
    # The parent source archive can include an empty submodule directory.
    if source.is_dir() and not source.is_symlink():
        source.rmdir()
    unpack(cached_archive(muparser, cache, staging), source)
    validate_addons(target, manifest)


def validate_addons(target, manifest):
    addons = target / "addons"
    project = (ROOT / "vosc.xcodeproj/project.pbxproj").read_text()
    project_inputs = set(re.findall(r'path = "?([^;"]+)"?; sourceTree = VOSC_ADDONS;', project))
    libraries = manifest["arm64_libraries"]
    ultralight = manifest["ultralight_libraries"]
    require_files(addons, sorted(project_inputs | set(libraries) | set(ultralight)))
    receiver_inputs(target)
    for relative in libraries:
        require_arm64(addons / relative)
    config = (ROOT / "src/Config.h").read_text()
    if not re.search(r"^\s*#define\s+USE_OFX_ULTRALIGHT\s+0\s*$", config, re.MULTILINE):
        for relative in ultralight:
            require_arm64(addons / relative)
    if not (addons / "ofxDeferredShading/data/shaders/deferred").is_dir():
        raise ValueError("Missing runtime shaders from ofxDeferredShading")


def record_tree(tree, inputs):
    receipt = tree / RECEIPT
    if receipt.exists() or receipt.is_symlink():
        raise ValueError(f"Unexpected setup receipt in archive: {receipt}")
    receipt.write_text(json.dumps({
        "inputs": inputs, "tree_sha256": tree_digest(tree),
    }, indent=2) + "\n")


def build_output(dest):
    if path_within(dest, ROOT):
        key = hashlib.sha256(os.fsencode(dest)).hexdigest()[:16]
        return Path.home() / "Library/Caches/VOSC/builds" / key
    return dest / "build"


def prepare(dest, sdk, output, manifest):
    addon_sdk = dest / "addon-sdks" / f"of-{SDK_VERSION}"
    managed_sdk = dest / "sdks" / f"of-{SDK_VERSION}"
    cache = dest / "cache"
    if sdk is not None and any(
        path_within(path, sdk) or path_within(sdk, path)
        for path in (addon_sdk, cache, output)
    ):
        raise ValueError("The setup destination overlaps the supplied SDK. Choose another --dest.")
    sdk_pin = manifest["sdk"]
    if output is not None:
        validate_output_path(output, sdk or managed_sdk, addon_sdk)
    inputs = addon_inputs(manifest) if output is not None else None
    dest.mkdir(parents=True, exist_ok=True)
    lock = os.open(dest / ".setup.lock", os.O_CREAT | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_RDWR, 0o600)
    with os.fdopen(lock, "r+") as stream:
        if not stat.S_ISREG(os.fstat(stream.fileno()).st_mode):
            raise ValueError(f"Expected a regular lock file at {dest / '.setup.lock'}")
        fcntl.flock(stream, fcntl.LOCK_EX)
        if sdk is None:
            require_directory(managed_sdk.parent)
        if output is not None:
            require_directory(addon_sdk.parent)
            require_directory(output)
        needs_sdk = sdk is None and not reuse(managed_sdk, sdk_pin)
        needs_addons = inputs is not None and not reuse(addon_sdk, inputs)
        if inputs is not None and not needs_addons:
            validate_addons(addon_sdk, manifest)
        if needs_sdk or needs_addons:
            require_directory(cache)
            cache.mkdir(exist_ok=True)
            with tempfile.TemporaryDirectory(prefix=".staging-", dir=dest) as scratch:
                staging = Path(scratch)
                new_sdk = staging / "sdk"
                new_addons = staging / "addon-sdk"
                if needs_sdk:
                    unpack(cached_archive(sdk_pin, cache, staging), new_sdk)
                    validate_sdk(new_sdk)
                    record_tree(new_sdk, sdk_pin)
                if needs_addons:
                    prepare_addons(new_addons, inputs, manifest, cache, staging)
                    record_tree(new_addons, inputs)
                for needed, source, target in (
                    (needs_sdk, new_sdk, managed_sdk), (needs_addons, new_addons, addon_sdk),
                ):
                    if needed:
                        target.parent.mkdir(exist_ok=True)
                        if target.exists() or target.is_symlink():
                            raise ValueError(f"Dependency target appeared during setup: {target}")
                        source.rename(target)
                        print(f"Prepared {target}", file=sys.stderr)
    return sdk or managed_sdk, addon_sdk


def main():
    manifest = json.loads(MANIFEST.read_text())
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--sdk-only", action="store_true", help="Extract the SDK only")
    mode.add_argument("--addons-only", action="store_true", help="Stage addons for --of-root")
    parser.add_argument("--of-root", type=Path, help="Existing openFrameworks SDK for --addons-only")
    parser.add_argument("--dest", type=Path, default=ROOT / ".vosc-macos",
                        help="Local setup directory (default: .vosc-macos in the VOSC checkout)")
    args = parser.parse_args()
    if args.addons_only != (args.of_root is not None):
        parser.error("--addons-only and --of-root must be used together")
    if platform.system() != "Darwin" or platform.machine() != "arm64":
        parser.error("Run setup using native Apple Silicon macOS Python")

    try:
        dest = args.dest.expanduser().resolve()
        existing = args.of_root.expanduser().resolve() if args.of_root else None
        if existing is not None:
            validate_sdk(existing)
        output = None if args.sdk_only else build_output(dest)
        sdk, addon_sdk = prepare(dest, existing, output, manifest)
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"{parser.prog}: {error}\n")

    if args.sdk_only:
        print(f"openFrameworks {SDK_VERSION} SDK: {sdk}", file=sys.stderr)
        print(shlex.join(["python3", str(ROOT / "scripts/setup_macos.py"),
                          "--addons-only", "--of-root", str(sdk), "--dest", str(dest)]))
    else:
        print(shlex.join(["python3", str(ROOT / "scripts/build_macos_profile.py"),
                          "--of-root", str(sdk), "--addon-sdk", str(addon_sdk),
                          "--configuration", "Debug", "--output", str(output)]))


if __name__ == "__main__":
    main()
