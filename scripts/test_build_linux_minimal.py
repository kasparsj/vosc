#!/usr/bin/env python3
"""Exercise minimal-profile Make selection, staging and SDK diagnostics."""

import os
from pathlib import Path
import shutil
import subprocess
from tempfile import TemporaryDirectory


ROOT = Path(__file__).resolve().parents[1]
EXCLUDED = {"ofxDeferredShading", "ofxHPVPlayer", "ofxUltralight"}


def verify_example_stage(source, staged):
    def contents(root):
        return {
            path.relative_to(root): path.read_bytes()
            for path in root.rglob("*")
            if path.is_file() and all(not part.startswith(".") for part in path.relative_to(root).parts)
        }

    expected = contents(source)
    assert Path("README.md") in expected
    assert Path("browse.json") in expected
    assert Path("organic-trails/preview.png") in expected
    assert contents(staged) == expected


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
        ".PHONY: Release ReleaseABI CleanReleaseABI CleanDebugABI force\n"
        "Release:\n"
        "\t$(MAKE) ReleaseABI\n"
        "TARGET = bin/vosc\n"
        "OF_PROJECT_OBJS = obj/renderer.o\n"
        "PLATFORM_REQUIRED_ADDONS = platformAddon\n"
        "remove-dupes-func = $(if $1,$(strip $(word 1,$1) $(call $0,$(filter-out $(word 1,$1),$1))))\n"
        "REQUESTED_PROJECT_ADDONS := $(shell cat addons.make)\n"
        "ReleaseABI: $(TARGET)\n"
        "CleanReleaseABI CleanDebugABI:\n"
        "\t@printf '%s\\n' '$(OF_ADDONS_PATH)'\n"
        "$(TARGET): $(OF_PROJECT_OBJS) force\n"
        "\t@mkdir -p bin\n"
        "\t@printf '%s\\n' '$(PROJECT_LDFLAGS)' > bin/link-inputs\n"
        "\t@touch $@\n"
        "$(OF_PROJECT_OBJS): force\n"
        "\t@mkdir -p obj\n"
        "\t@printf '%s\\n' '$(PROJECT_CFLAGS)' '$(PROJECT_EXCLUSIONS)' > obj/compile-inputs\n"
        "\t@printf '%s\\n' '$(OF_ADDONS_PATH)' > obj/addon-path\n"
        "\t@printf '%s\\n' '$(REQUESTED_PROJECT_ADDONS)' > obj/requested-addons\n"
        "\t@touch $@\n"
    )
    deferred = sdk / "addons/ofxDeferredShading/data/shaders/deferred"
    deferred.mkdir(parents=True)
    (deferred / "gbuffer.frag").write_text("Deferred shader fixture.\n")
    (sdk / "addons/obj").mkdir()
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
    picker_inputs = (project / "obj/compile-inputs").read_text()
    assert "-DOFXEXPR_NOGUI" in picker_inputs
    assert "./src/operator/macos ./src/operator/macos/%" in picker_inputs
    assert "./src/macos ./src/macos/%" in picker_inputs
    render_addons = Path((project / "obj/addon-path").read_text().strip())
    assert render_addons.resolve().is_relative_to((project / "build/vosc-render-addons").resolve())
    assert not (render_addons / "obj").exists()
    requested = (project / "obj/requested-addons").read_text().split()
    assert requested[0] == "platformAddon"
    assert requested[1:] == [name for name in (project / "addons.make").read_text().split()
                            if name not in {"ofxHPVPlayer", "ofxUltralight"}]
    post = Path("ofxPostProcessing/src/PostProcessing.cpp")
    assert "glPushAttrib" not in (render_addons / post).read_text()
    assert "glPushAttrib" in (sdk / "addons" / post).read_text()
    assert 'setUniform1i("isShadow", 0)' in (render_addons / "ofxDeferredShading/src/GBuffer.cpp").read_text()
    assert 'applyAo.setUniformTexture("colorTex"' in (render_addons / "ofxDeferredShading/src/pass/SsaoPass.cpp").read_text()
    expr_header = Path("ofxExpr/src/ofxExprParser.hpp")
    expr_source = Path("ofxExpr/src/ofxExpr.cpp")
    shadertoy = Path("ofxShadertoy/src/ofxShadertoy.cpp")
    midi_config = Path("ofxMidi/addon_config.mk")
    assert '#include "ofUtils.h"' in (render_addons / expr_header).read_text()
    assert '#include "ofUtils.h"' not in (sdk / "addons" / expr_header).read_text()
    assert (render_addons / expr_source).read_text().count("return success;") == 4
    assert "return success;" not in (sdk / "addons" / expr_source).read_text()
    assert ".time_since_epoch().count()" in (render_addons / shadertoy).read_text()
    assert ".time_since_epoch().count()" not in (sdk / "addons" / shadertoy).read_text()
    assert "linuxaarch64:" in (render_addons / midi_config).read_text()
    assert "linuxaarch64:" not in (sdk / "addons" / midi_config).read_text()
    assert not (render_addons / "ofxMidi/libs").is_symlink()
    assert (render_addons / "ofxMidi/libs/rtmidi/RtMidi.cpp").is_file()
    staged_shader = project / "bin/data/shaders/deferred/gbuffer.frag"
    assert staged_shader.read_bytes() == (deferred / "gbuffer.frag").read_bytes()
    verify_example_stage(project / "examples", project / "bin/data/examples")
    manifest = project / "bin/data/examples/video/scene.vosc.json"
    first_mtime = manifest.stat().st_mtime_ns

    result = run(["make", "-j2", "Release", *desktop, "VOSC_OPERATOR_UI=0", f"VOSC_RMLUI_DIR={disabled}"])
    assert result.returncode == 0, result
    assert not disabled.exists()
    inputs = (project / "obj/compile-inputs").read_text()
    assert "-DVOSC_OPERATOR_UI=0" in inputs
    assert "/src/operator" in inputs
    assert "./src/macos ./src/macos/%" in inputs
    assert "/generated" not in inputs
    assert not (project / "bin/link-inputs").read_text().strip()
    assert (project / "obj/addon-path").read_text().strip() == str(render_addons)
    assert manifest.stat().st_mtime_ns == first_mtime
    verify_example_stage(project / "examples", project / "bin/data/examples")
    for target in ("CleanReleaseABI", "CleanDebugABI"):
        result = run(["make", target, *desktop])
        assert result.returncode == 0 and result.stdout.strip() == str(render_addons), result
    config = sdk / "addons" / midi_config
    config.write_text(config.read_text() + "# Fixture update\n")
    result = run(["make", "CleanReleaseABI", *desktop])
    assert result.returncode == 0, result
    updated_addons = Path(result.stdout.strip())
    assert updated_addons != render_addons
    assert "# Fixture update" in (updated_addons / midi_config).read_text()


