import base64
import binascii
import asyncio
from contextlib import redirect_stdout
import io
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
import struct
from types import SimpleNamespace
from unittest import mock
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "mcp"))
from scene_recipe_schema import RecipeValidationError, validate_descriptor

spec = importlib.util.spec_from_file_location("scene_recipe_cli", ROOT / "scripts/scene_recipe.py")
cli = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cli)


PNG = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII="
CONTEXTUAL_IDENTIFIER_FIXTURES = json.loads(
    (ROOT / "tests/fixtures/contextual_identifiers.json").read_text())


def png_chunk(kind: bytes, data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", binascii.crc32(kind + data) & 0xffffffff)


def truecolor_png(extra: list[tuple[bytes, bytes]] = []) -> bytes:
    ihdr = struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0)
    chunks = [png_chunk(b"IHDR", ihdr)]
    chunks.extend(png_chunk(kind, data) for kind, data in extra)
    chunks.extend([png_chunk(b"IDAT", zlib.compress(b"\0\x01\x02\x03")), png_chunk(b"IEND", b"")])
    return b"\x89PNG\r\n\x1a\n" + b"".join(chunks)


def arg(kind, value):
    return {"type": kind, "value": value}


def descriptor(fragment="#version 330\nvoid main(){}\n"):
    return {
        "format": "vosc.osc-recipe", "version": 1,
        "resolution": {"width": 800, "height": 600}, "onset_gate": -1,
        "commands": [
            {"address": "/layers/reset", "args": [arg("i", 1), arg("s", "stack")]},
            {"address": "/shader", "args": [arg("i", 0), arg("asset", "assets/layer-0.frag")]},
            {"address": "/layer/size", "args": [arg("i", 0), arg("f", 800.0), arg("f", 600.0), arg("f", 0.0)]},
            {"address": "/layer/visible", "args": [arg("i", 0), arg("i", 1)]},
        ],
        "assets": [
            {"name": "assets/layer-0.frag", "kind": "fragment", "encoding": "utf8", "data": fragment},
            {"name": "assets/image.png", "kind": "image", "encoding": "base64", "data": PNG},
        ],
    }


