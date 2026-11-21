#!/usr/bin/env python3
"""Build this OF 0.11.1 app with existing Boost-backed libraries on modern Xcode.

Compiler overlays backport OF's filesystem namespace fix without modifying the
shared installation. This deliberately reuses the installed Debug core library;
it is not a clean rebuild of openFrameworks or its third-party libraries.
Namespace backport: https://github.com/openframeworks/openFrameworks/pull/7110
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]


def sdk_paths(of_root=None, addon_sdk=None):
    """Resolve CLI and environment overrides, then the local Xcode settings."""
    local = ROOT / "Project.local.xcconfig"
    settings = {}
    if local.is_file():
        for line in local.read_text().splitlines():
            key, sep, value = line.partition("=")
            if sep and key.strip() in ("OF_PATH", "VOSC_ADDON_SDK"):
                settings[key.strip()] = ROOT / value.strip()
    explicit_root = of_root or os.environ.get("OF_ROOT")
    sdk = explicit_root or settings.get("OF_PATH", ROOT / "../../..")
    addons = addon_sdk or os.environ.get("VOSC_ADDON_SDK")
    if not addons:
        addons = sdk if explicit_root else settings.get("VOSC_ADDON_SDK", sdk)
    # Keep aliases such as /tmp intact so Clang looks up the same paths in
    # the overlay that Xcode passed to the compiler.
    return tuple(Path(os.path.abspath(Path(path).expanduser())) for path in (sdk, addons))


RECEIVER_BASELINES = {
    "ofxOscReceiver.h": "fe010d0369bdb95330a84e1ac4b96485a6d5a4171f86060db43cc32833581931",
    "ofxOscReceiver.cpp": "73e69079b15b5948bc18b601d1d9a1ba5a56678c969badaf8279932be9516c6c",
}
OUTPUT_KEYS = (
    "TARGET_BUILD_DIR", "TARGET_TEMP_DIR", "DERIVED_FILE_DIR", "OBJECT_FILE_DIR",
    "BUILT_PRODUCTS_DIR", "PROJECT_TEMP_DIR", "CONFIGURATION_BUILD_DIR",
    "CONFIGURATION_TEMP_DIR", "OBJROOT", "SYMROOT", "SHARED_PRECOMPS_DIR",
    "CLANG_MODULE_CACHE_PATH", "DSTROOT", "CACHE_ROOT", "CCHROOT", "TEMP_ROOT",
    "DERIVED_FILES_DIR", "PROJECT_DERIVED_FILE_DIR",
)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify_output_settings(text, output):
    settings = {}
    for line in text.splitlines():
        if " = " in line:
            key, value = line.split(" = ", 1)
            settings[key.strip()] = value.strip()
    for key in OUTPUT_KEYS:
        value = settings.get(key)
        if not value or not Path(value).resolve().is_relative_to(output.resolve()):
            raise ValueError(f"Unsafe or unresolved build output: {key}={value}")
    return {key: settings[key] for key in OUTPUT_KEYS}


def invalidate_build_receipts(output):
    for name in ("native-build-receipt.json", "preservation.json"):
        (output / name).unlink(missing_ok=True)


def source_identity():
    paths = [path for path in (ROOT / "src").rglob("*")
             if path.suffix in (".cpp", ".h", ".hpp", ".mm", ".inl")]
    paths += [ROOT / "vendor/ofxOsc" / name for name in RECEIVER_BASELINES]
    return {str(path): digest(path) for path in sorted(paths)}


def write_build_receipt(output, library, receiver, sources, sanitizers, recovery_test_hooks):
    if source_identity() != sources:
        raise RuntimeError("Application sources changed during the private build; rebuild required")
    binary = output / "products/voscDebug.app/Contents/MacOS/voscDebug"
    receipt = {
        "format": "vosc.native-build", "version": 1, "status": "built",
        "binary": {"path": str(binary), "sha256": digest(binary)},
        "core": {"path": str(library), "sha256": digest(library)},
        "receiver_inputs": receiver, "source_sha256": sources,
        "sanitizers": list(dict.fromkeys(sanitizers)),
        "recovery_test_hooks": recovery_test_hooks,
    }
    (output / "native-build-receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    return receipt


def receiver_inputs(of_root):
    inputs = []
    for name, expected in RECEIVER_BASELINES.items():
        source = of_root / "addons/ofxOsc/src" / name
        replacement = ROOT / "vendor/ofxOsc" / name
        if not source.is_file() or digest(source) != expected:
            raise ValueError(f"Unexpected shared receiver baseline: {source}")
        if not replacement.is_file():
            raise ValueError(f"Missing repository receiver replacement: {replacement}")
        inputs.append({"source": str(source), "source_sha256": expected,
                       "replacement": str(replacement), "replacement_sha256": digest(replacement)})
    return inputs


def prepare_project(output):
    """Copy project metadata; all app inputs still resolve against this checkout."""
    project_root = output / "project"
    project = project_root / "vosc.xcodeproj"
    shutil.copytree(ROOT / "vosc.xcodeproj", project, dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns("xcuserdata", "*.xcuserstate"))
    # Xcode resolves INFOPLIST_FILE against PROJECT_DIR rather than the
    # command-line SRCROOT, so materialize this project metadata privately.
    shutil.copy2(ROOT / "openFrameworks-Info.plist", project_root / "openFrameworks-Info.plist")
    config = project_root / "Project.xcconfig"
    shutil.copy2(ROOT / "scripts/OpenFrameworks011.xcconfig", config)
    pbx = project / "project.pbxproj"
    text = (ROOT / "vosc.xcodeproj/project.pbxproj").read_text()
    text = text.replace("path = Project.xcconfig;", f'path = "{config}";')
    # SOURCE_ROOT and <group> roots resolve from PROJECT_DIR in Xcode's
    # project graph, independently of the command-line SRCROOT override.
    text = re.sub(r"path = (src(?:/[^;]+)?|bin/data|Rules);",
                  lambda match: "path = " + json.dumps(str(ROOT / match.group(1))) + ";", text)
    text = re.sub(r"^(\s+)(src(?:/[^,\n]+)?),$",
                  lambda match: match.group(1) + json.dumps(str(ROOT / match.group(2))) + ",",
                  text, flags=re.MULTILINE)
    pbx.write_text(text)
    return project


def copy_data(of_root, output, *, include_videos=True):
    """Materialize writable assets; runtime tests must never follow source links."""
    data = output / "products/data"
    if data.is_symlink():
        data.unlink()
    data.mkdir(parents=True, exist_ok=True)
    # Refresh only known assets inside this marked output directory.
    for source in (ROOT / "bin/data").iterdir():
        if source.name == "videos" and not include_videos:
            (data / "videos").mkdir(exist_ok=True)
            continue
        target = data / source.name
        if target.is_symlink():
            target.unlink()
        if source.is_dir():
            # Older builds used links at nested levels as well.
            if target.exists() and any(path.is_symlink() for path in target.rglob("*")):
                raise ValueError(f"Refusing linked existing asset tree; use a fresh output: {target}")
            shutil.copytree(source, target, dirs_exist_ok=True)
        else:
            shutil.copy2(source, target)
    deferred = data / "shaders/deferred"
    if not deferred.exists():
        shutil.copytree(of_root / "addons/ofxDeferredShading/data/shaders/deferred", deferred)
    return data


def replace_expected(text, original, replacement, description):
    """Replace one compatibility anchor, failing closed if OF sources drift."""
    count = text.count(original)
    if count != 1:
        raise ValueError(f"Expected exactly one {description} anchor, found {count}")
    return text.replace(original, replacement)


def patch_ofcolor(source, text):
    if source.name == "ofColor.h" and source.parent.name == "types":
        original = """    ofColor_():
        r(limit()),
        g(limit()),
        b(limit()),
        a(limit()) {};
