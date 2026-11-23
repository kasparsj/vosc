#!/usr/bin/env python3
"""Exercise managed data through real macOS processes in isolated test homes."""

import hashlib
import json
import os
from pathlib import Path
import plistlib
import select
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest

from macos_build import ROOT, sdk_paths
from test_macos_dmg import disposable_home


STOCK_V1 = {
    "assets/changed": "old!",
    "assets/edited": "stock edit",
    "assets/deleted": "stock deletion",
    "assets/retired": "retire",
    "assets/shared": "already present",
    "assets/linked": "stock link",
    "deleted-dir/value": "removed tree",
}
STOCK_V2 = {
    "assets/changed": "new!",
    "assets/edited": "new stock edit",
    "assets/deleted": "new stock deletion",
    "assets/shared": "new shared",
    "assets/linked": "new stock link",
    "new-dir/addition": "new stock",
    "deleted-dir/another": "new child",
}
PROBE = """
#include "macos/ManagedData.hpp"
#include <iostream>
#include <string>
int main(int argc, char**) {
    try {
        if (!vosc::macos::initializeManagedData(false)) return 2;
        std::cout << vosc::macos::managedDataRoot().string() << std::endl;
        if (argc > 1) {
            std::string input;
            std::getline(std::cin, input);
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
}
"""


