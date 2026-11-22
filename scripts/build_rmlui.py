#!/usr/bin/env python3
"""Prepare the pinned RmlUi core, GL3 renderer, and embedded operator font."""

import argparse
import fcntl
import hashlib
from pathlib import Path
import subprocess
import tempfile


VERSION = "6.3"
REVISION = "ba95ffe8bfb6370efb2cdcca927eaad4710c5413"
ARCHIVE_SHA256 = "1541ef5577115e9368f8ed389b29f0925ef6572f326a33d378ea16c3cfa2cde8"
ARCHIVE_URL = f"https://codeload.github.com/mikke89/RmlUi/tar.gz/{REVISION}"
DRIVER = Path(__file__).resolve().parent / "operator-ui"


def source_tree(output):
    source = output / f"RmlUi-{ARCHIVE_SHA256}"
    if not source.is_dir():
        with tempfile.TemporaryDirectory(prefix=".fetch-", dir=output) as temporary:
            scratch = Path(temporary)
            archive = scratch / "RmlUi.tar.gz"
            payload = scratch / "source"
            subprocess.run([
                "curl", "--fail", "--location", "--silent", "--show-error", "--retry", "2",
                "--connect-timeout", "20", "--max-time", "120", "--output", str(archive), ARCHIVE_URL,
            ], check=True)
            if hashlib.sha256(archive.read_bytes()).hexdigest() != ARCHIVE_SHA256:
                raise ValueError(f"RmlUi {VERSION} archive SHA-256 mismatch")
            payload.mkdir()
            subprocess.run([
                "tar", "-xf", str(archive), "-C", str(payload), "--strip-components", "1",
                "--no-same-owner", "--no-same-permissions",
            ], check=True)
            payload.rename(source)
    include_source = output / "source"
    if not include_source.is_symlink() or include_source.readlink() != Path(source.name):
        temporary_link = output / ".source"
        temporary_link.unlink(missing_ok=True)
        temporary_link.symlink_to(source.name, target_is_directory=True)
        temporary_link.replace(include_source)
    return source


def write_font(source, output):
    encoded = (
        f"// Lato Latin from RmlUi {VERSION}, {REVISION}.\n"
        "// Copyright 2010-2015 Łukasz Dziedzic. SIL Open Font License 1.1.\n"
        "#pragma once\n"
    )
    for weight in ("Regular", "Bold"):
        data = (source / f"Samples/assets/LatoLatin-{weight}.ttf").read_bytes()
        lines = (", ".join(f"0x{byte:02x}" for byte in data[i:i+16]) for i in range(0, len(data), 16))
        encoded += (
            f"inline constexpr unsigned char voscOperatorFont{weight}[] = {{\n    "
            + ",\n    ".join(lines) + "\n};\n"
        )
    directory = output / "generated"
    directory.mkdir(exist_ok=True)
    header = directory / "VoscOperatorFont.hpp"
    if not header.exists() or header.read_text(encoding="utf-8") != encoded:
        temporary_header = header.with_suffix(".tmp")
        temporary_header.write_text(encoded, encoding="utf-8")
        temporary_header.replace(header)


def build(output, *, of_root=None, architectures="", deployment_target="", sysroot=""):
    output.mkdir(parents=True, exist_ok=True)
    with (output / ".build.lock").open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        source = source_tree(output)
        build_directory = output / "build"
        command = [
            "cmake", "-S", str(DRIVER), "-B", str(build_directory),
            "-DCMAKE_BUILD_TYPE=Release", f"-DRMLUI_SOURCE={source}",
            f"-DRMLUI_ARCHIVES={output / 'lib'}",
        ]
        if of_root is not None:
            free_type = of_root / "libs/freetype/lib/macos/freetype.xcframework/macos-arm64_x86_64"
            command.extend([
                f"-DFREETYPE_LIBRARY_RELEASE={free_type / 'libfreetype.a'}",
                f"-DFREETYPE_INCLUDE_DIR_ft2build={free_type / 'Headers'}",
                f"-DFREETYPE_INCLUDE_DIR_freetype2={free_type / 'Headers'}",
                f"-DCMAKE_OSX_ARCHITECTURES={';'.join(architectures.split())}",
                f"-DCMAKE_OSX_DEPLOYMENT_TARGET={deployment_target}",
                f"-DCMAKE_OSX_SYSROOT={sysroot}",
            ])
        subprocess.run(command, check=True)
        subprocess.run([
            "cmake", "--build", str(build_directory), "--target", "vosc_rmlui_gl3", "--parallel", "6",
        ], check=True)
        write_font(source, output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        build(args.output.resolve())
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"{parser.prog}: {error}\n")


if __name__ == "__main__":
    main()
