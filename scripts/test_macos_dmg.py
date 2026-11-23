#!/usr/bin/env python3
"""Smoke-test the signed VOSC binary and Sieve example on a read-only macOS DMG."""

import argparse
from contextlib import closing, contextmanager
import hashlib
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time

from control_client import Client
from test_native_scene_cli import observe, read_state, require
from test_scene_recipe import capture, free_udp_port, osc, wait_socket

SIZE = (1024, 768)
TEXTURES = {"srctex": "black_hole.jpg", "sieve": "square_3x3.png"}
WHITE = (1, 1, 1, 1)
BLACK = (0, 0, 0, 1)


def command(*args):
    result = subprocess.run(args, capture_output=True, timeout=60)
    require(result.returncode == 0,
            f"{args[0]} exited {result.returncode}: {result.stderr.decode(errors='replace').strip()}")
    return result.stdout


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def mounted_device(entities, point):
    return next((item["dev-entry"] for item in entities
                 if item.get("mount-point") == str(point)), None)


@contextmanager
def mount_read_only(dmg, point, evidence):
    evidence.update(point=str(point), detached=False)
    device = None
    try:
        attach = command("/usr/bin/hdiutil", "attach", "-readonly", "-nobrowse", "-plist",
                         "-mountpoint", point, dmg)
        entities = plistlib.loads(attach)["system-entities"]
        volume = mounted_device(entities, point)
        device = volume or next((item["dev-entry"] for item in entities
                                 if re.fullmatch(r"/dev/disk[0-9]+", item.get("dev-entry", ""))), None)
        evidence["device"] = device
        evidence["read_only"] = bool(os.statvfs(point).f_flag & os.ST_RDONLY)
        require(volume and os.path.ismount(point) and evidence["read_only"],
                f"expected a read-only mount at {point}")
        yield
    finally:
        if device is None and os.path.ismount(point):
            # Recover only the mount at this run's private path after an interrupted attach.
            info = plistlib.loads(command("/usr/bin/hdiutil", "info", "-plist"))
            for image in info["images"]:
                device = mounted_device(image["system-entities"], point)
                if device:
                    break
            evidence["device"] = device
        if device:
            command("/usr/bin/hdiutil", "detach", device)
        evidence["detached"] = not os.path.ismount(point)
        require(evidence["detached"], f"could not detach the run's mount at {point}")


class ReleaseRenderer:
    def __init__(self, binary, manifest, scratch, log):
        self.binary = binary
        self.manifest = manifest
        self.cwd = scratch
        self.log = log
        self.runtime = Path(tempfile.mkdtemp(prefix="renderer-", dir=scratch))
        self.socket = self.runtime / "control.sock"
        self.port = free_udp_port()

    def client(self):
        return Client(self.socket)

    @contextmanager
    def run(self, evidence, *, negative=False):
        env = {name: value for name, value in os.environ.items() if not name.startswith("VOSC_")}
        env.update(LC_ALL="C", VOSC_CONTROL_SOCKET=str(self.socket), VOSC_OSC_PORT=str(self.port))
        with self.log.open("wb") as stream:
            self.process = subprocess.Popen(
                [self.binary, "--scene", self.manifest], cwd=self.cwd, env=env,
                stdout=stream, stderr=subprocess.STDOUT, start_new_session=True,
            )
            evidence.update(pid=self.process.pid, port=self.port, log=self.log.name,
                            binary=str(self.binary), manifest=str(self.manifest))
            try:
                yield
            except (Exception, KeyboardInterrupt) as error:
                evidence["error"] = str(error) or type(error).__name__
                raise
            finally:
                sent_term = self.process.poll() is None
                forced_kill = False
                if sent_term:
                    self.process.terminate()
                    try:
                        self.process.wait(timeout=8)
                    except subprocess.TimeoutExpired:
                        forced_kill = True
                        self.process.kill()
                        self.process.wait(timeout=8)
                return_code = self.process.returncode
                socket_removed = not self.socket.exists()
                evidence["cleanup"] = dict(return_code=return_code, sent_sigterm=sent_term,
                                           forced_kill=forced_kill, socket_removed=socket_removed)
                require(not forced_kill and socket_removed, "renderer cleanup did not complete normally")
                if not negative:
                    require(sent_term and return_code in (0, signal.SIGTERM),
                            f"unexpected renderer exit (status {return_code}, SIGTERM sent={sent_term})")


def matching_capture(client, path):
    width, height, pixels, evidence = capture(client, path, max_width=SIZE[0])
    require((width, height) == SIZE, f"expected preview {SIZE}, got {(width, height)}")
    evidence.pop("data_base64")
    return pixels, evidence


