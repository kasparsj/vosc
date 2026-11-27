#!/usr/bin/env python3
"""Check native draft bindings, OSC samples and independent show isolation."""

from __future__ import annotations

import argparse
from contextlib import ExitStack, closing
import json
import math
from pathlib import Path
import signal
import socket
import subprocess
import struct
import tempfile
import time

from native_validation import stage_validation_assets
from verify_26_round_trip import await_ready, compare, scene, write_json
from test_native_scene_cli import read_state, require
from test_scene_recipe import OwnedRenderer, capture, collect_cleanup, free_udp_port, osc
from runtime_smoke import encode_message


FIXTURE = b"""#version 330
uniform vec2 resolution;
uniform float gain = 0.6;
uniform vec3 tone = vec3(0.2, 0.5, 0.8);
out vec4 fragColor;
void main() {
    float x = 0.28 + 0.72 * gl_FragCoord.x / resolution.x;
    fragColor = vec4(tone * gain * x, 1.0);
}
"""


def own(stack, result, binary, output, name, options):
    renderer = OwnedRenderer(binary, output, name, free_udp_port(), [], arguments=options)
    node = result[name] = {"owner": renderer.owner}
    def finish():
        receipt, error = collect_cleanup(renderer)
        node["cleanup"] = receipt
        require(not error or (receipt["return_code"] == signal.SIGTERM and receipt["socket_removed"]
                             and not receipt["forced_kill"]), f"{name} did not close cleanly: {error}")
    stack.callback(finish)
    return renderer, stack.enter_context(closing(renderer.client()))


def update(client, method, params):
    initial = client.call(method, params)
    result = client.wait(initial["id"]) if initial["ok"] and initial["status"] in {"queued", "applied"} else initial
    require(result["ok"] and result["status"] == "rendered", result)
    return result


def observe(client, label, output, records):
    width, height, pixels, receipt = capture(client, output / f"{label}.png", max_width=320)
    receipt.pop("data_base64", None)
    state = read_state(client)
    records[label] = {"capture": receipt, "state": state}
    return width, height, pixels


def value(state, name):
    return json.loads(state["layers"][0]["shader"]["uniforms"][name]["rendered_value"])


def cadence(client):
    start = time.monotonic()
    first = read_state(client)["frame"]
    time.sleep(1.2)
    final = read_state(client)["frame"]
    elapsed = time.monotonic() - start
    performance = client.call("get_performance")["result"]
    return {"elapsed_seconds": elapsed, "frames": final - first, "observed_fps": (final - first) / elapsed,
            "renderer_performance": performance}


