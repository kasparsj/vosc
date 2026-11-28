#!/usr/bin/env python3
"""Prepare or stage the opt-in Render Host GLSL translation tools."""

import argparse
import base64
import hashlib
import json
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tempfile
from urllib.request import urlopen


GLSLANG_VERSION = "15.1.0"
GLSLANG_REVISION = "1062752a891c95b2bfeed9e356562d88f9df84ac"
TINT_REVISION = "e4775a0d00343d02d32daecd45e1eb683224214b"
TOOLS = {"glslang": GLSLANG_VERSION, "tint": TINT_REVISION}
# The Linux SDK includes the SPIR-V reader. Build the macOS tool from the same Dawn revision.
LINUX_TINT_HASH = "ec54e73015e6e852d982c8bfa1cde8e657a9db61b9a857a09cae16a426792bb3"
PROBE_WGSL_HASH = "0e5428d5fce353955de8681bebce2e76bc8dc06464562f26fc341a80cf8025f2"
# Tint's statically linked dependencies come from the corresponding Dawn DEPS.
GITHUB = "https://raw.githubusercontent.com"
NOTICES = {
    "glslang.txt": (f"{GITHUB}/KhronosGroup/glslang/{GLSLANG_REVISION}/LICENSE.txt",
                    "adb783e734e906d1f46db5df29991dbde84bdb0ceab502ac2febb44fe3c2b5f4"),
    "dawn-tint.txt": (f"{GITHUB}/google/dawn/{TINT_REVISION}/LICENSE",
                      "1c43dfd13b306e5bac47839ea90cd1882249a75c92963ade400d4c1165e42228"),
    "dawn-glslang.txt": (f"{GITHUB}/KhronosGroup/glslang/291d83913ac47c8103daf7138d16bc8072efdce3/LICENSE.txt",
                         "17e70c676e1521ff3e4686f04a2053d93a7e28a33be8de7ec37ab0ff72feb677"),
    "spirv-headers.txt": (f"{GITHUB}/KhronosGroup/SPIRV-Headers/f6f78c717057ac7f5f3ea021f3f962aaf9d4da7e/LICENSE",
                          "ea43b1de38a6f90c488800d66dec1ed671e68cda530266bc96951fb5b6307613"),
    "spirv-tools.txt": (f"{GITHUB}/KhronosGroup/SPIRV-Tools/b1d2dc272aee6e6c9454d202fe88cbcbbeaab72c/LICENSE",
                        "7d52847928f607914a1218a7afd40d8d0fe1570dcf094205c276c8aa52d5a816"),
    "abseil.txt": ("https://chromium.googlesource.com/chromium/src/third_party/abseil-cpp/+/"
                   "fd9aa96c94f60aa86933ba121e4f7f009dcdf509/LICENSE?format=TEXT",
                   "c79a7fea0e3cac04cd43f20e7b648e5a0ff8fa5344e644b0ee09ca1162b62747"),
}
LINUX_LIBRARIES = {
    "ld-linux-x86-64.so.2", "libc.so.6", "libgcc_s.so.1", "libm.so.6", "libpthread.so.0",
    "librt.so.1", "libstdc++.so.6",
}


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def manifest(tools):
    return {"format": "vosc.webgpu-tools", "version": 1, **{
        name: {"file": name, "version": version, "sha256": digest(tools / name)}
        for name, version in TOOLS.items()
    }}


def write_manifest(tools):
    (tools / "manifest.json").write_text(json.dumps(manifest(tools), indent=2) + "\n", encoding="utf-8")


def run(*args, cwd="/"):
    result = subprocess.run(args, cwd=cwd, env={"PATH": "/usr/bin:/bin", "LC_ALL": "C"},
                            capture_output=True, text=True, check=False, timeout=15)
    if result.returncode != 0:
        raise RuntimeError(f"{args[0]} exited with {result.returncode}: {result.stderr or result.stdout}")
    return result.stdout


def check_binary(path, target):
    if target == "linux-x64":
        with path.open("rb") as binary:
            header = binary.read(20)
        if len(header) != 20 or header[:6] != b"\x7fELF\x02\x01" or header[18:20] != b"\x3e\x00":
            raise RuntimeError(f"{path.name} must be a Linux x86-64 ELF executable")
        dynamic = run("readelf", "-dW", path)
        dependencies = set(re.findall(r"\(NEEDED\).*?\[([^\]]+)\]", dynamic))
        interpreter = re.findall(r"Requesting program interpreter: ([^\]]+)\]", run("readelf", "-lW", path))
        if (dependencies - LINUX_LIBRARIES or "(RPATH)" in dynamic or "(RUNPATH)" in dynamic or
                interpreter not in ([], ["/lib64/ld-linux-x86-64.so.2"])):
            raise RuntimeError(f"{path.name} requires a loader or libraries outside the Ubuntu x86-64 runtime")
    else:
        if run("lipo", "-archs", path).split() != ["arm64"]:
            raise RuntimeError(f"{path.name} must be an arm64-only Mach-O executable")
        dependencies = run("otool", "-L", path).splitlines()[1:]
        if any(not line.strip().startswith(("/usr/lib/", "/System/Library/")) for line in dependencies):
            raise RuntimeError(f"{path.name} requires libraries outside the macOS system runtime")


