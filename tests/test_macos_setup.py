"""Regression tests for setup and the Xcode builder's output boundaries."""

from pathlib import Path
import sys
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import macos_build
import setup_macos


class SetupPathTests(unittest.TestCase):
    def test_checkout_destination_gets_an_external_build_root(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            checkout = root / "checkout"
            destination = checkout / ".vosc-macos"
            sdk = destination / "sdks/of-0.12.1"
            addons = destination / "addon-sdks/of-0.12.1"
            with (
                patch.object(setup_macos, "ROOT", checkout),
                patch.object(macos_build, "ROOT", checkout),
                patch.object(Path, "home", return_value=root / "home"),
            ):
                output = setup_macos.build_output(destination)
                self.assertEqual(output.parent, root / "home/Library/Caches/VOSC/builds")
                self.assertEqual(output, setup_macos.build_output(destination))
                self.assertNotEqual(output, setup_macos.build_output(checkout / "other"))
                macos_build.prepare_output(output / "of-0.12.1/Debug", sdk, addons)
                with self.assertRaisesRegex(ValueError, "outside the checkout"):
                    macos_build.prepare_output(destination / "build", sdk, addons)

    def test_external_destination_keeps_build_root(self):
        with TemporaryDirectory() as temporary:
            destination = Path(temporary).resolve()
            self.assertEqual(setup_macos.build_output(destination), destination / "build")

    def test_case_alias_of_supplied_sdk_is_rejected_before_writes(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            sdk = root / "SDK"
            sdk.mkdir()
            alias = root / "sdk"
            if not alias.exists():
                self.skipTest("requires a case-insensitive filesystem")
            manifest = setup_macos.json.loads(setup_macos.MANIFEST.read_text())
            with self.assertRaisesRegex(ValueError, "overlaps the supplied SDK"):
                setup_macos.prepare(alias / "missing", sdk, root / "build", manifest)
            self.assertEqual(list(sdk.iterdir()), [])
