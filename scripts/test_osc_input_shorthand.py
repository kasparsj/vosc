#!/usr/bin/env python3
"""Check live OSC shorthand, rejection atomicity and the recipe boundary."""
import argparse
import json
from pathlib import Path
import socket
import struct
import uuid

from runtime_smoke import osc_string
from test_feedback_export_rejections import resource_snapshot, wait_settled
from test_scene_recipe import (OwnedRenderer, applied, capture, collect_cleanup, encode_message,
                               free_udp_port, osc_bundle, recipe_messages, receive_results)
from test_scene_reset import scene, send

SHADER = """#version 330
uniform float gain;
out vec4 fragColor;
void main() { fragColor = vec4(gain, 0.25, 0.5, 1.0); }
"""


def samples(renderer):
    return resource_snapshot(renderer)["input_samples"]


def input_recipe(renderer):
    client = renderer.client()
    try:
        response = client.call("export_scene", {})
        assert response["ok"], response
        return [command for command in response["result"]["recipe"]["commands"]
                if command["address"] in {"/input", "/input/set"}]
    finally:
        client.close()


def expected_sample(amplitude, scaled, loudness=0., onset=0, mfcc=()):
    return dict(amplitude=amplitude, scaled=scaled, loudness=loudness, onset=onset,
                mfcc=list(mfcc))


