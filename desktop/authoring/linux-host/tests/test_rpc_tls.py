import asyncio
import hashlib
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import AsyncMock, Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import rpc_tls


class Reader:
    def __init__(self, message):
        self.message = message
        self.cancelled = False

    async def read(self, size):
        if size == 1:  # Pre-TLS reservation watch.
            await asyncio.Future()
        if self.message is not None:
            chunk, self.message = self.message, None
            return chunk
        try:
            await asyncio.Future()
        except asyncio.CancelledError:
            self.cancelled = True
            raise


class Writer:
    def __init__(self, *, blocked=False, peer=None):
        self.transport = Mock()
        self.writes = []
        self.blocked = blocked
        self.peer = peer
        self.drain_cancelled = False

    def write(self, data):
        self.writes.append(data)

    def get_extra_info(self, name):
        assert name == "ssl_object"
        return self.peer

    async def drain(self):
        if self.blocked:
            try:
                await asyncio.Future()
            except asyncio.CancelledError:
                self.drain_cancelled = True
                raise


class RelayTests(unittest.IsolatedAsyncioTestCase):
    async def test_either_stalled_write_ends_the_authenticated_session(self):
        self.assertEqual(rpc_tls.RELAY_PROGRESS_SECONDS, 15)
        certificate = b"fixture peer certificate"
        pins = {hashlib.sha256(certificate).hexdigest()}
        peer = SimpleNamespace(session_reused=False, getpeercert=Mock(return_value=certificate))

        for side in ("bridge", "tls"):
            with self.subTest(side=side):
                bridge_reader, tls_reader = Reader(b"reply\n"), Reader(b"request\n")
                bridge_writer = Writer(blocked=side == "bridge")
                tls_writer = Writer(blocked=side == "tls", peer=peer)
                raw = Mock()
                deadline = asyncio.get_running_loop().time() + 1
                with (
                    patch.object(rpc_tls, "RELAY_PROGRESS_SECONDS", 0.02),
                    patch.object(rpc_tls.asyncio, "open_unix_connection",
                                 AsyncMock(return_value=(bridge_reader, bridge_writer))),
                    patch.object(rpc_tls, "secure_socket", AsyncMock(return_value=(tls_reader, tls_writer))),
                    patch.object(rpc_tls, "bridge_check", AsyncMock(return_value=True)),
                ):
                    await asyncio.wait_for(rpc_tls.session(raw, deadline, "fixture", "token", lambda: (None, pins)), 0.5)

                self.assertEqual(bridge_writer.writes, [b"request\n"])
                self.assertEqual(tls_writer.writes, [b"reply\n"])
                bridge_writer.transport.abort.assert_called_once_with()
                tls_writer.transport.abort.assert_called_once_with()
                stalled = bridge_writer if side == "bridge" else tls_writer
                opposite = bridge_reader if side == "bridge" else tls_reader
                self.assertTrue(stalled.drain_cancelled)
                self.assertTrue(opposite.cancelled)
                raw.close.assert_not_called()

    async def test_waiting_for_next_frame_does_not_consume_the_write_budget(self):
        chunks = iter((b"first\n", b"second\n", b""))

        async def receive(_):
            await asyncio.sleep(0.04)
            return next(chunks)

        reader, writer = SimpleNamespace(read=receive), Writer()
        with patch.object(rpc_tls, "RELAY_PROGRESS_SECONDS", 0.02):
            await asyncio.wait_for(rpc_tls.relay(reader, writer), 0.5)
        self.assertEqual(writer.writes, [b"first\n", b"second\n"])


if __name__ == "__main__":
    unittest.main()