class DescriptorTests(unittest.TestCase):
    def test_shared_contextual_identifier_policy_fixtures(self):
        for fixture in CONTEXTUAL_IDENTIFIER_FIXTURES:
            value = descriptor()
            context, name = fixture["context"], fixture["name"]
            if context == "input":
                command = {"address": "/input", "args": [
                    arg("s", name), arg("s", ""), arg("i", 0), arg("i", 256)]}
                value["commands"].insert(1, command)
            elif context == "uniform":
                command = {"address": "/shader/var", "args": [
                    arg("i", 0), arg("s", name), arg("f", 1.0)]}
                value["commands"].insert(2, command)
            elif context == "sampler":
                command = {"address": "/shader/texture", "args": [
                    arg("i", 0), arg("s", name), arg("asset", "assets/image.png")]}
                value["commands"].insert(2, command)
            else:
                self.fail(f"unknown contextual identifier fixture: {fixture}")
            with self.subTest(context=context, name=name, accepted=fixture["accepted"]):
                if fixture["accepted"]:
                    self.assertIs(validate_descriptor(value), value)
                else:
                    with self.assertRaises(RecipeValidationError):
                        validate_descriptor(value)

    def test_shortest_sc_float32_spelling_preserves_exact_bits_and_float_type(self):
        bit_patterns = [
            0x00000000, 0x80000000, 0x00000001, 0x007FFFFF, 0x00800000,
            0x3DCCCCCD, 0x3F800000, 0x47742400, 0x7F7FFFFF,
        ]
        bit_patterns.extend((index * 2654435761) & 0x7F7FFFFF for index in range(1, 128))
        for bits in bit_patterns:
            value = struct.unpack("!f", struct.pack("!I", bits))[0]
            if abs(value) > 1_000_000:
                continue
            rendered = cli._sc_float32(value)
            with self.subTest(bits=f"{bits:08x}", rendered=rendered):
                self.assertTrue("." in rendered or "e" in rendered)
                self.assertEqual(struct.pack("!f", value), struct.pack("!f", float(rendered)))
        self.assertEqual("-0.0", cli._sc_float32(-0.0))
        self.assertEqual("320.0", cli._sc_float32(320.0))
        self.assertEqual("650.0", cli._sc_float32(650.0))

    def test_readable_renderer_keeps_vectors_types_expressions_and_order(self):
        value = descriptor()
        value["commands"].extend([
            {"address": "/layer/pos", "args": [arg("i", 0), arg("f", -0.0), arg("s", "sin(t)*10"), arg("f", 1.0)]},
            {"address": "/shader/var", "args": [arg("i", 0), arg("s", "ivec"), arg("i", 1), arg("i", 2)]},
        ])
        text = cli.render_scene(value)
        sends = [line.strip() for line in text.splitlines() if "visuals.send(" in line]
        self.assertEqual(len(value["commands"]), len(sends))
        self.assertIn("visuals.send('/layer/pos', 0, [-0.0, \"sin(t)*10\", 1.0]);", sends)
        self.assertIn("visuals.send('/shader/var', 0, \"ivec\", [1, 2]);", sends)
        size_index = next(index for index, line in enumerate(sends) if line.startswith("visuals.send('/layer/size'"))
        self.assertLess(size_index, sends.index("visuals.send('/layer/visible', 0, 1);"))
        self.assertNotIn("OSCFunc", text)
        self.assertNotIn("var commands", text)

    def test_valid_descriptor_accepts_zero_z_and_large_fragment(self):
        value = descriptor("x" * 4096)
        self.assertIs(validate_descriptor(value), value)

    def test_float_components_must_already_be_float32_normalized(self):
        hostile = descriptor()
        hostile["commands"].append({"address": "/layer/alpha", "args": [arg("i", 0), arg("f", 0.1)]})
        with self.assertRaisesRegex(RecipeValidationError, "normalized to float32"):
            validate_descriptor(hostile)
        normalized = descriptor()
        normalized["commands"].append({"address": "/layer/alpha", "args": [
            arg("i", 0), arg("f", struct.unpack("!f", struct.pack("!f", 0.1))[0])]})
        self.assertIs(validate_descriptor(normalized), normalized)

    def test_osc_only_input_allows_empty_device(self):
        value = descriptor()
        value["commands"].insert(1, {"address": "/input", "args": [
            arg("s", "control"), arg("s", ""), arg("i", 0), arg("i", 256)]})
        validate_descriptor(value)

    def test_renderer_builtin_expression_identifiers(self):
        value = descriptor()
        value["commands"].append({"address": "/shader/var", "args": [
            arg("i", 0), arg("s", "modulation"), arg("s", "sin(t)+f*0+i+total")]})
        validate_descriptor(value)
        shadowed = descriptor()
        shadowed["commands"].insert(1, {"address": "/var", "args": [
            arg("s", "t"), arg("f", 0.25)]})
        shadowed["commands"].append({"address": "/shader/var", "args": [
            arg("i", 0), arg("s", "modulation"), arg("s", "t+1")]})
        validate_descriptor(shadowed)

    def test_raw_unsafe_asset_components_reject(self):
        for name in ("assets/./x.frag", "assets//x.frag", "assets/../x.frag", "/assets/x.frag", "assets\\x.frag"):
            value = descriptor()
            value["assets"][0]["name"] = name
            value["commands"][1]["args"][1]["value"] = name
            with self.subTest(name=name), self.assertRaises(RecipeValidationError):
                validate_descriptor(value)

    def test_unknown_fields_types_and_order_fail_closed(self):
        cases = []
        extra = descriptor(); extra["extra"] = True; cases.append(extra)
        boolean = descriptor(); boolean["commands"][0]["args"][0]["value"] = True; cases.append(boolean)
        order = descriptor(); order["commands"].reverse(); cases.append(order)
        unknown = descriptor(); unknown["commands"].append({"address": "/cam", "args": []}); cases.append(unknown)
        for value in cases:
            with self.assertRaises(RecipeValidationError): validate_descriptor(value)

    def test_expression_component_count_excludes_structure(self):
        value = descriptor()
        for _ in range(900):
            value["commands"].append({"address": "/layer/blend", "args": [arg("i", 0), arg("i", 1)]})
        validate_descriptor(value)
        for _ in range(1025):
            value["commands"].append({"address": "/layer/alpha", "args": [arg("i", 0), arg("f", 1.0)]})
        with self.assertRaises(RecipeValidationError): validate_descriptor(value)

    def test_graph_identity_image_and_binding_bounds(self):
        negative = descriptor(); negative["commands"].insert(1, {"address": "/input", "args": [arg("i", -1), arg("s", ""), arg("i", 0), arg("i", 256)]})
        bad_png = descriptor(); bad_png["assets"][1]["data"] = base64.b64encode(b"\x89PNG\r\n\x1a\nonly-signature").decode()
        reserved = descriptor(); reserved["commands"].insert(1, {"address": "/var", "args": [arg("s", "gain"), arg("s", "camPos + 1")]})
        cycle = descriptor(); cycle["commands"][1:1] = [
            {"address": "/var", "args": [arg("s", "a"), arg("f", 0.0)]},
            {"address": "/var", "args": [arg("s", "b"), arg("f", 0.0)]},
            {"address": "/var", "args": [arg("s", "a"), arg("s", "b + 1")]},
            {"address": "/var", "args": [arg("s", "b"), arg("s", "a + 1")]},
        ]
        mixed = descriptor(); mixed["commands"].insert(2, {"address": "/shader/var", "args": [arg("i", 0), arg("s", "u"), arg("i", 1), arg("f", 2.0)]})
        for value in (negative, bad_png, reserved, cycle, mixed):
            with self.subTest(value=value), self.assertRaises(RecipeValidationError): validate_descriptor(value)

    def test_png_rejects_crc_valid_palette_order_name_and_reserved_bit(self):
        malformed = [
            # Grayscale images may not carry PLTE.
            base64.b64decode(PNG)[:33] + png_chunk(b"PLTE", b"\0\0\0") + base64.b64decode(PNG)[33:],
            truecolor_png([(b"PLTE", b"\0\0\0"), (b"PLTE", b"\1\1\1")]),
            truecolor_png([(b"a1AA", b"")]),
            truecolor_png([(b"abca", b"")]),
        ]
        for image in malformed:
            value = descriptor()
            value["assets"][1]["data"] = base64.b64encode(image).decode()
            with self.subTest(image=image[:48]), self.assertRaises(RecipeValidationError):
                validate_descriptor(value)

    def test_all_expression_sites_and_shared_assignment_order_fail_closed(self):
        cases = []
        layer = descriptor(); layer["commands"].append({"address": "/layer/alpha", "args": [arg("i", 0), arg("s", "missing + 1")]}); cases.append(layer)
        texture = descriptor(); texture["commands"][2:2] = [
            {"address": "/tex", "args": [arg("i", 0), arg("asset", "assets/image.png")]},
            {"address": "/tex/speed", "args": [arg("i", 0), arg("s", "camOrbit + 1")]},
        ]; cases.append(texture)
        overwritten = descriptor(); overwritten["commands"][1:1] = [
            {"address": "/var", "args": [arg("s", "gain"), arg("f", 0.0)]},
            {"address": "/var", "args": [arg("s", "gain"), arg("s", "1 + 1")]},
            {"address": "/var", "args": [arg("s", "gain"), arg("s", "2 + 2")]},
        ]; cases.append(overwritten)
        dependency = descriptor(); dependency["commands"][1:1] = [
            {"address": "/var", "args": [arg("s", "a"), arg("f", 0.0)]},
            {"address": "/var", "args": [arg("s", "b"), arg("f", 0.0)]},
            {"address": "/var", "args": [arg("s", "a"), arg("s", "b + 1")]},
            {"address": "/var", "args": [arg("s", "b"), arg("s", "2 + 2")]},
        ]; cases.append(dependency)
        late = descriptor(); late["commands"].append({"address": "/var", "args": [arg("s", "late"), arg("f", 0.0)]}); cases.append(late)
        reserved_uniform = descriptor(); reserved_uniform["commands"].insert(2, {"address": "/shader/var", "args": [arg("i", 0), arg("s", "camPos"), arg("f", 1.0)]}); cases.append(reserved_uniform)
        for value in cases:
            with self.subTest(commands=value["commands"]), self.assertRaises(RecipeValidationError):
                validate_descriptor(value)

    def test_distinct_binding_limit_does_not_count_repeated_assignment(self):
        repeated = descriptor()
        repeated["commands"][2:2] = [
            {"address": "/shader/var", "args": [arg("i", 0), arg("s", "same"), arg("f", 1.0)]}
            for _ in range(129)
        ]
        validate_descriptor(repeated)
        distinct = descriptor()
        distinct["commands"][2:2] = [
            {"address": "/shader/var", "args": [arg("i", 0), arg("s", f"u{index}"), arg("f", 1.0)]}
            for index in range(129)
        ]
        with self.assertRaises(RecipeValidationError):
            validate_descriptor(distinct)


