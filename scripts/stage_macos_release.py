#!/usr/bin/env python3
"""Stage an isolated Universal Release build for Developer ID signing."""

import argparse
import json
from pathlib import Path
import plistlib
import stat
import subprocess
import tempfile
import unicodedata

from setup_macos import MANIFEST, RECEIPT, reuse, sha256
from stage_assets import ROOT, DEFERRED_SHADERS, stage_assets


MARKER = "VOSCDirectDownload"
STOCK_MANIFEST = "vosc-stock.json"
ARCHITECTURES = {"arm64", "x86_64"}


def stock_path(path):
    for part in path.split("/"):
        if (not part or part.startswith(".") or ":" in part or "\\" in part
                or any(unicodedata.category(character) in {"Cc", "Cs"} for character in part)):
            raise ValueError(f"Unsafe stock path: {path!r}")
    return unicodedata.normalize("NFD", path).casefold()


def stock_tree(root, *, skip_hidden=False):
    """Return relative file hashes and directory entries (None). Never follow links."""
    entries = {}
    if not stat.S_ISDIR(root.lstat().st_mode):
        raise ValueError(f"Expected a stock directory: {root}")

    def scan(directory):
        for child in sorted(directory.iterdir()):
            if skip_hidden and child.name.startswith("."):
                continue
            name = child.relative_to(root).as_posix()
            stock_path(name)
            status = child.lstat()
            if stat.S_ISDIR(status.st_mode):
                entries[name] = None
                scan(child)
            elif stat.S_ISREG(status.st_mode) and status.st_nlink == 1:
                entries[name] = sha256(child)
            else:
                raise ValueError(f"Expected an ordinary stock file or directory: {child}")

    scan(root)
    return entries


def public_checkout_files():
    """Enumerate public repository inputs independently of the staging walk."""
    dirty = subprocess.run(["git", "diff", "HEAD", "--quiet", "--", "assets", "examples"],
                           cwd=ROOT, check=False)
    if dirty.returncode != 0:
        raise ValueError("Release assets and examples must match the checked-out commit")
    raw = subprocess.check_output(["git", "ls-tree", "-rz", "--name-only", "HEAD",
                                   "--", "assets", "examples"], cwd=ROOT)
    return {Path(name.decode("utf-8")) for name in raw.split(b"\0") if name
            and not any(part.startswith(b".") for part in name.split(b"/"))}


def expected_stock(addon_sdk):
    receipt = addon_sdk / RECEIPT
    if not stat.S_ISREG(receipt.lstat().st_mode):
        raise ValueError(f"Expected a setup receipt: {receipt}")
    prepared = json.loads(receipt.read_text()).get("inputs", {})
    pin = json.loads(MANIFEST.read_text())["addons"]["ofxDeferredShading"]
    if prepared.get("addons", {}).get("ofxDeferredShading") != pin or not reuse(addon_sdk, prepared):
        raise ValueError(f"Expected the pinned deferred shaders from setup_macos.py: {addon_sdk}")
    public = public_checkout_files()
    stock = {}
    aliases = {}

    def insert(name, digest):
        key = stock_path(name)
        previous = aliases.setdefault(key, name)
        if previous != name:
            raise ValueError(f"Stock paths collide on macOS: {previous!r} and {name!r}")
        if name in stock and (digest is not None or stock[name] is not None):
            raise ValueError(f"Duplicate stock path: {name}")
        stock[name] = digest

    def merge(source, prefix):
        if prefix:
            parts = Path(prefix).parts
            for depth in range(1, len(parts) + 1):
                insert(Path(*parts[:depth]).as_posix(), None)
        for name, digest in source.items():
            target = (Path(prefix) / name).as_posix()
            insert(target, digest)

    for directory, prefix in (("assets", ""), ("examples", "examples")):
        tree = stock_tree(ROOT / directory, skip_hidden=True)
        tracked = {path.relative_to(directory).as_posix() for path in public
                   if path.parts[0] == directory}
        present = {name for name, digest in tree.items() if digest is not None}
        expected = tracked | {parent.as_posix() for name in tracked for parent in Path(name).parents
                              if parent != Path(".")}
        if present != tracked or set(tree) != expected:
            paths = ", ".join(sorted((present ^ tracked) | (set(tree) ^ expected)))
            raise ValueError(f"Unexpected or missing public {directory} entries: {paths}")
        merge(tree, prefix)
    deferred = stock_tree(addon_sdk / DEFERRED_SHADERS, skip_hidden=True)
    if not any(digest is not None for digest in deferred.values()):
        raise ValueError("Pinned deferred shader directory is empty")
    merge(deferred, "shaders/deferred")
    insert("videos", None)
    if any((name.startswith("videos/") or name == "shaders/shadertoy"
            or name.startswith("shaders/shadertoy/")) for name in stock):
        raise ValueError("Release stock must not contain clips or Shadertoy downloads")
    return stock


