import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from avahi_browse import peer_from_record


TARGET = b"09CC7FDE-ECDA-4E7C-8EE3-B25F19D72E93"
VALID_TXT = [b"role=draft", b"version=1", b"target=" + TARGET]


class BrowseTests(unittest.TestCase):
    def test_compatible_record_preserves_the_advertised_target(self):
        self.assertEqual(
            peer_from_record(b"VOSC Lab", "192.168.1.10", 2, 47491, VALID_TXT),
            {"name": "VOSC Lab", "target": TARGET.decode(),
             "endpoint": {"host": "192.168.1.10", "port": 47491}},
        )

    def test_link_local_address_includes_the_discovering_interface(self):
        with patch("avahi_browse.socket.if_indextoname", return_value="eth0") as interface:
            peer = peer_from_record(b"VOSC Lab", "fe80::a", 9, 47491, VALID_TXT)
            self.assertEqual(peer["endpoint"]["host"], "fe80::a%eth0")
            interface.assert_called_once_with(9)

    def test_incompatible_and_ambiguous_records_are_ignored(self):
        for txt in (
            VALID_TXT[:-1],
            [b"role=other", *VALID_TXT[1:]],
            [VALID_TXT[0], b"version=2", VALID_TXT[2]],
            [*VALID_TXT[:2], b"target=" + TARGET.replace(b"-", b"")],
            [*VALID_TXT[:2], b"target=" + TARGET + b"\0extra"],
            [*VALID_TXT, b"TARGET=" + TARGET],
        ):
            with self.subTest(txt=txt):
                self.assertIsNone(peer_from_record(b"VOSC Lab", "192.168.1.10", 2, 47491, txt))

    def test_invalid_names_and_endpoints_are_ignored(self):
        for name, host, port in (
            (b"Invalid\nName", "192.168.1.10", 47491),
            (b"x" * 64, "192.168.1.10", 47491),
            (b"VOSC Lab", "0.0.0.0", 47491),
            (b"VOSC Lab", "ff02::1", 47491),
            (b"VOSC Lab", "192.168.1.10", 0),
        ):
            with self.subTest(name=name, host=host, port=port):
                self.assertIsNone(peer_from_record(name, host, 2, port, VALID_TXT))

    @unittest.skipUnless(sys.platform == "linux", "Avahi discovery runs on Linux")
    def test_missing_system_bus_yields_a_discovery_error(self):
        with tempfile.TemporaryDirectory() as directory:
            output = subprocess.check_output(
                [sys.executable, "-I", "-B", Path(__file__).with_name("avahi_browse.py")],
                env={**os.environ, "DBUS_SYSTEM_BUS_ADDRESS": f"unix:path={directory}/absent.sock"},
                timeout=7,
            )
        report = json.loads(output)
        self.assertEqual(report["peers"], [])
        self.assertIn("LAN discovery is unavailable", report["error"])


if __name__ == "__main__":
    unittest.main()
