#!/usr/bin/env python3
"""Verify real importer CLI reports and record sample native renders."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
    __package__ = "scripts.authoring"

from .acquire import acquire
from .import_shader import prepare, read_catalog
from .native_validation import NativeValidator


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from test_engine_api import png_pixels  # noqa: E402

UPSTREAM = "https://github.com/tado/100fragments"
PINNED = "c3bb9905f927de5be1b2357ee7522408cf7946d5"
MIXED_COMMIT = "c2a27407237728eb4fd2ddb8ad19f8a1b2a96e70"
COHORT = {f"{number:02}.frag" for number in (8, 11, 18, 19, 20, 26, 33, 43, 45, 50)}
MIXED_NATIVE = "tests/fixtures/feedback/gradient.frag"
NATIVE = b"""#version 330
uniform vec2 resolution;
out vec4 color;
void main() { color = vec4(gl_FragCoord.xy / resolution, 0.25, 1.0); }
"""
NATIVE_PASTE = NATIVE.replace(b"0.25", b"0.75")
SHADERTOY_RESPONSE = {"Shader": {"info": {"id": "Abc123"}, "renderpass": [{
    "name": "Image", "type": "image", "inputs": [],
    "code": "void mainImage(out vec4 color, in vec2 pixel) {\n"
            "    color = vec4(pixel / iResolution.xy, fract(iTime * 0.29), 1.0);\n}\n",
}]}}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def cli(output, label, arguments, *, stdin=None, status=0):
    report = output / f"{label}.json"
    command = [sys.executable, str(ROOT / "scripts/authoring/import_shader.py"), *arguments,
               "--report", str(report)]
    result = subprocess.run(command, input=stdin, capture_output=True, timeout=150)
    (output / f"{label}.stderr").write_bytes(result.stderr)
    require(result.returncode == status, f"{label}: {result.returncode}: {result.stderr!r}")
    payload = report.read_bytes()
    require(payload == result.stdout, f"{label}: report and stdout disagree")
    return json.loads(payload)


def validated_entries(report, library, commit):
    require(report["complete"] and report["origin"]["commit"] == commit, "wrong repository resolution")
    found = {}
    paths = [row["origin"]["path"] for row in report["files"]]
    require(paths == sorted(set(paths)), "candidate paths must be ordered and unique")
    entries = read_catalog(library)
    for row in report["files"]:
        path = row["origin"]["path"]
        if row["status"] == "skipped":
            require(row["code"] and row["message"], f"{path}: skipped without diagnostics")
            continue
        require(row["status"] == "published", f"{path}: unexpected outcome {row['status']}")
        entry = row["entry"]
        validation = row["validation"]
        target = library / "shaders" / entry["id"]
        require(entries[entry["id"]] == entry, f"{path}: manifest lost provenance")
        require(digest((target / "original.frag").read_bytes()) == entry["source_sha256"],
                f"{path}: original digest mismatch")
        require(digest((target / "generated.frag").read_bytes()) == entry["generated_sha256"],
                f"{path}: generated digest mismatch")
        require(validation["ok"] and validation["source_sha256"] == entry["generated_sha256"]
                and validation["receipt"]["status"] == "rendered" and validation["shader"]["program"] > 0,
                f"{path}: no matching native program")
        found[path] = target
    require(len(found) == len(entries), "catalog contains unaccounted entries")
    return found


def capture(validator, source, output, label, *, loaded=None):
    loaded = loaded or validator.validate(source)
    require(loaded["ok"] and loaded["shader"]["program"] > 0, f"{label}: native program failed: {loaded}")
    path = output / f"{label}.png"
    screenshot = validator.capture(path, expected_source_sha256=digest(source))
    require(screenshot["ok"], f"{label}: capture failed: {screenshot}")
    width, height, pixels = png_pixels(path.read_bytes())
    colors = len(set(pixels))
    require(colors > 1 and any(any(pixel) for pixel in pixels), f"{label}: capture has no visible pattern")
    return {"native": loaded, "capture": screenshot, "width": width, "height": height,
            "distinct_colors": colors, "center_pixel": pixels[(height // 2) * width + width // 2]}


def run(binary, evidence, output):
    pinned_library = evidence / "pinned-library"
    mixed_library = evidence / "mixed-library"
    pinned_report = json.loads((evidence / "pinned.json").read_bytes())
    mixed_report = json.loads((evidence / "mixed.json").read_bytes())
    pinned = validated_entries(pinned_report, pinned_library, PINNED)
    mixed = validated_entries(mixed_report, mixed_library, MIXED_COMMIT)
    require(len(pinned_report["files"]) == 101 and COHORT <= pinned.keys(), "pinned cohort incomplete")
    require(mixed_report["counts"]["skipped"] > 0 and MIXED_NATIVE in mixed,
            "second repository has no mixed outcome outside the upstream pack")

    local_path = output / "local.frag"
    local_path.write_bytes(NATIVE)
    library = output / "individual-library"
    options = ["--renderer", str(binary), "--library", str(library)]
    raw = f"https://raw.githubusercontent.com/tado/100fragments/{PINNED}/26.frag"
    blob = f"{UPSTREAM}/blob/{PINNED}/26.frag"
    local = cli(output, "local", ["file", str(local_path), *options])["files"][0]
    paste = cli(output, "paste", ["paste", *options], stdin=NATIVE_PASTE)["files"][0]
    remote = cli(output, "github-blob", ["github", blob, *options])["files"][0]
    by_kind = {"local": local, "paste": paste, "github": remote}
    catalog = read_catalog(library)
    for label, row in by_kind.items():
        require(row["status"] == "published" and row["validation"]["receipt"]["status"] == "rendered",
                f"{label}: no native CLI publication")
        entry = row["entry"]
        target = library / "shaders" / entry["id"]
        require(catalog[entry["id"]] == entry and
                digest((target / "original.frag").read_bytes()) == entry["source_sha256"] and
                digest((target / "generated.frag").read_bytes()) == entry["generated_sha256"],
                f"{label}: source or generated bytes lost")
    for row, source in ((local, NATIVE), (paste, NATIVE_PASTE)):
        require(row["entry"]["generated_sha256"] == digest(source), "native input changed during import")
    require(remote["entry"]["id"] == pinned["26.frag"].name, "repository and single-file identities differ")

    artist_copy = library / "shaders" / remote["entry"]["id"] / "shader.frag"
    edit = artist_copy.read_bytes() + b"\n// artist's edit\n"
    artist_copy.write_bytes(edit)
    repeat = cli(output, "github-raw-repeat", ["github", raw, *options])["files"][0]
    require(repeat["status"] == "reused_edited" and repeat["entry"] == remote["entry"]
            and artist_copy.read_bytes() == edit and read_catalog(library) == catalog,
            "reimport changed edited source, content identity or catalog")
    limited = cli(output, "limited-scan", ["repository", UPSTREAM, "--ref", PINNED,
                  "--archive-entries", "1", "--stage-only"], status=1)
    require(limited["complete"] is False and limited["error"]["code"] == "acquisition_limit",
            "limit must report an incomplete batch")

    with NativeValidator(binary) as validator:
        renders = {}
        for label, row in by_kind.items():
            source = library / "shaders" / row["entry"]["id"] / "generated.frag"
            renders[label] = capture(validator, source.read_bytes(), output, label)
        # Render an unrelated native GLSL file from the mixed repository.
        mixed_source = (mixed[MIXED_NATIVE] / "generated.frag").read_bytes()
        renders["mixed-native"] = capture(validator, mixed_source, output, "mixed-native")
        batch = acquire("shadertoy", "https://www.shadertoy.com/view/Abc123",
                        shadertoy=lambda shader_id, fetch: SHADERTOY_RESPONSE)
        item, = prepare(batch)
        source = item.conversion.source
        first = capture(validator, source, output, "shadertoy-fixture-before")
        time.sleep(0.4)
        second = capture(validator, source, output, "shadertoy-fixture-after", loaded=first["native"])
        require(first["center_pixel"] != second["center_pixel"], "Shadertoy fixture did not visibly animate")
        renders["shadertoy-fixture"] = [first, second]

    return {"IR-04": "pass", "IR-06": "pass", "IR-05": "live provider deferred; fixture native render",
            "IR-07": "actual scan limit and reimport; see unit.log for injected rollback and retries",
            "pinned": pinned_report["counts"], "mixed": mixed_report["counts"],
            "cohort": sorted(COHORT), "renders": renders}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--repository-evidence", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True)
    report = run(args.binary.resolve(strict=True), args.repository_evidence.resolve(strict=True), output)
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(output / "report.json")


if __name__ == "__main__":
    main()