def verify_release_build(app, addon_sdk, scratch):
    contents = app / "Contents"
    resources = contents / "Resources"
    for path in (app, contents, resources, contents / "MacOS"):
        if not stat.S_ISDIR(path.lstat().st_mode):
            raise ValueError(f"Expected an ordinary build directory: {path}")
    info = contents / "Info.plist"
    binary = contents / "MacOS/vosc"
    for path in (info, binary):
        status = path.lstat()
        if not stat.S_ISREG(status.st_mode) or status.st_nlink != 1:
            raise ValueError(f"Expected an ordinary build file: {path}")
    plist = plistlib.loads(info.read_bytes())
    expected = {"CFBundlePackageType": "APPL", "CFBundleExecutable": "vosc",
                "CFBundleIdentifier": "cc.openFrameworks.vosc"}
    if app.name != "vosc.app" or any(plist.get(key) != value for key, value in expected.items()):
        raise ValueError(f"Expected the standalone macOS vosc.app: {app}")
    if MARKER in plist or any((resources / name).exists() or (resources / name).is_symlink()
                              for name in ("data", STOCK_MANIFEST)):
        raise ValueError(f"Expected a fresh Release bundle without staged resources: {app}")

    receipt = json.loads((app.parent.parent / "commands.json").read_text())
    command = receipt.get("app", [])
    if (app.parent.name != "products" or "-scheme" not in command
            or command[command.index("-scheme") + 1:][:1] != ["vosc Release"]
            or f"CONFIGURATION_BUILD_DIR={app.parent}" not in command
            or f"VOSC_ADDON_SDK={addon_sdk}" not in command or "build" not in command):
        raise ValueError("Build the app in an isolated directory with build_macos_profile.py --configuration Release")
    architectures = set(subprocess.check_output(["/usr/bin/lipo", "-archs", binary], text=True).split())
    if architectures != ARCHITECTURES:
        raise ValueError("Direct-download Release requires arm64 and x86_64 Mach-O slices")
    for architecture in sorted(architectures):
        thin = scratch / f"vosc-{architecture}"
        subprocess.run(["/usr/bin/lipo", binary, "-thin", architecture, "-output", thin], check=True)
        with thin.open("rb") as stream:
            # This literal belongs to ExamplesView, which is absent from UI-off builds.
            if b"example-reveal-scene" not in stream.read():
                raise ValueError(f"The {architecture} slice was built without the operator UI")
    return plist


def write_release_stock(app, addon_sdk, generation, plist, expected):
    resources = app / "Contents/Resources"
    data = resources / "data"
    data.mkdir()
    stage_assets(data, addon_sdk=addon_sdk)
    actual = stock_tree(data)
    if actual != expected:
        changed = sorted(name for name in actual.keys() | expected.keys()
                         if name not in actual or name not in expected or actual[name] != expected[name])
        raise ValueError(f"Staged stock differs from public build inputs: {', '.join(changed)}")
    manifest = {
        "schema": 1,
        "generation": generation,
        "files": {name: digest for name, digest in actual.items() if digest is not None},
        "directories": sorted(name for name, digest in actual.items() if digest is None),
    }
    (resources / STOCK_MANIFEST).write_text(json.dumps(manifest, sort_keys=True, indent=2) + "\n")
    plist[MARKER] = True
    (app / "Contents/Info.plist").write_bytes(plistlib.dumps(plist))


def stage_release(app, destination, *, addon_sdk, generation):
    if type(generation) is not int or not 0 < generation <= 2**63 - 1:
        raise ValueError("Stock generation must be a positive signed 64-bit integer")
    if destination.exists() or destination.is_symlink():
        raise ValueError(f"DMG staging root must be a new directory: {destination}")
    expected = expected_stock(addon_sdk)
    with tempfile.TemporaryDirectory(prefix=".vosc-release-", dir=destination.parent) as temporary:
        scratch = Path(temporary)
        plist = verify_release_build(app, addon_sdk, scratch)
        candidate = scratch / "vosc.app"
        subprocess.run(["/usr/bin/ditto", app, candidate], check=True)
        write_release_stock(candidate, addon_sdk, generation, plist, expected)
        destination.mkdir()
        try:
            candidate.rename(destination / "vosc.app")
        except OSError:
            destination.rmdir()
            raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path, help="vosc.app from an isolated Universal Release build")
    parser.add_argument("destination", type=Path, help="new DMG staging root under an existing directory")
    parser.add_argument("--addon-sdk", type=Path, required=True, help="pinned addon SDK from setup_macos.py")
    parser.add_argument("--generation", type=int, required=True, help="stock generation, increasing per release")
    args = parser.parse_args()
    try:
        stage_release(args.app.absolute(), args.destination.absolute(),
                      addon_sdk=args.addon_sdk.absolute(), generation=args.generation)
    except (OSError, ValueError, subprocess.CalledProcessError, plistlib.InvalidFileException) as error:
        parser.exit(1, f"{error}\n")
    print(args.destination / "vosc.app")


if __name__ == "__main__":
    main()
