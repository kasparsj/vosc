from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import unicodedata
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from visuals_quark import (
    class_config as quark_class_config,
    checkout_path as quark_checkout_path,
    source_identity as quark_source_identity,
)

SPEC = importlib.util.spec_from_file_location("scene_recipe_cli", ROOT / "scripts/scene_recipe.py")
CLI = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(CLI)
SCLANG = Path("/Applications/SuperCollider.app/Contents/MacOS/sclang")
FROZEN_UPSTREAM_COMMIT = "ef806c0c38bbeb4bb1f4a975166a9a70714f5442"
CONTEXTUAL_IDENTIFIER_FIXTURES = json.loads(
    (ROOT / "tests/fixtures/contextual_identifiers.json").read_text())

def _osc_string(value: str) -> bytes:
    raw = value.encode() + b"\0"
    return raw + b"\0" * ((-len(raw)) % 4)


def _osc_message(address: str, values: list[tuple[str, object]]) -> bytes:
    data = _osc_string(address) + _osc_string("," + "".join(kind for kind, _ in values))
    for kind, value in values:
        data += struct.pack(">i", value) if kind == "i" else _osc_string(str(value))
    return data


def _read_string(data: bytes, offset: int) -> tuple[str, int]:
    end = data.index(0, offset)
    value = data[offset:end].decode()
    return value, (end + 4) & ~3


def _decode_message(message: bytes) -> tuple[str, str, list[object], list[bytes]]:
    address, offset = _read_string(message, 0)
    tags, offset = _read_string(message, offset)
    values: list[object] = []
    encoded: list[bytes] = []
    for kind in tags[1:]:
        if kind == "i":
            raw = message[offset:offset + 4]
            values.append(struct.unpack(">i", raw)[0])
            offset += 4
            encoded.append(raw)
        elif kind == "f":
            raw = message[offset:offset + 4]
            values.append(struct.unpack(">f", raw)[0])
            offset += 4
            encoded.append(raw)
        elif kind == "s":
            start = offset
            value, offset = _read_string(message, offset)
            values.append(value)
            encoded.append(message[start:offset])
        else:
            raise ValueError(f"unexpected OSC type {kind}")
    return address, tags, values, encoded


def _bundle_messages(packet: bytes) -> list[tuple[str, str, list[object], list[bytes]]]:
    if packet[:8] != b"#bundle\0":
        raise ValueError("not an OSC bundle")
    messages = []
    offset = 16
    while offset < len(packet):
        size = struct.unpack_from(">i", packet, offset)[0]
        offset += 4
        messages.append(_decode_message(packet[offset:offset + size]))
        offset += size
    return messages


def _first_bundle_message(packet: bytes) -> tuple[str, list[object]]:
    address, _tags, values, _encoded = _bundle_messages(packet)[0]
    return address, values


def _descriptor() -> dict:
    return {
        "format": "vosc.osc-recipe", "version": 1,
        "resolution": {"width": 800, "height": 600}, "onset_gate": -1,
        "commands": [{"address": "/layers/reset", "args": [
            {"type": "i", "value": 0}, {"type": "s", "value": "stack"}]}],
        "assets": [],
    }


class _Renderer(threading.Thread):
    def __init__(self, *, status: str = "applied", frame: int = 7, detail: str = "owned-test",
                 capture_live: bool = False) -> None:
        super().__init__(daemon=True)
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.bind(("127.0.0.1", 0))
        self.port = self.socket.getsockname()[1]
        self.error: BaseException | None = None
        self.token: str | None = None
        self.status = status
        self.frame = frame
        self.detail = detail
        self.capture_live = capture_live
        self.packet: bytes | None = None
        self.live_packet: bytes | None = None

    def run(self) -> None:
        try:
            packet, _ = self.socket.recvfrom(65535)
            self.packet = packet
            address, values = _first_bundle_message(packet)
            if address != "/scene/reset" or len(values) != 4:
                raise ValueError("unexpected reset opener")
            token, reply_port = str(values[0]), int(values[1])
            self.token = token
            reply = _osc_message("/scene/replay/result", [
                ("s", token), ("s", self.status), ("i", self.frame), ("s", self.detail)])
            # Match native VOSC: a fresh reply socket has an ephemeral source port.
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reply_socket:
                reply_socket.bind(("127.0.0.1", 0))
                if reply_socket.getsockname()[1] == self.port:
                    raise AssertionError("reply source must differ from the renderer listener")
                reply_socket.sendto(reply, ("127.0.0.1", reply_port))
                reply_socket.sendto(_osc_message("/unrelated", []), ("127.0.0.1", reply_port))
            if self.capture_live:
                self.socket.settimeout(3)
                self.live_packet, _ = self.socket.recvfrom(65535)
        except BaseException as error:
            self.error = error
        finally:
            self.socket.close()


