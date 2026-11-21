#!/usr/bin/env python3
"""Send a reproducible OSC smoke scene to a disposable, task-owned vosc instance.

The scene phase resets layers and disables onset gating. Do not target a running
performance. UDP sends are stimuli only; inspect the renderer to verify results.
Audio phases use synthetic features, not microphone or SuperCollider audio.
"""

import argparse
import socket
import struct
import time


SHADER = """#version 330
uniform float time;
uniform vec2 resolution;
uniform float gain;
out vec4 fragColor;
void main() {
    vec2 uv = gl_FragCoord.xy / resolution;
    float rings = 0.5 + 0.5 * sin(length(uv - 0.5) * 40.0 - time * 2.0);
    vec3 palette = 0.5 + 0.5 * cos(time * 0.4 + uv.xyx * 4.0 + vec3(0, 2, 4));
    fragColor = vec4(palette * (0.35 + rings * 0.65) * gain, 1.0);
}
"""


def osc_string(value):
    data = value.encode("utf-8") + b"\0"
    return data + b"\0" * (-len(data) % 4)


def encode_message(address, arguments):
    types = ","
    payload = b""
    for value in arguments:
        if isinstance(value, str):
            types += "s"
            payload += osc_string(value)
        elif isinstance(value, int):
            types += "i"
            payload += struct.pack(">i", value)
        else:
            types += "f"
            payload += struct.pack(">f", value)
    return osc_string(address) + osc_string(types) + payload


def commands(phase):
    if phase == "scene":
        return [("/onset", [0]), ("/layers/reset", [1, "stack"]),
                ("/shader", [0, SHADER]), ("/shader/uniform", [0, "gain", 1.0])]
    if phase == "dim":
        return [("/shader/uniform", [0, "gain", 0.15])]
    if phase == "bright":
        return [("/shader/uniform", [0, "gain", 1.0])]
    if phase == "audio-high":
        # Run audio-low first to establish the binding. Only input data changes.
        return [("/input/data", ["baseline", 0.9, 0.0, 0])]
    return [("/input", ["baseline", "", 0, 256]),
            ("/input/data", ["baseline", 0.1, 0.0, 0]),
            ("/onset", [0]),
            ("/shader/uniform", [0, "gain", "in_baseline"])]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=["scene", "dim", "bright", "audio-low", "audio-high"])
    parser.add_argument("--port", type=int, default=33333)
    args = parser.parse_args()
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as connection:
        for address, arguments in commands(args.phase):
            connection.sendto(encode_message(address, arguments), ("127.0.0.1", args.port))
            time.sleep(0.1)
    print(f"Sent {args.phase} stimuli to localhost:{args.port}; verify the rendered output.")


if __name__ == "__main__":
    main()
