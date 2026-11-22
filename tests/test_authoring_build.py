import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


SCRIPTS = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPTS))
import build_macos_profile as profile

spec = importlib.util.spec_from_file_location("authoring_build", SCRIPTS / "build_macos_authoring.py")
authoring = importlib.util.module_from_spec(spec)
spec.loader.exec_module(authoring)


class AuthoringBuildTests(unittest.TestCase):
    def test_nested_default_and_local_sdk_override(self):
        with tempfile.TemporaryDirectory() as directory:
            sdk = Path(directory).resolve() / "sdk"
            checkout = sdk / "apps/myApps/vosc"
            checkout.mkdir(parents=True)
            local = checkout / "Project.local.xcconfig"
            with patch("macos_build.ROOT", checkout), patch.dict("os.environ", clear=True):
                self.assertEqual(authoring.sdk_paths(), (sdk, sdk))
                local.write_text("OF_PATH = alternate sdk\nVOSC_ADDON_SDK = alternate addons\n")
                self.assertEqual(authoring.sdk_paths(),
                                 (checkout / "alternate sdk", checkout / "alternate addons"))
                self.assertEqual(authoring.sdk_paths(sdk), (sdk, sdk))

    def test_supported_sdk_uses_the_standard_debug_scheme(self):
        with tempfile.TemporaryDirectory() as directory:
            sdk = Path(directory).resolve() / "of-0.12.1"
            header = sdk / "libs/openFrameworks/utils/ofConstants.h"
            header.parent.mkdir(parents=True)
            header.write_text("#define OF_VERSION_MAJOR 0\n#define OF_VERSION_MINOR 12\n"
                              "#define OF_VERSION_PATCH 1\n")
            args = ["builder", "--of-root", str(sdk), "--output", str(sdk.parent / "build")]
            with (patch.object(sys, "argv", args), patch.dict("os.environ", clear=True),
                  patch.object(authoring.subprocess, "run",
                               side_effect=subprocess.CalledProcessError(1, "build")) as run):
                with self.assertRaises(subprocess.CalledProcessError):
                    authoring.main()
            command = run.call_args.args[0]
            self.assertIn(f"OF_PATH={sdk}", command)
            self.assertIn(f"VOSC_ADDON_SDK={sdk}", command)
            self.assertIn("vosc Debug", command)
            self.assertIn('scripts/OpenFrameworks012.xcconfig',
                          (SCRIPTS.parent / "Project.xcconfig").read_text())

    def test_in_place_clean_does_not_require_an_installed_sdk(self):
        sdk = "/nonexistent/vosc-sdk"
        argv = ["builder", "--of-root", sdk, "--in-place", "--clean",
                "--configuration", "Release", "--", "-quiet"]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            products = root / "bin"
            for name in ("data", "voscDebug.app", "vosc.app"):
                (products / name).mkdir(parents=True)
            with (patch.object(sys, "argv", argv),
                  patch.object(profile, "ROOT", root),
                  patch.object(profile, "sdk_version") as version,
                  patch.object(profile.subprocess, "run") as run):
                profile.main()
            self.assertEqual({path.name for path in products.iterdir()}, {"data", "voscDebug.app"})
        version.assert_not_called()
        command = run.call_args.args[0]
        self.assertIn(f"OF_PATH={sdk}", command)
        self.assertIn("vosc Release", command)
        self.assertIn("CONFIGURATION_BUILD_DIR=$(BUILD_DIR)/$(CONFIGURATION)", command)
        self.assertNotIn("-xcconfig", command)
        self.assertEqual(command[-2:], ["-quiet", "clean"])

    def test_build_failure_does_not_keep_the_prior_success_manifest(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory).resolve()
            authoring.prepare_output(output, output / "of")
            manifest = output / "build-manifest.json"
            manifest.write_text('{"status":"built"}')
            with patch.object(sys, "argv", ["builder", "--of-root", str(output / "of"), "--output", str(output)]), \
                    patch.object(authoring, "sdk_version", return_value="0.12.1"), \
                    patch.object(authoring.subprocess, "run", side_effect=subprocess.CalledProcessError(1, "build")) as run, \
                    patch.object(authoring, "copy_data") as copy_data:
                with self.assertRaises(subprocess.CalledProcessError):
                    authoring.main()
            self.assertEqual(run.call_count, 1)
            copy_data.assert_not_called()
            self.assertFalse(manifest.exists())

    def test_success_binds_app_to_the_built_core_and_hashes_artifacts(self):
        for recovery in (False, True):
            with self.subTest(recovery=recovery), tempfile.TemporaryDirectory() as directory:
                output = Path(directory).resolve()
                core_dir = output / "core-of-0.12.1"
                core = authoring.core_archive(core_dir, "Debug")
                sdk = output / "explicit-sdk"
                addon_sdk = output / "explicit-addon-sdk"
                binary = output / "products/voscDebug.app/Contents/MacOS/voscDebug"

                def build(command, **kwargs):
                    self.assertIn(f"OF_PATH={sdk}", command)
                    self.assertIn(f"VOSC_ADDON_SDK={addon_sdk}", command)
                    self.assertIn(f"VOSC_COMPAT_BUILD_DIR={core_dir}", command)
                    self.assertEqual(any("VOSC_RECOVERY_TESTING=1" in arg for arg in command), recovery)
                    for artifact, content in ((core, b"core"), (binary, b"app")):
                        artifact.parent.mkdir(parents=True, exist_ok=True)
                        artifact.write_bytes(content)

                args = ["builder", "--of-root", str(sdk), "--addon-sdk", str(addon_sdk), "--output", str(output)]
                if recovery:
                    args.extend(("--recovery-test-hooks", "--without-videos"))
                with (
                    patch.object(sys, "argv", args),
                    patch.object(authoring, "sdk_version", return_value="0.12.1"),
                    patch.object(authoring.subprocess, "run", side_effect=build),
                    patch.object(authoring, "sdk_revision", return_value=None) as revision,
                    patch.object(authoring, "copy_data") as copy_data,
                ):
                    self.assertEqual(authoring.main(), 0)
                copy_data.assert_called_once_with(addon_sdk, output, include_videos=not recovery)
                revision.assert_called_once_with(sdk)
                manifest = json.loads((output / "build-manifest.json").read_text())
                self.assertEqual(manifest["core"]["sha256"], hashlib.sha256(b"core").hexdigest())
                self.assertEqual(manifest["binary"]["sha256"], hashlib.sha256(b"app").hexdigest())
                self.assertEqual(manifest["requested_sanitizers"], [])
                self.assertEqual(manifest["sdk_version"], "0.12.1")
                self.assertIsNone(manifest["sdk_revision"])
                self.assertEqual(manifest["source_sha256"], authoring.source_identity())
                self.assertEqual(manifest["requested_recovery_test_hooks"], recovery)
                self.assertEqual(manifest["included_videos"], not recovery)


if __name__ == "__main__":
    unittest.main()
