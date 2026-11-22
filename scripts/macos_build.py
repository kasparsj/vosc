"""Shared helpers for isolated macOS SDK preparation and builds."""

import hashlib
import json
import os
from pathlib import Path
import shutil


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


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_identity():
    paths = [path for path in (ROOT / "src").rglob("*")
             if path.suffix in (".cpp", ".h", ".hpp", ".mm", ".inl")]
    paths += [ROOT / "vendor/ofxOsc" / name for name in RECEIVER_BASELINES]
    return {str(path): digest(path) for path in sorted(paths)}


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
