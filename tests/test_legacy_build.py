"""Regression coverage for the read-only openFrameworks compiler overlay."""

import importlib.util
import hashlib
import json
from pathlib import Path
import tempfile
import subprocess
import unittest
from unittest.mock import patch


spec = importlib.util.spec_from_file_location(
    "legacy_build", Path(__file__).resolve().parents[1] / "scripts/build_macos_legacy.py"
)
legacy_build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(legacy_build)


class OverlayTests(unittest.TestCase):
    def test_output_rejects_shared_installation_and_unowned_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            of_root = root / "of"
            for output in (of_root, of_root / "products", legacy_build.ROOT / "build"):
                with self.assertRaisesRegex(ValueError, "outside the checkout"):
                    legacy_build.prepare_output(output, of_root)
                self.assertFalse((output / ".vosc-legacy-build").exists())
            (root / "unowned").mkdir()
            (root / "unowned/keep.txt").write_text("preserve")
            with self.assertRaisesRegex(ValueError, "nonempty output"):
                legacy_build.prepare_output(root / "unowned", of_root)
            self.assertEqual((root / "unowned/keep.txt").read_text(), "preserve")
            output = root / "owned"
            legacy_build.prepare_output(output, of_root)
            legacy_build.prepare_output(output, of_root)

    def test_assets_are_writable_copies_with_no_source_links(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            checkout = root / "checkout"
            (checkout / "bin/data/shaders").mkdir(parents=True)
            (checkout / "bin/data/settings.json").write_text("source")
            (checkout / "bin/data/shaders/shader.frag").write_text("shader")
            of_root = root / "of"
            deferred = of_root / "addons/ofxDeferredShading/data/shaders/deferred"
            deferred.mkdir(parents=True)
            (deferred / "pass.frag").write_text("deferred")
            with patch.object(legacy_build, "ROOT", checkout):
                data = legacy_build.copy_data(of_root, root / "output")
            self.assertFalse(any(path.is_symlink() for path in data.rglob("*")))
            (data / "settings.json").write_text("runtime")
            self.assertEqual((checkout / "bin/data/settings.json").read_text(), "source")
            self.assertEqual((data / "shaders/deferred/pass.frag").read_text(), "deferred")

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
            with patch.object(legacy_build, "ROOT", checkout), \
                    patch.object(legacy_build, "RECEIVER_BASELINES", baseline):
                inputs = legacy_build.receiver_inputs(root / "of")
                self.assertEqual(len(inputs), 2)
                utils = root / "of/libs/openFrameworks/utils"
                utils.mkdir(parents=True)
                (utils / "ofConstants.h").write_text(
                    "#define OF_VERSION_MAJOR 0\n#define OF_VERSION_MINOR 11\n#define OF_VERSION_PATCH 1\n")
                (checkout / "addons.make").write_text("ofxOsc\n")
                output = root / "output"
                output.mkdir()
                overlay = legacy_build.prepare_overlay(root / "of", output)
                mapping = {item["name"]: item["external-contents"]
                           for item in json.loads(overlay.read_text())["roots"]}
                for name in baseline:
                    self.assertEqual(mapping[str(source / name)], str(vendor / name))
                    self.assertEqual((source / name).read_text(), "installed " + name)
                self.assertEqual(json.loads((output / "receiver-inputs.json").read_text()), inputs)
                self.assertEqual(legacy_build.prepare_overlay(root / "of", output), overlay)
                (vendor / "ofxOscReceiver.cpp").write_text("updated receiver implementation")
                updated = legacy_build.prepare_overlay(root / "of", output)
                self.assertNotEqual(updated, overlay)
                self.assertNotEqual(
                    legacy_build.build_command(root / "of", output, overlay, ""),
                    legacy_build.build_command(root / "of", output, updated, ""))
                self.assertEqual(inputs[0]["replacement_sha256"],
                                 hashlib.sha256((vendor / inputs[0]["source"].split("/")[-1]).read_bytes()).hexdigest())
                (source / "ofxOscReceiver.h").write_text("changed upstream")
                with self.assertRaisesRegex(ValueError, "Unexpected shared receiver baseline"):
                    legacy_build.receiver_inputs(root / "of")

    def test_private_project_rebases_of_inputs_and_keeps_checkout_sources(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "output"
            of_root = Path("/private/isolated-of")
            original = legacy_build.ROOT / "vosc.xcodeproj/project.pbxproj"
            before = original.read_bytes()
            project = legacy_build.prepare_project(output)
            text = (project / "project.pbxproj").read_text()
            self.assertIn('path = "ofxOsc/src/ofxOscReceiver.cpp"; sourceTree = VOSC_ADDONS;', text)
            self.assertIn('path = "' + str(output / "project/Project.xcconfig") + '";', text)
            self.assertIn('$(VOSC_ADDONS)/ofxUltralight/libs/lib/osx', text)
            self.assertEqual((output / "project/Project.xcconfig").read_bytes(),
                             (legacy_build.ROOT / "scripts/OpenFrameworks011.xcconfig").read_bytes())
            self.assertEqual(original.read_bytes(), before)
            plist = project.parent / "openFrameworks-Info.plist"
            self.assertEqual(plist.read_bytes(), (legacy_build.ROOT / plist.name).read_bytes())
            self.assertFalse(plist.is_symlink())
            self.assertIn('INFOPLIST_FILE = "openFrameworks-Info.plist";', text)
            # Resolve Xcode's actual file/group graph, including relative
            # children such as inspector/TextureInspector.cpp.
            parsed = json.loads(subprocess.check_output([
                "plutil", "-convert", "json", "-o", "-", str(project / "project.pbxproj")]))
            objects = parsed["objects"]
            parents = {child: key for key, item in objects.items()
                       if item.get("isa") == "PBXGroup" for child in item.get("children", [])}

            def resolve(key):
                item = objects[key]
                value = Path(item.get("path", ""))
                if value.is_absolute():
                    return value
                base = resolve(parents[key]) if item.get("sourceTree") == "<group>" and key in parents else project.parent
                return base / value

            app_sources = []
            for item in objects.values():
                if item.get("isa") != "PBXSourcesBuildPhase":
                    continue
                for build_key in item["files"]:
                    path = resolve(objects[build_key]["fileRef"])
                    if path.is_relative_to(legacy_build.ROOT / "src"):
                        self.assertTrue(path.is_file(), str(path))
                        app_sources.append(path)
                    self.assertFalse(path.is_relative_to(project.parent / "src"), str(path))
            self.assertIn(legacy_build.ROOT / "src/inspector/TextureInspector.cpp", app_sources)
            for item in objects.values():
                paths = item.get("buildSettings", {}).get("HEADER_SEARCH_PATHS", [])
                if isinstance(paths, list):
                    self.assertFalse(any(path == "src" or path.startswith("src/") for path in paths))
            command = legacy_build.build_command(of_root, output, output / "overlay.json", "")
            self.assertIn(f"OF_PATH={of_root}", command)
            self.assertIn(f"SRCROOT={legacy_build.ROOT}", command)
            self.assertIn(str(project), command)

    def test_success_receipt_binds_binary_sources_and_is_invalidated_before_retry(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            checkout, output = root / "checkout", root / "output"
            (checkout / "src").mkdir(parents=True)
            source = checkout / "src/owner.cpp"
            source.write_text("first source")
            vendor = checkout / "vendor/ofxOsc"
            vendor.mkdir(parents=True)
            for name in legacy_build.RECEIVER_BASELINES:
                (vendor / name).write_text("corrected " + name)
            binary = output / "products/voscDebug.app/Contents/MacOS/voscDebug"
            binary.parent.mkdir(parents=True)
            binary.write_bytes(b"app")
            library = root / "core.a"
            library.write_bytes(b"core")
            with patch.object(legacy_build, "ROOT", checkout):
                sources = legacy_build.source_identity()
                receipt = legacy_build.write_build_receipt(
                    output, library, [{"replacement": "tracked"}], sources,
                    ["address", "undefined"], True)
                self.assertEqual(receipt["binary"]["sha256"], hashlib.sha256(b"app").hexdigest())
                self.assertEqual(receipt["source_sha256"][str(source)],
                                 hashlib.sha256(b"first source").hexdigest())
                self.assertTrue(receipt["recovery_test_hooks"])
                self.assertEqual(receipt["sanitizers"], ["address", "undefined"])
                (output / "preservation.json").write_text('{"return_code":0}')
                legacy_build.invalidate_build_receipts(output)
                self.assertFalse((output / "native-build-receipt.json").exists())
                self.assertFalse((output / "preservation.json").exists())
                source.write_text("concurrent edit")
                with self.assertRaisesRegex(RuntimeError, "sources changed"):
                    legacy_build.write_build_receipt(output, library, [], sources, [], False)
                self.assertFalse((output / "native-build-receipt.json").exists())

    def test_app_output_settings_reject_shared_and_missing_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory).resolve()
            settings = {key: str(output / key) for key in legacy_build.OUTPUT_KEYS}
            render = lambda: "\n".join(f"    {key} = {value}" for key, value in settings.items())
            legacy_build.verify_output_settings(render(), output)
            settings["OBJROOT"] = str(output.parent / "shared")
            with self.assertRaisesRegex(ValueError, "Unsafe or unresolved"):
                legacy_build.verify_output_settings(render(), output)
            with self.assertRaises(ValueError):
                legacy_build.verify_output_settings("", output)

    def test_overlay_preserves_sources_and_boost_types(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            core = root / "of/libs/openFrameworks"
            (core / "utils").mkdir(parents=True)
            constants = core / "utils/ofConstants.h"
            original = (
                b"#define OF_VERSION_MAJOR 0\n#define OF_VERSION_MINOR 11\n"
                b"#define OF_VERSION_PATCH 1\n// legacy byte: \xc4\n"
                b"namespace std { namespace filesystem = boost::filesystem; }\n"
            )
            constants.write_bytes(original)
            types = core / "types"
            types.mkdir()
            color_header = types / "ofColor.h"
            color_header.write_text("""    ofColor_():
        r(limit()),
        g(limit()),
        b(limit()),
        a(limit()) {};
""")
            color_source = types / "ofColor.cpp"
            color_source.write_text('''#include "ofConstants.h"
template<typename PixelType>
ofColor_<PixelType>::ofColor_(float _r, float _g, float _b, float _a):
    r(_r),
    g(_g),
    b(_b),
    a(_a){
}

template<typename PixelType>
ofColor_<PixelType>::ofColor_ (const ofColor_<PixelType>& color, float _a):
    r(color.r),
    g(color.g),
    b(color.b),
    a(_a){
}

template<typename PixelType>
ofColor_<PixelType>::ofColor_(float gray, float _a):
    r(gray),
    g(gray),
    b(gray),
    a(_a){
}
''')
            inline = core / "mesh.inl"
            inline.write_text("void load(const std::filesystem::path&);\n")
            untouched = core / "unrelated.h"
            untouched.write_text("int unaffected;\n")
            file_utils = core / "utils/ofFileUtils.cpp"
            file_utils.write_text("filesystem::path normalize();\n")
            imgui = root / "of/addons/ofxImGui/src/EngineGLFW.cpp"
            imgui.parent.mkdir(parents=True)
            imgui.write_text("const ImDrawIdx* idx_buffer_offset = 0;\n"
                "glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, idx_buffer_offset);\n"
                "idx_buffer_offset += pcmd->ElemCount;\n")
            output = root / "output"
            output.mkdir()

            with patch.object(legacy_build, "receiver_inputs", return_value=[]):
                overlay = legacy_build.prepare_overlay(root / "of", output)
            mapping = {item["name"]: Path(item["external-contents"])
                       for item in json.loads(overlay.read_text())["roots"]}
            self.assertEqual(constants.read_bytes(), original)
            self.assertEqual(inline.read_text(), "void load(const std::filesystem::path&);\n")
            self.assertIn(b"namespace of { namespace filesystem = boost::filesystem; }",
                          mapping[str(constants)].read_bytes())
            self.assertIn(b"\xc4", mapping[str(constants)].read_bytes())
            self.assertEqual(mapping[str(inline)].read_text(),
                             "void load(const of::filesystem::path&);\n")
            self.assertNotIn(str(untouched), mapping)
            self.assertEqual(mapping[str(file_utils)].read_text(), "of::filesystem::path normalize();\n")
            self.assertIn("uintptr_t idx_buffer_offset = 0;", mapping[str(imgui)].read_text())
            self.assertIn("reinterpret_cast<const void*>(idx_buffer_offset)", mapping[str(imgui)].read_text())
            self.assertIn("pcmd->ElemCount * sizeof(ImDrawIdx)", mapping[str(imgui)].read_text())
            self.assertIn("std::numeric_limits<PixelType>::max()",
                          mapping[str(color_header)].read_text())
            self.assertIn("ofColorChannelCast<PixelType>", mapping[str(color_source)].read_text())
            self.assertIn("std::isnan(value)", mapping[str(color_source)].read_text())
            modified = mapping[str(constants)].stat().st_mtime_ns
            with patch.object(legacy_build, "receiver_inputs", return_value=[]):
                legacy_build.prepare_overlay(root / "of", output)
            self.assertEqual(mapping[str(constants)].stat().st_mtime_ns, modified)

    def test_other_framework_versions_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            utils = root / "libs/openFrameworks/utils"
            utils.mkdir(parents=True)
            (utils / "ofConstants.h").write_text("#define OF_VERSION_MAJOR 1\n")
            with self.assertRaisesRegex(ValueError, "requires openFrameworks 0.11.1"):
                legacy_build.prepare_overlay(root, root / "output")

    def test_ofcolor_overlay_rejects_anchor_drift(self):
        with self.assertRaisesRegex(ValueError, "default constructor.*found 0"):
            legacy_build.patch_ofcolor(Path("types/ofColor.h"), "changed upstream")
        with self.assertRaisesRegex(ValueError, "channel constructors.*found 0"):
            legacy_build.patch_ofcolor(
                Path("types/ofColor.cpp"), '#include "ofConstants.h"\nchanged upstream')

    def test_recovery_hooks_and_sanitizers_are_opt_in_compile_and_link_flags(self):
        root = Path("/tmp/example")
        ordinary = legacy_build.build_command(root / "of", root / "normal", root / "overlay.json", "-framework Cocoa")
        ordinary_text = "\n".join(ordinary)
        self.assertNotIn("VOSC_RECOVERY_TESTING", ordinary_text)
        self.assertNotIn("-fsanitize", ordinary_text)
        self.assertTrue(any(value.startswith("OTHER_CPLUSPLUSFLAGS=$(inherited) ") for value in ordinary))
        self.assertIn("OTHER_LDFLAGS=$(inherited)", ordinary)
        self.assertIn("VOSC_COMPAT_CXXFLAGS=", ordinary)

        instrumented = legacy_build.build_command(
            root / "of", root / "asan", root / "overlay.json", "-framework Cocoa",
            ("address", "undefined"), True,
        )
        instrumented_text = "\n".join(instrumented)
        self.assertIn("-DVOSC_RECOVERY_TESTING=1", instrumented_text)
        self.assertIn("-fsanitize=address,undefined", instrumented_text)
        self.assertIn("-fno-omit-frame-pointer", instrumented_text)
        self.assertIn("OTHER_LDFLAGS=$(inherited) -fsanitize=address,undefined", instrumented)

        private_core = root / "private/openFrameworksDebug.a"
        private = legacy_build.build_command(root / "of", root / "private-build",
            root / "overlay.json", "-framework Cocoa", ("address",), True, private_core)
        self.assertIn(f"LIB_OF_DEBUG={private_core}", private)


if __name__ == "__main__":
    unittest.main()