"""
        replacement = """    ofColor_() {
        if constexpr (std::numeric_limits<PixelType>::is_integer) {
            r = g = b = a = std::numeric_limits<PixelType>::max();
        } else {
            r = g = b = a = limit();
        }
    };
"""
        return replace_expected(text, original, replacement, "ofColor default constructor")
    if source.name == "ofColor.cpp" and source.parent.name == "types":
        constructors = """template<typename PixelType>
ofColor_<PixelType>::ofColor_(float _r, float _g, float _b, float _a):
    r(_r),
    g(_g),
    b(_b),
    a(_a){
}

template<typename PixelType>
ofColor_<PixelType>::ofColor_ (const ofColor_<PixelType>& color, float _a):
    r(color.r),
    g(color.g),
    b(color.b),
    a(_a){
}

template<typename PixelType>
ofColor_<PixelType>::ofColor_(float gray, float _a):
    r(gray),
    g(gray),
    b(gray),
    a(_a){
}
"""
        safe_constructors = """template<typename PixelType>
PixelType ofColorChannelCast(float value) {
    if constexpr (std::numeric_limits<PixelType>::is_integer) {
        if (std::isnan(value)) {
            return 0;
        }
        const long double wideValue = value;
        if (wideValue >= static_cast<long double>(std::numeric_limits<PixelType>::max())) {
            return std::numeric_limits<PixelType>::max();
        }
        if (wideValue <= static_cast<long double>(std::numeric_limits<PixelType>::lowest())) {
            return std::numeric_limits<PixelType>::lowest();
        }
    }
    return static_cast<PixelType>(value);
}

