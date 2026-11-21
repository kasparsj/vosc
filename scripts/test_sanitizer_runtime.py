#!/usr/bin/env python3
"""Prove the selected sanitizer still fails closed on controlled defects."""

import argparse
import json
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sanitizer", choices=("address", "undefined"), required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    cases = ("heap", "uaf") if args.sanitizer == "address" else ("undefined",)
    with tempfile.TemporaryDirectory(prefix="vosc-sanitizer-probe-", dir="/tmp") as directory:
        binary = Path(directory) / "probe"
        flags = ["-std=c++17", f"-fsanitize={args.sanitizer}", "-fno-omit-frame-pointer"]
        compile_command = ["xcrun", "clang++", *flags,
                           str(ROOT / "tests/recovery/SanitizerProbe.cpp"), "-o", str(binary)]
        subprocess.run(compile_command, check=True)
        results = []
        for case in cases:
            run = subprocess.run([str(binary), case], text=True, stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT,
                                 env={"ASAN_OPTIONS": "abort_on_error=1:halt_on_error=1",
                                      "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"})
            marker = "AddressSanitizer" if args.sanitizer == "address" else "UndefinedBehaviorSanitizer"
            if run.returncode == 0 or marker not in run.stdout:
                raise RuntimeError({"case": case, "return_code": run.returncode, "output": run.stdout})
            results.append({"case": case, "return_code": run.returncode,
                            "marker": marker, "output": run.stdout})
    evidence = {"status": "PASS", "sanitizer": args.sanitizer,
                "compile_command": compile_command, "cases": results}
    if args.output:
        args.output.write_text(json.dumps(evidence, indent=2, sort_keys=True) + "\n")
    print(f"PASS {args.sanitizer} runtime negative controls: {', '.join(cases)}")


if __name__ == "__main__":
    main()
