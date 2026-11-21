import importlib.util
import contextlib
import io
import hashlib
import json
import shutil
import shlex
import subprocess
from unittest.mock import patch
from pathlib import Path
import sys
import tempfile
import unittest


SCRIPTS = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPTS))
import build_macos_legacy as legacy
spec = importlib.util.spec_from_file_location("recovery_core", SCRIPTS / "build_macos_recovery_core.py")
core = importlib.util.module_from_spec(spec)
spec.loader.exec_module(core)


class OutputIsolationTests(unittest.TestCase):
    def test_default_and_explicit_modes_remain_unambiguous(self):
        default = core.parse_args(["--output", "/tmp/core"])
        rooted = core.parse_args(["--of-root", "/tmp/explicit-of", "--output", "/tmp/core"])
        self.assertEqual(rooted.of_root, Path("/tmp/explicit-of"))
        self.assertEqual(default.configuration, "Debug")
        self.assertFalse(default.no_sanitizers)
        self.assertEqual(default.sanitizers, ["address"])
        explicit = core.parse_args(["--output", "/tmp/core", "--sanitizers", "address",
                                    "--sanitizers", "undefined"])
        self.assertEqual(explicit.sanitizers, ["address", "undefined"])
        ordinary = core.parse_args(["--output", "/tmp/core", "--no-sanitizers"])
        self.assertTrue(ordinary.no_sanitizers)
        self.assertEqual(ordinary.sanitizers, [])
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            core.parse_args(["--output", "/tmp/core", "--no-sanitizers",
                             "--sanitizers", "address"])

    def test_xcode_configurations_select_matching_core(self):
        for configuration in ("Debug", "Release", "AppStore"):
            args = core.parse_args(["--output", "/tmp/core", "--no-sanitizers",
                                    "--configuration", configuration])
            self.assertEqual(args.configuration, configuration)
            self.assertEqual(args.sanitizers, [])

    def test_ordinary_flags_omit_instrumentation(self):
        ordinary = core.compile_flags(Path("/tmp/core output/overlay.json"), [])
        self.assertIn('"/tmp/core output/overlay.json"', ordinary)
        self.assertNotIn("-fsanitize", ordinary)
        instrumented = core.compile_flags(Path("/tmp/overlay.json"), ["address", "undefined"])
        self.assertIn("-fsanitize=address,undefined", instrumented)
        self.assertIn("$(inherited)", ordinary)

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
            constants = root / "of/libs/openFrameworks/utils"
            constants.mkdir(parents=True)
            (constants / "ofConstants.h").write_text(
                "#define OF_VERSION_MAJOR 0\n#define OF_VERSION_MINOR 11\n#define OF_VERSION_PATCH 1\n")
            (checkout / "addons.make").write_text("ofxOsc\n")
            output = root / "output"
            output.mkdir()
            source = root / "probe.cpp"
            source.write_text('#include "' + str(installed / "ofxOscReceiver.h") + '"\nint value = PROBE_VALUE;\n')
            config = (SCRIPTS / "OpenFrameworks011.xcconfig").read_text()
            flags = next(line.split(" = ", 1)[1] for line in config.splitlines()
                         if line.startswith("VOSC_COMPAT_CXXFLAGS = "))
            flags = shlex.split(flags.replace("$(VOSC_COMPAT_BUILD_DIR)", str(output)))
            with patch.object(legacy, "ROOT", checkout), patch.object(legacy, "RECEIVER_BASELINES", baseline):
                stamps = []
                for value in (1, 2):
                    (vendor / "ofxOscReceiver.h").write_text(f"#define PROBE_VALUE {value}\n")
                    overlay = legacy.prepare_overlay(root / "of", output)
                    core.publish_xcode_overlay(overlay, output)
                    header = output / "overlay-inputs.h"
                    stamps.append(header.read_bytes())
                    stamp_time = header.stat().st_mtime_ns
                    core.publish_xcode_overlay(overlay, output)
                    self.assertEqual(header.stat().st_mtime_ns, stamp_time)
                    self.assertEqual(json.loads((output / "filesystem-overlay.json").read_text()),
                                     json.loads(overlay.read_text()))
                    result = subprocess.run(
                        ["clang++", *flags, "-E", "-P", "-MD", "-MF", str(root / "probe.d"), str(source)],
                        check=True, capture_output=True, text=True)
                    self.assertIn(f"int value = {value};", result.stdout)
                    # Make dependency escaping independent of the temporary path's spaces.
                    self.assertIn("overlay-inputs.h", (root / "probe.d").read_text())
                self.assertNotEqual(stamps[0], stamps[1])
                self.assertEqual((installed / "ofxOscReceiver.h").read_text(), "// installed input\n")

    def test_all_resolved_outputs_must_stay_private(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory).resolve()
            settings = {key: str(output / key) for key in core.OUTPUT_KEYS}
            render = lambda: "\n".join(f"    {key} = {value}" for key, value in settings.items())
            self.assertEqual(core.verify_output_settings(render(), output), settings)
            for key in core.OUTPUT_KEYS:
                original = settings[key]
                settings[key] = str(output.parent / "shared-framework")
                with self.assertRaises(ValueError, msg=key):
                    core.verify_output_settings(render(), output)
                settings[key] = original

    def test_missing_and_empty_outputs_fail_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory).resolve()
            with self.assertRaises(ValueError):
                core.verify_output_settings("", output)
            with self.assertRaises(ValueError):
                core.verify_output_settings("    TARGET_BUILD_DIR = \n", output)


if __name__ == "__main__":
    unittest.main()
