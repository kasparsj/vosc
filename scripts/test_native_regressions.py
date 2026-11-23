#!/usr/bin/env python3
"""Run the native shader reload and local image regressions."""

import argparse
import os
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True, help="Renderer executable")
    parser.add_argument("--cwd", type=Path, required=True, help="Staged runtime working directory")
    parser.add_argument("--output", type=Path, required=True, help="New directory for test evidence")
    args = parser.parse_args()
    binary, cwd = args.binary.resolve(), args.cwd.resolve()
    if not binary.is_file() or not os.access(binary, os.X_OK):
        parser.error(f"--binary must be an executable file: {binary}")
    if not cwd.is_dir():
        parser.error(f"--cwd must be a directory: {cwd}")
    try:
        args.output.mkdir(mode=0o700)
    except OSError as error:
        parser.error(f"cannot create --output: {error}")
    output = args.output.resolve()

    scripts = Path(__file__).resolve().parent
    shader_harness = scripts / "test_shader_reload_diagnostics.py"
    cases = (
        ("explicit", shader_harness, ("--case", "explicit")),
        ("watcher", shader_harness, ("--case", "watcher")),
        ("texture", shader_harness, ("--case", "texture")),
        ("local-image", scripts / "test_local_image_replacement.py", ()),
    )
    failed = False
    for name, harness, options in cases:
        case_dir = output / name
        case_dir.mkdir()
        log_path = case_dir / "harness.log"
        command = [
            sys.executable, "-u", str(harness), *options,
            "--binary", str(binary), "--cwd", str(cwd),
            "--output", str(case_dir / "artifacts"),
        ]
        with log_path.open("wb") as log:
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=False)
        if result.returncode == 0:
            print(f"PASS {name} ({case_dir})", flush=True)
        else:
            failed = True
            print(f"FAIL {name} (exit {result.returncode}, see {log_path})", flush=True)
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
