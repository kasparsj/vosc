"""Explicit dependency selection, real compilation and native-launch ordering."""
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from contextlib import redirect_stderr
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import visuals_quark as quark

spec = importlib.util.spec_from_file_location("quark_native_harness", ROOT / "scripts/test_scene_recipe.py")
harness = importlib.util.module_from_spec(spec)
spec.loader.exec_module(harness)
SCLANG = Path("/Applications/SuperCollider.app/Contents/MacOS/sclang")


class SelectionTests(unittest.TestCase):
    def test_explicit_selection_and_no_installed_fallback(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            valid = root / "selected"
            valid.mkdir()
            (valid / "OSCVisuals.sc").write_text("OSCVisuals {}\n")
            file = root / "file"
            file.write_text("not a directory")
            empty = root / "empty"
            empty.mkdir()
            with patch.dict(os.environ, {"VISUALS_QUARK_PATH": str(valid)}):
                self.assertEqual(quark.checkout_path(), valid.resolve())
                for invalid in (root / "missing", file, empty):
                    with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                        quark.checkout_path(invalid)
            with patch.dict(os.environ, {}, clear=True), self.assertRaises(ValueError):
                quark.checkout_path()

    def test_config_excludes_user_extensions_and_suppresses_startup(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            selected = root / "Quark ü"
            selected.mkdir()
            (selected / "OSCVisuals.sc").write_text("OSCVisuals {}\n")
            config = quark.class_config(root / "owned", selected)
            text = config.read_text()
            self.assertIn(str(selected), text)
            self.assertIn(str(quark.STOCK_LIBRARY), text)
            self.assertNotIn("downloaded-quarks", text)
            self.assertIn("loadStartupFiles { ^this }", (config.parent / "SystemOverwrites/extPlatform.sc").read_text())

    def test_source_identity_includes_support_classes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "OSCVisuals.sc").write_text("OSCVisuals {}\n")
            support = root / "OSCVisualsReplay.sc"
            support.write_text("OSCVisualsReplay {}\n")
            before = quark.source_identity(root)
            support.write_text("OSCVisualsReplay { *version { ^1 } }\n")
            self.assertNotEqual(before["sha256"], quark.source_identity(root)["sha256"])

    def test_missing_invalid_incomplete_selection_precedes_native_launch(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            empty = root / "incomplete"
            empty.mkdir()
            file = root / "file"
            file.write_text("not a directory")
            for index, selected in enumerate((None, root / "missing", empty, file)):
                args = ["test_scene_recipe.py", "--binary", "/usr/bin/true", "--sclang", str(SCLANG),
                        "--output", str(root / f"output-{index}"), "--allow-owned-restart"]
                if selected is not None:
                    args += ["--visuals-quark", str(selected)]
                with self.subTest(selected=selected), patch.object(sys, "argv", args), \
                        patch.dict(os.environ, {}, clear=True), patch.object(harness, "OwnedRenderer") as renderer, \
                        redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as caught:
                    harness.main()
                self.assertEqual(caught.exception.code, 2)
                renderer.assert_not_called()


@unittest.skipUnless(SCLANG.is_file(), "requires actual SuperCollider")
class RealCompilerTests(unittest.TestCase):
    def test_selected_checkout_compiles_and_has_source_receipt(self):
        selected = quark.checkout_path()
        with tempfile.TemporaryDirectory() as directory:
            receipt = quark.probe(SCLANG, selected, Path(directory) / "owned")
            self.assertEqual(receipt["return_code"], 0)
            self.assertTrue(receipt["source_stable"])
            self.assertEqual(receipt["before"], quark.source_identity(selected))
            self.assertIn("OSCVisualsReplay.sc", receipt["before"]["sources"])
            self.assertEqual(Path(receipt["compiled_source"]), selected / "OSCVisuals.sc")

    def test_old_checkout_is_rejected_before_native_launch(self):
        selected = quark.checkout_path()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            old = root / "old"
            shutil.copytree(selected, old, ignore=shutil.ignore_patterns(".git", "tests", "__pycache__"))
            source = old / "OSCVisuals.sc"
            text = source.read_text()
            self.assertIn("*sceneReplayVersion", text)
            source.write_text(text.replace("*sceneReplayVersion", "*oldSceneReplayVersion", 1))
            args = ["test_scene_recipe.py", "--binary", "/usr/bin/true", "--sclang", str(SCLANG),
                    "--output", str(root / "output"), "--allow-owned-restart", "--visuals-quark", str(old)]
            with patch.object(sys, "argv", args), patch.object(harness, "OwnedRenderer") as renderer, \
                    redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as caught:
                harness.main()
            self.assertEqual(caught.exception.code, 2)
            renderer.assert_not_called()
            receipt = json.loads((root / "output/quark-probe/receipt.json").read_text())
            self.assertEqual(receipt["return_code"], 2)
            self.assertIn(str(old), receipt["before"]["path"])
            self.assertIn("scene replay API 1 is required", (root / "output/quark-probe/sclang.log").read_text())


if __name__ == "__main__":
    unittest.main()
