#!/usr/bin/env python3
"""Exercise a disposable running engine through real local API and OSC requests.

Requires --allow-reset: this test replaces layers/shaders and changes onset gating.
It does not launch or stop applications. Evidence stays in the selected directory.
"""

import argparse
import base64
import json
from pathlib import Path
import socket
import struct
import time
import zlib

from control_client import Client
from runtime_smoke import encode_message


AUTHOR_SHADER = """#version 330
uniform float time;
uniform vec2 resolution;
uniform float gain;
uniform vec2 uOffset;
uniform vec3 uTint;
uniform vec2 offset;
uniform vec3 tint;
uniform vec4 accent;
uniform int mode;
uniform float weights[2];
uniform sampler2D sampleTex;
out vec4 fragColor;
void main () {
    vec2 uv = gl_FragCoord.xy / resolution;
    float wave = 0.02 * sin(time + uv.x * 10.0);
    vec3 color = gain * uTint + vec3(uOffset, accent.w) * 0.01 + accent.rgb * 0.01;
    color += vec3(offset, 0.0) * 0.001 + tint * 0.001;
    color += texture(sampleTex, uv).rgb * 0.1 + vec3(float(mode) + weights[0] + weights[1]) * 0.01;
    fragColor = vec4(color + wave, 1.0);
}
"""
STATIC_SHADER = "#version 330\nout vec4 fragColor; void main() { fragColor = vec4(0.2,0.4,0.6,1.0); }"
COMPILE_ERROR = "#version 330\nout vec4 fragColor; void main() { fragColor = BROKEN; }"
LINK_ERROR = "#version 330\nin vec4 missing_vertex_output; out vec4 fragColor; void main() { fragColor = missing_vertex_output; }"


def png_pixels(data, *, include_alpha=False):
    """Decode 8-bit RGB/RGBA PNG pixels so uniform output is an artifact oracle."""
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    width, height, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", data[16:29])
    assert depth == 8 and color in (2, 6) and interlace == 0
    compressed = b""
    at = 8
    while at < len(data):
        length = struct.unpack(">I", data[at:at + 4])[0]
        if data[at + 4:at + 8] == b"IDAT":
            compressed += data[at + 8:at + 8 + length]
        at += length + 12
    raw = zlib.decompress(compressed)
    channels = 3 if color == 2 else 4
    stride = width * channels
    previous = bytearray(stride)
    pixels = []
    at = 0
    for _ in range(height):
        kind = raw[at]
        row = bytearray(raw[at + 1:at + 1 + stride])
        at += stride + 1
        for index in range(stride):
            left = row[index - channels] if index >= channels else 0
            up = previous[index]
            upper_left = previous[index - channels] if index >= channels else 0
            if kind == 0:
                predictor = 0
            elif kind == 1:
                predictor = left
            elif kind == 2:
                predictor = up
            elif kind == 3:
                predictor = (left + up) // 2
            else:
                assert kind == 4
                p = left + up - upper_left
                distances = [abs(p - left), abs(p - up), abs(p - upper_left)]
                predictor = [left, up, upper_left][distances.index(min(distances))]
            row[index] = (row[index] + predictor) & 255
        if include_alpha:
            pixels.extend(tuple(row[i:i + channels]) + (() if channels == 4 else (255,))
                          for i in range(0, stride, channels))
        else:
            pixels.extend(tuple(row[i:i + 3]) for i in range(0, stride, channels))
        previous = row
    return width, height, pixels


