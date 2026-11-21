#!/usr/bin/env python3
"""Exercise the render budget and compare deterministic framebuffer output."""

import argparse
from contextlib import closing
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import socket
import subprocess
from threading import Thread
import time
import uuid
from unittest.mock import patch

from test_feedback_export_rejections import ordinary, resource_snapshot
from test_scene_recipe import (ROOT, OwnedRenderer, applied, arm_recovery_hook, capture,
                              encode_message, free_udp_port, osc, osc_bundle, png_rgba, receive_results,
                              recipe_messages, wait_recovery_hook)
from test_scene_reset import send


LAYER = """#version 330
uniform vec2 resolution;
out vec4 fragColor;
void main() {
    vec2 uv = gl_FragCoord.xy / resolution;
    fragColor = vec4(0.45 + uv.x * 0.3, 0.2, uv.y * 0.5, 0.65);
}
"""
OVERLAY = """#version 330
out vec4 fragColor;
void main() {
    gl_FragDepth = 0.0;
    fragColor = vec4(0.1, 0.6, 0.4, 0.7);
}
"""
FRAMEBUFFER_SAMPLES = """#version 410
out vec4 fragColor;
void main() {
    fragColor = gl_NumSamples == 1 ? vec4(0., 1., 0., 1.) : vec4(1., 0., 0., 1.);
}
"""


def query(renderer, method):
    with closing(renderer.client()) as client:
        reply = client.call(method)
        assert reply["ok"], reply
        return reply["result"]


def snapshot(renderer, output, label, expected_size=None):
    for attempt in range(3):
        with closing(renderer.client()) as client:
            scene = client.call("get_scene")["result"]
            try:
                width, height, pixels, _ = capture(client, output / f"{label}.png",
                                                   max_width=scene["width"])
                break
            except RuntimeError as error:
                # A native window event can retire a preview receipt before it is read.
                if attempt == 2 or "request_not_found" not in str(error):
                    raise
                if query(renderer, "get_scene")["external_epoch"] == scene["external_epoch"]:
                    raise
    if expected_size:
        assert (width, height) == expected_size, (label, width, height)
    return width, height, bytes(channel for pixel in pixels for channel in pixel)


def check_targets(renderer, *, post, deferred):
    deadline = time.monotonic() + 2
    while not renderer.hooks_enabled and time.monotonic() < deadline:
        renderer.hooks_enabled = (renderer.runtime / "events.jsonl").is_file()
        if not renderer.hooks_enabled:
            time.sleep(0.02)
    assert renderer.hooks_enabled, "candidate must be built with --recovery-test-hooks"
    actual = resource_snapshot(renderer)["render_targets"]
    assert actual == {"post": post, "deferred": deferred}, actual


def stop(renderer):
    try:
        if renderer.process.poll() is None and (renderer.runtime / "events.jsonl").is_file():
            renderer.hooks_enabled = True
            arm_recovery_hook(renderer, {"id": uuid.uuid4().hex, "operation": "shutdown"})
            try:
                renderer.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                pass
    finally:
        renderer.stop()


def resize(renderer, width, height):
    command = {"id": uuid.uuid4().hex, "operation": "native_resize", "width": width, "height": height}
    existing = arm_recovery_hook(renderer, command)
    wait_recovery_hook(renderer, command, existing)
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        scene = query(renderer, "get_scene")
        if (scene["width"], scene["height"]) == (width, height):
            return
        time.sleep(0.05)
    raise AssertionError(f"resize to {width}x{height} did not settle")


def configure_layers(renderer, layers):
    with closing(renderer.client()) as client:
        applied(client, "configure_layers", {"count": len(layers), "layout": "stack"})
        for index, source in enumerate(layers):
            applied(client, "publish_shader", {"layer": index, "source": source})