def main():
    with TemporaryDirectory(prefix="vosc-minimal-build-test-") as temporary:
        work = Path(temporary)
        project = work / "project"
        for relative in ("Makefile", "config.make", "addons.make", "scripts/build_linux_minimal.py",
                         "scripts/addon_compat.py", "scripts/stage_linux_render_addons.py",
                         "scripts/stage_assets.py", "src/main.cpp"):
            target = project / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / relative, target)
        for ui_source in ("operator/DiagnosticOverlay.cpp", "operator/InspectorView.cpp",
                          "operator/InspectorPreview.cpp", "operator/BrowseCatalog.cpp", "operator/BrowseView.cpp"):
            target = project / "src" / ui_source
            target.parent.mkdir(exist_ok=True)
            target.touch()
        mac_source = project / "src/macos/WebcamTexMac.mm"
        mac_source.parent.mkdir()
        mac_source.touch()
        shutil.copytree(ROOT / "examples", project / "examples")
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
        for name in retained | {"ofxHPVPlayer", "ofxUltralight"}:
            (sdk / "addons" / name).mkdir(parents=True)
        midi_settings = (
            "\tADDON_PKG_CONFIG_LIBRARIES = alsa\n"
            "\tADDON_SOURCES_EXCLUDE = libs/pgmidi/%\n"
            "\tADDON_SOURCES_EXCLUDE += src/ios/%\n"
            "\tADDON_INCLUDES_EXCLUDE = libs/pgmidi/%\n"
            "\tADDON_INCLUDES_EXCLUDE += src/ios/%\n"
        )
        midi_config = sdk / "addons/ofxMidi/addon_config.mk"
        midi_original = f"linuxarmv7l:\n{midi_settings}\nosx:\n\tADDON_FRAMEWORKS = CoreMIDI\n"
        midi_config.write_bytes(midi_original.encode("ascii"))
        midi_library = midi_config.parent / "libs/rtmidi/RtMidi.cpp"
        midi_library.parent.mkdir(parents=True)
        midi_library.touch()
        expr_header = sdk / "addons/ofxExpr/src/ofxExprParser.hpp"
        expr_header.parent.mkdir()
        expr_header.write_text('#include "ofEvents.h"\n')
        (expr_header.parent / "ofxExpr.cpp").write_text(
            "".join(f"    for (int i=0; i<size(); i++) {{\n        expr[i]->{call};\n    }}\n}}\n"
                    for call in ("addVar(name, value, recompile, setTime)", "addDummyVar(name, recompile)",
                                 "addConst(name, value, recompile)", "compile(setTime)"))
        )
        shadertoy = sdk / "addons/ofxShadertoy/src/ofxShadertoy.cpp"
        shadertoy.parent.mkdir()
        shadertoy.write_text("long latestUpdate = std::filesystem::last_write_time(ofToDataPath(mainShaderFile));\n")
        render_sources = {
            "ofxPostProcessing/src/PostProcessing.cpp": (
                "glPushAttrib(GL_ENABLE_BIT);\n" * 3 + "glPopAttrib();\n" * 2 + "glDisable(GL_LIGHTING);\n"
            ),
            "ofxDeferredShading/src/DeferredProcessor.cpp": (
                "glPushAttrib(GL_ENABLE_BIT);\nglPopAttrib();\nglDisable(GL_LIGHTING);\n"
            ),
            "ofxDeferredShading/src/GBuffer.cpp": 'shader.setUniform1f("isShadow", 0);\n',
            "ofxDeferredShading/src/pass/SsaoPass.cpp": (
                'calcAo.setUniformTexture("colorTex", gbuffer.getTexture(GBuffer::TYPE_ALBEDO), 2);\n'
            ),
        }
        for name, text in render_sources.items():
            render_source = sdk / "addons" / name
            render_source.parent.mkdir(parents=True, exist_ok=True)
            render_source.write_text(text)
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
        patched = {"ofxExpr", "ofxMidi", "ofxPostProcessing", "ofxShadertoy"}
        assert set((stage / "addons.make").read_text().split()) == (retained - patched) | {f"addons/{name}" for name in patched}
        staged_midi = stage / "addons/ofxMidi/addon_config.mk"
        assert staged_midi.read_text() == (
            f"{midi_original}\nlinuxaarch64:\n{midi_settings}"
            "\tADDON_PKG_CONFIG_LIBRARIES += jack\n"
        )
        assert midi_config.read_bytes() == midi_original.encode("ascii")
        assert '#include "ofUtils.h"' in (stage / "addons/ofxExpr/src/ofxExprParser.hpp").read_text()
        assert '#include "ofUtils.h"' not in expr_header.read_text()
        assert not (stage / "src/operator").exists()
        assert not (stage / "src/macos").exists()
        assert (stage / "bin/data/shaders/direct.frag").is_file()
        assert not (stage / "bin/data/shaders/deferred").exists()
        assert list((stage / "bin/data/videos").iterdir()) == []
        assert not list(stage.rglob("*RmlUi*"))
        verify_example_stage(project / "examples", stage / "bin/data/examples")
        staged = run(["python3", "scripts/build_linux_minimal.py", "--of-root", str(sdk),
                      "--make", "vosc-test-make-must-not-run", "--stage-only"])
        assert staged.returncode == 0, staged
        message = "Staged VOSC minimal in "
        assert staged.stdout.startswith(message), staged
        stage_only = Path(staged.stdout.strip().removeprefix(message))
        assert stage_only != stage
        assert not (stage_only / "bin/vosc").exists()
        assert (stage_only / "bin/data/shaders/direct.frag").is_file()
        verify_example_stage(project / "examples", stage_only / "bin/data/examples")
        verify_desktop_selection(project, sdk, rules, run)
    print("PASS Linux minimal staging and desktop operator UI input selection with a fixture SDK")


if __name__ == "__main__":
    main()
