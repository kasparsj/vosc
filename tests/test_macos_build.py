"""Regression coverage for private macOS build outputs and receiver overlays."""

import hashlib
import json
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


SCRIPTS = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPTS))
import macos_build
import stage_assets


class MacOSBuildTests(unittest.TestCase):
    def test_output_rejects_shared_installation_and_unowned_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            of_root = root / "of"
            for output in (of_root, of_root / "products", macos_build.ROOT / "build"):
                with self.assertRaisesRegex(ValueError, "outside the checkout"):
                    macos_build.prepare_output(output, of_root)
                self.assertFalse((output / ".vosc-legacy-build").exists())
            (root / "unowned").mkdir()
            (root / "unowned/keep.txt").write_text("preserve")
            with self.assertRaisesRegex(ValueError, "nonempty output"):
                macos_build.prepare_output(root / "unowned", of_root)
            self.assertEqual((root / "unowned/keep.txt").read_text(), "preserve")
            output = root / "owned"
            macos_build.prepare_output(output, of_root)
            macos_build.prepare_output(output, of_root)

    def test_assets_are_writable_copies_with_no_source_links(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            checkout = root / "checkout"
            (checkout / "assets/shaders").mkdir(parents=True)
            (checkout / "assets/.DS_Store").touch()
            shader = checkout / "assets/shaders/shader.frag"
            shader.write_text("shader")
            example = checkout / "examples/fixture/scene.vosc.json"
            example.parent.mkdir(parents=True)
            example.write_text(json.dumps({"version": 1, "title": "Fixture", "size": [640, 480],
                                           "onset_gate": 0, "commands": []}))
            (checkout / "media/videos").mkdir(parents=True)
            (checkout / "media/videos/private.mov").write_text("local clip")
            of_root = root / "of"
            deferred = of_root / "addons/ofxDeferredShading/data/shaders/deferred"
            deferred.mkdir(parents=True)
            (deferred / "pass.frag").write_text("deferred")
            data = root / "output/data"
            with patch.object(stage_assets, "ROOT", checkout):
                stage_assets.stage_assets(data, addon_sdk=of_root)
                self.assertFalse((data / ".DS_Store").exists())
                self.assertFalse((data / "videos/private.mov").exists())
                self.assertEqual((data / "shaders/deferred/pass.frag").read_text(), "deferred")
                self.assertEqual((data / "examples/fixture/scene.vosc.json").read_bytes(), example.read_bytes())
                self.assertFalse(any(path.is_symlink() for path in data.rglob("*")))
                staged_shader = data / "shaders/shader.frag"
                staged_shader.write_text("runtime")
                self.assertEqual(shader.read_text(), "shader")
                shader.write_text("new shader")
                cache = data / "shaders/shadertoy"
                cache.mkdir()
                (cache / "download.frag").write_text("cached")
                (data / "videos/operator.mov").write_text("runtime clip")
                stage_assets.stage_assets(data, addon_sdk=of_root, include_videos=True)
                self.assertEqual(staged_shader.read_text(), "new shader")
                self.assertEqual((cache / "download.frag").read_text(), "cached")
                self.assertEqual((data / "videos/operator.mov").read_text(), "runtime clip")
                self.assertEqual((data / "videos/private.mov").read_text(), "local clip")
                staged_shader.unlink()
                staged_shader.symlink_to(shader)
                with self.assertRaisesRegex(ValueError, "ordinary runtime file"):
                    stage_assets.stage_assets(data, addon_sdk=of_root)
                self.assertEqual(shader.read_text(), "new shader")

    def test_receiver_overlay_requires_exact_sources_and_tracks_replacements(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            checkout = root / "checkout"
            vendor = checkout / "vendor/ofxOsc"
            source = root / "of/addons/ofxOsc/src"
            vendor.mkdir(parents=True)
            source.mkdir(parents=True)
            baseline = {}
            for name in ("ofxOscReceiver.h", "ofxOscReceiver.cpp"):
                (source / name).write_text("installed " + name)
                (vendor / name).write_text("corrected " + name)
                baseline[name] = hashlib.sha256((source / name).read_bytes()).hexdigest()
            with patch.object(macos_build, "ROOT", checkout), \
                    patch.object(macos_build, "RECEIVER_BASELINES", baseline):
                inputs = macos_build.receiver_inputs(root / "of")
                self.assertEqual(len(inputs), 2)
                for item in inputs:
                    filename = Path(item["source"]).name
                    self.assertEqual(Path(item["source"]), source / filename)
                    self.assertEqual(Path(item["replacement"]), vendor / filename)
                    self.assertEqual(item["source_sha256"], baseline[filename])
                    self.assertEqual(item["replacement_sha256"], macos_build.digest(vendor / filename))
                (source / "ofxOscReceiver.h").write_text("changed upstream")
                with self.assertRaisesRegex(ValueError, "Unexpected shared receiver baseline"):
                    macos_build.receiver_inputs(root / "of")

    @unittest.skipUnless(shutil.which("clang++"), "requires clang++")
    def test_xcode_flags_consume_overlay_and_track_replacement_changes(self):
        with tempfile.TemporaryDirectory(prefix="vosc overlay test ") as directory:
            root = Path(directory)
            checkout = root / "checkout"
            vendor = checkout / "vendor/ofxOsc"
            installed = root / "of/addons/ofxOsc/src"
            vendor.mkdir(parents=True)
            installed.mkdir(parents=True)
            baseline = {}
            for name in ("ofxOscReceiver.h", "ofxOscReceiver.cpp"):
                (installed / name).write_text("// installed input\n")
                (vendor / name).write_text("// repository replacement\n")
                baseline[name] = hashlib.sha256((installed / name).read_bytes()).hexdigest()
            for header in ("ofUtils.h", "ofAppRunner.h"):
                (root / header).touch()
            output = root / "output"
            output.mkdir()
            source = root / "probe.cpp"
            source.write_text('#include "' + str(installed / "ofxOscReceiver.h") + '"\nint value = PROBE_VALUE;\n')
            config = (SCRIPTS / "OpenFrameworks012.xcconfig").read_text()
            flags = next(line.split(" = ", 1)[1] for line in config.splitlines()
                         if line.startswith("VOSC_COMPAT_CXXFLAGS = "))
            flags = shlex.split(flags.replace("$(VOSC_COMPAT_BUILD_DIR)", str(output)))
            with patch.object(macos_build, "ROOT", checkout), patch.object(macos_build, "RECEIVER_BASELINES", baseline):
                stamps = []
                for value in (1, 2):
                    (vendor / "ofxOscReceiver.h").write_text(f"#define PROBE_VALUE {value}\n")
                    entries = [
                        {"type": "file", "name": item["source"], "external-contents": item["replacement"]}
                        for item in macos_build.receiver_inputs(root / "of")
                    ]
                    overlay = macos_build.write_overlay(output, entries)
                    macos_build.publish_xcode_overlay(overlay, output)
                    header = output / "overlay-inputs.h"
                    stamps.append(header.read_bytes())
                    stamp_time = header.stat().st_mtime_ns
                    macos_build.publish_xcode_overlay(overlay, output)
                    self.assertEqual(header.stat().st_mtime_ns, stamp_time)
                    self.assertEqual(json.loads((output / "filesystem-overlay.json").read_text()),
                                     json.loads(overlay.read_text()))
                    result = subprocess.run(
                        ["clang++", "-I", str(root), *flags, "-E", "-P", "-MD", "-MF", str(root / "probe.d"), str(source)],
                        check=True, capture_output=True, text=True)
                    self.assertIn(f"int value = {value};", result.stdout)
                    # Make dependency escaping independent of the temporary path's spaces.
                    self.assertIn("overlay-inputs.h", (root / "probe.d").read_text())
                self.assertNotEqual(stamps[0], stamps[1])
                self.assertEqual((installed / "ofxOscReceiver.h").read_text(), "// installed input\n")