def paired_case(renderer, output, shorthand):
    setup = lambda target: (target,) if shorthand else (target, "", 0, 256)
    send(renderer, [("/onset", (0,)), ("/layers/reset", (1, "stack")),
                    ("/input", setup("named")), ("/input", setup(0)),
                    ("/input/set", (0, "maxAmp", .5)),
                    ("/input/set", ("named", "maxLoud", 1.)),
                    ("/input", setup("named")), ("/input", setup(0))])
    assert set(samples(renderer)) == {"named", "0"}
    client = renderer.client()
    try:
        applied(client, "publish_shader", {"layer": 0, "source": SHADER})
    finally:
        client.close()
    send(renderer, [("/shader/var", (0, "gain", "in_0"))])
    recipe = input_recipe(renderer)
    setup_commands = [entry["args"] for entry in recipe if entry["address"] == "/input"]
    assert setup_commands == [
        [{"type": "i", "value": 0}, {"type": "s", "value": ""}, {"type": "i", "value": 0},
         {"type": "i", "value": 256}],
        [{"type": "s", "value": "named"}, {"type": "s", "value": ""}, {"type": "i", "value": 0},
         {"type": "i", "value": 256}],
    ], recipe

    observed = []
    for index, (amplitude, scaled, loudness) in enumerate(((.25, .5, 0.), (.75, 1., .125))):
        send(renderer, [("/input/data", ("0", .125, .75, 1, .375, .625))])
        assert samples(renderer)["0"] == expected_sample(.125, .25, .75, 1, (.375, .625))
        full = ("0", amplitude, loudness, 0)
        args = full[:2 + index] if shorthand else full
        before = scene(renderer)
        after = send(renderer, [("/input/data", args)])
        assert after["external_epoch"] == before["external_epoch"] + 1
        assert samples(renderer)["0"] == expected_sample(amplitude, scaled, loudness)
        client = renderer.client()
        try:
            width, height, pixels, _ = capture(client, output / f"{shorthand}-{index}.png")
        finally:
            client.close()
        center = pixels[(height // 2) * width + width // 2]
        assert abs(center[0] - round(scaled * 255)) <= 1 and center[3] == 255, center
        observed.append({"sample": samples(renderer)["0"], "pixel": center})

    send(renderer, [("/onset", (1,)), ("/layer/alpha", (0, .25))])
    assert scene(renderer)["legacy_pending"] == 1
    after = send(renderer, [("/input/data", (0, .25) if shorthand else (0, .25, 0., 0))])
    assert after["legacy_pending"] == 1 and after["layers"][0]["alpha"] == 1.
    if shorthand:
        boolean = osc_string("/input/data") + osc_string(",iffT") + struct.pack(">iff", 0, .25, 0.)
        before_epoch = after["external_epoch"]
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
            sender.sendto(boolean, ("127.0.0.1", renderer.port))
        after = wait_settled(renderer, before_epoch)
    else:
        after = send(renderer, [("/input/data", (0, .25, 0., 1))])
    assert after["legacy_pending"] == 0 and after["layers"][0]["alpha"] == .25
    send(renderer, [("/input/data", (0, .25) if shorthand else (0, .25, 0., 0)),
                    ("/layer/alpha", (0, .75))])
    assert scene(renderer)["legacy_pending"] == 1
    send(renderer, [("/onset/force", ())])

    send(renderer, [("/onset", (0,)), ("/layers/reset", (2, "stack")), ("/layer/alpha", (0, .25))])
    after = send(renderer, [("/layers/layout", ("grid",))])
    assert len(after["layers"]) == 2 and after["layout"] == "grid" and after["layers"][0]["alpha"] == .25
    after = send(renderer, [("/layers", ("row",) if shorthand else (9, "row"))])
    assert len(after["layers"]) == 9 and after["layout"] == "row" and after["layers"][0]["alpha"] == 1., after
    send(renderer, [("/layers", (2, "stack")), ("/layer/alpha", (0, .25))])
    after = send(renderer, [("/layers/reset", ("col",) if shorthand else (9, "col"))])
    assert len(after["layers"]) == 9 and after["layout"] == "col" and after["layers"][0]["alpha"] == 1.
    return {"samples": observed, "recipe": recipe, "layout": after["layout"]}


def rejection_case(renderer):
    def send_with_fence(packets, expected_delta=1):
        before = scene(renderer)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
            sender.sendto(osc_bundle([*packets, encode_message("/input/set", ("missingFenceSlot", "maxAmp", 1.))]),
                          ("127.0.0.1", renderer.port))
        after = wait_settled(renderer, before["external_epoch"])
        assert after["external_epoch"] == before["external_epoch"] + expected_delta, (before, after)
        return after

    empty = scene(renderer)
    assert not empty["onset_gate"]
    after = send_with_fence([encode_message("/input/data", ("*", .5)),
                             encode_message("/input/data", ("absent", .5)),
                             encode_message("/input", ("*", "", 0, 256))], expected_delta=2)
    assert not after["onset_gate"] and samples(renderer) == {}

    send(renderer, [("/input", (0, "", 0, 512))])
    assert not scene(renderer)["onset_gate"]
    assert send(renderer, [("/input/data", (0, .25))])["onset_gate"]

    send(renderer, [("/input", (0, "", 0, 512)), ("/input", ("named", "", 0, 512)),
                    ("/input", ("*", "", 0, 256)), ("/input/set", ("*", "maxAmp", .5)),
                    ("/input/data", ("*", .25, .5, 0, .75)), ("/layers/reset", (1, "row")),
                    ("/onset", (1,)), ("/layer/alpha", (0, .25))])
    before = scene(renderer)
    seeded = samples(renderer)
    assert seeded == {name: expected_sample(.25, .5, .5, 0, (.75,)) for name in ("0", "named")}
    recipe = input_recipe(renderer)
    assert all(entry["args"][3]["value"] == 256 for entry in recipe if entry["address"] == "/input"), recipe
    assert before["legacy_pending"] == 1 and before["layers"][0]["alpha"] == 1.
    malformed = [
        ("/input", ("new", 123)), ("/input", (0, "", 0, 0)), ("/input", ("*",)),
        ("/input/set", ("*", "maxAmp")), ("/input/set", (0, "maxAmp", "expression")),
        ("/input/data", ("*", .875, .875, 1, .25, "bad")),
        ("/input/data", (0, float("nan"))), ("/input/data", (0, .5, 0., .5)),
        ("/layers/reset", ("unknown",)), ("/layers/reset", (-1,)), ("/layers/layout", ("grid", 1)),
    ]
    wide_target = osc_string("/input/data") + osc_string(",hf") + struct.pack(">qf", 4294967296, .75)
    after = send_with_fence([encode_message(*command) for command in malformed] + [wide_target])
    assert after["onset_gate"] and after["legacy_pending"] == 1, after
    assert after["layers"] == before["layers"] and after["layout"] == before["layout"], after
    assert samples(renderer) == seeded and input_recipe(renderer) == recipe
    recovered = send(renderer, [("/input/data", ("*", .375, 0., 1))])
    assert recovered["legacy_pending"] == 0 and recovered["layers"][0]["alpha"] == .25
    assert samples(renderer) == {name: expected_sample(.375, .75, 0., 1) for name in seeded}
    return {"samples": seeded, "rejected_epoch_delta": after["external_epoch"] - before["external_epoch"],
            "pending_before": before["legacy_pending"], "pending_after_recovery": recovered["legacy_pending"]}


def replay_case(renderer):
    def run(commands, sample=None):
        token = uuid.uuid4().hex
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as listener:
            listener.bind(("127.0.0.1", 0))
            packets = recipe_messages(token, listener.getsockname()[1], commands, final_gate=-1)
            if sample:
                packets.append(encode_message("/input/data", sample))
            listener.sendto(osc_bundle(packets), ("127.0.0.1", renderer.port))
            reply = receive_results(listener, 1)[0]
        assert reply["token"] == token
        return reply

    accepted = []
    for sample in (("0", .375, 0., 0), ("0", .375)):
        before = scene(renderer)["external_epoch"]
        reply = run([("/layers/reset", (0, "stack")), ("/input", (0, "", 0, 256))], sample)
        assert reply["status"] == "applied" and reply["frame"] >= 0, reply
        assert samples(renderer) == {"0": expected_sample(.375, .375)}
        after = scene(renderer)
        assert after["external_epoch"] > before and not after["onset_gate"], after
        assert input_recipe(renderer)[0]["args"][0] == {"type": "i", "value": 0}
        accepted.append(reply)

    rejected = []
    for commands in ([("/layers/reset", ("stack",))],
                     [("/layers/reset", (0, "stack")), ("/input", (0,))],
                     [("/layers/reset", (0, "stack")), ("/input/data", (0, .25, 0., 0))]):
        reply = run(commands)
        assert reply["status"] == "failed" and reply["frame"] < 0, reply
        rejected.append(reply)
    return {"accepted": accepted, "rejected": rejected}


def rejection_and_replay_case(renderer):
    return {"rejection": rejection_case(renderer), "replay": replay_case(renderer)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    cases = {
        "explicit": lambda renderer: paired_case(renderer, args.output, False),
        "shorthand": lambda renderer: paired_case(renderer, args.output, True),
        "rejection-and-replay": rejection_and_replay_case,
    }
    evidence = {}
    try:
        for name, check in cases.items():
            renderer = OwnedRenderer(args.binary.resolve(), args.output, name, free_udp_port(), [])
            record = evidence[name] = {"owner": renderer.owner}
            try:
                record["result"] = check(renderer)
            except BaseException as error:
                record["error"] = repr(error)
                raise
            finally:
                record["cleanup"], cleanup_error = collect_cleanup(renderer)
            if cleanup_error:
                raise RuntimeError(cleanup_error)
            print(f"PASS {name}", flush=True)
        assert evidence["explicit"]["result"] == evidence["shorthand"]["result"]
    finally:
        (args.output / "evidence.json").write_text(json.dumps(evidence, indent=2) + "\n")


if __name__ == "__main__":
    main()
