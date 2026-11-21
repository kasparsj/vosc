#!/usr/bin/env python3
"""Compile and run actual overlaid OF color constructors with UBSAN."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from build_macos_legacy import ROOT, prepare_output, prepare_overlay, sdk_paths


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--of-root", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--expect-original-failure", action="store_true",
                        help="Compile without the overlay and require the known OF UBSAN failure")
    args = parser.parse_args()
    output = args.output.resolve()
    of_root, _ = sdk_paths(args.of_root)
    prepare_output(output, of_root)
    overlay = prepare_overlay(of_root, output) if not args.expect_original_failure else None
    binary = output / "ofcolor-constructor-tests"
    command = [
        "xcrun", "clang++", "-std=c++17", "-fsanitize=undefined",
        "-fno-sanitize-recover=undefined", "-fno-omit-frame-pointer",
        "-I", str(of_root / "libs/openFrameworks/types"),
        "-I", str(of_root / "libs/openFrameworks/utils"),
        "-I", str(of_root / "libs/glm/include"),
        "-I", str(of_root / "libs/glew/include"),
        "-I", str(of_root / "libs/tess2/include"),
        str(ROOT / "tests/color/OFColorConstructorTests.cpp"),
        str(of_root / "libs/openFrameworks/types/ofColor.cpp"),
        "-o", str(binary),
    ]
    if overlay is not None:
        command[6:6] = ["-ivfsoverlay", str(overlay)]
    (output / "compile-command.json").write_text(json.dumps(command, indent=2) + "\n")
    with (output / "compile.log").open("w") as stream:
        compiled = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT)
    evidence = {"mode": "original" if args.expect_original_failure else "overlaid",
                "compile_return_code": compiled.returncode, "status": "FAIL",
                "source_sha256": hashlib.sha256(
                    (of_root / "libs/openFrameworks/types/ofColor.cpp").read_bytes()).hexdigest()}
    if compiled.returncode:
        (output / "result.json").write_text(json.dumps(evidence, indent=2) + "\n")
        print((output / "compile.log").read_text())
        return compiled.returncode
    run_command = [str(binary)]
    (output / "run-command.json").write_text(json.dumps(run_command, indent=2) + "\n")
    with (output / "run.log").open("w") as stream:
        result = subprocess.run(run_command, stdout=stream, stderr=subprocess.STDOUT)
    run_log = (output / "run.log").read_text()
    evidence["run_return_code"] = result.returncode
    evidence["binary_sha256"] = hashlib.sha256(binary.read_bytes()).hexdigest()
    print(run_log, end="")
    if args.expect_original_failure:
        expected = "ofColor.cpp:173:7: runtime error:"
        reproduced = (result.returncode != 0 and expected in run_log
                      and "outside the range" in run_log and "of type 'int'" in run_log)
        evidence["status"] = "PASS" if reproduced else "FAIL"
        (output / "result.json").write_text(json.dumps(evidence, indent=2) + "\n")
        if not reproduced:
            print("FAIL original OF source did not reproduce the expected constructor UBSAN diagnostic")
            return 1
        print(f"PASS original OF source reproduced constructor UBSAN failure (exit {result.returncode})")
        return 0
    evidence["status"] = "PASS" if result.returncode == 0 else "FAIL"
    (output / "result.json").write_text(json.dumps(evidence, indent=2) + "\n")
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