def _run_scene(scene: Path, renderer_port: int, *, quark: Path | None = None,
               stock_only: bool = False) -> tuple[int, str, int]:
    if (not isinstance(renderer_port, int) or isinstance(renderer_port, bool)
            or renderer_port < 1024 or renderer_port > 65535 or renderer_port == 33333):
        raise ValueError("owned renderer port must be 1024..65535 and not the default 33333")
    owned = scene.parent / "sclang-owned"
    selected_quark = None if stock_only else quark_checkout_path(quark)
    source_before = None if selected_quark is None else quark_source_identity(selected_quark)
    config = quark_class_config(owned, selected_quark, stock_only=stock_only)
    text = scene.read_text()
    endpoint = "var rendererPort = 33333;"
    if text.count(endpoint) != 1:
        raise AssertionError("generated scene must contain exactly one editable default renderer port")
    text = text.replace(endpoint, f"var rendererPort = {renderer_port};", 1)
    scene.write_text(text)
    runner = owned / "runner.scd"
    escaped = str(scene.resolve()).replace("\\", "\\\\").replace('"', '\\"')
    source_marker = "" if stock_only else (
        '["VOSC_QUARK_SOURCE", OSCVisuals.filenameSymbol.asString].postln;\n')
    runner.write_text("(\n"
        "var sentinel = OSCFunc({ \"VOSC_SCENE_UNRELATED_OK\".postln }, '/unrelated', nil);\n"
        "SystemClock.sched(2.0, { \"VOSC_SCENE_FOLLOWUP_OK\".postln; sentinel.free; 0.exit; nil });\n"
        f"{source_marker}"
        f"thisProcess.interpreter.executeFile(\"{escaped}\");\n"
        ")\n")
    language = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    language.bind(("127.0.0.1", 0))
    language_port = language.getsockname()[1]
    language.close()
    if language_port == 33333:
        raise AssertionError("refusing the default renderer port for an owned language")
    env = {**os.environ, "XDG_CONFIG_HOME": str(owned / "config"),
           "XDG_DATA_HOME": str(owned / "data")}
    process = subprocess.Popen([
        str(SCLANG), "-a", "-l", str(config), "-D", "-u", str(language_port),
        "-i", "none", str(runner),
    ], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    try:
        output, _ = process.communicate(timeout=20)
    except subprocess.TimeoutExpired:
        process.terminate()
        try:
            output, _ = process.communicate(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            output, _ = process.communicate(timeout=3)
        raise AssertionError(f"owned sclang PID {process.pid} timed out and was reaped\n{output}")
    if selected_quark is not None:
        source_after = quark_source_identity(selected_quark)
        if source_before != source_after:
            raise AssertionError("selected VisualsQuark source changed during owned sclang execution")
        expected_source = str(selected_quark / "OSCVisuals.sc")
        if unicodedata.normalize("NFD", expected_source) not in unicodedata.normalize("NFD", output):
            raise AssertionError(f"owned sclang did not compile selected OSCVisuals.sc: {expected_source}\n{output}")
        print(f"VOSC_QUARK_SELECTION={selected_quark} SHA256={source_before['sha256']} "
              f"COMPILED={expected_source}")
    print(f"OWNED_SCLANG_PID={process.pid} EXIT={process.returncode} PORT={language_port}")
    return process.returncode, output, process.pid


@unittest.skipUnless(SCLANG.is_file(), "SuperCollider 3.14.1 is unavailable")
class GeneratedSceneSclangTests(unittest.TestCase):
    def test_shared_contextual_identifier_policy_fixtures(self):
        expected_error = {
            "input": "invalid input identifier",
            "uniform": "invalid shader uniform",
            "sampler": "invalid sampler binding",
        }
        for fixture in CONTEXTUAL_IDENTIFIER_FIXTURES:
            with self.subTest(**fixture), tempfile.TemporaryDirectory(
                    prefix="vosc-sc-contextual-name-") as temporary:
                folder = Path(temporary)
                assets = folder / "assets"
                assets.mkdir()
                (assets / "layer.frag").write_text("#version 330\nout vec4 fragColor; void main(){ fragColor=vec4(1.0); }\n")
                (assets / "image.png").write_bytes(b"contextual-name-fixture")
                value = _descriptor()
                value["commands"][0]["args"][0]["value"] = 1
                context, name = fixture["context"], fixture["name"]
                if context == "input":
                    value["commands"].append({"address": "/input", "args": [
                        {"type": "s", "value": name}, {"type": "s", "value": ""},
                        {"type": "i", "value": 0}, {"type": "i", "value": 256}]})
                else:
                    value["commands"].append({"address": "/shader", "args": [
                        {"type": "i", "value": 0},
                        {"type": "asset", "value": "assets/layer.frag"}]})
                    if context == "uniform":
                        value["commands"].append({"address": "/shader/var", "args": [
                            {"type": "i", "value": 0}, {"type": "s", "value": name},
                            {"type": "f", "value": 1.0}]})
                    elif context == "sampler":
                        value["commands"].append({"address": "/shader/texture", "args": [
                            {"type": "i", "value": 0}, {"type": "s", "value": name},
                            {"type": "asset", "value": "assets/image.png"}]})
                    else:
                        self.fail(f"unknown contextual identifier fixture: {fixture}")
                scene = folder / "scene.scd"
                scene.write_text(CLI.render_scene(value))
                if fixture["accepted"]:
                    renderer = _Renderer()
                    renderer.start()
                    return_code, output, pid = _run_scene(scene, renderer.port)
                    renderer.join(timeout=3)
                    self.assertEqual(return_code, 0, f"owned sclang PID {pid}\n{output}")
                    self.assertIsNone(renderer.error)
                    self.assertRegex(output, r"(?m)^VOSC_SCENE_RESULT applied")
                else:
                    unused = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                    unused.bind(("127.0.0.1", 0)); port = unused.getsockname()[1]; unused.close()
                    return_code, output, pid = _run_scene(scene, port)
                    self.assertEqual(return_code, 0, f"owned sclang PID {pid}\n{output}")
                    self.assertIn("scene preflight: " + expected_error[context], output)
                    self.assertIsNone(re.search(r"(?m)^VOSC_SCENE_RESULT sent$", output))

    def test_owned_launcher_rejects_nonprivate_renderer_ports(self):
        with tempfile.TemporaryDirectory(prefix="vosc-sc-port-boundary-") as temporary:
            scene = Path(temporary) / "scene.scd"
            scene.write_text(CLI.render_scene(_descriptor()))
            for port in (0, 1023, 33333, 65536, True):
                with self.subTest(port=port), self.assertRaises(ValueError):
                    _run_scene(scene, port)

    def test_recorder_snapshots_mutable_vector_and_expression_string(self):
        with tempfile.TemporaryDirectory(prefix="vosc-sc-snapshot-") as temporary:
            folder = Path(temporary)
            value = _descriptor()
            value["commands"][0]["args"][0]["value"] = 1
            value["commands"].append({"address": "/layer/pos", "args": [
                {"type": "i", "value": 0}, {"type": "f", "value": 1.0},
                {"type": "s", "value": "sin(t)"}, {"type": "f", "value": -0.0}]})
            source = CLI.render_scene(value)
            source = source.replace(
                "var visuals;",
                "var visuals; var mutableVector; var mutableExpression; var mutableAddress;",
                1,
            )
            source = source.replace(
                "visuals.replay({ |visuals|",
                "mutableExpression = String.newFrom(\"sin(t)\");\n"
                "mutableAddress = String.newFrom(\"/layer/pos\");\n"
                "mutableVector = [1.0, mutableExpression, -0.0];\n"
                "visuals.replay({ |visuals|",
                1,
            )
            source = source.replace(
                "    visuals.send('/layer/pos', 0, [1.0, \"sin(t)\", -0.0]);",
                "    visuals.send(mutableAddress, 0, mutableVector);\n"
                "    mutableVector[0] = 2.0;\n"
                "    mutableExpression[0] = $c;\n"
                "    mutableAddress[1] = $x;",
                1,
            )
            scene = folder / "scene.scd"
            scene.write_text(source)
            renderer = _Renderer()
            renderer.start()
            return_code, output, pid = _run_scene(scene, renderer.port)
            renderer.join(timeout=3)
            self.assertEqual(return_code, 0, f"owned sclang PID {pid}\n{output}")
            self.assertIsNone(renderer.error)
            self.assertIn("VOSC_SCENE_RESULT sent", output, output)
            messages = [message for message in _bundle_messages(renderer.packet or b"")
                        if message[0] == "/scene/command"]
            position = messages[1]
            self.assertEqual(",sisifsf", position[1])
            self.assertRegex(position[2][0], r"^[0-9A-F]{32}$")
            self.assertEqual([2, "/layer/pos", 0, 1.0, "sin(t)", -0.0], position[2][1:])
            self.assertEqual(struct.pack("!f", -0.0), position[3][-1])

    def test_exported_float32_typetags_bytes_expressions_and_live_arrays(self):
        values = [
            struct.unpack("!f", struct.pack("!I", bits))[0]
            for bits in (0x80000000, 0x00000001, 0x007FFFFF, 0x00800000, 0x3DCCCCCD, 0x49742400)
        ]
        sampled = [struct.unpack("!f", struct.pack("!I", (index * 2654435761) & 0x7F7FFFFF))[0]
                   for index in range(1, 128)]
        values.extend(value for value in sampled if value <= 1_000_000)
        with tempfile.TemporaryDirectory(prefix="vosc-sc-wire-") as temporary:
            folder = Path(temporary)
            value = _descriptor()
            value["commands"][0]["args"][0]["value"] = 1
            for item in values:
                value["commands"].append({"address": "/layer/alpha", "args": [
                    {"type": "i", "value": 0}, {"type": "f", "value": item}]})
            value["commands"].append({"address": "/layer/alpha", "args": [
                {"type": "i", "value": 0}, {"type": "s", "value": "sin(t)+f*0"}]})
            scene = folder / "scene.scd"
            source = CLI.render_scene(value).replace(
                "~exportedScene = visuals; // use a different environment variable if preferred",
                "~exportedScene = visuals; // use a different environment variable if preferred\n"
                "~exportedScene.send('/layer/pos', 0, [1.0, \"sin(t)\", -0.0]);",
                1,
            )
            scene.write_text(source)
            renderer = _Renderer(capture_live=True)
            renderer.start()
            return_code, output, pid = _run_scene(scene, renderer.port)
            renderer.join(timeout=4)
            self.assertEqual(return_code, 0, f"owned sclang PID {pid}\n{output}")
            self.assertIsNone(renderer.error)
            self.assertIn("VOSC_SCENE_RESULT sent", output)
            messages = _bundle_messages(renderer.packet or b"")
            command_messages = [message for message in messages if message[0] == "/scene/command"]
            float_messages = command_messages[1:1 + len(values)]
            self.assertEqual(len(values), len(float_messages))
            for expected, (_address, tags, decoded, encoded) in zip(values, float_messages):
                self.assertEqual(",sisif", tags)
                self.assertEqual(struct.pack("!f", expected), encoded[-1])
                self.assertEqual(struct.pack("!f", expected), struct.pack("!f", decoded[-1]))
            expression = command_messages[-1]
            self.assertEqual(",sisis", expression[1])
            self.assertEqual("sin(t)+f*0", expression[2][-1])
            live = _decode_message(renderer.live_packet or b"")
            self.assertEqual(("/layer/pos", ",is"), live[:2])
            self.assertEqual([0, '[1.0, "sin(t)", -0.0]'], live[2])

    def test_missing_old_and_incompatible_quark_fail_before_transport(self):
        selected = quark_checkout_path()
        frozen = subprocess.run(
            ["git", "-C", str(selected), "show", f"{FROZEN_UPSTREAM_COMMIT}:OSCVisuals.sc"],
            check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        ).stdout
        self.assertIn("OSCVisuals {", frozen)
        with tempfile.TemporaryDirectory(prefix="vosc-sc-quark-fixtures-") as fixtures:
            fixture_root = Path(fixtures)
            old_quark = fixture_root / "old"
            old_quark.mkdir()
            (old_quark / "OSCVisuals.sc").write_text(frozen)
            incompatible_quark = fixture_root / "incompatible"
            incompatible_quark.mkdir()
            for source in selected.glob("*.sc"):
                shutil.copyfile(source, incompatible_quark / source.name)
            main_source = incompatible_quark / "OSCVisuals.sc"
            incompatible = main_source.read_text()
            self.assertEqual(incompatible.count("*sceneReplayVersion"), 1)
            incompatible, replacements = re.subn(r"(\*sceneReplayVersion\s*\{\s*)\^1", r"\g<1>^2", incompatible)
            self.assertEqual(replacements, 1)
            main_source.write_text(incompatible)
            cases = [
                ("missing", None, True),
                ("old", old_quark, False),
                ("incompatible", incompatible_quark, False),
            ]
            for label, selected_quark, stock_only in cases:
                with self.subTest(label=label), tempfile.TemporaryDirectory(prefix=f"vosc-sc-quark-{label}-") as temporary:
                    folder = Path(temporary)
                    scene = folder / "scene.scd"
                    scene.write_text(CLI.render_scene(_descriptor()))
                    listener = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                    listener.bind(("127.0.0.1", 0))
                    listener.settimeout(0.05)
                    port = listener.getsockname()[1]
                    return_code, output, pid = _run_scene(
                        scene, port, quark=selected_quark, stock_only=stock_only)
                    with self.assertRaises(socket.timeout):
                        listener.recvfrom(65535)
                    listener.close()
                    self.assertNotIn("VOSC_SCENE_RESULT sent", output)
                    self.assertTrue(return_code != 0 or "ERROR:" in output,
                                    f"owned sclang PID {pid} unexpectedly accepted {label} Quark\n{output}")
                    if label != "missing":
                        self.assertIn("VisualsQuark scene replay API 1 is required", output)

    def test_callback_error_nested_replay_and_retained_recorder_lifecycle(self):
        with tempfile.TemporaryDirectory(prefix="vosc-sc-callback-error-") as temporary:
            folder = Path(temporary)
            scene = folder / "scene.scd"
            scene.write_text(CLI.render_scene(_descriptor()).replace(
                "    visuals.send('/layers/reset', 0, \"stack\");",
                "    Error(\"owned callback failure\").throw;",
                1,
            ))
            listener = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            listener.bind(("127.0.0.1", 0)); listener.settimeout(0.05)
            port = listener.getsockname()[1]
            return_code, output, pid = _run_scene(scene, port)
            self.assertEqual(return_code, 0, f"owned sclang PID {pid}\n{output}")
            with self.assertRaises(socket.timeout):
                listener.recvfrom(65535)
            listener.close()
            self.assertIn("owned callback failure", output)
            self.assertNotIn("VOSC_SCENE_RESULT sent", output)

        with tempfile.TemporaryDirectory(prefix="vosc-sc-recorder-life-") as temporary:
            folder = Path(temporary)
            scene = folder / "scene.scd"
            generated = CLI.render_scene(_descriptor())
            generated = generated.replace("var visuals;", "var visuals; var facade; var retained;", 1)
            generated = generated.replace("visuals.replay({ |visuals|", "facade = visuals;\nvisuals.replay({ |visuals|\n    retained = visuals;\n    try { facade.replay({ |nested| nested.send('/layers/reset', 0, \"stack\") }, sceneRoot, width, height) } { |error| if(error.errorString.contains(\"nested replay\").not) { error.throw }; \"VOSC_NESTED_REJECTED\".postln };", 1)
            generated = generated.replace("~exportedScene = visuals;", "try { retained.send('/layers/reset', 0, \"stack\") } { |error| \"VOSC_RETAINED_REJECTED\".postln };\n~exportedScene = visuals;", 1)
            scene.write_text(generated)
            renderer = _Renderer()
            renderer.start()
            return_code, output, pid = _run_scene(scene, renderer.port)
            renderer.join(timeout=3)
            self.assertEqual(return_code, 0, f"owned sclang PID {pid}\n{output}")
            self.assertIsNone(renderer.error)
            self.assertIn("VOSC_NESTED_REJECTED", output)
            self.assertIn("VOSC_RETAINED_REJECTED", output)
            self.assertRegex(output, r"(?m)^VOSC_SCENE_RESULT applied")

    def test_builtin_expression_symbols_preflight_and_send(self):
        with tempfile.TemporaryDirectory(prefix="vosc-sc-builtins-") as temporary:
            folder = Path(temporary)
            value = _descriptor()
            value["commands"].extend([
                {"address": "/var", "args": [{"type": "s", "value": "wave"}, {"type": "f", "value": 0.0}]},
                {"address": "/var", "args": [{"type": "s", "value": "wave"}, {"type": "s", "value": "sin(t)+f*0+i+total"}]},
            ])
            scene = folder / "scene.scd"
            scene.write_text(CLI.render_scene(value))
            renderer = _Renderer()
            renderer.start()
            return_code, output, pid = _run_scene(scene, renderer.port)
            renderer.join(timeout=2)
            self.assertEqual(return_code, 0, f"owned sclang PID {pid}\n{output}")
            self.assertIsNone(renderer.error)
            self.assertRegex(output, r"(?m)^VOSC_SCENE_RESULT applied frame=7 detail=owned-test$")
            self.assertIn("VOSC_SCENE_FOLLOWUP_OK", output)

    def test_positive_reply_keeps_language_and_unrelated_responder_alive(self):
        with tempfile.TemporaryDirectory(prefix="vosc-sc-positive-") as temporary:
            folder = Path(temporary)
            scene = folder / "scene.scd"
            scene.write_text(CLI.render_scene(_descriptor()))
            renderer = _Renderer()
            renderer.start()
            return_code, output, pid = _run_scene(scene, renderer.port)
            renderer.join(timeout=3)
            self.assertIsNone(renderer.error)
            self.assertRegex(renderer.token or "", r"^[0-9A-F]{32}$")
            self.assertEqual(return_code, 0, f"owned sclang PID {pid}\n{output}")
            self.assertRegex(output, r"(?m)^VOSC_SCENE_RESULT applied frame=7 detail=owned-test$")
            self.assertIn("VOSC_SCENE_UNRELATED_OK", output)
            self.assertIn("VOSC_SCENE_FOLLOWUP_OK", output)
            self.assertNotRegex(output, r"(?m)^VOSC_SCENE_RESULT unknown")

    def test_generated_token_uses_os_randomness_and_low_reply_port_preflights(self):
        generated = CLI.render_scene(_descriptor())
        quark = quark_checkout_path()
        replay_sources = "\n".join(source.read_text() for source in sorted(quark.glob("*.sc")))
        self.assertIn('File("/dev/urandom", "rb")', replay_sources)
        self.assertNotIn(".choose", replay_sources)
        with tempfile.TemporaryDirectory(prefix="vosc-sc-reply-port-") as temporary:
            folder = Path(temporary)
            scene = folder / "scene.scd"
            scene.write_text(generated)
            fixture_quark = folder / "VisualsQuark-low-port"
            fixture_quark.mkdir()
            for source in quark.glob("*.sc"):
                shutil.copyfile(source, fixture_quark / source.name)
            replay = fixture_quark / "OSCVisualsReplay.sc"
            source = replay.read_text()
            marker = "var replyPort = NetAddr.langPort;"
            self.assertEqual(source.count(marker), 1)
            replay.write_text(source.replace(marker, "var replyPort = 999;", 1))
            unused = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            unused.bind(("127.0.0.1", 0)); port = unused.getsockname()[1]; unused.close()
            return_code, output, pid = _run_scene(scene, port, quark=fixture_quark)
            self.assertEqual(return_code, 0, f"owned sclang PID {pid}\n{output}")
            self.assertIn("scene preflight: reply port outside 1024..65535", output)
            self.assertIsNone(re.search(r"(?m)^VOSC_SCENE_RESULT sent$", output))

    def test_invalid_identifier_fails_before_send(self):
        with tempfile.TemporaryDirectory(prefix="vosc-sc-negative-") as temporary:
            folder = Path(temporary)
            value = _descriptor()
            value["commands"].insert(1, {"address": "/var", "args": [
                {"type": "s", "value": "bad-name"}, {"type": "f", "value": 1.0}]})
            scene = folder / "scene.scd"
            scene.write_text(CLI.render_scene(value))
            unused = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            unused.bind(("127.0.0.1", 0))
            port = unused.getsockname()[1]
            unused.close()
            return_code, output, pid = _run_scene(scene, port)
            self.assertEqual(return_code, 0, f"owned sclang PID {pid}\n{output}")
            self.assertIn("scene preflight: invalid shared variable", output)
            self.assertIsNone(re.search(r"(?m)^VOSC_SCENE_RESULT sent$", output))
            self.assertIn("VOSC_SCENE_FOLLOWUP_OK", output)

    def test_catalog_asset_dimension_gate_and_dependency_negatives_precede_send(self):
        cases = []
        unsupported = _descriptor(); unsupported["commands"].append({"address": "/shader/texture/arbitrary", "args": [{"type": "i", "value": 0}]})
        cases.append(("unsupported", unsupported, None, "unsupported setup address"))
        missing = _descriptor(); missing["commands"][0]["args"][0]["value"] = 1
        missing["commands"].append({"address": "/shader", "args": [{"type": "i", "value": 0}, {"type": "asset", "value": "assets/missing.frag"}]})
        cases.append(("missing", missing, None, "asset is missing or not a regular file"))
        oversized = _descriptor(); oversized["commands"][0]["args"][0]["value"] = 1
        oversized["commands"].append({"address": "/shader", "args": [{"type": "i", "value": 0}, {"type": "asset", "value": "assets/large.frag"}]})
        cases.append(("oversized", oversized, b"x" * 131073, "fragment asset exceeds 131072 bytes"))
        dimensions = _descriptor(); dimensions["resolution"]["width"] = 63
        cases.append(("dimensions", dimensions, None, "invalid renderer dimensions"))
        gate = _descriptor(); gate["onset_gate"] = 2
        cases.append(("gate", gate, None, "invalid final onset gate"))
        dependency = _descriptor(); dependency["commands"].extend([
            {"address": "/var", "args": [{"type": "s", "value": "a"}, {"type": "f", "value": 0.0}]},
            {"address": "/var", "args": [{"type": "s", "value": "b"}, {"type": "f", "value": 0.0}]},
            {"address": "/var", "args": [{"type": "s", "value": "a"}, {"type": "s", "value": "b + 1"}]},
            {"address": "/var", "args": [{"type": "s", "value": "b"}, {"type": "s", "value": "2 + 2"}]},
        ])
        cases.append(("dependency", dependency, None, "shared expression dependency used before assignment"))

        for label, value, asset, message in cases:
            with self.subTest(label=label), tempfile.TemporaryDirectory(prefix=f"vosc-sc-{label}-") as temporary:
                folder = Path(temporary)
                if asset is not None:
                    (folder / "assets").mkdir()
                    (folder / "assets/large.frag").write_bytes(asset)
                scene = folder / "scene.scd"
                scene.write_text(CLI.render_scene(value))
                unused = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                unused.bind(("127.0.0.1", 0)); port = unused.getsockname()[1]; unused.close()
                return_code, output, pid = _run_scene(scene, port)
                self.assertEqual(return_code, 0, f"owned sclang PID {pid}\n{output}")
                self.assertIn("scene preflight: " + message, output)
                self.assertIsNone(re.search(r"(?m)^VOSC_SCENE_RESULT sent$", output))

    def test_failed_reply_and_shortened_timeout_cleanup_without_exiting_language(self):
        with tempfile.TemporaryDirectory(prefix="vosc-sc-failed-") as temporary:
            scene = Path(temporary) / "scene.scd"
            scene.write_text(CLI.render_scene(_descriptor()))
            renderer = _Renderer(status="failed", frame=-1, detail="owned-failure")
            renderer.start()
            return_code, output, pid = _run_scene(scene, renderer.port)
            renderer.join(timeout=3)
            self.assertIsNone(renderer.error)
            self.assertEqual(return_code, 0, f"owned sclang PID {pid}\n{output}")
            self.assertRegex(output, r"(?m)^VOSC_SCENE_RESULT failed frame=-1 detail=owned-failure$")
            self.assertIn("VOSC_SCENE_UNRELATED_OK", output)
            self.assertIn("VOSC_SCENE_FOLLOWUP_OK", output)

        with tempfile.TemporaryDirectory(prefix="vosc-sc-timeout-") as temporary:
            scene = Path(temporary) / "scene.scd"
            text = CLI.render_scene(_descriptor()).replace("var replyTimeout = 15.0;", "var replyTimeout = 0.2;", 1)
            scene.write_text(text)
            unused = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            unused.bind(("127.0.0.1", 0)); port = unused.getsockname()[1]; unused.close()
            return_code, output, pid = _run_scene(scene, port)
            self.assertEqual(return_code, 0, f"owned sclang PID {pid}\n{output}")
            self.assertEqual(len(re.findall(r"(?m)^VOSC_SCENE_RESULT sent$", output)), 1)
            self.assertEqual(len(re.findall(
                r"(?m)^VOSC_SCENE_RESULT unknown frame=-1 detail=timeout$", output)), 1)
            self.assertIn("VOSC_SCENE_FOLLOWUP_OK", output)

    def test_invalid_applied_frame_and_oversize_detail_cannot_complete(self):
        invalid = [
            ("frame", _Renderer(status="applied", frame=-1, detail="bad-frame")),
            ("detail", _Renderer(status="applied", frame=1, detail="x" * 513)),
        ]
        for label, renderer in invalid:
            with self.subTest(label=label), tempfile.TemporaryDirectory(prefix=f"vosc-sc-reply-{label}-") as temporary:
                scene = Path(temporary) / "scene.scd"
                text = CLI.render_scene(_descriptor()).replace("var replyTimeout = 15.0;", "var replyTimeout = 0.2;", 1)
                scene.write_text(text)
                renderer.start()
                return_code, output, pid = _run_scene(scene, renderer.port)
                renderer.join(timeout=3)
                self.assertIsNone(renderer.error)
                self.assertEqual(return_code, 0, f"owned sclang PID {pid}\n{output}")
                self.assertNotRegex(output, r"(?m)^VOSC_SCENE_RESULT applied")
                self.assertEqual(len(re.findall(
                    r"(?m)^VOSC_SCENE_RESULT unknown frame=-1 detail=timeout$", output)), 1)
                self.assertIn("VOSC_SCENE_UNRELATED_OK", output)
                self.assertIn("VOSC_SCENE_FOLLOWUP_OK", output)


if __name__ == "__main__":
    unittest.main()