def probe(tools):
    glslang, tint = (tools / name for name in TOOLS)
    version = run(glslang, "--version").splitlines()
    if not version or not re.fullmatch(r"Glslang Version: \d+:" + re.escape(GLSLANG_VERSION), version[0]):
        raise RuntimeError(f"glslang must report version {GLSLANG_VERSION}")
    with tempfile.TemporaryDirectory(prefix="vosc-webgpu-tools-") as scratch:
        directory = Path(scratch)
        source = directory / "probe.frag"
        spirv = directory / "probe.spv"
        source.write_text("#version 450\nlayout(location=0) out vec4 color;\n"
                          "void main() { color = vec4(1.0); }\n", encoding="utf-8")
        run(glslang, "-V", "-S", "frag", "-o", spirv, source, cwd=scratch)
        wgsl = run(tint, "--format", "wgsl", "--validate", "true", spirv, cwd=scratch)
        if hashlib.sha256(wgsl.encode()).hexdigest() != PROBE_WGSL_HASH:
            raise RuntimeError("The shader compilers did not reproduce the pinned WGSL probe output")


def regular(path):
    if path.is_symlink() or not path.is_file():
        raise RuntimeError(f"Expected a regular file at {path}")
    return path


def stage_tools(source, destination, target):
    source = source.resolve(strict=True)
    if destination.resolve().is_relative_to(source):
        raise RuntimeError("Choose a WebGPU tools destination outside the input directory")
    supplied = json.loads(regular(source / "manifest.json").read_text(encoding="utf-8"))
    for name in TOOLS:
        regular(source / name)
    if not isinstance(supplied, dict) or type(supplied.get("version")) is not int or supplied != manifest(source):
        raise RuntimeError("WebGPU tools manifest must identify the pinned tools and match their SHA-256 hashes")
    if target == "linux-x64" and supplied["tint"]["sha256"] != LINUX_TINT_HASH:
        raise RuntimeError("Tint must match the executable in the pinned Dawn SDK for this platform")
    licenses = source / "licenses"
    if licenses.is_symlink() or not licenses.is_dir():
        raise RuntimeError("Missing WebGPU tools licenses directory")
    destination.mkdir()
    try:
        for name in TOOLS:
            shutil.copyfile(source / name, destination / name)
            (destination / name).chmod(0o755)
            check_binary(destination / name, target)
        if manifest(destination) != supplied:
            raise RuntimeError("WebGPU tool bytes changed while staging")
        (destination / "licenses").mkdir()
        for name, (_, expected) in NOTICES.items():
            copied = destination / "licenses" / name
            shutil.copyfile(regular(licenses / name), copied)
            if digest(copied) != expected:
                raise RuntimeError(f"WebGPU license SHA-256 mismatch: {name}")
        probe(destination)
        write_manifest(destination)
    except Exception:
        shutil.rmtree(destination)
        raise


def prepare_tools(glslang, tint, destination, target):
    with tempfile.TemporaryDirectory(prefix="vosc-webgpu-input-") as scratch:
        source = Path(scratch)
        for name, path in (("glslang", glslang), ("tint", tint)):
            shutil.copyfile(path, source / name)
        write_manifest(source)
        (source / "licenses").mkdir()
        for name, (url, _) in NOTICES.items():
            with urlopen(url, timeout=20) as response:
                content = response.read()
            (source / "licenses" / name).write_bytes(base64.b64decode(content) if name == "abseil.txt" else content)
        stage_tools(source, destination, target)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    prepare = commands.add_parser("prepare", help="Prepare tools from pinned executables and fetch verified notices")
    prepare.add_argument("--glslang", type=Path, required=True)
    prepare.add_argument("--tint", type=Path, required=True)
    prepare.add_argument("--output", type=Path, required=True)
    stage = commands.add_parser("stage", help="Verify and copy a prepared Linux tools directory")
    stage.add_argument("source", type=Path)
    stage.add_argument("destination", type=Path)
    args = parser.parse_args()
    if args.command == "stage":
        stage_tools(args.source, args.destination, "linux-x64")
    else:
        target = {("Linux", "x86_64"): "linux-x64", ("Darwin", "arm64"): "macos-arm64"}.get(
            (platform.system(), platform.machine()))
        if target is None:
            parser.error("Prepare tools on Linux x86-64 or macOS arm64")
        prepare_tools(args.glslang, args.tint, args.output.resolve(), target)


if __name__ == "__main__":
    main()
