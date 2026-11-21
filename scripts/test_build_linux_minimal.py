#!/usr/bin/env python3
"""Exercise minimal-profile Make selection, staging and SDK diagnostics."""

import os
from pathlib import Path
import shutil
import subprocess
from tempfile import TemporaryDirectory


ROOT = Path(__file__).resolve().parents[1]
EXCLUDED = {"ofxDeferredShading", "ofxImGui", "ofxHPVPlayer", "ofxUltralight"}


def main():
    with TemporaryDirectory(prefix="vosc-minimal-build-test-") as temporary:
        work = Path(temporary)
        project = work / "project"
        for relative in ("Makefile", "config.make", "addons.make", "scripts/build_linux_minimal.py",
                         "scripts/addon_compat.py", "src/main.cpp"):
            target = project / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / relative, target)
        data = project / "bin/data/shaders"
        (data / "deferred").mkdir(parents=True)
        (data / "direct.frag").touch()
        (data / "deferred/pass.frag").touch()
        sdk = work / "sdk"
        header = sdk / "libs/openFrameworks/utils/ofConstants.h"
        header.parent.mkdir(parents=True)
        header.write_text("#define OF_VERSION_MAJOR 0\n#define OF_VERSION_MINOR 11\n#define OF_VERSION_PATCH 1\n")
        retained = set((project / "addons.make").read_text().split()) - EXCLUDED
        for name in retained:
            (sdk / "addons" / name).mkdir(parents=True)
        expr_header = sdk / "addons/ofxExpr/src/ofxExprParser.hpp"
        expr_header.parent.mkdir()
        expr_header.write_text('#include "ofEvents.h"\n')
        rules = sdk / "libs/openFrameworksCompiled/project/makefileCommon/compile.project.mk"
        rules.parent.mkdir(parents=True)
        rules.write_text(
            "Release:\n"
            "\t@test -f $(PROJECT_ROOT)/src/main.cpp\n"
            "\t@test '$(PROJECT_CFLAGS)' = '-DVOSC_MINIMAL_BUILD=1'\n"
            "\t@touch bin/vosc\n"
        )
        commands = work / "commands"
        commands.mkdir()
        uname = commands / "uname"
        uname.write_text("#!/bin/sh\nif [ \"$1\" = -s ]; then echo Linux; else exec /usr/bin/uname \"$@\"; fi\n")
        uname.chmod(0o755)
        env = {**os.environ, "PATH": f"{commands}:{os.environ['PATH']}"}
        build = ["make", "Release", "VOSC_PROFILE=minimal", f"OF_ROOT={sdk}"]

        def run(argv):
            return subprocess.run(argv, cwd=project, env=env, text=True,
                                  capture_output=True, timeout=30)

        dry_run = run([build[0], "-n", *build[1:]])
        assert dry_run.returncode == 0, dry_run
        assert not (project / ".vosc-linux").exists()
        rejected = run(build)
        assert rejected.returncode and "requires openFrameworks 0.12.1" in rejected.stderr, rejected
        assert "reports 0.11.1" in rejected.stderr, rejected
        assert not (project / ".vosc-linux").exists()
        header.write_text(header.read_text().replace("OF_VERSION_MINOR 11", "OF_VERSION_MINOR 12"))
        result = run(build)
        assert result.returncode == 0, result
        stage, = (project / ".vosc-linux").iterdir()
        assert set((stage / "addons.make").read_text().split()) == (retained - {"ofxExpr"}) | {"addons/ofxExpr"}
        assert '#include "ofUtils.h"' in (stage / "addons/ofxExpr/src/ofxExprParser.hpp").read_text()
        assert '#include "ofUtils.h"' not in expr_header.read_text()
        assert (stage / "bin/data/shaders/direct.frag").is_file()
        assert not (stage / "bin/data/shaders/deferred").exists()
    print("PASS minimal SDK selection, Make dry-run, and staging with a fixture SDK")


if __name__ == "__main__":
    main()