@unittest.skipUnless(sys.platform == "darwin", "scene-folder publication requires macOS atomic rename")
class FolderTests(unittest.TestCase):
    def test_default_folder_contains_native_manifest(self):
        with tempfile.TemporaryDirectory() as temporary:
            destination = Path(temporary) / "Native scene ü"
            value = descriptor()
            cli.write_folder(destination, value)
            self.assertEqual({"README.md", "scene.vosc.json", "assets"},
                             {path.name for path in destination.iterdir()})
            expected = {
                "version": 1,
                "title": destination.name,
                "size": [value["resolution"]["width"], value["resolution"]["height"]],
                "onset_gate": value["onset_gate"],
                "commands": value["commands"],
            }
            manifest = json.loads((destination / "scene.vosc.json").read_text())
            self.assertEqual(expected, manifest)
            self.assertNotIn("SuperCollider", (destination / "README.md").read_text())
            companion = Path(temporary) / "companion" / destination.name
            cli.write_folder(companion, value, scd=True)
            self.assertEqual((destination / "scene.vosc.json").read_bytes(),
                             (companion / "scene.vosc.json").read_bytes())
            self.assertEqual((destination / "assets/layer-0.frag").read_bytes(),
                             (companion / "assets/layer-0.frag").read_bytes())

    def test_optional_supercollider_companion(self):
        with tempfile.TemporaryDirectory() as temporary:
            destination = Path(temporary) / "moved scene ü"
            cli.write_folder(destination, descriptor(), scd=True)
            self.assertEqual({"README.md", "scene.vosc.json", "scene.scd", "assets"},
                             {path.name for path in destination.iterdir()})
            text = (destination / "scene.scd").read_text()
            self.assertIn("thisProcess.nowExecutingPath", text)
            self.assertIn("visuals.replay", text)
            self.assertIn("visuals.send('/layers/reset'", text)
            self.assertNotIn("vosc.scene", text)
            self.assertTrue((destination / "assets/layer-0.frag").exists())
            self.assertIn("OSCVisuals(rendererHost, rendererPort)", text)
            self.assertIn("OSCVisuals.sceneReplayVersion", text)
            self.assertNotIn("replayFactory", text)
            self.assertNotIn("support/", text)
            with self.assertRaises(FileExistsError): cli.write_folder(destination, descriptor())

    def test_scene_and_readme_write_failures_clean_owned_staging(self):
        for filename in ("scene.vosc.json", "scene.scd", "README.md"):
            with self.subTest(filename=filename), tempfile.TemporaryDirectory() as temporary:
                parent = Path(temporary)
                destination = parent / "scene"
                real_open = Path.open
                failures = []

                def fail_output(path, *args, **kwargs):
                    mode = args[0] if args else kwargs.get("mode", "r")
                    if path.name == filename and mode == "w" and ".scene.staging-" in path.parent.name:
                        failures.append((str(path), mode))
                        raise OSError("injected output write failure")
                    return real_open(path, *args, **kwargs)

                with mock.patch.object(Path, "open", fail_output):
                    with self.assertRaisesRegex(OSError, "injected output write failure"):
                        cli.write_folder(destination, descriptor(), scd=True)
                self.assertEqual(1, len(failures))
                self.assertFalse(destination.exists())
                self.assertEqual([], list(parent.iterdir()))

    def test_generation_needs_no_quark_checkout_or_support_file(self):
        with tempfile.TemporaryDirectory() as temporary:
            destination = Path(temporary) / "scene"
            with mock.patch.dict("os.environ", {"VISUALS_QUARK_PATH": "/absent/VisualsQuark"}), \
                    mock.patch.object(Path, "read_bytes", side_effect=AssertionError("export must not read client source")):
                cli.write_folder(destination, descriptor(), scd=True)
            self.assertFalse((destination / "support").exists())
            self.assertIn("VisualsQuark", (destination / "README.md").read_text())
            self.assertIn("OSCVisuals(rendererHost, rendererPort)", (destination / "scene.scd").read_text())

    def test_invalid_descriptor_publishes_nothing(self):
        with tempfile.TemporaryDirectory() as temporary:
            destination = Path(temporary) / "scene"
            value = descriptor(); value["assets"][0]["data"] = "\0"
            with self.assertRaises(RecipeValidationError): cli.write_folder(destination, value)
            self.assertFalse(destination.exists())
            self.assertEqual([], list(Path(temporary).iterdir()))

    def test_atomic_publish_never_replaces_concurrent_directory(self):
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "owned-staging"
            destination = Path(temporary) / "scene"
            source.mkdir(); (source / "marker").write_text("owned")
            destination.mkdir(); (destination / "foreign").write_text("preserve")
            with self.assertRaises(FileExistsError):
                cli._rename_exclusive(source, destination)
            self.assertEqual("preserve", (destination / "foreign").read_text())
            self.assertEqual("owned", (source / "marker").read_text())

    def test_symlink_parent_retarget_cannot_redirect_publication(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            original = root / "original"; redirected = root / "redirected"
            original.mkdir(); redirected.mkdir()
            link = root / "current"; link.symlink_to(original, target_is_directory=True)
            real_rename = cli._rename_exclusive

            def retarget(source, destination, *, parent_fd=None):
                link.unlink(); link.symlink_to(redirected, target_is_directory=True)
                return real_rename(source, destination, parent_fd=parent_fd)

            cli._rename_exclusive = retarget
            try:
                cli.write_folder(link / "scene", descriptor())
            finally:
                cli._rename_exclusive = real_rename
            self.assertTrue((original / "scene/scene.vosc.json").is_file())
            self.assertFalse((redirected / "scene").exists())

    def test_replaced_staging_inode_rejects_and_is_not_deleted(self):
        with tempfile.TemporaryDirectory() as temporary:
            parent = Path(temporary)
            real_readme = cli.render_readme
            replacement: Path | None = None

            def replace_staging(value, *, scd):
                nonlocal replacement
                staging = next(parent.glob(".scene.staging-*"))
                staging.rename(parent / "owned-original")
                staging.mkdir()
                (staging / "foreign-marker").write_text("preserve")
                replacement = staging
                return real_readme(value, scd=scd)

            cli.render_readme = replace_staging
            try:
                with self.assertRaisesRegex(OSError, "staging directory changed"):
                    cli.write_folder(parent / "scene", descriptor())
            finally:
                cli.render_readme = real_readme
            self.assertIsNotNone(replacement)
            self.assertEqual("preserve", (replacement / "foreign-marker").read_text())
            self.assertFalse((parent / "scene").exists())

    def test_shared_writable_parent_rejects_before_staging(self):
        with tempfile.TemporaryDirectory() as temporary:
            parent = Path(temporary)
            parent.chmod(0o777)
            try:
                with self.assertRaises(PermissionError):
                    cli.write_folder(parent / "scene", descriptor())
                self.assertEqual([], list(parent.iterdir()))
            finally:
                parent.chmod(0o700)


class CliReplyTests(unittest.TestCase):
    def test_valid_native_failure_preserves_code_message_and_diagnostics(self):
        failure = {"id": "q", "ok": False, "status": "failed", "revision": 4,
                   "frame": 9, "external_epoch": 2,
                   "error": {"code": "unsupported_scene",
                             "message": "layer 2 field texture reason unsupported looper",
                             "diagnostics": [{"layer": 2, "field": "texture", "reason": "looper"}]}}
        original = cli.EngineClient

        class FailedClient:
            def __init__(self, _socket): pass

            async def query(self, method, params, *, timeout):
                self.method = method; self.params = params
                return failure

        cli.EngineClient = FailedClient
        output = io.StringIO()
        try:
            with redirect_stdout(output):
                result = asyncio.run(cli.run(SimpleNamespace(socket=Path("/private/owned.sock"),
                                                             directory=Path("unused"))))
        finally:
            cli.EngineClient = original
        self.assertEqual(result, 2)
        rendered = output.getvalue()
        self.assertIn("unsupported_scene", rendered)
        self.assertIn("layer 2 field texture reason unsupported looper", rendered)
        self.assertIn('"reason": "looper"', rendered)


if __name__ == "__main__":
    unittest.main()
