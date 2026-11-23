"""Release stock inventory and app-only staging checks."""

import hashlib
import json
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


SCRIPTS = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPTS))
import setup_macos
import stage_assets
import stage_macos_release as release


class ReleaseStockTests(unittest.TestCase):
    def setUp(self):
        scratch = tempfile.TemporaryDirectory(prefix="vosc-release-test-")
        self.addCleanup(scratch.cleanup)
        self.root = Path(scratch.name).resolve()
        self.source = self.root / "source"
        self.source.mkdir()
        self.shader = self.write(self.source / "assets/shaders/base.frag", "public shader")
        self.write(self.source / "assets/.gitkeep", "")
        self.scene = self.write(self.source / "examples/sieve/scene.vosc.json", "public example")
        self.write(self.source / "media/videos/private.mov", "private clip")
        self.addon = self.root / "addon-sdk"
        self.deferred = self.write(self.addon / stage_assets.DEFERRED_SHADERS / "pass.frag", "deferred shader")
        pin = json.loads(setup_macos.MANIFEST.read_text())["addons"]["ofxDeferredShading"]
        setup_macos.record_tree(self.addon, {"addons": {"ofxDeferredShading": pin}})
        self.git("init", "--quiet")
        self.git("add", "assets", "examples")
        self.commit()
        for module in (stage_assets, release):
            patcher = patch.object(module, "ROOT", self.source)
            patcher.start()
            self.addCleanup(patcher.stop)

    def git(self, *args):
        subprocess.run(["git", *args], cwd=self.source, check=True, capture_output=True)

    def commit(self):
        self.git("-c", "user.name=Test", "-c", "user.email=test@example.invalid",
                 "commit", "--quiet", "--no-verify", "--no-gpg-sign", "-m", "fixture")

    def write(self, path, content):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
        return path

    def test_public_inventory_uses_checkout_and_pinned_sdk_and_excludes_private_clips(self):
        self.assertEqual(
            release.expected_stock(self.addon),
            {
                "shaders": None,
                "shaders/base.frag": hashlib.sha256(self.shader.read_bytes()).hexdigest(),
                "examples": None,
                "examples/sieve": None,
                "examples/sieve/scene.vosc.json": hashlib.sha256(self.scene.read_bytes()).hexdigest(),
                "shaders/deferred": None,
                "shaders/deferred/pass.frag": hashlib.sha256(self.deferred.read_bytes()).hexdigest(),
                "videos": None,
            },
        )
        self.write(self.source / "assets/untracked.obj", "local")
        with self.assertRaisesRegex(ValueError, "Unexpected or missing public assets.*untracked.obj"):
            release.expected_stock(self.addon)

    def test_changed_dependency_and_checkout_bytes_fail(self):
        self.shader.write_text("local edits")
        with self.assertRaisesRegex(ValueError, "must match"):
            release.expected_stock(self.addon)
        self.git("restore", "assets/shaders/base.frag")
        self.deferred.write_text("local SDK changes")
        with self.assertRaisesRegex(ValueError, "dependency tree conflicts"):
            release.expected_stock(self.addon)

    def test_stock_links_and_special_files_fail(self):
        for make in (lambda path: path.symlink_to(self.shader),
                     lambda path: os.link(self.shader, path), os.mkfifo):
            with self.subTest(make=make):
                linked = self.source / "assets/linked.frag"
                make(linked)
                try:
                    with self.assertRaisesRegex(ValueError, "ordinary stock"):
                        release.expected_stock(self.addon)
                finally:
                    linked.unlink()

    def test_cross_source_case_collision_fails_before_staging(self):
        self.write(self.source / "assets/Examples/extra.frag", "extra")
        self.git("add", "assets")
        self.commit()
        with self.assertRaisesRegex(ValueError, "collide on macOS"):
            release.expected_stock(self.addon)

    @unittest.skipUnless(sys.platform == "darwin" and shutil.which("clang"), "requires macOS and clang")
    def test_stage_copies_only_app_verifies_slices_and_rejects_invalid_output(self):
        app = self.root / "build/products/vosc.app"
        info = app / "Contents/Info.plist"
        info.parent.mkdir(parents=True)
        info.write_bytes(plistlib.dumps({"CFBundlePackageType": "APPL", "CFBundleExecutable": "vosc",
                                         "CFBundleIdentifier": "cc.openFrameworks.vosc"}))
        (app / "Contents/Resources").mkdir()
        binary = app / "Contents/MacOS/vosc"
        binary.parent.mkdir()
        c_source = self.write(self.root / "fixture.c",
                              '#include <stdio.h>\nint main(void) { puts("example-reveal-scene"); }\n')
        self.write(app.parent / "data/trap.frag", "adjacent trap")
        self.write(app.parent.parent / "commands.json", json.dumps({"app": [
            "xcodebuild", "-scheme", "vosc Release", f"CONFIGURATION_BUILD_DIR={app.parent}",
            f"VOSC_ADDON_SDK={self.addon}", "build",
        ]}))
        subprocess.run(["clang", "-arch", "arm64", "-arch", "x86_64", str(c_source),
                        "-o", str(binary)], check=True, capture_output=True)
        stage = self.root / "stage"
        release.stage_release(app, stage, addon_sdk=self.addon, generation=31)
        staged = stage / "vosc.app/Contents"
        self.assertEqual([entry.name for entry in stage.iterdir()], ["vosc.app"])
        self.assertIs(plistlib.loads((staged / "Info.plist").read_bytes())[release.MARKER], True)
        self.assertNotIn(release.MARKER, plistlib.loads(info.read_bytes()))
        self.assertFalse((app / "Contents/Resources/data").exists())
        manifest = json.loads((staged / "Resources" / release.STOCK_MANIFEST).read_text())
        tree = release.stock_tree(staged / "Resources/data")
        self.assertEqual(manifest["files"], {name: digest for name, digest in tree.items() if digest is not None})
        self.assertEqual(manifest["directories"], sorted(name for name, digest in tree.items() if digest is None))
        self.assertEqual(manifest["generation"], 31)
        self.assertEqual(manifest["schema"], 1)
        self.assertFalse((staged / "Resources/data/trap.frag").exists())
        with self.assertRaisesRegex(ValueError, "new directory"):
            release.stage_release(app, stage, addon_sdk=self.addon, generation=31)

        # The staging oracle detects an incomplete tree even if a manifest could match that tree.
        with patch.object(release, "stage_assets"):
            with self.assertRaisesRegex(ValueError, "differs from public build inputs"):
                release.stage_release(app, self.root / "incomplete", addon_sdk=self.addon, generation=31)
        self.assertFalse((self.root / "incomplete").exists())

        c_source.write_text("int main(void) { return 0; }\n")
        subprocess.run(["clang", "-arch", "arm64", "-arch", "x86_64", str(c_source),
                        "-o", str(binary)], check=True, capture_output=True)
        with self.assertRaisesRegex(ValueError, "without the operator UI"):
            release.stage_release(app, self.root / "no-ui", addon_sdk=self.addon, generation=31)
        self.assertFalse((self.root / "no-ui").exists())


if __name__ == "__main__":
    unittest.main()
