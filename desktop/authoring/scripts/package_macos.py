#!/usr/bin/env python3
"""Build private Developer ID signed arm64 VOSC authoring apps."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import plistlib
import shutil
import subprocess
import tarfile
import tempfile
from urllib.request import urlretrieve


DESKTOP = Path(__file__).resolve().parents[1]
ROOT = DESKTOP.parents[1]
ELECTRON_ID = "com.github.Electron"
CONTROLLER_ID = "com.vosc.authoring"
HOST_ID = "com.vosc.render-host"
SERVICE = "_vosc-draft._tcp"
OS_MINIMUM = "15.0"
PYTHON_VERSION = "3.13.18"
PYTHON_URL = ("https://github.com/astral-sh/python-build-standalone/releases/download/20261124/"
              "cpython-3.13.18%2B20261124-aarch64-apple-darwin-install_only.tar.gz")
PYTHON_SHA256 = "ae7030747b3daddea6925f4e270918c5d977f0347fe5d03adae94ce3a630ed74"
ELECTRON_VERSION = "45.5.0"
ELECTRON_URL = ("https://github.com/electron/electron/releases/download/v45.5.0/"
                "electron-v45.5.0-darwin-arm64.zip")
ELECTRON_SHA256 = "00d345b061064de7e6e2c02bec7ffa1adb7ef488fe4b673bd8ad3e059a182245"
MACH_O = {bytes.fromhex(value) for value in ("cafebabe", "bebafeca", "cafebabf", "bfbafeca",
                                            "feedface", "cefaedfe", "feedfacf", "cffaedfe")}
JIT = {"com.apple.security.cs.allow-jit": True}
IMPORTER_FILES = ("acquire.py", "glsl.py", "import_shader.py", "source_profiles.py")


def run(argv, *, capture=False, env=None):
    result = subprocess.run([str(arg) for arg in argv], cwd="/", env=env, check=True,
                            text=True, stdout=subprocess.PIPE if capture else None)
    return result.stdout


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def mach_o(path):
    if path.is_symlink() or not path.is_file():
        return False
    with path.open("rb") as stream:
        return stream.read(4) in MACH_O


def executable(artifact):
    info = plistlib.loads((artifact / "Contents/Info.plist").read_bytes())
    return artifact / "Contents/MacOS" / info["CFBundleExecutable"]


def app_metadata(identifier, name, executable_name, version, description):
    return {
        "CFBundleIdentifier": identifier,
        "CFBundleName": name,
        "CFBundleDisplayName": name,
        "CFBundleExecutable": executable_name,
        "CFBundlePackageType": "APPL",
        "CFBundleShortVersionString": version,
        "CFBundleVersion": version,
        "LSMinimumSystemVersion": OS_MINIMUM,
        "NSHighResolutionCapable": True,
        "NSBonjourServices": [SERVICE],
        "NSLocalNetworkUsageDescription": description,
    }


def stage_python(archive, destination):
    if digest(archive) != PYTHON_SHA256:
        raise RuntimeError("Pinned standalone Python archive SHA-256 mismatch")
    with tarfile.open(archive, "r:gz") as source:
        source.extractall(path=destination, filter="data")
    python = destination / "python" / "bin" / "python3.13"
    if not python.is_file():
        raise RuntimeError("Missing standalone Python executable")
    return python


def stage_controller(output, electron_dist, python_archive, manifest, fixture=None):
    source = electron_dist / "Electron.app"
    app_name = "VOSC Authoring Fixture" if fixture else "VOSC Authoring"
    bundle_id = CONTROLLER_ID + ".fixture" if fixture else CONTROLLER_ID
    target = output / f"{app_name}.app"
    shutil.copytree(source, target, symlinks=True)
    old = executable(target)
    launcher = old.with_name(app_name)
    old.rename(launcher)
    info_path = target / "Contents/Info.plist"
    info = plistlib.loads(info_path.read_bytes())
    info.update(app_metadata(bundle_id, app_name, launcher.name, manifest["version"],
                            "Find and connect to paired VOSC render hosts on your local network."))
    info.pop("CFBundleURLTypes", None)
    info_path.write_bytes(plistlib.dumps(info))
    for helper in (target / "Contents/Frameworks").rglob("*.app"):
        helper_info_path = helper / "Contents/Info.plist"
        helper_info = plistlib.loads(helper_info_path.read_bytes())
        helper_info["CFBundleIdentifier"] = helper_info["CFBundleIdentifier"].replace(ELECTRON_ID, bundle_id, 1)
        helper_info_path.write_bytes(plistlib.dumps(helper_info))

    resources = target / "Contents/Resources"
    (resources / "default_app.asar").unlink(missing_ok=True)
    shutil.copytree(DESKTOP / "build/app", resources / "app")
    python = stage_python(python_archive, resources)
    importer = resources / "importer/scripts/authoring"
    importer.mkdir(parents=True)
    for name in IMPORTER_FILES:
        shutil.copyfile(ROOT / "scripts/authoring" / name, importer / name)
    shutil.copyfile(DESKTOP / "scripts/publish_staged.py", importer / "publish_staged.py")
    if fixture:
        if fixture.stat().st_size > 1024 * 1024:
            raise RuntimeError("Provider fixture exceeds its artifact size limit")
        fixture_data = json.loads(fixture.read_bytes())
        (importer / "shadertoy-response.json").write_text(json.dumps(fixture_data, separators=(",", ":")) + "\n")
        shutil.copyfile(DESKTOP / "scripts/stage_shadertoy_fixture.py", importer / "stage_shadertoy_fixture.py")
        package_metadata = resources / "app/package.json"
        package = json.loads(package_metadata.read_bytes())
        package_metadata.write_text(json.dumps({**package, "productName": app_name,
                                                "voscShadertoyFixture": True}) + "\n")
    run(["xcrun", "swiftc", "-target", f"arm64-apple-macos{OS_MINIMUM}",
         DESKTOP / "discovery/main.swift", "-o", launcher.parent / "vosc-browse"])

    notices = resources / "licenses"
    notices.mkdir()
    for name in ("LICENSE", "LICENSES.chromium.html"):
        shutil.copyfile(electron_dist / name, notices / f"Electron-{name}")
    for package in ("react", "react-dom", "scheduler"):
        shutil.copyfile(DESKTOP / f"node_modules/{package}/LICENSE", notices / f"{package}-LICENSE")
    return target, python


def stage_host(output, version, renderer_app):
    target = output / "VOSC Render Host.app"
    binary = target / "Contents/MacOS/VOSC Render Host"
    binary.parent.mkdir(parents=True)
    info = app_metadata(HOST_ID, target.stem, binary.name, version,
                        "Offer private VOSC draft previews to paired controllers on your local network.")
    (target / "Contents/Info.plist").write_bytes(plistlib.dumps(info))
    files = sorted((DESKTOP / "host").glob("*.swift"))
    run(["xcrun", "swiftc", "-target", f"arm64-apple-macos{OS_MINIMUM}", *files, "-o", binary])
    # Preserve ordinary VOSC as a separate bundle beside the optional companion.
    run(["codesign", "--verify", "--deep", "--strict", renderer_app])
    renderer = output / renderer_app.name
    shutil.copytree(renderer_app, renderer, symlinks=True)
    for binary in sorted(path for path in renderer.rglob("*") if mach_o(path)):
        arches = run(["lipo", "-archs", binary], capture=True).strip().split()
        if arches != ["arm64"]:
            thin = binary.with_name(binary.name + ".arm64")
            run(["lipo", binary, "-thin", "arm64", "-output", thin])
            thin.chmod(binary.stat().st_mode & 0o777)
            thin.replace(binary)
    return target, renderer


def sign(artifact, identity, entitlements=None):
    def inner(path, grants=None):
        options = ["--entitlements", grants] if grants else []
        run(["codesign", "--force", "--sign", identity, "--options", "runtime",
             "--timestamp=none", *options, path])

    codes = [item for item in sorted(artifact.rglob("*")) if mach_o(item)]
    for path in codes:
        archs = run(["lipo", "-archs", path], capture=True).strip().split()
        if archs != ["arm64"]:
            raise RuntimeError(f"{path.relative_to(artifact)} is not arm64-only")
        inner(path, entitlements if "Helper" in path.name else None)
    for container in sorted(artifact.rglob("*"), key=lambda path: -len(path.parts)):
        if not container.is_dir() or container.is_symlink() or container.suffix not in {".app", ".framework"}:
            continue
        if container.suffix == ".framework" and (container / "Versions/A").is_dir():
            inner(container / "Versions/A")
        else:
            inner(container, entitlements if container.suffix == ".app" else None)
    inner(artifact, entitlements)
    run(["codesign", "--verify", "--deep", "--strict", artifact])
    return [str(path.relative_to(artifact)) for path in codes]


def inventory(root):
    paths = {}
    for path in sorted(root.rglob("*")):
        relative = path.relative_to(root).as_posix()
        if path.is_symlink():
            paths[relative] = {"link": os.readlink(path)}
        elif path.is_file():
            paths[relative] = {"sha256": digest(path)}
    encoded = json.dumps(paths, sort_keys=True, separators=(",", ":")).encode()
    return {"sha256": hashlib.sha256(encoded).hexdigest(), "paths": paths}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path, help="New directory for the signed internal test apps")
    parser.add_argument("--identity", required=True, help="Installed Developer ID Application name or fingerprint")
    parser.add_argument("--python-archive", type=Path, help="Reuse the exact checksum-pinned arm64 Python archive")
    parser.add_argument("--electron-archive", type=Path, help="Reuse the exact checksum-pinned Electron archive")
    parser.add_argument("--vosc-app", required=True, type=Path, help="Signed renderer app to stage beside the companion")
    parser.add_argument("--shadertoy-fixture", type=Path, help="Build a distinct signed fixture controller")
    args = parser.parse_args()
    if platform.system() != "Darwin" or platform.machine() != "arm64":
        parser.error("Package on macOS arm64")
    identity_text = run(["security", "find-identity", "-v", "-p", "codesigning"], capture=True)
    if not any(args.identity in line and "Developer ID Application:" in line for line in identity_text.splitlines()):
        parser.error("--identity must select an installed Developer ID Application certificate")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    (output / "controller").mkdir()
    (output / "render-host").mkdir()
    manifest = json.loads((DESKTOP / "package.json").read_text())
    if manifest["devDependencies"]["electron"] != ELECTRON_VERSION:
        raise RuntimeError("Electron runtime and package pins differ")
    with tempfile.TemporaryDirectory(prefix="vosc-authoring-package-") as work:
        temporary = Path(work)
        archive = args.python_archive
        if archive is None:
            archive = temporary / "python.tar.gz"
            urlretrieve(PYTHON_URL, archive)
        electron_archive = args.electron_archive
        if electron_archive is None:
            electron_archive = temporary / "electron.zip"
            urlretrieve(ELECTRON_URL, electron_archive)
        if digest(electron_archive) != ELECTRON_SHA256:
            raise RuntimeError("Pinned Electron archive SHA-256 mismatch")
        electron_dist = temporary / "electron-dist"
        run(["/usr/bin/ditto", "-x", "-k", electron_archive.resolve(), electron_dist])
        controller, python = stage_controller(output / "controller", electron_dist, archive.resolve(), manifest,
                                              args.shadertoy_fixture)
        host, renderer = stage_host(output / "render-host", manifest["version"], args.vosc_app.resolve(strict=True))
        entitlements = temporary / "controller.plist"
        entitlements.write_bytes(plistlib.dumps(JIT))
        artifacts = {}
        for app, grants in ((controller, entitlements), (host, None), (renderer, None)):
            artifacts[app.name] = {"path": str(app), "arm64_mach_o": sign(app, args.identity, grants), **inventory(app)}

    versions = json.loads(run([executable(controller), "-p", "JSON.stringify(process.versions)"],
                              capture=True, env={"ELECTRON_RUN_AS_NODE": "1", "PATH": "/usr/bin:/bin"}))
    installed_python = run([python, "-I", "-B", "-c", "import platform; print(platform.python_version())"],
                           capture=True, env={"PATH": "/usr/bin:/bin"}).strip()
    if versions["electron"] != manifest["devDependencies"]["electron"] or installed_python != PYTHON_VERSION:
        raise RuntimeError("Packaged runtime versions differ from pins")
    report = {"host_os": platform.mac_ver()[0], "minimum_macos": OS_MINIMUM,
              "electron": versions["electron"], "node": versions["node"], "python": installed_python,
              "python_archive_sha256": PYTHON_SHA256, "electron_archive_sha256": ELECTRON_SHA256, "service": SERVICE,
              "artifacts": artifacts, "signature": "Developer ID Application, hardened runtime",
              "shadertoy_fixture": args.shadertoy_fixture is not None}
    (output / "package-receipt.json").write_text(json.dumps(report, indent=2) + "\n")
    print(output / "package-receipt.json")


if __name__ == "__main__":
    main()
