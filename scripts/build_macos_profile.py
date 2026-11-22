#!/usr/bin/env python3
"""Build VOSC with an isolated openFrameworks 0.11.1 core."""

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys

from build_macos_legacy import ROOT, prepare_output, sdk_paths


def sdk_version(sdk):
    header = sdk / "libs/openFrameworks/utils/ofConstants.h"
    if not header.is_file():
        raise ValueError(f"Missing openFrameworks version header: {header}")
    components = dict(re.findall(r"^#define\s+OF_VERSION_(MAJOR|MINOR|PATCH)\s+(\d+)\b",
                                 header.read_text(encoding="latin-1"), re.MULTILINE))
    version = ".".join(components.get(key, "?") for key in ("MAJOR", "MINOR", "PATCH"))
    if version != "0.11.1":
        raise ValueError(f"{sdk} reports openFrameworks {version}; expected 0.11.1")
    return version


def sdk_revision(sdk):
    # A release archive can live inside an unrelated Git working tree.
    if not (sdk / ".git").exists():
        return None
    return subprocess.check_output(["git", "-C", str(sdk), "rev-parse", "HEAD"], text=True).strip()


def write_if_changed(path, content):
    if not path.exists() or path.read_bytes() != content:
        path.write_bytes(content)


def build_xcode_inputs(sdk, addon_sdk, output, configuration):
    subprocess.run([
        sys.executable, str(ROOT / "scripts/build_macos_recovery_core.py"),
        "--of-root", str(sdk), "--addon-sdk", str(addon_sdk), "--output", str(output),
        "--configuration", configuration, "--no-sanitizers",
    ], check=True)


def core_archive(output, configuration):
    name = "openFrameworksDebug.a" if configuration == "Debug" else "openFrameworks.a"
    return output / "products" / name


def app_build_command(output, configuration, sdk, addon_sdk, *, xcodebuild="xcodebuild"):
    command = [
        xcodebuild, "-project", str(ROOT / "vosc.xcodeproj"), "-scheme", f"vosc {configuration}",
        f"OF_PATH={sdk}", f"VOSC_ADDON_SDK={addon_sdk}",
    ]
    if output is not None:
        command.extend(("-derivedDataPath", str(output / "derived"),
                        f"CONFIGURATION_BUILD_DIR={output / 'products'}"))
    return command


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--of-root", type=Path)
    parser.add_argument("--addon-sdk", type=Path, help="Installation providing the VOSC addon set")
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--output", type=Path, help="Dedicated build root")
    mode.add_argument("--in-place", action="store_true", help="Use Xcode's normal build locations")
    mode.add_argument("--sdk-version", action="store_true", help="Print the selected SDK version")
    mode.add_argument("--sdk-root", action="store_true", help="Print the selected SDK path")
    parser.add_argument("--configuration", choices=("Debug", "Release", "AppStore"), default="Debug")
    parser.add_argument("--clean", action="store_true")
    parser.add_argument("--prepare-xcode", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--expected-version", help=argparse.SUPPRESS)
    parser.add_argument("--xcodebuild", default="xcodebuild", help=argparse.SUPPRESS)
    parser.add_argument("xcode_flags", nargs=argparse.REMAINDER, help=argparse.SUPPRESS)
    args = parser.parse_args()
    sdk, addon_sdk = sdk_paths(args.of_root, args.addon_sdk)
    if args.sdk_root:
        print(sdk)
        return
    clean_only = args.in_place and args.clean
    try:
        version = None if clean_only else sdk_version(sdk)
    except ValueError as error:
        parser.error(str(error))
    if args.sdk_version:
        print(version)
        return
    output = args.output.expanduser().resolve() if args.output else None
    if args.prepare_xcode:
        if version != args.expected_version:
            parser.error(f"Xcode settings select {args.expected_version} but the SDK is {version}. "
                         "Use make Debug or make Release to select the matching settings.")
        build_xcode_inputs(sdk, addon_sdk, output, args.configuration)
        return
    if args.configuration == "AppStore":
        parser.error("Select Debug or Release for a standalone build")
    if output is not None:
        output = output / f"of-{version}" / args.configuration
        prepare_output(output, sdk, addon_sdk)
    command = app_build_command(output, args.configuration, sdk, addon_sdk,
                                xcodebuild=args.xcodebuild)
    command.extend(args.xcode_flags[1:] if args.xcode_flags[:1] == ["--"] else args.xcode_flags)
    if args.clean:
        command.append("clean")
    if not clean_only:
        command.append("build")
    if output is not None:
        write_if_changed(output / "commands.json", (json.dumps({
            "sdk_version": version, "sdk_revision": sdk_revision(sdk), "app": command,
        }, indent=2) + "\n").encode())
    subprocess.run(command, cwd=ROOT, check=True)


if __name__ == "__main__":
    main()
