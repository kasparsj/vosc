#!/usr/bin/env python3
"""Exercise the corrected receiver with real OF types and loopback UDP.

Uses the isolated application build's resolved settings and mandatory VFS overlay.
Each lifecycle case is a separate, bounded child; timeout is failure, never pass.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
CASES = ("immediate", "destruction", "idle", "active", "restart", "bind_failure",
         "launch_failure", "worker_failure")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def tokens(settings, key):
    value = settings.get(key, "")
    return value if isinstance(value, list) else shlex.split(value)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--of-root", type=Path, required=True)
    parser.add_argument("--build-output", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--sanitizers", choices=("address,undefined", "thread", "none"), default="address,undefined")
    parser.add_argument("--core-library", type=Path,
                        help="Explicit core override, required when changing core instrumentation")
    args = parser.parse_args()
    of_root, build, output = (value.resolve() for value in
                              (args.of_root, args.build_output, args.output))
    for protected in (ROOT, of_root, build):
        if output == protected or protected in output.parents:
            parser.error("Harness output must be outside checkout, shared OF and app build")
    marker = output / ".vosc-receiver-tests"
    if output.exists() and any(output.iterdir()) and (
            not marker.is_file() or marker.read_text() != str(ROOT)):
        parser.error("Refusing nonempty unowned output")
    output.mkdir(parents=True, exist_ok=True)
    marker.write_text(str(ROOT))
    (output / "results.json").unlink(missing_ok=True)
    inputs = json.loads((build / "build-inputs.json").read_text())
    settings = json.loads((build / "build-settings.json").read_text())
    overlay = Path(inputs.get("overlay", str(build / "filesystem-overlay.json"))).resolve()
    entries = {entry["name"]: Path(entry["external-contents"])
               for entry in json.loads(overlay.read_text())["roots"]}
    osc = of_root / "addons/ofxOsc"
    for name in ("ofxOscReceiver.h", "ofxOscReceiver.cpp"):
        virtual = osc / "src" / name
        replacement = ROOT / "vendor/ofxOsc" / name
        if str(virtual) not in entries or entries[str(virtual)].read_bytes() != replacement.read_bytes():
            raise RuntimeError(f"App overlay does not select this checkout's {name}")
    compile_flags = ["-std=c++17", "-O1", "-g", "-fno-omit-frame-pointer",
                     "-DVOSC_RECEIVER_LIFECYCLE_TESTING=1", "-ivfsoverlay", str(overlay)]
    for value in tokens(settings, "GCC_PREPROCESSOR_DEFINITIONS"):
        if value != "$(inherited)":
            compile_flags.append("-D" + value)
    for value in tokens(settings, "HEADER_SEARCH_PATHS"):
        if value == "$(inherited)":
            continue
        if "$" in value:
            raise RuntimeError(f"Unresolved header path: {value}")
        if value.endswith("/**"):
            parent = Path(value[:-3])
            paths = [parent, *sorted(path for path in parent.rglob("*") if path.is_dir())]
        else:
            paths = [Path(value)]
        compile_flags.extend("-I" + str(path) for path in paths)
    # Exact addon locations supplement Xcode's resolved headers, without stubs.
    compile_flags += ["-I" + str(osc / "src"),
                      "-I" + str(osc / "libs/oscpack/src/ip"),
                      "-I" + str(osc / "libs/oscpack/src/osc")]
    core = args.core_library.resolve() if args.core_library else Path(inputs["core_library"])
    if args.sanitizers == "thread" and not args.core_library:
        raise RuntimeError("TSan requires --core-library pointing to a non-ASan core")
    if not core.is_file():
        raise RuntimeError(f"Missing core archive: {core}")
    link_flags = [value for value in tokens(settings, "OTHER_LDFLAGS")
                  if value != "$(inherited)" and not value.startswith("-fsanitize=")
                  and Path(value).name not in ("openFrameworksDebug.a", "libfmod.dylib")]
    if any("$" in value for value in link_flags):
        raise RuntimeError("Unresolved linker flags")
    sources = [ROOT / "tests/osc/ReceiverLifecycleTests.cpp", osc / "src/ofxOscReceiver.cpp",
               osc / "src/ofxOscMessage.cpp", *sorted((osc / "libs/oscpack/src/ip/posix").glob("*.cpp")),
               osc / "libs/oscpack/src/ip/IpEndpointName.cpp",
               *sorted((osc / "libs/oscpack/src/osc").glob("*.cpp"))]
    binary = output / "bin/ReceiverLifecycleTests"
    binary.parent.mkdir(exist_ok=True)
    # The harness exercises no audio/UI APIs. Retain resolved link inputs but
    # do not force unused dynamic addon/audio dependencies into this CLI.
    link_flags.extend(["-Wl,-dead_strip", "-Wl,-dead_strip_dylibs"])
    sanitizer = [] if args.sanitizers == "none" else ["-fsanitize=" + args.sanitizers]
    command = ["xcrun", "clang++", *compile_flags, *sanitizer,
               *(str(path) for path in sources), str(core), *link_flags, *sanitizer,
               "-o", str(binary)]
    (output / "compile-command.json").write_text(json.dumps(command, indent=2) + "\n")
    with (output / "compile.log").open("w") as log:
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=180)
    environment = dict(os.environ, ASAN_OPTIONS="halt_on_error=1:abort_on_error=1",
                       UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1",
                       TSAN_OPTIONS="halt_on_error=1")
    results = []
    for case in CASES:
        try:
            run = subprocess.run([str(binary), case], env=environment, text=True,
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=20)
            result = {"case": case, "return_code": run.returncode, "timeout": False,
                      "passed": run.returncode == 0 and f"PASS {case}" in run.stdout}
            log = run.stdout
        except subprocess.TimeoutExpired as error:
            result = {"case": case, "return_code": None, "timeout": True, "passed": False}
            log = error.stdout or b""
            if isinstance(log, bytes):
                log = log.decode(errors="replace")
        (output / f"{case}.log").write_text(log)
        results.append(result)
        print(("PASS " if result["passed"] else "FAIL ") + case, flush=True)
    evidence = {"format": "vosc.receiver-lifecycle", "status": "PASS" if all(r["passed"] for r in results) else "FAIL",
                "sanitizers": args.sanitizers, "cases": results, "core_library": str(core),
                "core_sha256": digest(core), "overlay_sha256": digest(overlay),
                "receiver_header_sha256": digest(ROOT / "vendor/ofxOsc/ofxOscReceiver.h"),
                "source_sha256": {str(path): digest(entries.get(str(path), path)) for path in sources},
                "limitations": "Prebuilt core/third-party archives are not made instrumented by this harness."}
    (output / "results.json").write_text(json.dumps(evidence, indent=2) + "\n")
    return 0 if evidence["status"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