def pipeline(binary, output, label, *, window_samples=None):
    setting = {} if window_samples is None else {"VOSC_TEST_WINDOW_SAMPLES": str(window_samples)}
    with patch.dict(os.environ, setting):
        renderer = OwnedRenderer(binary, output, label, free_udp_port(), [])
    inspect = window_samples is not None
    default_post = window_samples == 4
    try:
        if inspect:
            check_targets(renderer, post=default_post, deferred=False)
            resize(renderer, 800, 600)
            check_targets(renderer, post=default_post, deferred=False)
            resize(renderer, 1024, 768)
        configure_layers(renderer, [FRAMEBUFFER_SAMPLES])
        sample_frame = snapshot(renderer, output, f"{label}-framebuffer-samples", (1024, 768))
        assert sample_frame[2] == bytes((0, 255, 0, 255)) * (1024 * 768)
        configure_layers(renderer, [LAYER])
        single = snapshot(renderer, output, f"{label}-single", (1024, 768))
        configure_layers(renderer, [LAYER, OVERLAY])
        send(renderer, [("/layer/blend", (0, 1)), ("/layer/blend", (1, 1)),
                        ("/layer/pos", (1, 0., 0., 1.))])
        blended = snapshot(renderer, output, f"{label}-blended")
        assert single != blended, "overlay did not reach the framebuffer"
        send(renderer, [("/shading/passes", ("contrast",))])
        post = snapshot(renderer, output, f"{label}-post")
        assert post != blended, "forward pass had no visible effect"
        if inspect:
            check_targets(renderer, post=True, deferred=False)
        send(renderer, [("/shading/passes", ())])
        assert snapshot(renderer, output, f"{label}-cleared") == blended
        if inspect:
            check_targets(renderer, post=default_post, deferred=False)
        send(renderer, [("/layers/reset", (1, "stack")), ("/cam", ("default",)),
                        ("/cam/pos", (0., 0., 1200.)), ("/cam/look", (0., 0., 0.)),
                        ("/geom", (0, "penguin/penguin.dae")), ("/light", (0, 0., 0., 1200.))])
        camera = snapshot(renderer, output, f"{label}-camera")
        assert any(camera[2][0::4]), "forward camera fixture did not render visible geometry"
        send(renderer, [("/shading/passes", ("pointlight",)), ("/shading", ("deferred",))])
        deferred = snapshot(renderer, output, f"{label}-deferred")
        assert any(deferred[2][0::4]) and deferred != camera, "deferred fixture did not render a distinct image"
        if inspect:
            check_targets(renderer, post=False, deferred=True)
        send(renderer, [("/shading/passes", ("contrast",)), ("/shading", ("forward",))])
        if inspect:
            check_targets(renderer, post=True, deferred=False)
            resize(renderer, 800, 600)
            snapshot(renderer, output, f"{label}-resized-pass", (800, 600))
            check_targets(renderer, post=True, deferred=False)
            send(renderer, [("/shading/passes", ())])
            send(renderer, [("/shading/passes", ("fxaa",))])
            check_targets(renderer, post=True, deferred=False)
        return {"framebuffer-samples": sample_frame, "single": single, "blended": blended,
                "post": post, "camera": camera, "deferred": deferred}
    finally:
        stop(renderer)


def budget(binary, output):
    negatives = [
        ("--size",), ("--size", "1280X720"), ("--size", "0x720"), ("--size", "-2x720"),
        ("--size", "2147483648x720"), ("--size", "1x1", "--size", "2x2"), ("--fps",),
        ("--fps", "1.5"), ("--fps", "0"), ("--fps", "-30"), ("--fps", "2147483648"),
        ("--fps", "30", "--fps", "60"),
    ]
    for arguments in negatives:
        result = subprocess.run([binary, *arguments], capture_output=True, text=True, timeout=10)
        assert result.returncode != 0 and "Usage:" in result.stderr, (arguments, result)
    help_run = subprocess.run([binary, "--help"], capture_output=True, text=True, check=True, timeout=10)
    assert all(flag in help_run.stdout for flag in ("--scene", "--size", "--fps"))
    scene = ROOT / "examples/editable-scene/scene.vosc.json"
    before = scene.read_bytes()
    results = {}
    for name, arguments, size, fps in (
        ("scene", ("--scene", scene), (640, 480), 60),
        ("budget", ("--size", "400x300", "--fps", "30"), (400, 300), 30),
        ("combined", ("--fps", "30", "--scene", scene, "--size", "520x320"), (520, 320), 30),
    ):
        renderer = OwnedRenderer(binary, output, name, free_udp_port(), [], arguments=arguments)
        try:
            if name != "budget":
                deadline = time.monotonic() + 12
                while "VOSC_SCENE_READY " not in renderer.log.read_text(errors="replace"):
                    assert renderer.process.poll() is None and time.monotonic() < deadline, renderer.log
                    time.sleep(0.05)
            snapshot(renderer, output, name, size)
            deadline = time.monotonic() + 8
            performance = query(renderer, "get_performance")
            while performance["frame_interval_ms"]["samples"] < 60 and time.monotonic() < deadline:
                time.sleep(0.1)
                performance = query(renderer, "get_performance")
            interval = performance["frame_interval_ms"]
            assert performance["target_fps"] == fps, performance
            expected = 1000 / fps
            # Vsync can alternate short and long intervals. Check the observed
            # pacing without turning this functional check into a benchmark.
            assert interval["samples"] >= 60 and expected * 0.75 <= interval["p95"] <= expected * 1.8, interval
            results[name] = {"size": size, "target_fps": fps, "frame_interval_ms": interval}
        finally:
            stop(renderer)
    assert scene.read_bytes() == before
    return results