template<typename PixelType>
ofColor_<PixelType>::ofColor_(float _r, float _g, float _b, float _a):
    r(ofColorChannelCast<PixelType>(_r)),
    g(ofColorChannelCast<PixelType>(_g)),
    b(ofColorChannelCast<PixelType>(_b)),
    a(ofColorChannelCast<PixelType>(_a)){
}

template<typename PixelType>
ofColor_<PixelType>::ofColor_ (const ofColor_<PixelType>& color, float _a):
    r(color.r),
    g(color.g),
    b(color.b),
    a(ofColorChannelCast<PixelType>(_a)){
}

template<typename PixelType>
ofColor_<PixelType>::ofColor_(float gray, float _a):
    r(ofColorChannelCast<PixelType>(gray)),
    g(ofColorChannelCast<PixelType>(gray)),
    b(ofColorChannelCast<PixelType>(gray)),
    a(ofColorChannelCast<PixelType>(_a)){
}
"""
        text = replace_expected(text, constructors, safe_constructors, "ofColor channel constructors")
        return replace_expected(text, '#include "ofConstants.h"\n',
                                '#include "ofConstants.h"\n#include <cmath>\n',
                                "ofColor include")
    return text


def path_within(path, root):
    """Include aliases of existing directories, such as case variants on APFS."""
    path, root = path.resolve(), root.resolve()
    if path.is_relative_to(root):
        return True
    if root.exists():
        for ancestor in (path, *path.parents):
            try:
                if ancestor.samefile(root):
                    return True
            except (FileNotFoundError, NotADirectoryError):
                continue
    return False


def validate_output_path(output, *of_roots):
    for protected in (ROOT, *of_roots):
        if path_within(output, protected):
            raise ValueError("Build output must be outside the checkout and shared OF installation")


def prepare_output(output, *of_roots):
    validate_output_path(output, *of_roots)
    marker = output / ".vosc-legacy-build"
    if output.exists() and any(output.iterdir()):
        if not marker.is_file() or marker.read_text() != str(ROOT):
            raise ValueError("Refusing a nonempty output directory without this project's build marker")
    output.mkdir(parents=True, exist_ok=True)
    marker.write_text(str(ROOT))


def patch_imgui_glfw(text):
    return text.replace(
        "const ImDrawIdx* idx_buffer_offset = 0;", "uintptr_t idx_buffer_offset = 0;",
    ).replace(
        "GL_UNSIGNED_INT, idx_buffer_offset);",
        "GL_UNSIGNED_INT, reinterpret_cast<const void*>(idx_buffer_offset));",
    ).replace(
        "idx_buffer_offset += pcmd->ElemCount;",
        "idx_buffer_offset += pcmd->ElemCount * sizeof(ImDrawIdx);",
    )


def write_overlay(output, entries):
    # Dependency files name the virtual installed paths. Change the compiler
    # command whenever replacement bytes change, so an incremental Xcode build
    # cannot reuse objects compiled from an earlier repository receiver.
    identity = [(entry["name"], digest(Path(entry["external-contents"])))
                for entry in entries]
    fingerprint = hashlib.sha256(json.dumps(identity).encode()).hexdigest()
    overlay = output / f"filesystem-overlay-{fingerprint}.json"
    content = json.dumps({"version": 0, "use-external-names": False,
                          "roots": entries}, indent=2)
    if not overlay.exists() or overlay.read_text() != content:
        overlay.write_text(content)
    return overlay


def prepare_overlay(of_root, output, addon_sdk=None):
    core = of_root / "libs/openFrameworks"
    constants = (core / "utils/ofConstants.h").read_bytes().decode("latin-1")
    for name, value in (("MAJOR", 0), ("MINOR", 11), ("PATCH", 1)):
        if not re.search(rf"#define OF_VERSION_{name}\s+{value}\b", constants):
            raise ValueError("This compatibility build requires openFrameworks 0.11.1")
    addon_sdk = addon_sdk or of_root
    roots = [(of_root, core)]
    for addon in (ROOT / "addons.make").read_text().splitlines():
        if addon.strip() and not addon.startswith("#"):
            roots.append((addon_sdk, addon_sdk / "addons" / addon.strip()))
    inputs = receiver_inputs(addon_sdk)
    receiver_replacements = {item["source"]: item["replacement"] for item in inputs}
    entries = []
    for sdk, source_root in roots:
        for source in sorted(source_root.rglob("*")):
            if source.suffix not in (".h", ".hpp", ".inl", ".cpp", ".mm"):
                continue
            text = source.read_bytes().decode("latin-1")
            if str(source) in receiver_replacements:
                entries.append({"type": "file", "name": str(source),
                                "external-contents": receiver_replacements[str(source)]})
                continue
            patched = re.sub(
                r"namespace std\s*\{\s*namespace filesystem = boost::filesystem;\s*\}",
                "namespace of { namespace filesystem = boost::filesystem; }",
                text,
            ).replace("std::filesystem", "of::filesystem")
            if source.name == "ofFileUtils.cpp":
                patched = re.sub(r"(?<![\w:])filesystem::", "of::filesystem::", patched)
            patched = patch_ofcolor(source, patched)
            if source.name == "EngineGLFW.cpp" and source.parent.name == "src" and source.parent.parent.name == "ofxImGui":
                patched = patch_imgui_glfw(patched)
            if patched == text:
                continue
            target = output / "overlay" / source.relative_to(sdk)
            target.parent.mkdir(parents=True, exist_ok=True)
            content = patched.encode("latin-1")
            if not target.exists() or target.read_bytes() != content:
                target.write_bytes(content)
            entries.append({"type": "file", "name": str(source),
                            "external-contents": str(target)})
    (output / "receiver-inputs.json").write_text(json.dumps(inputs, indent=2) + "\n")
    return write_overlay(output, entries)


def build_command(of_root, output, overlay, frameworks, sanitizers=(), recovery_test_hooks=False,
                  core_library=None, project=None):
    compile_flags = ["$(inherited)", "-ivfsoverlay", f'"{overlay}"']
    link_flags = ["$(inherited)"]
    if recovery_test_hooks:
        compile_flags.append("-DVOSC_RECOVERY_TESTING=1")
    if sanitizers:
        sanitizer_flag = "-fsanitize=" + ",".join(sanitizers)
        compile_flags.extend([sanitizer_flag, "-fno-omit-frame-pointer"])
        link_flags.append(sanitizer_flag)
    command = [
        "xcodebuild", "-project", str(project or output / "project/vosc.xcodeproj"), "-scheme", "vosc Debug",
        "-configuration", "Debug", "-derivedDataPath", str(output / "derived"),
        f"SRCROOT={ROOT}", f"CONFIGURATION_BUILD_DIR={output / 'products'}", "MACOSX_DEPLOYMENT_TARGET=12.6",
        "ARCHS=arm64", "ONLY_ACTIVE_ARCH=YES", "CODE_SIGNING_ALLOWED=NO",
        "CLANG_CXX_LANGUAGE_STANDARD=c++17",
        "VOSC_USE_PREBUILT_OF=YES", "VOSC_COMPAT_CXXFLAGS=", f"OF_PATH={of_root}",
        f"VOSC_ADDON_SDK={of_root}",
        f"OF_CORE_FRAMEWORKS={frameworks}",
        "OTHER_CPLUSPLUSFLAGS=" + " ".join(compile_flags),
        "OTHER_LDFLAGS=" + " ".join(link_flags),
        "build",
    ]
    for key, name in {"SYMROOT": "sym", "OBJROOT": "obj", "CONFIGURATION_TEMP_DIR": "temp",
                      "SHARED_PRECOMPS_DIR": "precompiled", "CLANG_MODULE_CACHE_PATH": "modules",
                      "CACHE_ROOT": "cache", "DSTROOT": "dst"}.items():
        command.insert(-1, f"{key}={output / name}")
    if core_library is not None:
        command.insert(-1, f"LIB_OF_DEBUG={core_library}")
    return command


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--of-root", type=Path)
    parser.add_argument("--output", type=Path, required=True,
                        help="Dedicated build directory (outside the checkout)")
    parser.add_argument("--sanitizers", choices=("address", "undefined"), action="append", default=[],
                        help="Instrument app sources and link the app with the selected sanitizer")
    parser.add_argument("--recovery-test-hooks", action="store_true",
                        help="Compile the private VOSC_RECOVERY_TESTING native harness seam")
    parser.add_argument("--core-library", type=Path,
                        help="Explicit private openFrameworks core archive (default: installed prebuilt core)")
    args = parser.parse_args()
    of_root, _ = sdk_paths(args.of_root)
    output = args.output.resolve()
    prepare_output(output, of_root)
    invalidate_build_receipts(output)
    library = args.core_library.resolve() if args.core_library else of_root / "libs/openFrameworksCompiled/lib/osx/openFrameworksDebug.a"
    if not library.is_file():
        parser.error(f"Missing prebuilt OF Debug library: {library}")
    overlay = prepare_overlay(of_root, output)
    core_config = (of_root / "libs/openFrameworksCompiled/project/osx/CoreOF.xcconfig").read_text()
    frameworks = re.search(r"^OF_CORE_FRAMEWORKS = (.+)$", core_config, re.MULTILINE).group(1)
    # The GLFW/AVFoundation runtime does not use these removed SDK frameworks.
    frameworks = frameworks.replace("-framework AGL", "").replace("-framework QTKit", "")
    products = output / "products"
    copy_data(of_root, output)
    project = prepare_project(output)
    command = build_command(of_root, output, overlay, frameworks,
                            tuple(dict.fromkeys(args.sanitizers)), args.recovery_test_hooks,
                            library, project)
    print("Reusing prebuilt core:", library, flush=True)
    print("Compiler overlay:", overlay, flush=True)
    print("Recovery test hooks:", "enabled" if args.recovery_test_hooks else "disabled", flush=True)
    print("App sanitizers:", ",".join(dict.fromkeys(args.sanitizers)) or "none", flush=True)
    log = output / "build.log"
    (output / "command.json").write_text(json.dumps(command, indent=2) + "\n")
    settings = subprocess.run(command[:-1] + ["-showBuildSettings"], cwd=ROOT,
                              capture_output=True, text=True)
    (output / "settings.log").write_text(settings.stdout + settings.stderr)
    settings.check_returncode()
    verified = verify_output_settings(settings.stdout, output)
    resolved_settings = {}
    for line in settings.stdout.splitlines():
        if " = " in line:
            key, value = line.split(" = ", 1)
            resolved_settings[key.strip()] = value.strip()
    (output / "build-settings.json").write_text(json.dumps(resolved_settings, indent=2) + "\n")
    shared = [of_root / "addons/ofxOsc/src" / name for name in RECEIVER_BASELINES]
    shared += [of_root / "addons/ofxOsc/libs/oscpack/src/ip/posix/UdpSocket.cpp", library]
    before = {str(path): digest(path) for path in shared}
    sources = source_identity()
    receiver = receiver_inputs(of_root)
    (output / "build-inputs.json").write_text(json.dumps({
        "of_root": str(of_root), "checkout": str(ROOT), "overlay": str(overlay),
        "project": str(project), "core_library": str(library),
        "verified_output_paths": verified, "shared_before": before,
        "source_sha256": sources, "receiver_inputs": receiver,
    }, indent=2) + "\n")
    with log.open("w") as stream:
        result = subprocess.run(command, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT)
    after = {str(path): digest(path) for path in shared}
    (output / "preservation.json").write_text(json.dumps({
        "before": before, "after": after, "unchanged": before == after,
        "return_code": result.returncode}, indent=2) + "\n")
    if before != after:
        raise RuntimeError("Shared build inputs changed during the private app build")
    print("Build log:", log)
    if result.returncode:
        for line in log.read_text().splitlines():
            if "error:" in line or "BUILD FAILED" in line:
                print(line)
    else:
        write_build_receipt(output, library, receiver, sources, args.sanitizers,
                            args.recovery_test_hooks)
        print("Built:", products / "voscDebug.app")
    return result.returncode


if __name__ == "__main__":
    sys.exit(main())
