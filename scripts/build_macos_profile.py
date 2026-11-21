#!/usr/bin/env python3
"""Build VOSC with an isolated openFrameworks 0.11.1 or 0.12.1 core."""

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

from build_macos_legacy import (
    ROOT, patch_imgui_glfw, prepare_output, receiver_inputs,
    replace_expected, sdk_paths, write_overlay,
)
from build_macos_recovery_core import publish_xcode_overlay


def sdk_version(sdk):
    header = sdk / "libs/openFrameworks/utils/ofConstants.h"
    if not header.is_file():
        raise ValueError(f"Missing openFrameworks version header: {header}")
    components = dict(re.findall(r"^#define\s+OF_VERSION_(MAJOR|MINOR|PATCH)\s+(\d+)\b",
                                 header.read_text(encoding="latin-1"), re.MULTILINE))
    version = ".".join(components.get(key, "?") for key in ("MAJOR", "MINOR", "PATCH"))
    if version not in ("0.11.1", "0.12.1"):
        raise ValueError(f"{sdk} reports openFrameworks {version}; expected 0.11.1 or 0.12.1")
    return version


def sdk_revision(sdk):
    # A release archive can live inside an unrelated Git working tree.
    if not (sdk / ".git").exists():
        return None
    return subprocess.check_output(["git", "-C", str(sdk), "rev-parse", "HEAD"], text=True).strip()


def write_if_changed(path, content):
    if not path.exists() or path.read_bytes() != content:
        path.write_bytes(content)


def prepare_core(sdk, output):
    # The SDK project has post-build phases that install its archive in the
    # SDK. Give those phases a private SDK layout while sharing dependencies.
    libs = output / "sdk/libs"
    compiled = libs / "openFrameworksCompiled"
    source = Path("libs/openFrameworks/gl/ofTexture.cpp")
    shutil.copytree(sdk / "libs/openFrameworks", libs / "openFrameworks",
                    ignore=shutil.ignore_patterns("ofTexture.cpp"), dirs_exist_ok=True)
    shutil.copytree(sdk / "libs/openFrameworksCompiled/project/osx", compiled / "project/osx",
                    ignore=shutil.ignore_patterns("xcuserdata", "build"), dirs_exist_ok=True)
    for dependency in (sdk / "libs").iterdir():
        link = libs / dependency.name
        if not dependency.is_dir():
            continue
        if link.is_symlink() and link.readlink() != dependency:
            link.unlink()
        if not link.exists():
            link.symlink_to(dependency)
    # Use the committed allocator for a Git checkout and the shipped source
    # for an extracted release. Neither path modifies the installation.
    if (sdk / ".git").exists():
        stock = subprocess.check_output(["git", "-C", str(sdk), "show", f"HEAD:{source}"])
    else:
        stock = (sdk / source).read_bytes()
    write_if_changed(output / "sdk" / source, stock)
    return compiled


def patch_expression_returns(text):
    # Preserve evaluation of every component and report any component failure.
    for call in ("addVar(name, value, recompile, setTime)", "addDummyVar(name, recompile)",
                 "addConst(name, value, recompile)", "compile(setTime)"):
        text = replace_expected(
            text, f"    for (int i=0; i<size(); i++) {{\n        expr[i]->{call};\n    }}\n}}",
            f"    bool success = true;\n    for (int i=0; i<size(); i++) {{\n"
            f"        success = expr[i]->{call} && success;\n    }}\n    return success;\n}}",
            f"vector expression {call}",
        )
    return text


