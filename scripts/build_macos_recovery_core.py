#!/usr/bin/env python3
"""Build a private OF 0.11.1 core for ordinary or instrumented builds.

Copies Xcode metadata, overlays sources, and verifies resolved output paths before
building. The installed core, project and third-party archives are never replaced.
"""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

from build_macos_legacy import prepare_output, prepare_overlay, sdk_paths


OUTPUT_KEYS = (
    "TARGET_BUILD_DIR", "TARGET_TEMP_DIR", "DERIVED_FILE_DIR", "OBJECT_FILE_DIR",
    "BUILT_PRODUCTS_DIR", "PROJECT_TEMP_DIR", "CONFIGURATION_BUILD_DIR",
    "CONFIGURATION_TEMP_DIR", "OBJROOT", "SYMROOT", "SHARED_PRECOMPS_DIR",
    "CLANG_MODULE_CACHE_PATH", "DSTROOT",
    "CACHE_ROOT", "CCHROOT", "TEMP_ROOT", "DERIVED_FILES_DIR", "PROJECT_DERIVED_FILE_DIR",
)


def verify_output_settings(text, output):
    settings = {}
    for line in text.splitlines():
        if " = " in line:
            key, value = line.split(" = ", 1)
            settings[key.strip()] = value.strip()
    for key in OUTPUT_KEYS:
        value = settings.get(key)
        if not value or not Path(value).resolve().is_relative_to(output):
            raise ValueError(f"Unsafe or unresolved core output: {key}={value}")
    return {key: settings[key] for key in OUTPUT_KEYS}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--of-root", type=Path)
    parser.add_argument("--addon-sdk", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--configuration", choices=("Debug", "Release", "AppStore"), default="Debug")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--sanitizers", choices=("address", "undefined"), action="append")
    mode.add_argument("--no-sanitizers", action="store_true",
                      help="Build an ordinary private core (default remains ASAN)")
    args = parser.parse_args(argv)
    args.sanitizers = [] if args.no_sanitizers else (args.sanitizers or ["address"])
    return args


def compile_flags(overlay, sanitizers):
    flags = f'$(inherited) -ivfsoverlay "{overlay}"'
    if sanitizers:
        flags += f" -fsanitize={','.join(dict.fromkeys(sanitizers))} -fno-omit-frame-pointer"
    return flags


def publish_xcode_overlay(overlay, output):
    """Expose the overlay to Xcode's fixed flags and track replacement bytes.

    The forced-include header is a compiler dependency. Updating its contents
    invalidates application objects even when an overlaid source's installed
    path and timestamp have not changed. Leave both files untouched on no-op
    builds so Xcode can reuse its objects.
    """
    # The producer fingerprints external replacement bytes in the filename.
    fingerprint = hashlib.sha256(overlay.name.encode() + overlay.read_bytes()).hexdigest()
    files = {
        output / "filesystem-overlay.json": overlay.read_bytes(),
        output / "overlay-inputs.h": (
            '// Generated compatibility input identity.\n'
            f'#define VOSC_COMPAT_OVERLAY_FINGERPRINT "{fingerprint}"\n'
        ).encode(),
    }
    for path, content in files.items():
        if not path.exists() or path.read_bytes() != content:
            temporary = path.with_suffix(path.suffix + ".tmp")
            temporary.write_bytes(content)
            temporary.replace(path)


def main():
    args = parse_args()
    output = args.output.expanduser().resolve()
    of_root, addon_sdk = sdk_paths(args.of_root, args.addon_sdk)
    prepare_output(output, of_root, addon_sdk)
    original = of_root / "libs/openFrameworksCompiled/project/osx"
    project = output / "project-osx"
    if not project.exists():
        shutil.copytree(original, project,
                        ignore=shutil.ignore_patterns("xcuserdata", "*.xcuserstate"))
    pbx = project / "openFrameworksLib.xcodeproj/project.pbxproj"
    project_text = (original / "openFrameworksLib.xcodeproj/project.pbxproj").read_text().replace(
        "../../../openFrameworks", str(of_root / "libs/openFrameworks"))
    if pbx.read_text() != project_text:
        pbx.write_text(project_text)
    shared = [original / "openFrameworksLib.xcodeproj/project.pbxproj",
              of_root / "libs/openFrameworksCompiled/lib/osx/openFrameworksDebug.a",
              of_root / "libs/openFrameworksCompiled/lib/osx/openFrameworks.a"]
    before = {str(path): digest(path) if path.is_file() else None for path in shared}
    overlay = prepare_overlay(of_root, output, addon_sdk)
    publish_xcode_overlay(overlay, output)
    flags = compile_flags(overlay, args.sanitizers)
    command = ["xcodebuild", "-project", str(project / "openFrameworksLib.xcodeproj"),
               "-target", "openFrameworks", "-configuration", ("Debug" if args.configuration == "Debug" else "Release"), "-sdk", "macosx",
               f"SRCROOT={original}", f"OF_PATH={of_root}"]
    for key, name in {"SYMROOT": "sym", "OBJROOT": "obj", "CONFIGURATION_TEMP_DIR": "temp",
                      "CONFIGURATION_BUILD_DIR": "products", "SHARED_PRECOMPS_DIR": "precompiled",
                      "CLANG_MODULE_CACHE_PATH": "modules", "CACHE_ROOT": "cache", "DSTROOT": "dst"}.items():
        command.append(f"{key}={output / name}")
    command += ["ARCHS=arm64", "ONLY_ACTIVE_ARCH=YES", "MACOSX_DEPLOYMENT_TARGET=12.6",
                "CLANG_CXX_LANGUAGE_STANDARD=c++17", "CODE_SIGNING_ALLOWED=NO", "SKIP_INSTALL=YES",
                f"OTHER_CFLAGS={flags}", f"OTHER_CPLUSPLUSFLAGS={flags}", "OTHER_LDFLAGS="]
    (output / "command.json").write_text(json.dumps(command, indent=2) + "\n")
    settings = subprocess.run(command + ["-showBuildSettings"], capture_output=True, text=True)
    (output / "settings.log").write_text(settings.stdout + settings.stderr)
    settings.check_returncode()
    verified = verify_output_settings(settings.stdout, output)
    with (output / "build.log").open("w") as stream:
        result = subprocess.run(command + ["-jobs", "2", "build"], stdout=stream,
                                stderr=subprocess.STDOUT)
    after = {str(path): digest(path) if path.is_file() else None for path in shared}
    (output / "preservation.json").write_text(json.dumps(
        {"before": before, "after": after, "unchanged": before == after,
         "verified_output_paths": verified, "return_code": result.returncode}, indent=2) + "\n")
    if before != after:
        raise RuntimeError("Shared framework inputs changed during the private build")
    print("Core build log:", output / "build.log")
    if result.returncode:
        for line in (output / "build.log").read_text().splitlines():
            if "error:" in line or "BUILD FAILED" in line:
                print(line)
    archive = "openFrameworksDebug.a" if args.configuration == "Debug" else "openFrameworks.a"
    print("Private core:", output / "products" / archive)
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