class ManagedData(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if sys.platform != "darwin":
            raise unittest.SkipTest("requires macOS")
        temporary = tempfile.TemporaryDirectory(prefix="vosc-native-data-", dir="/tmp")
        cls.addClassCleanup(temporary.cleanup)
        cls.scratch = Path(temporary.name).resolve()
        source = cls.scratch / "probe.cpp"
        source.write_text(PROBE)
        cls.probe = cls.scratch / "probe"
        sdk, _ = sdk_paths()
        subprocess.run([
            "xcrun", "clang++", "-std=c++23", "-fobjc-arc", "-DVOSC_MANAGED_DATA_TESTING",
            "-framework", "AppKit", "-I", ROOT / "src", "-I", sdk / "libs/json/include",
            source, ROOT / "src/macos/ManagedData.mm", "-o", cls.probe,
        ], check=True, timeout=60)

    def setUp(self):
        temporary = tempfile.TemporaryDirectory(dir=self.scratch)
        self.addCleanup(temporary.cleanup)
        self.fixture = Path(temporary.name)
        self.home, self.data = disposable_home(self.fixture, "home")
        self.control = self.data.parent / ".vosc-managed"
        self.old = self.bundle("old", 1, STOCK_V1)
        self.new = self.bundle("new", 2, STOCK_V2)

    def bundle(self, name, generation, files):
        contents = self.fixture / (name + ".app") / "Contents"
        stock = contents / "Resources/data"
        stock.mkdir(parents=True)
        binary = contents / "MacOS/probe"
        binary.parent.mkdir()
        shutil.copy2(self.probe, binary)
        (contents / "Info.plist").write_bytes(plistlib.dumps({
            "VOSCDirectDownload": True, "CFBundleExecutable": binary.name,
            "CFBundleIdentifier": "org.vosc.managed-data-test", "CFBundlePackageType": "APPL",
        }))
        hashes = {}
        for path, value in files.items():
            file = stock / path
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text(value)
            hashes[path] = hashlib.sha256(file.read_bytes()).hexdigest()
        dirs = sorted(p.relative_to(stock).as_posix() for p in stock.rglob("*") if p.is_dir())
        (stock.parent / "vosc-stock.json").write_text(json.dumps({
            "schema": 1, "generation": generation, "files": hashes, "directories": dirs,
        }, sort_keys=True))
        return binary

    def env(self, crash=None):
        env = dict(os.environ, HOME=str(self.home), CFFIXED_USER_HOME=str(self.home))
        env.pop("VOSC_DATA_TEST_CRASH", None)
        env.pop("VOSC_DATA_TEST_STOP", None)
        if crash:
            env["VOSC_DATA_TEST_CRASH"] = crash
        return env

    def launch(self, binary, *, crash=None, code=0):
        result = subprocess.run([binary], env=self.env(crash), cwd=self.fixture,
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, code, result.stderr)
        if code == 0:
            self.assertEqual(Path(result.stdout.strip()), self.data)
        return result

    def state(self):
        return json.loads((self.control / "state.json").read_text())

    def test_upgrade_keeps_edits_retired_files_deletions_and_late_editor_write(self):
        self.launch(self.old)
        (self.data / "assets/edited").write_text("personal edit")
        (self.data / "assets/deleted").unlink()
        shutil.rmtree(self.data / "deleted-dir")
        (self.data / "personal").write_text("added")
        with (self.data / "assets/changed").open("r+") as editor:
            self.launch(self.new)
            self.assertEqual((self.data / "assets/changed").read_text(), "new!")
            self.launch(self.old)
            self.assertEqual(self.state()["generation"], 2)
            editor.write("late")
            editor.flush()
        recovery, = (self.control / "recovery").glob("*/files/assets/changed")
        self.assertEqual(recovery.read_text(), "late")
        self.assertEqual((self.data / "assets/edited").read_text(), "personal edit")
        self.assertEqual((self.data / "assets/retired").read_text(), "retire")
        self.assertEqual((self.data / "new-dir/addition").read_text(), "new stock")
        self.assertEqual((self.data / "personal").read_text(), "added")
        self.assertFalse((self.data / "assets/deleted").exists())
        self.assertFalse((self.data / "deleted-dir").exists())
        self.launch(self.new)
        clash = self.bundle("clash", 2, {"assets/changed": "clash"})
        self.assertIn("same generation", self.launch(clash, code=1).stderr)

    def test_add_only_adoption_preserves_identical_files_and_unsafe_conflicts(self):
        assets = self.data / "assets"
        assets.mkdir(parents=True)
        shared = assets / "shared"
        shared.write_text(STOCK_V1["assets/shared"])
        sentinel = self.fixture / "outside"
        sentinel.write_text("outside words")
        os.link(sentinel, assets / "linked")
        (assets / "edited").symlink_to(sentinel)
        (assets / "deleted").mkdir()
        self.launch(self.old)
        self.launch(self.new)
        self.assertEqual(shared.read_text(), STOCK_V1["assets/shared"])
        self.assertNotIn("assets/shared", self.state()["owned"])
        self.assertEqual(sentinel.read_text(), "outside words")
        self.assertTrue((assets / "edited").is_symlink())
        self.assertEqual((assets / "linked").stat().st_ino, sentinel.stat().st_ino)
        self.assertTrue((assets / "deleted").is_dir())

    def test_removing_a_preexisting_stock_directory_blocks_new_descendants(self):
        (self.data / "deleted-dir").mkdir(parents=True)
        self.launch(self.old)
        shutil.rmtree(self.data / "deleted-dir")
        self.launch(self.new)
        self.assertFalse((self.data / "deleted-dir").exists())
        self.assertIn("deleted-dir", self.state()["deleted_directories"])

    def test_seed_recovers_each_interruption_checkpoint(self):
        for point in ("before_journal", "journal_published", "root_published", "before_commit", "state_committed"):
            with self.subTest(point=point):
                original = self.home, self.data, self.control
                self.home, self.data = disposable_home(self.fixture, point)
                self.control = self.data.parent / ".vosc-managed"
                self.launch(self.old, crash=point, code=73)
                self.launch(self.old)
                for name, value in STOCK_V1.items():
                    self.assertEqual((self.data / name).read_text(), value)
                self.assertEqual(self.state()["generation"], 1)
                self.assertEqual(list(self.control.glob("txn-*")), [])
                self.assertEqual(list(self.control.glob("stage-*")), [])
                self.home, self.data, self.control = original

    def test_older_app_resumes_interrupted_refresh_from_staged_bytes(self):
        self.launch(self.old)
        self.launch(self.new, crash="file_published", code=73)
        self.launch(self.old)
        self.assertEqual(self.state()["generation"], 2)
        self.assertEqual((self.data / "assets/changed").read_text(), "new!")
        self.assertEqual((self.data / "new-dir/addition").read_text(), "new stock")
        self.launch(self.new)

    def test_seed_recovery_rejects_altered_bytes_before_publication(self):
        self.launch(self.old, crash="journal_published", code=73)
        work, = self.control.glob("txn-*")
        staged = work / "tree/assets/changed"
        staged.write_text("tampered")
        self.assertIn("no longer matches", self.launch(self.old, code=1).stderr)
        self.assertFalse(self.data.exists())
        staged.write_text("old!")
        self.launch(self.old)

    def test_upgrade_recovery_rejects_ambiguous_missing_stage(self):
        self.launch(self.old)
        self.launch(self.new, crash="journal_published", code=73)
        work, = self.control.glob("txn-*")
        staged = work / "files/new-dir/addition"
        parked = self.fixture / "parked"
        staged.rename(parked)
        collision = self.data / "new-dir/addition"
        collision.parent.mkdir()
        collision.write_text("user bytes")
        self.assertIn("ambiguous stock publication", self.launch(self.new, code=1).stderr)
        self.assertEqual(collision.read_text(), "user bytes")
        self.assertEqual((self.data / "assets/changed").read_text(), "old!")
        self.assertEqual(self.state()["generation"], 1)
        parked.rename(staged)
        self.launch(self.new)
        self.assertNotIn("new-dir/addition", self.state()["owned"])
        self.assertEqual(collision.read_text(), "user bytes")

    def test_journal_loss_after_exchange_preserves_displaced_original(self):
        self.launch(self.old)
        self.launch(self.new, crash="before_commit", code=73)
        work, = self.control.glob("txn-*")
        journal = work / "journal.json"
        saved = self.fixture / "saved-journal"
        journal.rename(saved)
        displaced = work / "files/assets/changed"
        displaced.write_text("late")
        self.assertIn("missing journal", self.launch(self.new, code=1).stderr)
        self.assertEqual(displaced.read_text(), "late")
        self.assertEqual(self.state()["generation"], 1)
        saved.rename(journal)
        self.launch(self.new)
        recovery, = (self.control / "recovery").glob("*/files/assets/changed")
        self.assertEqual(recovery.read_text(), "late")

    def test_concurrent_first_launch_and_shared_reader(self):
        first = subprocess.Popen([self.old, "--hold"], cwd=self.fixture, env=self.env(),
                                 stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            self.launch(self.old)
            readable, _, _ = select.select([first.stdout], [], [], 5)
            self.assertTrue(readable, "first launch did not acquire its shared reader lock")
            self.assertEqual(Path(first.stdout.readline().strip()), self.data)
            self.assertIn("another VOSC window", self.launch(self.new, code=1).stderr)
        finally:
            _, err = first.communicate(input="quit\n", timeout=10)
        self.assertEqual(first.returncode, 0, err)
        self.launch(self.new)
        self.assertEqual(self.state()["generation"], 2)

    def test_refresh_restores_directory_that_races_the_exchange(self):
        self.launch(self.old)
        env = self.env()
        env["VOSC_DATA_TEST_STOP"] = "before_exchange"
        process = subprocess.Popen([self.new], cwd=self.fixture, env=env,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            until = time.monotonic() + 5
            while time.monotonic() < until:
                pid, status = os.waitpid(process.pid, os.WUNTRACED | os.WNOHANG)
                if pid:
                    self.assertTrue(os.WIFSTOPPED(status), "probe exited before exchange")
                    break
                time.sleep(.01)
            else:
                self.fail("probe did not reach the exchange")
            live = self.data / "assets/changed"
            live.rename(self.fixture / "saved")
            live.mkdir()
            (live / "personal").write_text("preserved")
            process.send_signal(signal.SIGCONT)
            _, err = process.communicate(timeout=10)
            self.assertEqual(process.returncode, 0, err)
            self.assertIn("Stock update skipped", err)
            self.assertEqual((live / "personal").read_text(), "preserved")
            self.assertEqual((self.fixture / "saved").read_text(), "old!")
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate(timeout=5)

    def test_corrupt_state_and_unwritable_root_fail_then_recover(self):
        self.launch(self.old)
        state = self.control / "state.json"
        valid = state.read_bytes()
        state.write_text("{")
        self.assertIn("invalid metadata", self.launch(self.new, code=1).stderr)
        self.assertEqual((self.data / "assets/changed").read_text(), "old!")
        state.write_bytes(valid)
        self.data.chmod(0o500)
        try:
            self.assertIn("writable", self.launch(self.new, code=1).stderr)
        finally:
            self.data.chmod(0o700)
        self.launch(self.new)

    def test_missing_managed_root_is_not_recreated(self):
        self.launch(self.old)
        parked = self.data.with_name("saved")
        self.data.rename(parked)
        self.assertIn("Restore the data folder", self.launch(self.new, code=1).stderr)
        self.assertFalse(self.data.exists())
        parked.rename(self.data)
        self.launch(self.new)


if __name__ == "__main__":
    unittest.main()