def pixel_change(before, after):
    require(len(before) == len(after), "Sieve captures have different pixel counts")
    changed = expected = 0
    unchanged = set()
    for old, new in zip(before, after):
        old_rgb, new_rgb = old[:3], new[:3]
        if old_rgb == new_rgb or max(abs(a - b) for a, b in zip(old_rgb, new_rgb)) <= 8:
            unchanged.add(new_rgb)
        else:
            changed += 1
            expected += min(old_rgb) >= 247 and max(new_rgb) <= 8
    return {"total": len(before), "changed": changed, "white_to_black": expected,
            "white_to_black_fraction": expected / len(before), "unchanged_rgb_colors": len(unchanged)}


def prove_sieve(renderer, root, output, label, evidence, host_arch):
    before, observation = observe(renderer, output, f"{label}-before", SIZE, native=True, sieve=True)
    ready, = observation["ready"]
    require(re.fullmatch(r"VOSC_SCENE_READY frame=[1-9][0-9]* title=Sieve", ready), ready)
    log = renderer.log.read_text(errors="replace")
    arch = re.search(r"ARM64: (Yes|No)", log)
    require(arch, "renderer did not report its executing architecture")
    evidence["executing_architecture"] = "arm64" if arch[1] == "Yes" else "x86_64"
    require(evidence["executing_architecture"] == host_arch, "renderer is not running natively")
    shader = observation["state"]["layers"][0]["shader"]
    evidence.update(ready=ready, textures=shader["textures"], before=observation["capture"])
    for sampler, filename in TEXTURES.items():
        source = Path(shader["textures"][sampler]["source"]).resolve(strict=True)
        require(source == root / "examples/sieve/assets" / filename, f"{sampler} loaded from {source}")
    evidence["baseline_vColor"] = json.loads(shader["uniforms"]["vColor"]["rendered_value"])
    require(tuple(evidence["baseline_vColor"]) == WHITE, "unexpected baseline vColor")

    with closing(renderer.client()) as client:
        repeat, _ = matching_capture(client, output / f"{label}-baseline-repeat.png")
        require(before == repeat and any(pixel[:3] != before[0][:3] for pixel in before),
                "Sieve baseline changed or is flat")
        osc(renderer.port, "/shader/var", 0, "vColor", *map(float, BLACK))
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            uniform = read_state(client)["layers"][0]["shader"]["uniforms"]["vColor"]
            evidence["cue_vColor"] = json.loads(uniform["rendered_value"])
            if tuple(evidence["cue_vColor"]) == BLACK:
                after, receipt = matching_capture(client, output / f"{label}-after.png")
                stats = pixel_change(before, after)
                evidence.update(after=receipt, pixels=stats)
                if (0.20 <= stats["white_to_black_fraction"] <= 0.35
                        and stats["white_to_black"] >= 0.99 * stats["changed"]
                        and stats["unchanged_rgb_colors"] > 1):
                    return
            time.sleep(0.05)
        raise RuntimeError(f"Sieve cue did not produce the expected pixel change: {evidence.get('pixels')}")


def check_layout(binary, root, scratch, output, label, evidence, host_arch):
    root = root.resolve(strict=True)
    example = root / "examples/sieve"
    for path in (example / "scene.vosc.json", example / "assets/sieve.frag",
                 root / "shaders/passthru.vert",
                 *(example / "assets" / name for name in TEXTURES.values())):
        require(path.is_file() and path.resolve().is_relative_to(root), f"missing packaged file {path}")
    evidence["data_root"] = str(root)
    evidence["attempts"] = []
    for attempt in (1, 2):
        run = {}
        evidence["attempts"].append(run)
        renderer = ReleaseRenderer(binary, example / "scene.vosc.json", scratch,
                                   output / f"{label}-{attempt}.log")
        with renderer.run(run):
            wait_socket(renderer.process, renderer.socket, renderer.log)
            udp = subprocess.run(
                ["/usr/sbin/lsof", "-nP", "-a", "-p", str(renderer.process.pid),
                 f"-iUDP:{renderer.port}"], capture_output=True, text=True, timeout=5,
            )
            run["udp_listener"] = udp.stdout.strip()
            if udp.returncode == 1 and not udp.stderr:
                run["setup_error"] = "renderer did not bind its assigned OSC port"
                continue
            require(udp.returncode == 0 and run["udp_listener"], f"could not verify OSC port: {udp.stderr}")
            prove_sieve(renderer, root, output, label, run, host_arch)
        return
    raise RuntimeError("renderer did not bind either assigned OSC port")