def verify(args, records):
    output = args.output.resolve()
    binary = args.binary.resolve(strict=True)
    show_manifest = scene(output, "scheduled-output", FIXTURE)
    with tempfile.TemporaryDirectory(prefix="vosc-draft-control-", dir="/tmp") as disposable, ExitStack() as stack:
        stock = Path(disposable) / "data"
        records["vertex_assets"] = stage_validation_assets(binary, stock)
        show, show_client = own(stack, records, binary, output, "show", ["--scene", show_manifest])
        await_ready(show, "26.frag — scheduled-output")
        show_first = observe(show_client, "show-first", output, records)
        records["show_baseline_cadence"] = cadence(show_client)
        draft, client = own(stack, records, binary, output, "draft",
                            ["--draft-data-root", stock, "--draft-sample-port", 0, "--size", "320x240", "--fps", 30])
        capability = client.call("get_capabilities")["result"]
        port = capability["draft_sample_port"]
        listeners = subprocess.check_output(["/usr/sbin/lsof", "-nP", "-a", "-p", str(draft.process.pid), "-i"], text=True)
        require(listeners.count("UDP") == 1 and f"UDP 127.0.0.1:{port}" in listeners and "TCP" not in listeners,
                f"unexpected private draft listener: {listeners!r}")
        records["draft"]["network"] = listeners
        require(not show_client.call("draft_configure_slot", {"slot": "sTest", "initial": 0})["ok"],
                "ordinary show admitted a draft mutation")
        records["configure"] = update(client, "configure_layers", {"count": 1, "layout": "stack"})
        records["activate"] = update(client, "draft_activate", {"layer": 0, "source": FIXTURE.decode()})
        records["slot"] = update(client, "draft_configure_slot", {"slot": "sTest", "initial": 0.1})
        bad_arity = client.call("draft_bind_uniform", {"layer": 0, "name": "tone", "components": [{"fixed": 1}]})
        require(not bad_arity["ok"] and bad_arity["error"]["code"] == "invalid_params", bad_arity)
        records["bad_arity"] = bad_arity
        managed = client.call("draft_bind_uniform", {"layer": 0, "name": "resolution",
                              "components": [{"fixed": 1}, {"fixed": 1}]})
        require(not managed["ok"] and managed["error"]["code"] == "invalid_uniform", managed)
        records["managed_exclusion"] = managed
        records["tone"] = update(client, "draft_bind_uniform", {"layer": 0, "name": "tone",
                                   "components": [{"fixed": 0.2}, {"fixed": 0.723456789}, {"fixed": 0.8}]})
        records["gain"] = update(client, "draft_bind_uniform", {"layer": 0, "name": "gain",
                                   "components": [{"slot": "sTest", "low": 0.2, "high": 0.8}]})
        failed = client.call("draft_activate", {"layer": 0, "source": "#version 330\nvoid main(){bad_shader();}"})
        require(not failed["ok"] and failed["error"]["code"] == "shader_failed" and
                read_state(client)["revision"] == records["gain"]["revision"], failed)
        records["compile_rejected"] = failed
        records["show_concurrent_cadence"] = cadence(show_client)

        frames = []
        for label, amplitude, expected in (("low", 0.0, 0.2), ("middle", 0.5, 0.5), ("high", 1.0, 0.8)):
            osc(port, "/input/data", "sTest", amplitude)
            time.sleep(0.16)
            frames.append(observe(client, label, output, records))
            state = records[label]["state"]
            require(state["draft_slots"]["sTest"] == amplitude and state["revision"] == records["gain"]["revision"]
                    and math.isclose(value(state, "gain"), expected, abs_tol=1e-6), state)
            require(all(math.isclose(got, want, abs_tol=1e-7)
                        for got, want in zip(value(state, "tone"), [0.2, 0.723456789, 0.8], strict=True)), state)
        records["sweep"] = [compare(left, right) for left, right in zip(frames[:-1], frames[1:], strict=True)]
        require(all(item["pixels_changed_by_at_least_16"] > 15_000 for item in records["sweep"]),
                records["sweep"])

        for address, arguments in (("/input/data", ("unknown", 0.1)), ("/input/data", ("*", 0.2)),
                ("/input/data", ("sTest",)), ("/input/data", ("sTest", 0.2, 0.4)),
                ("/input/data", ("sTest", 2.0)), ("/layer/alpha", (0, 0.0))):
            osc(port, address, *arguments)
        message = encode_message("/input/data", ("sTest", 0.1))
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
            sender.sendto(b"#bundle\0" + struct.pack(">QI", 1, len(message)) + message, ("127.0.0.1", port))
        time.sleep(0.2)
        unchanged = observe(client, "rejected-packets", output, records)
        require(compare(frames[-1], unchanged)["max_channel_error"] == 0
                and records["rejected-packets"]["state"]["draft_slots"]["sTest"] == 1
                and records["rejected-packets"]["state"]["external_epoch"] == records["high"]["state"]["external_epoch"],
                "an unsupported Datagram changed the private draft")

        records["fixed_restore"] = update(client, "draft_bind_uniform", {"layer": 0, "name": "gain",
                                            "components": [{"fixed": 0.34}]})
        osc(port, "/input/data", "sTest", 0.1)
        time.sleep(0.15)
        require(math.isclose(value(read_state(client), "gain"), 0.34, abs_tol=1e-6), "fixed mode still follows OSC")
        show_last = observe(show_client, "show-last", output, records)
        first, last = (records[name]["state"] for name in ("show-first", "show-last"))
        require(compare(show_first, show_last)["max_channel_error"] == 0 and last["frame"] > first["frame"]
                and all(first[key] == last[key] for key in ("layers", "revision", "external_epoch")),
                "independent show was changed")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True)
    result = {}
    try:
        verify(args, result)
        result["status"] = "pass"
    except Exception as error:
        result["status"] = "fail"
        result["error"] = str(error)
        raise
    finally:
        write_json(args.output / "report.json", result)