def prepare_addon_overlay(addon_sdk, output):
    addons = addon_sdk / "addons"
    entries = [{"type": "file", "name": item["source"], "external-contents": item["replacement"]}
               for item in receiver_inputs(addon_sdk)]
    patches = {
        "ofxExpr/src/ofxExpr.cpp": patch_expression_returns,
        "ofxImGui/src/EngineGLFW.cpp": patch_imgui_glfw,
        "ofxImGui/src/ImHelpers.h": lambda text: replace_expected(
            text, '#include "imgui.h"', '#include "imgui.h"\n#include <stack>', "ImGui helpers include",
        ),
        "ofxShadertoy/src/ofxShadertoy.cpp": lambda text: replace_expected(
            text, "std::filesystem::last_write_time(ofToDataPath(mainShaderFile))",
            "std::filesystem::last_write_time(ofToDataPath(mainShaderFile)).time_since_epoch().count()",
            "Shadertoy last-write time",
        ),
    }
    for filename in ("ofxOscSender.h", "ofxOscSender.cpp", "ofxOscParameterSync.h"):
        patches[f"ofxOsc/src/{filename}"] = lambda text: replace_expected(
            text, '"ofParameterGroup.h"', '"ofParameter.h"', "OSC parameter include",
        )
    for relative, patch in patches.items():
        source = addons / relative
        target = output / "addons" / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        write_if_changed(target, patch(source.read_bytes().decode("latin-1")).encode("latin-1"))
        entries.append({"type": "file", "name": str(source), "external-contents": str(target)})
    return write_overlay(output, entries)


def build_xcode_inputs(sdk, addon_sdk, output, configuration, version):
    if version == "0.11.1":
        subprocess.run([
            sys.executable, str(ROOT / "scripts/build_macos_recovery_core.py"),
            "--of-root", str(sdk), "--addon-sdk", str(addon_sdk), "--output", str(output),
            "--configuration", configuration, "--no-sanitizers",
        ], check=True)
        return
    prepare_output(output, sdk, addon_sdk)
    compiled = prepare_core(sdk, output)
    core_configuration = "Debug" if configuration == "Debug" else "Release"
    command = [
        "xcodebuild", "-project", str(compiled / "project/osx/openFrameworksLib.xcodeproj"),
        "-target", "openFrameworks", "-configuration", core_configuration, f"OF_PATH={sdk}",
        "ARCHS=arm64", "ONLY_ACTIVE_ARCH=YES", "MACOSX_DEPLOYMENT_TARGET=12.6",
        "CODE_SIGNING_ALLOWED=NO", "-jobs", "6", "build",
    ]
    overlay = prepare_addon_overlay(addon_sdk, output)
    subprocess.run(command, cwd=ROOT, env={**os.environ, "CCACHE_DISABLE": "1"}, check=True)
    archive = core_archive(output, configuration)
    if not archive.is_file():
        raise FileNotFoundError(archive)
    publish_xcode_overlay(overlay, output)


def core_archive(output, configuration, version="0.12.1"):
    name = "openFrameworksDebug.a" if configuration == "Debug" else "openFrameworks.a"
    if version == "0.11.1":
        return output / "products" / name
    return output / "sdk/libs/openFrameworksCompiled/lib/osx" / name


def app_build_command(output, configuration, sdk, addon_sdk, *, version, xcodebuild="xcodebuild"):
    command = [
        xcodebuild, "-project", str(ROOT / "vosc.xcodeproj"), "-scheme", f"vosc {configuration}",
        f"OF_PATH={sdk}", f"VOSC_ADDON_SDK={addon_sdk}",
    ]
    if version is not None:
        settings = "OpenFrameworks011.xcconfig" if version == "0.11.1" else "OpenFrameworks012.xcconfig"
        command.extend(("-xcconfig", str(ROOT / "scripts" / settings)))
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
        build_xcode_inputs(sdk, addon_sdk, output, args.configuration, version)
        return
    if args.configuration == "AppStore":
        parser.error("Select Debug or Release for a standalone build")
    if output is not None:
        output = output / f"of-{version}" / args.configuration
        prepare_output(output, sdk, addon_sdk)
    command = app_build_command(output, args.configuration, sdk, addon_sdk,
                                version=version, xcodebuild=args.xcodebuild)
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