def check_missing_asset(binary, shipped_scene, scratch, output, evidence):
    fixture = scratch / "missing-sieve"
    shutil.copytree(shipped_scene, fixture)
    missing = fixture / "assets" / TEXTURES["sieve"]
    missing.unlink()
    evidence["removed_asset"] = str(missing)
    renderer = ReleaseRenderer(binary, fixture / "scene.vosc.json", scratch, output / "missing-asset.log")
    with renderer.run(evidence, negative=True):
        require(renderer.process.wait(timeout=20) == 1, "missing asset must exit with status 1")
    log = renderer.log.read_text(errors="replace")
    require("command 4 /shader/texture" in log and str(missing) in log and "No such file" in log
            and "VOSC_SCENE_READY" not in log and "GPU :" not in log,
            "renderer did not identify the missing texture before graphics initialization")


def qualify(dmg, expected, output, report):
    require(sys.platform == "darwin", "run this command in a macOS desktop session")
    report["macos"] = command("/usr/bin/sw_vers").decode().strip()
    arm = command("/usr/sbin/sysctl", "-in", "hw.optional.arm64").strip()
    translated = command("/usr/sbin/sysctl", "-in", "sysctl.proc_translated").strip()
    report["host_architecture"] = "arm64" if b"1" in (arm, translated) else os.uname().machine
    report["phase"] = "sha256"
    report["sha256"] = sha256(dmg)
    require(report["sha256"] == expected, "SHA-256 does not match the expected artifact")

    scratch = Path(tempfile.mkdtemp(prefix="vosc-dmg-", dir="/tmp")).resolve()
    mount = scratch / "mounted"
    try:
        report["phase"] = "mount"
        with mount_read_only(dmg, mount, report.setdefault("mount", {})):
            report["phase"] = "artifact signatures and slices"
            app = mount / "vosc.app"
            binary = app / "Contents/MacOS/vosc"
            command("/usr/bin/codesign", "--verify", "--deep", "--strict", app)
            report["binary_sha256"] = sha256(binary)
            report["slices"] = command("/usr/bin/lipo", "-archs", binary).decode().split()
            require({"arm64", "x86_64"}.issubset(report["slices"]), "DMG does not contain a Universal binary")
            copied_app = scratch / "app-only/vosc.app"
            copied_app.parent.mkdir()
            command("/usr/bin/ditto", app, copied_app)
            command("/usr/bin/codesign", "--verify", "--deep", "--strict", copied_app)
            copied_binary = copied_app / "Contents/MacOS/vosc"
            require(sha256(copied_binary) == report["binary_sha256"], "app copy changed the executable")
            require(not (copied_app.parent / "data").exists(), "app-only fixture has an adjacent data folder")
            report["signature_checks"] = {"mounted": "pass", "app-only": "pass"}

            for label, executable, root in (
                ("adjacent", binary, mount / "data"),
                ("app-only", copied_binary, copied_app / "Contents/Resources/data"),
            ):
                report["phase"] = label
                evidence = report.setdefault(label, {})
                check_layout(executable, root, scratch, output, label, evidence, report["host_architecture"])
                evidence["status"] = "pass"
            report["phase"] = "missing_asset"
            evidence = report.setdefault("missing_asset", {})
            check_missing_asset(binary, mount / "data/examples/sieve", scratch, output, evidence)
            evidence["status"] = "pass"
            report["phase"] = "detach"
    finally:
        if not os.path.ismount(mount):
            shutil.rmtree(scratch)
        report["scratch_removed"] = not scratch.exists()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dmg", type=Path)
    parser.add_argument("--sha256", required=True, help="expected digest from the final release artifact")
    parser.add_argument("--output", required=True, type=Path, help="new directory for the smoke evidence")
    args = parser.parse_args()
    if not re.fullmatch(r"[0-9a-fA-F]{64}", args.sha256):
        parser.error("--sha256 must contain 64 hexadecimal digits")
    output = args.output.resolve()
    try:
        output.mkdir()
    except OSError as error:
        parser.exit(1, f"cannot create evidence directory: {error}\n")

    dmg = args.dmg.resolve()
    expected = args.sha256.lower()
    report = {"dmg": str(dmg), "expected_sha256": expected, "phase": "host"}
    try:
        qualify(dmg, expected, output, report)
    except (Exception, KeyboardInterrupt) as error:
        report.update(status="fail", error=str(error) or type(error).__name__)
        print(f"FAIL {report['phase']}: {report['error']}", file=sys.stderr)
    else:
        report.update(status="pass", phase="complete")
        print("PASS macOS DMG smoke")
    (output / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Evidence: {output / 'result.json'}")
    return 0 if report["status"] == "pass" else 1


if __name__ == "__main__":
    signal.signal(signal.SIGTERM, signal.default_int_handler)
    sys.exit(main())
