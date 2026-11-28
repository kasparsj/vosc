#!/usr/bin/env python3
"""Stage and build the minimal Linux renderer in its own openFrameworks project."""

import argparse
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile

from addon_compat import linux_addon_updates
from stage_assets import stage_examples


ROOT = Path(__file__).resolve().parents[1]
EXCLUDED_ADDONS = {"ofxDeferredShading", "ofxHPVPlayer", "ofxUltralight"}
LINUX_SDK_VERSION = "0.12.1"


def ignore_assets(directory, names):
    path = Path(directory).relative_to(ROOT / "assets")
    excluded = {name for name in names
                if name.startswith(".") or Path(name).suffix.lower() in {".hpv", ".gv", ".html", ".htm"}}
    if path == Path("."):
        # Local video collections can be large. The normal video backend remains available.
        excluded.add("videos")
    if path == Path("shaders"):
        excluded.add("deferred")
    return excluded


def stage_legacy_addons(sdk, stage, addons):
    for index, name in enumerate(addons):
        root = sdk / "addons" / name
        updated = linux_addon_updates(root)
        if not updated:
            continue
        local = Path("addons") / name
        shutil.copytree(root, stage / local, ignore=shutil.ignore_patterns(".git", ".DS_Store"))
        for filename, content in updated.items():
            (stage / local / filename).write_bytes(content)
        addons[index] = local.as_posix()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--make", default="make")
    parser.add_argument("--of-root", required=True, type=Path)
    parser.add_argument("--dawn-sdk", type=Path, help="Enable WebGPU M1 with an extracted Dawn SDK")
    parser.add_argument("--stage-only", action="store_true",
                        help="Prepare a fresh project and runtime data without invoking Make")
    args = parser.parse_args()
    sdk = args.of_root.resolve()
    header = sdk / "libs/openFrameworks/utils/ofConstants.h"
    if not header.is_file():
        parser.error(f"openFrameworks version header missing: {header}")
    parts = dict(re.findall(r"^\s*#define[ \t]+OF_VERSION_(MAJOR|MINOR|PATCH)[ \t]+(\d+)\b",
                            header.read_text(encoding="latin-1"), re.MULTILINE))
    version = ".".join(parts.get(part, "?") for part in ("MAJOR", "MINOR", "PATCH"))
    if version != LINUX_SDK_VERSION:
        parser.error(f"minimal Linux build requires openFrameworks {LINUX_SDK_VERSION}; {sdk} reports {version}")
    of_make = sdk / "libs/openFrameworksCompiled/project/makefileCommon/compile.project.mk"
    if not of_make.is_file():
        parser.error(f"openFrameworks project makefile missing: {of_make}")

    addons = [line.partition("#")[0].strip() for line in (ROOT / "addons.make").read_text().splitlines()]
    addons = [name for name in addons if name and name not in EXCLUDED_ADDONS]
    missing = [name for name in addons if not (sdk / "addons" / name).is_dir()]
    if missing:
        parser.error(f"missing retained addons in {sdk / 'addons'}: {', '.join(missing)}")

    parent = ROOT / ".vosc-linux"
    parent.mkdir(exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix="minimal-", dir=parent))
    stage_legacy_addons(sdk, stage, addons)
    (stage / "addons.make").write_text("\n".join(addons) + "\n")
    shutil.copyfile(ROOT / "scripts/webgpu_m1.mk", stage / "webgpu_m1.mk")
    (stage / "Makefile").write_text(
        "APPNAME = vosc\n"
        "PROJECT_ROOT = .\n"
        "override PROJECT_CFLAGS += -DVOSC_MINIMAL_BUILD=1 -DVOSC_OPERATOR_UI=0 -DOFXEXPR_NOGUI\n"
        "include webgpu_m1.mk\n"
        "include $(OF_ROOT)/libs/openFrameworksCompiled/project/makefileCommon/compile.project.mk\n"
    )
    source = ROOT / "src"
    def ignore_non_linux_sources(directory, names):
        return {"operator", "macos"} if Path(directory) == source else ()

    shutil.copytree(source, stage / "src", ignore=ignore_non_linux_sources)
    shutil.copytree(ROOT / "assets", stage / "bin/data", ignore=ignore_assets)
    (stage / "bin/data/videos").mkdir()
    stage_examples(stage / "bin/data/examples")
    if args.stage_only:
        print(f"Staged VOSC minimal in {stage}")
        return 0
    print(f"Building VOSC minimal in {stage}", flush=True)
    command = [*shlex.split(args.make), f"OF_ROOT={sdk}"]
    if args.dawn_sdk:
        command.append(f"VOSC_DAWN_SDK={args.dawn_sdk.resolve()}")
    result = subprocess.run([*command, "Release"], cwd=stage)
    if result.returncode:
        return result.returncode
    binary = stage / "bin/vosc"
    if not binary.is_file():
        parser.exit(1, f"Build did not produce {binary}\n")
    print(f"Run ./vosc --size 1280x720 --fps 30 from {binary.parent}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