class Checks:
    def __init__(self, client, output, port):
        self.client, self.output, self.port = client, output, port
        self.trace = []
        self.completed = []

    def call(self, method, params=None, request_id=None):
        response = self.client.call(method, params, request_id)
        compact = json.loads(json.dumps(response))
        if "result" in compact and isinstance(compact["result"], dict):
            compact["result"].pop("data_base64", None)
            if isinstance(compact["result"].get("result"), dict):
                compact["result"]["result"].pop("data_base64", None)
        self.trace.append({"method": method, "params": params, "response": compact})
        return response

    def scene(self):
        return self.call("get_scene")["result"]

    def applied(self, method, params, request_id=None):
        response = self.call(method, params, request_id)
        assert response["ok"], response
        final = self.client.wait(response["id"])
        assert final["status"] == "rendered", final
        assert final["frame"] > final["applied_after_frame"]
        return final

    def error(self, method, params, code=None):
        before = self.scene()["revision"]
        response = self.call(method, params)
        assert not response["ok"] and response["status"] == "failed", response
        assert response["error"]["message"]
        if code:
            assert response["error"]["code"] == code, response
        assert self.scene()["revision"] == before
        return response

    def osc(self, address, *arguments):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as connection:
            connection.sendto(encode_message(address, arguments), ("127.0.0.1", self.port))
        time.sleep(0.06)

    def done(self, name):
        self.completed.append(name)
        print("PASS", name, flush=True)

    def preview(self, name, minimum=None):
        params = {"max_width": 320}
        if minimum is not None:
            params["minimum_revision"] = minimum
        response = self.call("capture_preview", params)
        assert response["status"] == "queued", response
        captured = self.client.wait(response["id"])
        assert captured["ok"] and captured["status"] == "captured", captured
        image = captured["result"]
        data = base64.b64decode(image["data_base64"], validate=True)
        (self.output / name).write_bytes(data)
        width, height, pixels = png_pixels(data)
        assert (width, height) == (image["width"], image["height"])
        assert width <= 320 and height <= 1024 and captured["frame"] > response["frame"]
        if minimum is not None:
            assert captured["revision"] >= minimum
        return response["id"], pixels

    def run(self):
        capabilities = self.call("get_capabilities")["result"]
        assert capabilities["version"] == 1 and capabilities["limits"]["pending"] == 16
        # API v1 extensions are additive; retain every original operation check.
        assert set(capabilities["operations"]).issuperset({
            "get_capabilities", "get_scene", "get_diagnostics", "get_request", "configure_layers",
            "publish_shader", "set_uniform", "set_layer", "capture_preview"})
        self.done("VAL-API-CAPABILITIES")
        start_frame = self.scene()["frame"]
        for raw in (b"not-json\n", b"[" * 65 + b"0" + b"]" * 65 + b"\n",
                    b'{"id":"overflow","method":"set_layer","params":{"layer":0,"property":"alpha","value":1e999}}\n'):
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
                connection.settimeout(5)
                connection.connect(str(self.client.socket.getpeername()))
                connection.sendall(raw)
                response = json.loads(connection.makefile("rb").readline())
                assert response["error"]["code"] == "invalid_json"
        self.error("get_scene", {"unexpected": True})
        assert self.scene()["frame"] > start_frame
        self.done("VAL-API-TRANSPORT (live malformed traffic; transport regression suite covers bounds/lifecycle)")

        self.applied("configure_layers", {"count": 0, "layout": "stack"})
        assert self.scene()["layers"] == []
        for count in (-1, 33, 1.5, True):
            self.error("configure_layers", {"count": count, "layout": "stack"})
        self.error("configure_layers", {"count": 1, "layout": "unknown"})
        for layout in ("grid", "row", "col", "stack"):
            self.applied("configure_layers", {"count": 2, "layout": layout})
            assert len(self.scene()["layers"]) == 2 and self.scene()["layout"] == layout
        self.applied("configure_layers", {"count": 32, "layout": "stack"})
        assert len(self.scene()["layers"]) == 32
        self.applied("configure_layers", {"count": 1, "layout": "stack"})
        assert not self.scene()["layers"][0]["shader"]["loaded"]
        self.done("VAL-API-LAYERS")

        published = self.applied("publish_shader", {"layer": 0, "source": AUTHOR_SHADER})
        for params in ({"layer": 31, "source": AUTHOR_SHADER}, {"layer": 0, "source": ""},
                       {"layer": 0, "source": "x" * 131073}):
            self.error("publish_shader", params)
        assert self.scene()["layers"][0]["shader"]["program"] > 0
        self.done("VAL-API-SHADER")

        uniform_values = {"gain": 0.5, "uOffset": [0.2, 0.3], "uTint": [0.8, 0.5, 0.3],
                          "accent": [0.1, 0.2, 0.3, 0.4]}
        for name, value in uniform_values.items():
            self.applied("set_uniform", {"layer": 0, "name": name, "value": value})
            actual = self.scene()["layers"][0]["shader"]["uniforms"][name]
            expected = value if isinstance(value, list) else [value]
            assert all(abs(a - b) < 0.0001 for a, b in zip(actual["configured_value"], expected))
            gpu = json.loads(actual["rendered_value"])
            gpu = gpu if isinstance(gpu, list) else [gpu]
            assert len(gpu) == len(expected) and all(abs(a - b) < 0.0001 for a, b in zip(gpu, expected))
        for name, value in (("absent", 1), ("time", 1), ("mode", 1), ("sampleTex", 1),
                            ("weights", 1), ("weights[0]", 1), ("offset", [0.2, 0.3]), ("tint", [1, 0, 0]),
                            ("gain", True), ("gain", 1000001), ("gain", [0.5]), ("uOffset", [0.5]), ("uOffset", [0.5, False])):
            self.error("set_uniform", {"layer": 0, "name": name, "value": value})
        self.done("VAL-API-UNIFORM")

        for property_name, value in (("alpha", 0.6), ("visible", False), ("visible", True), ("alpha", 1.0)):
            self.applied("set_layer", {"layer": 0, "property": property_name, "value": value})
            actual = self.scene()["layers"][0][property_name]
            assert actual == value if isinstance(value, bool) else abs(actual - value) < .000001
        for property_name, value in (("alpha", -0.1), ("alpha", 1.1), ("visible", 1), ("other", 1)):
            self.error("set_layer", {"layer": 0, "property": property_name, "value": value})
        self.done("VAL-API-LAYER")

        before_retry = self.scene()["revision"]
        retry = self.call("publish_shader", {"layer": 0, "source": AUTHOR_SHADER}, published["id"])
        assert retry["status"] == "rendered" and self.scene()["revision"] == before_retry
        conflict = self.call("publish_shader", {"layer": 0, "source": STATIC_SHADER}, published["id"])
        assert not conflict["ok"] and conflict["error"]["code"] == "id_conflict"

        self.osc("/shader/texture", 0, "sampleTex", "#ff0000")
        original = self.scene()["layers"][0]["shader"]
        assert original["textures"]["sampleTex"]["loaded"]
        for source, target, keyword in ((COMPILE_ERROR, "VAL-API-COMPILE", "compile"),
                                        (LINK_ERROR, "VAL-API-LINK", "link")):
            failed = self.error("publish_shader", {"layer": 0, "source": source}, "shader_failed")
            assert failed["error"]["diagnostics"]
            assert keyword in json.dumps(failed["error"]["diagnostics"]).lower()
            current = self.scene()["layers"][0]["shader"]
            assert current["program"] == original["program"] and current["textures"] == original["textures"]
            assert current["uniforms"]["gain"]["configured_value"] == original["uniforms"]["gain"]["configured_value"]
            self.preview(target + ".png")
            self.done(target)
        self.osc("/shader", 0, COMPILE_ERROR)
        assert self.scene()["layers"][0]["shader"]["program"] == original["program"]
        self.osc("/shader", 0, LINK_ERROR)
        assert self.scene()["layers"][0]["shader"]["program"] == original["program"]
        self.osc("/shader/uniform", 0, "gain", 0.7)
        state = self.scene()
        assert abs(state["layers"][0]["shader"]["uniforms"]["gain"]["configured_value"][0] - 0.7) < .0001
        self.done("VAL-API-SCENE")
        self.osc("/shader", 0, AUTHOR_SHADER)
        assert self.scene()["layers"][0]["shader"]["loaded"]
        self.done("VAL-API-OSC")

        self.osc("/onset", 1)
        self.osc("/layer/alpha", 0, 0.25)
        assert self.scene()["legacy_pending"] >= 1
        before_alpha = self.scene()["layers"][0]["alpha"]
        self.applied("set_layer", {"layer": 0, "property": "visible", "value": False})
        assert self.scene()["layers"][0]["alpha"] == before_alpha and self.scene()["legacy_pending"] >= 1
        params = {"layer": 0, "property": "visible", "value": True, "timing": "onset"}
        queued = self.call("set_layer", params)
        assert queued["status"] == "queued" and self.call("set_layer", params, queued["id"])["status"] == "queued"
        self.osc("/onset/force")
        assert self.client.wait(queued["id"])["status"] == "rendered"
        assert self.scene()["layers"][0]["alpha"] == 0.25 and self.scene()["layers"][0]["visible"]

        stale = self.call("set_uniform", {"layer": 0, "name": "gain", "value": 0.2, "timing": "onset"})
        self.applied("publish_shader", {"layer": 0, "source": STATIC_SHADER})
        later = self.call("set_layer", {"layer": 0, "property": "alpha", "value": 0.8, "timing": "onset"})
        before_force = self.scene()["revision"]
        self.osc("/onset/force")
        assert self.client.wait(stale["id"])["error"]["code"] == "invalid_uniform"
        assert self.client.wait(later["id"])["status"] == "rendered"
        assert self.scene()["revision"] == before_force + 1

        self.applied("configure_layers", {"count": 2, "layout": "stack"})
        stale = self.call("set_layer", {"layer": 1, "property": "alpha", "value": 0.2, "timing": "onset"})
        self.applied("configure_layers", {"count": 1, "layout": "stack"})
        self.osc("/onset/force")
        assert self.client.wait(stale["id"])["error"]["code"] == "invalid_target"

        pending = []
        for index in range(16):
            pending.append(self.call("set_layer", {"layer": 0, "property": "alpha", "value": index / 16, "timing": "onset"}))
        self.error("set_layer", {"layer": 0, "property": "alpha", "value": 0.5, "timing": "onset"}, "queue_full")
        self.error("set_layer", {"layer": 0, "property": "visible", "value": True}, "queue_full")
        self.osc("/onset/force")
        assert self.client.wait(pending[0]["id"])["status"] == "rendered"
        pending = []
        for index in range(15):
            pending.append(self.call("set_layer", {"layer": 0, "property": "alpha", "value": index / 16, "timing": "onset"}))
        for _ in range(70):
            self.applied("set_layer", {"layer": 0, "property": "visible", "value": True})
        assert self.call("get_request", {"request_id": pending[0]["id"]})["result"]["status"] == "queued"
        assert self.call("get_request", {"request_id": published["id"]})["error"]["code"] == "request_not_found"
        self.osc("/onset/force")
        assert self.client.wait(pending[0]["id"])["status"] == "rendered"
        assert self.scene()["layers"][0]["alpha"] == 14 / 16
        self.done("VAL-API-RETRY")

        timeout_request = self.call("set_layer", {"layer": 0, "property": "alpha", "value": 0.3, "timing": "onset"})
        print("Checking bounded onset timeout while rendering continues...", flush=True)
        timeout = self.client.wait(timeout_request["id"], timeout=33)
        assert timeout["error"]["code"] == "queue_timeout"
        self.osc("/onset", 0)
        opened = self.call("set_layer", {"layer": 0, "property": "alpha", "value": 1.0, "timing": "onset"})
        assert self.client.wait(opened["id"])["status"] == "rendered"
        self.done("VAL-API-QUEUE")

        for _ in range(520):
            assert not self.call("unknown_method")["ok"]
        diagnostics = self.call("get_diagnostics", {"after": 0, "limit": 128})["result"]
        assert diagnostics["gap"] and len(diagnostics["entries"]) == 128
        latest = diagnostics["latest_cursor"]
        empty = self.call("get_diagnostics", {"after": latest})["result"]
        assert empty["entries"] == [] and not empty["gap"]
        continuation = self.call("get_diagnostics", {"after": diagnostics["next_cursor"], "limit": 10})["result"]
        assert len(continuation["entries"]) == 10
        self.done("VAL-API-DIAGNOSTICS")

        final = self.applied("publish_shader", {"layer": 0, "source": STATIC_SHADER})
        self.error("capture_preview", {"minimum_revision": final["revision"] + 1})
        self.error("capture_preview", {"max_width": 0})
        limited = self.call("capture_preview", {"max_readback_pixels": 1})
        assert limited["status"] == "queued"
        assert self.client.wait(limited["id"])["error"]["code"] == "readback_failed"
        first_image, pixels = self.preview("overlay-free.png", final["revision"])
        assert all(all(abs(value - expected) <= 1 for value, expected in zip(pixel, (51, 102, 153))) for pixel in pixels)
        self.preview("second.png")
        self.preview("third.png")
        expired = self.call("get_request", {"request_id": first_image})["result"]
        assert expired["status"] == "expired" and expired["error"]["code"] == "preview_expired"
        self.done("VAL-API-PREVIEW")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--socket", required=True)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--port", type=int, default=33333)
    parser.add_argument("--allow-reset", action="store_true", required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    client = Client(args.socket)
    checks = Checks(client, args.output, args.port)
    try:
        checks.run()
        return 0
    finally:
        (args.output / "trace.json").write_text(json.dumps(checks.trace, indent=2))
        (args.output / "results.json").write_text(json.dumps({"completed": checks.completed}, indent=2))
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
