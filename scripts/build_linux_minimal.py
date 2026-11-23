#!/usr/bin/env python3
"""Stage and build the minimal Linux renderer in its own openFrameworks project."""

import argparse
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile

from addon_compat import (EXPR_COMPILE_LOOP, POST_PROCESSING_ATTRIBUTE, SHADERTOY_TIMESTAMP,
                          patch_expression_returns, patch_post_processing, patch_shadertoy_timestamp,
                          replace_addon_anchor)
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
    def patch_expr_header(text):
        if '#include "ofUtils.h"' in text:
            return text
        return replace_addon_anchor(text, '#include "ofEvents.h"',
                                    '#include "ofEvents.h"\n#include "ofUtils.h"',
                                    "ofxExpr event include")

    patches = {
        "ofxExpr": (("ofxExpr.cpp", EXPR_COMPILE_LOOP, patch_expression_returns),
                    ("ofxExprParser.hpp", '#include "ofEvents.h"', patch_expr_header)),
        "ofxPostProcessing": (("PostProcessing.cpp", POST_PROCESSING_ATTRIBUTE, patch_post_processing),),
        "ofxShadertoy": (("ofxShadertoy.cpp", SHADERTOY_TIMESTAMP + ";", patch_shadertoy_timestamp),),
    }
    for name, files in patches.items():
        if name not in addons:
            continue
        root = sdk / "addons" / name
        updated = {}
        for filename, anchor, patch in files:
            source = root / "src" / filename
            if not source.is_file():
                continue
            text = source.read_text(encoding="latin-1")
            if anchor in text and (patched := patch(text)) != text:
                updated[filename] = patched
        if not updated:
            continue
        local = Path("addons") / name
        shutil.copytree(root, stage / local, ignore=shutil.ignore_patterns(".git", ".DS_Store"))
        for filename, text in updated.items():
            (stage / local / "src" / filename).write_text(text, encoding="latin-1")
        addons[addons.index(name)] = local.as_posix()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--make", default="make")
    parser.add_argument("--of-root", required=True, type=Path)
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
    (stage / "Makefile").write_text(
        "APPNAME = vosc\n"
        "PROJECT_ROOT = .\n"
        "override PROJECT_CFLAGS += -DVOSC_MINIMAL_BUILD=1 -DVOSC_OPERATOR_UI=0 -DOFXEXPR_NOGUI\n"
        "include $(OF_ROOT)/libs/openFrameworksCompiled/project/makefileCommon/compile.project.mk\n"
    )
    source = ROOT / "src"
    def ignore_non_linux_sources(directory, names):
        return {"operator", "macos"} if Path(directory) == source else ()

    shutil.copytree(source, stage / "src", ignore=ignore_non_linux_sources)
    shutil.copytree(ROOT / "assets", stage / "bin/data", ignore=ignore_assets)
    (stage / "bin/data/videos").mkdir()
    stage_examples(stage / "bin/data/examples")
    print(f"Building VOSC minimal in {stage}", flush=True)
    result = subprocess.run([*shlex.split(args.make), f"OF_ROOT={sdk}", "Release"], cwd=stage)
    if result.returncode:
        return result.returncode
    binary = stage / "bin/vosc"
    if not binary.is_file():
        parser.exit(1, f"Build did not produce {binary}\n")
    print(f"Run ./vosc --size 1280x720 --fps 30 from {binary.parent}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
