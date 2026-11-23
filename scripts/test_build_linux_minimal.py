#!/usr/bin/env python3
"""Exercise minimal-profile Make selection, staging and SDK diagnostics."""

import os
from pathlib import Path
import shutil
import subprocess
from tempfile import TemporaryDirectory


ROOT = Path(__file__).resolve().parents[1]
EXCLUDED = {"ofxDeferredShading", "ofxHPVPlayer", "ofxUltralight"}


def verify_desktop_selection(project, sdk, rules, run):
    helper = project / "scripts/build_rmlui.py"
    helper.write_text(
        "from pathlib import Path\n"
        "import sys\n"
        "out = Path(sys.argv[2])\n"
        "for name in ('lib/libvosc_rmlui_gl3.a', 'lib/librmlui.a', 'generated/VoscOperatorFont.hpp'):\n"
        "    target = out / name\n"
        "    target.parent.mkdir(parents=True, exist_ok=True)\n"
        "    target.touch()\n"
    )
    rules.write_text(
        ".PHONY: Release ReleaseABI force\n"
        "Release:\n"
        "\t$(MAKE) ReleaseABI\n"
        "TARGET = bin/vosc\n"
        "OF_PROJECT_OBJS = obj/renderer.o\n"
        "ReleaseABI: $(TARGET)\n"
        "$(TARGET): $(OF_PROJECT_OBJS) force\n"
        "\t@mkdir -p bin\n"
        "\t@printf '%s\\n' '$(PROJECT_LDFLAGS)' > bin/link-inputs\n"
        "\t@touch $@\n"
        "$(OF_PROJECT_OBJS): force\n"
        "\t@mkdir -p obj\n"
        "\t@printf '%s\\n' '$(PROJECT_CFLAGS)' '$(PROJECT_EXCLUSIONS)' > obj/compile-inputs\n"
        "\t@touch $@\n"
    )
    desktop = [f"OF_ROOT={sdk}", "VOSC_PROFILE=default"]
    enabled = project / "rmlui-enabled"
    disabled = project / "rmlui-disabled"
    result = run(["make", "-j2", "Release", *desktop, f"VOSC_RMLUI_DIR={enabled}"])
    assert result.returncode == 0, result
    assert (enabled / "generated/VoscOperatorFont.hpp").is_file()
    assert (enabled / "lib/librmlui.a").is_file()
    assert "-DVOSC_OPERATOR_UI=1" in (project / "obj/compile-inputs").read_text()
    assert f"-I{enabled}/generated" in (project / "obj/compile-inputs").read_text()
    assert f"{enabled}/lib/libvosc_rmlui_gl3.a" in (project / "bin/link-inputs").read_text()

    result = run(["make", "-j2", "Release", *desktop, "VOSC_OPERATOR_UI=0", f"VOSC_RMLUI_DIR={disabled}"])
    assert result.returncode == 0, result
    assert not disabled.exists()
    inputs = (project / "obj/compile-inputs").read_text()
    assert "-DVOSC_OPERATOR_UI=0" in inputs
    assert "/src/operator" in inputs
    assert "/generated" not in inputs
    assert not (project / "bin/link-inputs").read_text().strip()


def main():
    with TemporaryDirectory(prefix="vosc-minimal-build-test-") as temporary:
        work = Path(temporary)
        project = work / "project"
        for relative in ("Makefile", "config.make", "addons.make", "scripts/build_linux_minimal.py",
                         "scripts/addon_compat.py", "scripts/stage_assets.py", "src/main.cpp"):
            target = project / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / relative, target)
        for ui_source in ("operator/DiagnosticOverlay.cpp", "operator/InspectorView.cpp", "operator/InspectorPreview.cpp"):
            target = project / "src" / ui_source
            target.parent.mkdir(exist_ok=True)
            target.touch()
        data = project / "assets/shaders"
        (data / "deferred").mkdir(parents=True)
        (data / "direct.frag").touch()
        (data / "deferred/pass.frag").touch()
        (project / "media/videos").mkdir(parents=True)
        (project / "media/videos/private.mov").touch()
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
            "\t@test '$(PROJECT_CFLAGS)' = '-DVOSC_MINIMAL_BUILD=1 -DVOSC_OPERATOR_UI=0 -DOFXEXPR_NOGUI'\n"
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
        assert not (stage / "src/operator").exists()
        assert (stage / "bin/data/shaders/direct.frag").is_file()
        assert not (stage / "bin/data/shaders/deferred").exists()
        assert list((stage / "bin/data/videos").iterdir()) == []
        assert not list(stage.rglob("*RmlUi*"))
        verify_desktop_selection(project, sdk, rules, run)
    print("PASS Linux minimal staging and desktop operator UI input selection with a fixture SDK")


if __name__ == "__main__":
    main()
