import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "desktop/authoring/scripts"))
import package_macos
import stage_webgpu_tools as tools


class WebGPUToolPackagingTests(unittest.TestCase):
    def setUp(self):
        scratch = tempfile.TemporaryDirectory()
        self.addCleanup(scratch.cleanup)
        self.root = Path(scratch.name).resolve()
        self.input = self.root / "input"
        self.input.mkdir()
        for name in tools.TOOLS:
            (self.input / name).write_bytes(bytes.fromhex("cffaedfe") + name.encode())
        tools.write_manifest(self.input)
        self.before = tools.manifest(self.input)

    def test_changed_or_redirected_tool_is_rejected_before_use(self):
        destination = self.root / "stage"
        (self.input / "glslang").write_bytes(b"changed tool")
        with self.assertRaisesRegex(RuntimeError, "match their SHA-256 hashes"):
            tools.stage_tools(self.input, destination, "linux-x64")
        self.assertFalse(destination.exists())
        redirected = tools.manifest(self.input)
        redirected["glslang"]["file"] = str(self.input / "glslang")
        (self.input / "manifest.json").write_text(json.dumps(redirected))
        with self.assertRaisesRegex(RuntimeError, "identify the pinned tools"):
            tools.stage_tools(self.input, destination, "linux-x64")
        self.assertFalse(destination.exists())

    def test_matching_hash_does_not_allow_a_different_tint_build(self):
        with self.assertRaisesRegex(RuntimeError, "executable in the pinned Dawn SDK"):
            tools.stage_tools(self.input, self.root / "stage", "linux-x64")

    def test_probe_requires_pinned_wgsl_output(self):
        with patch.object(tools, "run", side_effect=["Glslang Version: 11:15.1.0", "", "@fragment fn main() {}"]):
            with self.assertRaisesRegex(RuntimeError, "pinned WGSL probe output"):
                tools.probe(self.input)

    def test_linux_rejects_other_architectures_and_external_dependencies(self):
        binary = self.input / "glslang"
        with self.assertRaisesRegex(RuntimeError, "Linux x86-64 ELF executable"):
            tools.check_binary(binary, "linux-x64")
        binary.write_bytes(b"\x7fELF\x02\x01" + bytes(12) + b"\x3e\x00")
        with patch.object(tools, "run", side_effect=["(NEEDED) Shared library: [libexternal.so]", ""]):
            with self.assertRaisesRegex(RuntimeError, "outside the Ubuntu x86-64 runtime"):
                tools.check_binary(binary, "linux-x64")

    def test_mac_repackaging_stages_only_the_selected_tools(self):
        app = self.root / "vosc.app"
        previous = app / "Contents/Resources/webgpu-tools"
        previous.mkdir(parents=True)
        (previous / "old-compiler").touch()

        with patch.object(package_macos, "run"), patch.object(tools, "stage_tools") as stage:
            _, plain = package_macos.stage_host(self.root / "plain", "1", app)
            self.assertFalse((plain / "Contents/Resources/webgpu-tools").exists())
            stage.assert_not_called()
            _, bundled = package_macos.stage_host(self.root / "bundled", "1", app, self.input)
            destination = bundled / "Contents/Resources/webgpu-tools"
            self.assertFalse(destination.exists())
            stage.assert_called_once_with(self.input, destination, "macos-arm64")

        self.assertTrue((previous / "old-compiler").is_file())

    def test_signed_tool_hashes_are_written_before_sealing_the_app(self):
        app = self.root / "vosc.app"
        packaged = app / "Contents/Resources/webgpu-tools"
        packaged.parent.mkdir(parents=True)
        self.input.rename(packaged)
        sealed = []

        def codesign(args, **kwargs):
            binary = args[-1]
            if args[0] == "lipo":
                return "arm64\n"
            if "--force" in args and binary.parent == packaged:
                binary.write_bytes(binary.read_bytes() + b"codesign altered bytes")
            if "--force" in args and binary == app:
                sealed.append(json.loads((packaged / "manifest.json").read_text()))

        with patch.object(package_macos, "run", side_effect=codesign), patch.object(tools, "probe") as probe:
            signed = package_macos.sign(app, "Developer ID Test", webgpu_tools=packaged)
        self.assertEqual(signed, [f"Contents/Resources/webgpu-tools/{name}" for name in tools.TOOLS])
        self.assertEqual(sealed, [tools.manifest(packaged)])
        for name in tools.TOOLS:
            self.assertNotEqual(sealed[0][name]["sha256"], self.before[name]["sha256"])
        probe.assert_called_once_with(packaged)