def minimal_rejections(binary, output):
    renderer = OwnedRenderer(binary, output, "minimal", free_udp_port(), [])
    try:
        configure_layers(renderer, [LAYER])
        picture = snapshot(renderer, output, "minimal-before")
        send(renderer, [("/shading/passes", ("fxaa",))])
        check_targets(renderer, post=True, deferred=False)
        before = resource_snapshot(renderer)
        for address, args, expected in (
            ("/shading", ("deferred",), "deferred shading"),
            ("/shading/passes", ("bloom", "pointlight"), "deferred pass pointlight"),
            ("/tex", (0, "hpv:unused.hpv"), "HPV textures"),
            ("/tex", (0, "https://example.invalid/unused.hpv"), "HPV textures"),
            ("/tex", ("new", "https://example.invalid/example.html"), "HTML/Ultralight textures"),
            ("/tex/choose", (0, "gv"), "GV textures"),
            ("/shader/texture", (0, "excluded", "test.hpv"), "HPV textures"),
        ):
            osc(renderer.port, address, *args)
            log = renderer.log.read_text(errors="replace")
            assert f"{address}: {expected} unavailable in this build" in log, log
            after = resource_snapshot(renderer)
            for key in ("render_targets", "render_pipeline", "resources", "revision"):
                assert after[key] == before[key], (address, key, before[key], after[key])
        send(renderer, [("/shading/passes", ())])
        assert snapshot(renderer, output, "minimal-after") == picture

        with closing(renderer.client()) as client:
            trial = client.call("trial_shader", {"layer": 0, "source": LAYER, "ttl_ms": 30000,
                                                 "gpu_budget_ms": 16.0})
            assert trial["ok"], trial
            ready = client.wait(trial["id"], timeout=15)
            assert ready["status"] == "ready", ready
            osc(renderer.port, "/tex", 0, "hpv:trial-rejection.hpv")
            current = query(renderer, "get_scene")
            assert current["trial"]["trial_id"] == trial["id"], current["trial"]
            assert current["trial"]["phase"] == "ready", current["trial"]
            log = renderer.log.read_text(errors="replace")
            assert log.count("/tex: HPV textures unavailable in this build") == 3, log
            applied(client, "cancel_trial", {"trial_id": trial["id"]})

        for gated in (False, True):
            alias = "hpv:gated" if gated else "hpv:open"
            slot = "gated_alias" if gated else "open_alias"
            commands = [("/tex", (alias, "#ff0000")), ("/tex", (0, alias)),
                        ("/shader/texture", (0, slot, alias))]
            if gated:
                send(renderer, [("/onset", (1,))])
                ordinary(renderer, commands)
                deadline = time.monotonic() + 5
                while True:
                    queued = query(renderer, "get_scene")
                    if queued["legacy_pending"] == len(commands):
                        break
                    assert time.monotonic() < deadline, queued
                    time.sleep(0.02)
                assert slot not in queued["layers"][0]["shader"]["textures"], queued
                state = send(renderer, [("/onset/force", ())])
            else:
                state = send(renderer, commands)
            textures = state["layers"][0]["shader"]["textures"]
            for name in ("tex", slot):
                assert textures[name]["loaded"] and textures[name]["source"] == "color", textures
            assert state["legacy_pending"] == 0, state

        token = uuid.uuid4().hex
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as peer:
            peer.bind(("127.0.0.1", 0))
            messages = recipe_messages(token, peer.getsockname()[1], [("/layers/reset", (1, "stack"))])
            messages.insert(-1, encode_message("/tex", (0, "hpv:replay-rejection.hpv")))
            peer.sendto(osc_bundle(messages), ("127.0.0.1", renderer.port))
            result, = receive_results(peer, 1)
            assert result["token"] == token and result["status"] == "applied", result
        log = renderer.log.read_text(errors="replace")
        assert log.count("/tex: HPV textures unavailable in this build") == 4, log
        remote_image(renderer, output)
    finally:
        stop(renderer)


def remote_image(renderer, output):
    green = (0, 255, 0, 255)
    image = png_rgba(8, 8, green, green)

    class ImageHandler(BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path != "/test.png":
                self.send_error(404)
                return
            self.send_response(200)
            self.send_header("Content-Type", "image/png")
            self.send_header("Content-Length", str(len(image)))
            self.end_headers()
            self.wfile.write(image)

        def log_message(self, format, *args):
            pass

    with ThreadingHTTPServer(("127.0.0.1", 0), ImageHandler) as server:
        thread = Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            url = f"http://127.0.0.1:{server.server_port}/test.png"
            state = send(renderer, [("/tex", (0, url))])
            assert state["layers"][0]["shader"]["textures"]["tex"]["source"] == url, state
            _, _, pixels = snapshot(renderer, output, "minimal-remote-image")
            assert bytes(green) in pixels, "remote PNG did not render"
        finally:
            server.shutdown()
            thread.join()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("baseline", "candidate", "output"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    parser.add_argument("--minimal", type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir()
    results = budget(args.candidate.resolve(), output)
    original = pipeline(args.baseline.resolve(), output, "baseline")
    for samples in (0, 4):
        changed = pipeline(args.candidate.resolve(), output, f"candidate-{samples}", window_samples=samples)
        for name, (width, height, expected) in original.items():
            w, h, actual = changed[name]
            assert (w, h) == (width, height), (name, (w, h), (width, height))
            differences = sum(a != b for a, b in zip(actual, expected))
            assert differences == 0, (samples, name, differences, "different channels")
    if args.minimal:
        minimal_rejections(args.minimal.resolve(), output)
    (output / "results.json").write_text(json.dumps({"budget": results, "exact_matches": list(original),
        "window_samples_tested": [0, 4], "minimal_rejections_and_aliases": bool(args.minimal)}, indent=2) + "\n")


if __name__ == "__main__":
    main()
