#!/usr/bin/env python3
"""Build an ordinary authoring app against its corrected private OF core.

Builds only into an explicit owned directory; never installs, launches or replaces
the active authoring release. Existing sanitizer helpers remain separate.
"""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from build_macos_legacy import ROOT, copy_data, prepare_output, sdk_paths, source_identity
from build_macos_profile import app_build_command, core_archive, sdk_revision, sdk_version


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--of-root", type=Path)
    parser.add_argument("--addon-sdk", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--recovery-test-hooks", action="store_true")
    parser.add_argument("--without-videos", action="store_true")
    args = parser.parse_args()
    output = args.output.expanduser().resolve()
    sdk, addon_sdk = sdk_paths(args.of_root, args.addon_sdk)
    prepare_output(output, sdk, addon_sdk)
    manifest_path = output / "build-manifest.json"
    # An unsuccessful rebuild must not leave a prior success receipt current.
    manifest_path.unlink(missing_ok=True)
    try:
        version = sdk_version(sdk)
    except ValueError as error:
        parser.error(str(error))
    core_dir = output / f"core-of-{version}"
    command = app_build_command(output, "Debug", sdk, addon_sdk, version=version)
    command.append(f"VOSC_COMPAT_BUILD_DIR={core_dir}")
    if args.recovery_test_hooks:
        command.append("GCC_PREPROCESSOR_DEFINITIONS=$(inherited) VOSC_RECOVERY_TESTING=1")
    command.append("build")
    sources = source_identity()
    subprocess.run(command, cwd=ROOT, check=True)
    if sources != source_identity():
        raise RuntimeError("Application sources changed during the private build")
    copy_data(addon_sdk, output, include_videos=not args.without_videos)
    core = core_archive(core_dir, "Debug", version)
    binary = output / "products/voscDebug.app/Contents/MacOS/voscDebug"
    manifest = {
        "format": "vosc.authoring-build", "version": 1, "status": "built",
        "requested_sanitizers": [], "requested_recovery_test_hooks": args.recovery_test_hooks,
        "commands": [command],
        "source_sha256": sources,
        "sdk_version": version, "sdk_revision": sdk_revision(sdk),
        "included_videos": not args.without_videos,
        "core": {"path": str(core), "sha256": hashlib.sha256(core.read_bytes()).hexdigest()},
        "binary": {"path": str(binary), "sha256": hashlib.sha256(binary.read_bytes()).hexdigest()},
    }
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    print("Authoring build manifest:", manifest_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
