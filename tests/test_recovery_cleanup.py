import importlib.util
import io
import json
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from unittest.mock import Mock, call, patch


scripts = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(scripts))
spec = importlib.util.spec_from_file_location("scene_recipe_harness", scripts / "test_scene_recipe.py")
harness = importlib.util.module_from_spec(spec)
spec.loader.exec_module(harness)


def renderer(process, binary, socket, log, stream, *, linux_identity=None):
    owned = object.__new__(harness.OwnedRenderer)
    owned.process = process
    owned.binary = binary
    owned.socket = socket
    owned.log = log
    owned.stream = stream
    owned.runtime = log.parent / "runtime"
    owned.owner = {"sanitizers_expected": []}
    owned.linux_identity = linux_identity
    return owned


class OwnedCleanupTests(unittest.TestCase):
    def test_source_change_invalidates_successful_matrix(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            binary, output = root / "owned", root / "evidence"
            binary.write_bytes(b"test executable identity")
            output.mkdir()
            owned = Mock()
            owned.owner = {"pid": 123}
            owned.stop.return_value = {"return_code": 0, "forced_kill": False, "socket_removed": True}
            with patch.object(harness, "OwnedRenderer", return_value=owned), \
                    patch.object(harness, "run_native_matrix", return_value={"matrix": "PASS"}), \
                    patch.object(harness, "source_snapshot", side_effect=[{"sha256": "before"}, {"sha256": "after"}]):
                with self.assertRaisesRegex(RuntimeError, "sources changed"):
                    harness.run_matrix_only(binary, output, [], True, None)
            receipt = json.loads((output / "matrix-evidence.json").read_text())
            self.assertEqual(receipt["status"], "FAILED")
            self.assertFalse(receipt["source_stable"])

    def test_cleanup_failure_keeps_original_failure_and_writes_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            binary, output = root / "owned", root / "evidence"
            binary.write_bytes(b"test executable identity")
            output.mkdir()
            owned = Mock()
            owned.owner = {"pid": 123}
            owned.stop.side_effect = subprocess.TimeoutExpired("owned", 8)
            with patch.object(harness, "OwnedRenderer", return_value=owned), \
                    patch.object(harness, "run_native_matrix", side_effect=RuntimeError("setup failed")), \
                    patch.object(harness, "source_snapshot", return_value={"sha256": "stable"}):
                with self.assertRaisesRegex(RuntimeError, "setup failed"):
                    harness.run_matrix_only(binary, output, [], True, None)
            evidence = json.loads((output / "matrix-evidence.json").read_text())
            self.assertEqual(evidence["error"], "RuntimeError('setup failed')")
            self.assertIn("TimeoutExpired", evidence["cleanup_error"])
            self.assertEqual(evidence["cleanup"]["pid"], 123)
            self.assertIn("unexpected cleanup exception", evidence["cleanup"]["cleanup_errors"][0])
            self.assertEqual(evidence["status"], "FAILED")

    def test_successful_matrix_with_cleanup_exception_is_failed_and_nonzero(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            binary, output = root / "owned", root / "evidence"
            binary.write_bytes(b"test executable identity")
            output.mkdir()
            owned = Mock()
            owned.owner = {"pid": 456}
            owned.process = Mock(pid=456, returncode=0)
            owned.socket = Mock(exists=lambda: False)
            owned.stop.side_effect = RuntimeError("unexpected stop failure")
            stdout = io.StringIO()
            with patch.object(harness, "OwnedRenderer", return_value=owned), \
                    patch.object(harness, "run_native_matrix", return_value={"matrix": "PASS"}), \
                    patch.object(harness, "source_snapshot", return_value={"sha256": "stable"}), \
                    redirect_stdout(stdout):
                with self.assertRaisesRegex(RuntimeError, "unexpected cleanup exception"):
                    harness.run_matrix_only(binary, output, [], True, None)
            evidence = json.loads((output / "matrix-evidence.json").read_text())
            self.assertEqual(evidence["status"], "FAILED")
            self.assertEqual(evidence["matrix"], {"matrix": "PASS"})
            self.assertEqual(evidence["cleanup"]["pid"], 456)
            self.assertNotIn("PASS native recipe matrix", stdout.getvalue())

    def test_invalid_cleanup_receipt_cannot_pass_matrix(self):
        invalid_receipts = [
            {"return_code": 7, "forced_kill": False, "socket_removed": True},
            {"return_code": 0, "forced_kill": True, "socket_removed": True},
            {"return_code": 0, "forced_kill": False, "socket_removed": False},
        ]
        for receipt in invalid_receipts:
            with self.subTest(receipt=receipt), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                binary, output = root / "owned", root / "evidence"
                binary.write_bytes(b"test executable identity")
                output.mkdir()
                owned = Mock()
                owned.owner = {"pid": 789}
                owned.stop.return_value = receipt
                with patch.object(harness, "OwnedRenderer", return_value=owned), \
                        patch.object(harness, "run_native_matrix", return_value={"matrix": "PASS"}), \
                        patch.object(harness, "source_snapshot", return_value={"sha256": "stable"}):
                    with self.assertRaisesRegex(RuntimeError, "invalid cleanup"):
                        harness.run_matrix_only(binary, output, [], True, None)
                evidence = json.loads((output / "matrix-evidence.json").read_text())
                self.assertEqual(evidence["status"], "FAILED")
                self.assertEqual(evidence["cleanup"], receipt)

    def test_hung_owned_process_is_killed_and_reaped(self):
        for platform in ("linux", "darwin"):
            with self.subTest(platform=platform), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                binary, log = root / "owned", root / "renderer.log"
                log.write_text("")
                identity = {"executable": str(binary), "start_ticks": 100}
                process = Mock(pid=123, returncode=-signal.SIGKILL)
                process.poll.return_value = None
                process.wait.side_effect = [subprocess.TimeoutExpired("owned", 8), -signal.SIGKILL]
                stream = io.BytesIO()
                owned = renderer(process, binary, Mock(exists=lambda: False), log, stream,
                                 linux_identity=identity if platform == "linux" else None)
                cleanup = Mock()
                cleanup.attach_mock(process.send_signal, "send_signal")
                cleanup.attach_mock(process.wait, "wait")
                with (
                    patch.object(owned, "read_linux_identity", return_value=identity.copy()),
                    patch.object(harness.subprocess, "check_output", return_value=str(binary) + "\n") as ps,
                    patch.object(harness.os, "kill", cleanup.kill),
                    self.assertRaises(harness.RendererCleanupError) as raised,
                ):
                    owned.stop()

                if platform == "linux":
                    ps.assert_not_called()
                    term = call.send_signal(signal.SIGTERM)
                else:
                    ps.assert_called_once_with(["ps", "-p", "123", "-o", "comm="], text=True)
                    term = call.kill(123, signal.SIGTERM)
                self.assertEqual(cleanup.mock_calls, [
                    term, call.wait(timeout=8), call.send_signal(signal.SIGKILL), call.wait(timeout=8),
                ])
                self.assertEqual(raised.exception.receipt["identity"], str(binary))
                self.assertEqual(raised.exception.receipt["return_code"], -signal.SIGKILL)
                self.assertTrue(raised.exception.receipt["forced_kill"])
                self.assertIn("forced kill", str(raised.exception))
                self.assertTrue(stream.closed)

    def test_identity_mismatch_never_signals_process(self):
        cases = (
            ("linux executable", {"executable": "/someone/else"}),
            ("linux start ticks", {"start_ticks": 101}),
            ("darwin executable", None),
        )
        for scenario, changed_identity in cases:
            with self.subTest(scenario=scenario), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                binary, log = root / "owned", root / "renderer.log"
                log.write_text("")
                identity = {"executable": str(binary), "start_ticks": 100}
                process, stream = Mock(pid=123, returncode=None), io.BytesIO()
                process.poll.return_value = None
                owned = renderer(process, binary, Mock(exists=lambda: False), log, stream,
                                 linux_identity=identity if changed_identity is not None else None)
                message = ("unexpected renderer PID 123" if changed_identity is not None
                           else "unexpected process identity")
                with (
                    patch.object(owned, "read_linux_identity", return_value={**identity, **(changed_identity or {})}),
                    patch.object(harness.subprocess, "check_output", return_value="/someone/else\n"),
                    patch.object(harness.os, "kill") as kill,
                    self.assertRaisesRegex(harness.RendererCleanupError, message) as raised,
                ):
                    owned.stop()

                self.assertEqual(raised.exception.receipt["pid"], 123)
                kill.assert_not_called()
                process.send_signal.assert_not_called()
                process.terminate.assert_not_called()
                process.kill.assert_not_called()
                process.wait.assert_not_called()
                self.assertTrue(stream.closed)

    def test_log_closes_even_if_reaping_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            binary, log = root / "owned", root / "renderer.log"
            log.write_text("")
            identity = {"executable": str(binary), "start_ticks": 100}
            process, stream = Mock(pid=123, returncode=None), io.BytesIO()
            process.poll.return_value = None
            process.wait.side_effect = subprocess.TimeoutExpired("owned", 8)
            owned = renderer(process, binary, Mock(exists=lambda: False), log, stream, linux_identity=identity)
            with (
                patch.object(owned, "read_linux_identity", return_value=identity.copy()),
                patch.object(harness.os, "kill"),
                self.assertRaisesRegex(harness.RendererCleanupError, "TimeoutExpired"),
            ):
                owned.stop()

            self.assertEqual(process.send_signal.call_args_list, [call(signal.SIGTERM), call(signal.SIGKILL)])
            self.assertEqual(process.wait.call_args_list, [call(timeout=8), call(timeout=8)])
            self.assertTrue(stream.closed)

    def test_stop_rejects_nonzero_exit_and_retained_socket(self):
        cases = [
            (5, False, "exited nonzero"),
            (0, True, "retained control socket"),
        ]
        for return_code, socket_exists, message in cases:
            with self.subTest(return_code=return_code, socket_exists=socket_exists), \
                    tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                binary, log = root / "owned", root / "renderer.log"
                binary.write_bytes(b"owned"); log.write_text("")
                process = Mock(pid=123, returncode=return_code)
                process.poll.return_value = return_code
                stream = io.BytesIO()
                owned = renderer(process, binary, Mock(exists=lambda: socket_exists), log, stream)
                with self.assertRaisesRegex(harness.RendererCleanupError, message) as raised:
                    owned.stop()
                self.assertEqual(raised.exception.receipt["return_code"], return_code)
                self.assertEqual(raised.exception.receipt["socket_removed"], not socket_exists)
                self.assertTrue(stream.closed)

    def test_full_flow_generic_cleanup_exception_persists_failed_receipt(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            binary, sclang, output = root / "owned", root / "sclang", root / "evidence"
            binary.write_bytes(b"owned binary")
            sclang.write_bytes(b"owned sclang")
            quark = root / "VisualsQuark"
            quark.mkdir()
            (quark / "OSCVisuals.sc").write_text("OSCVisuals {}\n")
            owned = Mock()
            owned.owner = {"pid": 321}
            owned.client.return_value.call.side_effect = [
                {"result": {"onset_gate": False}, "external_epoch": 0},
                {"result": {"onset_gate": False}, "external_epoch": 0},
                {"result": {"onset_gate": True}, "external_epoch": 2},
            ]
            owned.process = Mock(pid=321, returncode=None)
            owned.socket = Mock(exists=lambda: False)
            owned.stop.side_effect = ValueError("unexpected cleanup failure")
            argv = ["test_scene_recipe.py", "--binary", str(binary), "--sclang", str(sclang),
                    "--output", str(output), "--allow-owned-restart", "--visuals-quark", str(quark)]
            with patch.object(sys, "argv", argv), \
                    patch.object(harness, "probe_visuals_quark", return_value={"api_version": 1}), \
                    patch.object(harness, "OwnedRenderer", return_value=owned), \
                    patch.object(harness, "osc"), \
                    patch.object(harness, "resize_scene", return_value={"owned": True}), \
                    patch.object(harness, "configure_scene", side_effect=RuntimeError("primary flow failure")), \
                    patch.object(harness, "source_snapshot", return_value={"sha256": "stable"}):
                with self.assertRaisesRegex(RuntimeError, "primary flow failure"):
                    harness.main()
            evidence = json.loads((output / "evidence.json").read_text())
            self.assertEqual(evidence["status"], "FAILED")
            self.assertEqual(evidence["error"], "RuntimeError('primary flow failure')")
            self.assertEqual(evidence["producer_cleanup"]["pid"], 321)
            self.assertIn("unexpected cleanup exception", evidence["cleanup_errors"][0])


class SclangEndpointTests(unittest.TestCase):
    def test_default_endpoint_rewrites_once_and_owned_endpoint_reverifies(self):
        source = "(\nvar rendererPort = 33333; // editable\n)\n"
        rewritten, mode = harness.prepare_scene_endpoint(source, 45678)
        self.assertEqual(mode, "rewritten-default")
        self.assertIn("var rendererPort = 45678; // editable", rewritten)
        self.assertNotIn("33333", rewritten)
        verified, mode = harness.prepare_scene_endpoint(rewritten, 45678)
        self.assertEqual(mode, "verified-owned")
        self.assertEqual(rewritten, verified)

    def test_unowned_default_and_format_drift_fail_before_sclang_evaluation(self):
        hostile_sources = [
            "var rendererPort = 44444;\n",
            "var rendererPort=33333;\n",
            "var rendererPort = 33333; executeSomethingUnexpected\n",
            "var rendererPort = 33333;\nvar rendererPort = 33333;\n",
        ]
        for index, source in enumerate(hostile_sources):
            with self.subTest(source=source), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                scene, output, sclang = root / "scene.scd", root / "output", root / "sclang"
                scene.write_text(source)
                output.mkdir(); sclang.write_bytes(b"owned")
                with patch.object(harness.subprocess, "run") as run:
                    with self.assertRaises(ValueError):
                        harness.run_sclang(sclang, scene, output, 45678, f"hostile-{index}")
                run.assert_not_called()
                self.assertEqual(source, scene.read_text())
                self.assertFalse((output / f"sclang-owned-hostile-{index}").exists())

    def test_default_port_is_never_accepted_as_owned(self):
        with self.assertRaisesRegex(ValueError, "default renderer port"):
            harness.prepare_scene_endpoint("var rendererPort = 33333;\n", 33333)


if __name__ == "__main__":
    unittest.main()
