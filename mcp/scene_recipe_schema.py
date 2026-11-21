"""Strict validation for the public editable-scene export descriptor."""

from __future__ import annotations

import base64
import binascii
import math
from pathlib import PurePosixPath
import re
import struct
import zlib
from typing import Any

FORMAT = "vosc.osc-recipe"
VERSION = 1
MAX_COMMANDS = 2048
MAX_ASSETS = 64
MAX_FRAGMENT = 128 * 1024
MAX_FRAGMENTS = 1024 * 1024
MAX_IMAGES = 4 * 1024 * 1024
MAX_DECODED_IMAGES = 64 * 1024 * 1024
MAX_STRING = 2048
MAX_COMPONENTS = 1024
MAX_BUNDLE = 61_440
UINT64_MAX = 18_446_744_073_709_551_615
RECIPE_LIMITS = {
    "recipe_commands": MAX_COMMANDS,
    "recipe_assets": MAX_ASSETS,
    "recipe_components": MAX_COMPONENTS,
    "recipe_fragment_bytes": MAX_FRAGMENT,
    "recipe_fragments_bytes": MAX_FRAGMENTS,
    "recipe_images_bytes": MAX_IMAGES,
    "recipe_bundle_bytes": MAX_BUNDLE,
}
IDENTIFIER = re.compile(r"^[A-Za-z_][A-Za-z0-9_]{0,63}$")
RESERVED = frozenset({"camPos", "camLook", "camOrbit"})
LAYOUTS = frozenset({"stack", "grid", "col", "row"})


class RecipeValidationError(ValueError):
    pass


def _exact(value: Any, fields: set[str], label: str) -> dict[str, Any]:
    if not isinstance(value, dict) or set(value) != fields:
        raise RecipeValidationError(f"{label} has invalid fields")
    return value


def _int(value: Any, minimum: int, maximum: int, label: str) -> int:
    if type(value) is not int or not minimum <= value <= maximum:
        raise RecipeValidationError(f"{label} must be an integer in {minimum}..{maximum}")
    return value


def _float(value: Any, label: str, *, positive: bool = False) -> float:
    if type(value) is not float or not math.isfinite(value) or abs(value) > 1_000_000:
        raise RecipeValidationError(f"{label} must be a bounded finite JSON float")
    if positive and not 0 < value <= 2048:
        raise RecipeValidationError(f"{label} must be in (0, 2048]")
    normalized = struct.unpack("!f", struct.pack("!f", value))[0]
    if value != normalized:
        raise RecipeValidationError(f"{label} must already be normalized to float32")
    return normalized


def _string(value: Any, label: str, *, empty: bool = False) -> str:
    if not isinstance(value, str) or "\0" in value or (not empty and not value):
        raise RecipeValidationError(f"{label} must be a valid nonempty string")
    try:
        encoded = value.encode("utf-8")
    except UnicodeError as error:
        raise RecipeValidationError(f"{label} is not valid UTF-8") from error
    if len(encoded) > MAX_STRING:
        raise RecipeValidationError(f"{label} exceeds {MAX_STRING} UTF-8 bytes")
    return value


def _identifier(value: Any, label: str, *, reserved: bool = False) -> str:
    value = _string(value, label)
    if not IDENTIFIER.fullmatch(value) or (not reserved and value in RESERVED):
        raise RecipeValidationError(f"{label} is not a supported identifier")
    return value


def _asset_name(value: Any, kind: str) -> str:
    value = _string(value, "asset name")
    if "\\" in value or ":" in value or not value.startswith("assets/"):
        raise RecipeValidationError("asset name must be a relative POSIX path under assets/")
    raw_parts = value.split("/")
    if any(part in {"", ".", ".."} for part in raw_parts):
        raise RecipeValidationError("asset name is unsafe")
    path = PurePosixPath(value)
    if path.is_absolute() or any(part in {"", ".", ".."} for part in path.parts):
        raise RecipeValidationError("asset name is unsafe")
    expected = ".frag" if kind in {"fragment", "feedback_fragment"} else ".png"
    if path.suffix != expected:
        raise RecipeValidationError(f"{kind} assets must use {expected}")
    return value


def _validate_png(data: bytes) -> int:
    if not data.startswith(b"\x89PNG\r\n\x1a\n"):
        raise RecipeValidationError("image asset is not a PNG")
    offset, width, height, compressed, ended, saw_idat, idat_closed = 8, 0, 0, bytearray(), False, False, False
    channels = color = 0
    saw_palette = False
    while offset + 12 <= len(data):
        length = struct.unpack(">I", data[offset:offset + 4])[0]
        if length > 4 * 1024 * 1024 or offset + 12 + length > len(data):
            raise RecipeValidationError("image PNG chunks are malformed")
        kind = data[offset + 4:offset + 8]; payload = data[offset + 8:offset + 8 + length]
        if len(kind) != 4 or any(not (65 <= byte <= 90 or 97 <= byte <= 122) for byte in kind):
            raise RecipeValidationError("image PNG chunk name is invalid")
        if kind[2] & 0x20:
            raise RecipeValidationError("image PNG chunk uses the reserved bit")
        checksum = struct.unpack(">I", data[offset + 8 + length:offset + 12 + length])[0]
        if zlib.crc32(kind + payload) & 0xFFFFFFFF != checksum:
            raise RecipeValidationError("image PNG checksum is invalid")
        if offset == 8:
            if kind != b"IHDR" or length != 13: raise RecipeValidationError("image PNG lacks IHDR")
            width, height = struct.unpack(">II", payload[:8])
            if not (1 <= width <= 2048 and 1 <= height <= 2048):
                raise RecipeValidationError("image dimensions are outside bounds")
            depth, color, compression, filtering, interlace = payload[8:]
            if depth != 8 or color not in {0, 2, 4, 6} or compression or filtering or interlace:
                raise RecipeValidationError("image PNG encoding is unsupported")
            channels = {0: 1, 2: 3, 4: 2, 6: 4}[color]
        elif kind == b"IHDR":
            raise RecipeValidationError("image PNG has duplicate IHDR")
        if kind[0] & 0x20 == 0 and kind not in {b"IHDR", b"PLTE", b"IDAT", b"IEND"}:
            raise RecipeValidationError("image PNG has an unknown critical chunk")
        if kind == b"PLTE":
            if (saw_palette or saw_idat or color in {0, 4} or not payload or
                    len(payload) % 3 or len(payload) > 768):
                raise RecipeValidationError("image PNG palette is malformed")
            saw_palette = True
        if kind == b"IDAT":
            if idat_closed: raise RecipeValidationError("image PNG IDAT chunks must be contiguous")
            saw_idat = True; compressed.extend(payload)
        elif saw_idat and kind != b"IEND": idat_closed = True
        if kind == b"IEND":
            if length != 0 or not saw_idat: raise RecipeValidationError("image PNG IEND is malformed")
            ended = True; offset += 12 + length; break
        offset += 12 + length
    if not ended or offset != len(data) or not compressed:
        raise RecipeValidationError("image PNG is incomplete")
    expected = height * (1 + width * channels)
    try:
        decoder = zlib.decompressobj()
        raw = decoder.decompress(bytes(compressed), expected + 1)
    except zlib.error as error: raise RecipeValidationError("image PNG data is invalid") from error
    if len(raw) != expected or not decoder.eof or decoder.unused_data or decoder.unconsumed_tail:
        raise RecipeValidationError("image PNG scanline data has invalid length")
    stride = 1 + width * channels
    if any(raw[row * stride] > 4 for row in range(height)):
        raise RecipeValidationError("image PNG uses an invalid row filter")
    return width * height * 4


def _arg(arg: Any, label: str) -> tuple[str, Any]:
    arg = _exact(arg, {"type", "value"}, label)
    kind = arg["type"]
    value = arg["value"]
    if kind == "i":
        return kind, _int(value, -2_147_483_648, 2_147_483_647, label)
    if kind == "f":
        return kind, _float(value, label)
    if kind == "s":
        return kind, _string(value, label, empty=True)
    if kind == "asset":
        return kind, _string(value, label)
    raise RecipeValidationError(f"{label} has unknown type")


def _number(arg: tuple[str, Any], label: str, *, positive: bool = False) -> None:
    if arg[0] not in {"f", "s"}:
        raise RecipeValidationError(f"{label} must be a float or expression string")
    if arg[0] == "s" and not arg[1]:
        raise RecipeValidationError(f"{label} expression must not be empty")
    if positive and arg[0] == "f" and not 0 < arg[1] <= 2048:
        raise RecipeValidationError(f"{label} is outside the size bound")


def _expression_dependencies(expression: str, shared: set[str], inputs: set[str]) -> set[str]:
    dependencies: set[str] = set()
    constants = {"left", "right", "top", "bottom", "center", "pi", "e", "i", "total"}
    for match in re.finditer(r"[A-Za-z_][A-Za-z0-9_]*", expression):
        token = match.group()
        following = expression[match.end():].lstrip()
        if following.startswith("(") or token in constants: continue
        if token in RESERVED: raise RecipeValidationError("expression references a reserved camera variable")
        if token in shared: dependencies.add(token); continue
        if token.startswith("in_") and token[3:] in inputs: continue
        if token in {"t", "f"}: continue
        raise RecipeValidationError(f"expression has unresolved reference: {token}")
    return dependencies


def _component_count(address: str, args: list[tuple[str, Any]]) -> int:
    if address in {"/input/set", "/var"}:
        values = args[-1:]
    elif address in {"/shader/var", "/tex/var"}:
        values = args[2:]
    elif address in {"/tex/size", "/tex/speed"}:
        values = args[1:]
    elif address in {"/shader/texture/size", "/shader/texture/speed"}:
        values = args[2:]
    elif address in {
        "/layer/pos", "/layer/rot", "/layer/scale", "/layer/pivot", "/layer/size",
        "/layer/tint", "/layer/alpha", "/layer/bri", "/layer/speed", "/layer/visible",
    }:
        values = args[1:]
    else:
        values = []
    return sum(kind in {"f", "i", "s"} for kind, _ in values)


def _validate_expression_graph(expressions: dict[str, set[str]]) -> None:
    graph: dict[str, set[str]] = {name: set() for name in expressions}
    for name, dependencies in expressions.items():
        graph[name].update(dependency for dependency in dependencies if dependency in expressions)
    visiting: set[str] = set(); visited: set[str] = set()
    def visit(name: str) -> None:
        if name in visiting: raise RecipeValidationError("shared expression dependency cycle")
        if name in visited: return
        visiting.add(name)
        for dependency in graph[name]: visit(dependency)
        visiting.remove(name); visited.add(name)
    for name in graph: visit(name)


def validate_descriptor(value: Any) -> dict[str, Any]:
    root = _exact(value, {"format", "version", "resolution", "onset_gate", "commands", "assets"}, "recipe")
    if root["format"] != FORMAT or type(root["version"]) is not int or root["version"] != VERSION:
        raise RecipeValidationError("unsupported recipe format/version")
    resolution = _exact(root["resolution"], {"width", "height"}, "resolution")
    _int(resolution["width"], 64, 2048, "width")
    _int(resolution["height"], 64, 2048, "height")
    _int(root["onset_gate"], -1, 1, "onset_gate")
    assets = root["assets"]
    if not isinstance(assets, list) or len(assets) > MAX_ASSETS:
        raise RecipeValidationError("assets must be a bounded array")
    declared: dict[str, tuple[str, bytes]] = {}
    image_costs: dict[str, int] = {}
    fragment_total = image_total = 0
    for index, raw in enumerate(assets):
        asset = _exact(raw, {"name", "kind", "encoding", "data"}, f"asset {index}")
        kind = asset["kind"]
        if kind not in {"fragment", "feedback_fragment", "image"}:
            raise RecipeValidationError("asset kind is unsupported")
        name = _asset_name(asset["name"], kind)
        if name in declared:
            raise RecipeValidationError("asset names collide")
        data = asset["data"]
        if kind in {"fragment", "feedback_fragment"}:
            if asset["encoding"] != "utf8" or not isinstance(data, str) or "\0" in data:
                raise RecipeValidationError("fragment encoding must be utf8")
            try:
                decoded = data.encode("utf-8")
            except UnicodeError as error:
                raise RecipeValidationError("fragment is not valid UTF-8") from error
            if len(decoded) > MAX_FRAGMENT:
                raise RecipeValidationError("fragment exceeds per-file bound")
            fragment_total += len(decoded)
        else:
            if asset["encoding"] != "base64" or not isinstance(data, str):
                raise RecipeValidationError("image encoding must be base64")
            try:
                decoded = base64.b64decode(data, validate=True)
            except (binascii.Error, ValueError) as error:
                raise RecipeValidationError("image data is invalid base64") from error
            image_total += len(decoded)
            image_costs[name] = _validate_png(decoded)
        declared[name] = (kind, decoded)
    if fragment_total > MAX_FRAGMENTS or image_total > MAX_IMAGES:
        raise RecipeValidationError("aggregate asset bound exceeded")

    commands = root["commands"]
    if not isinstance(commands, list) or not 1 <= len(commands) <= MAX_COMMANDS:
        raise RecipeValidationError("commands must be a nonempty bounded array")
    normalized: list[tuple[str, list[tuple[str, Any]]]] = []
    components = decoded_image_bytes = 0
    for index, raw in enumerate(commands):
        command = _exact(raw, {"address", "args"}, f"command {index}")
        address = _string(command["address"], "command address")
        if not isinstance(command["args"], list):
            raise RecipeValidationError("command args must be an array")
        args = [_arg(item, f"command {index} arg {position}") for position, item in enumerate(command["args"])]
        components += _component_count(address, args)
        decoded_image_bytes += sum(image_costs.get(value, 0) for kind, value in args if kind == "asset")
        if decoded_image_bytes > MAX_DECODED_IMAGES:
            raise RecipeValidationError("scene exceeds the 64 MiB decoded image budget")
        normalized.append((address, args))
    if components > MAX_COMPONENTS:
        raise RecipeValidationError("recipe exceeds scalar component bound")
    first_address, first = normalized[0]
    if first_address != "/layers/reset" or len(first) != 2 or first[0][0] != "i" or first[1][0] != "s":
        raise RecipeValidationError("first command must be /layers/reset count layout")
    count = _int(first[0][1], 0, 32, "layer count")
    if first[1][1] not in LAYOUTS:
        raise RecipeValidationError("layout is unsupported")
    inputs: set[str] = set()
    shared: set[str] = set()
    shared_expressions: dict[str, set[str]] = {}
    shaders: set[int] = set()
    default_images: set[int] = set()
    samplers: set[tuple[int, str]] = set()
    holder_bindings: dict[int, set[str]] = {}
    first_shared_assignment: int | None = None
    first_dependent: int | None = None
    shared_assignment_positions: dict[str, int] = {}
    shared_assignment_dependencies: dict[str, set[str]] = {}
    feedback: dict[int, dict[str, Any]] = {}
    for position, (address, args) in enumerate(normalized):
        if position and address == "/layers/reset":
            raise RecipeValidationError("/layers/reset must occur exactly once")
        _validate_command(address, args, count, declared, inputs, shared, shared_expressions,
                          shaders, default_images, samplers, feedback)
        if address not in {"/layers/reset", "/input", "/input/set", "/var"}:
            first_dependent = position if first_dependent is None else first_dependent
        if address in {"/input", "/input/set"} and (first_shared_assignment is not None or first_dependent is not None):
            raise RecipeValidationError("inputs and input settings must precede shared assignments and dependents")
        if address == "/var":
            name = args[0][1]
            if args[1][0] == "f":
                if first_shared_assignment is not None or first_dependent is not None:
                    raise RecipeValidationError("shared bootstraps must precede assignments and dependents")
            else:
                if first_dependent is not None:
                    raise RecipeValidationError("shared assignments must precede dependent commands")
                first_shared_assignment = position if first_shared_assignment is None else first_shared_assignment
                shared_assignment_positions[name] = position
                shared_assignment_dependencies[name] = set(shared_expressions[name])
        if address == "/shader/var":
            layer = args[0][1]; holder_bindings.setdefault(layer, set()).add("uniform:" + args[1][1])
        elif address == "/shader/texture":
            layer = args[0][1]; holder_bindings.setdefault(layer, set()).add("sampler:" + args[1][1])
        elif address.startswith("/layer/"):
            layer = args[0][1]; holder_bindings.setdefault(layer, set()).add(address)
        elif address.startswith("/tex/"):
            layer = args[0][1]; key = address + (":" + str(args[1][1]) if address == "/tex/set" else "")
            holder_bindings.setdefault(layer, set()).add(key)
        elif address.startswith("/shader/texture/"):
            layer = args[0][1]; key = address + ":" + str(args[1][1])
            if address.endswith("/set"): key += ":" + str(args[2][1])
            holder_bindings.setdefault(layer, set()).add(key)
        if address != "/layers/reset" and len(holder_bindings.get(args[0][1] if args and args[0][0] == "i" else -1, ())) > 128:
            raise RecipeValidationError("holder exceeds binding bound")
    for state in feedback.values():
        if not {"/tex/size", "/tex/speed", "internalformat", "textureTarget", "aspectRatio", "static", "blendMode"} <= state["settings"]:
            raise RecipeValidationError("incomplete feedback setup")
        if "filter" in state["settings"] and ({"minFilter", "magFilter"} & state["settings"]):
            raise RecipeValidationError("feedback filter settings overlap")
        if not ("filter" in state["settings"] or {"minFilter", "magFilter"} <= state["settings"]):
            raise RecipeValidationError("feedback requires explicit linear filtering")
    if len(inputs) > 16 or len(shared) > 64:
        raise RecipeValidationError("recipe exceeds input or shared-variable bounds")
    _validate_expression_graph(shared_expressions)
    for name, dependencies in shared_assignment_dependencies.items():
        for dependency in dependencies:
            dependency_position = shared_assignment_positions.get(dependency)
            if dependency_position is not None and dependency_position >= shared_assignment_positions[name]:
                raise RecipeValidationError("shared expression dependency must be assigned before use")
    return root


def validate_recipe_capabilities(result: Any) -> None:
    """Validate the advertised recipe contract without constraining unrelated capabilities."""
    if not isinstance(result, dict):
        raise RecipeValidationError("capability result must be an object")
    operations = result.get("operations")
    features = result.get("features")
    limits = result.get("limits")
    if not isinstance(operations, list) or not isinstance(features, dict) or not isinstance(limits, dict):
        raise RecipeValidationError("capability operations/features/limits are invalid")
    advertised = "export_scene" in operations
    if advertised:
        if features.get("scene_recipe_export") is not True:
            raise RecipeValidationError("export_scene requires scene_recipe_export true")
        for name, expected in RECIPE_LIMITS.items():
            if type(limits.get(name)) is not int or limits[name] != expected:
                raise RecipeValidationError(f"capability {name} does not match the recipe contract")
    elif features.get("scene_recipe_export") is True:
        raise RecipeValidationError("scene_recipe_export requires export_scene")


def validate_export_reply(reply: Any) -> dict[str, Any]:
    """Validate a strict export success or failure envelope shared by CLI and MCP."""
    if not isinstance(reply, dict) or not isinstance(reply.get("id"), str):
        raise RecipeValidationError("export response must be an object with a string id")
    if type(reply.get("ok")) is not bool or not isinstance(reply.get("status"), str):
        raise RecipeValidationError("export response ok/status fields are invalid")
    for field in ("revision", "frame", "external_epoch"):
        _int(reply.get(field), 0, UINT64_MAX, field)
    if reply["ok"] is False:
        if reply["status"] != "failed":
            raise RecipeValidationError("failed export response must have status failed")
        error = _exact(reply.get("error"), {"code", "message", "diagnostics"}, "export error")
        _string(error["code"], "export error code")
        _string(error["message"], "export error message")
        if not isinstance(error["diagnostics"], list) or len(error["diagnostics"]) > 512:
            raise RecipeValidationError("export diagnostics must be a bounded array")
        return reply
    if reply["status"] != "ok":
        raise RecipeValidationError("successful export response must have status ok")
    result = _exact(reply.get("result"), {"recipe"}, "export result")
    validate_descriptor(result["recipe"])
    return reply


def _layer(args: list[tuple[str, Any]], count: int) -> int:
    if not args or args[0][0] != "i":
        raise RecipeValidationError("layer command requires an integer layer index")
    return _int(args[0][1], 0, count - 1, "layer index")


def _validate_command(address: str, args: list[tuple[str, Any]], count: int,
                      assets: dict[str, tuple[str, bytes]], inputs: set[str], shared: set[str],
                      expressions: dict[str, set[str]],
                      shaders: set[int], defaults: set[int], samplers: set[tuple[int, str]], feedback: dict[int, dict[str, Any]]) -> None:
    if address == "/layers/reset":
        return
    if address == "/input":
        if len(args) != 4 or args[0][0] not in {"s", "i"} or args[1][0] != "s" or args[2][0] != "i" or args[3][0] != "i":
            raise RecipeValidationError("invalid /input signature")
        name = str(args[0][1])
        if args[0][0] == "s": _identifier(name, "input name")
        else: _int(args[0][1], 0, 2_147_483_647, "input ID")
        if name in inputs: raise RecipeValidationError("duplicate input")
        channels = _int(args[2][1], 0, 32, "input channels")
        _int(args[3][1], 64, 8192, "input buffer")
        if channels and not args[1][1]: raise RecipeValidationError("audio input requires a device")
        inputs.add(name); return
    if address == "/input/set":
        if len(args) != 3 or args[0][0] not in {"s", "i"} or args[1] not in {("s", "maxAmp"), ("s", "maxLoud")} or args[2][0] != "f":
            raise RecipeValidationError("invalid /input/set signature")
        if str(args[0][1]) not in inputs: raise RecipeValidationError("input setting precedes input")
        if args[1][1] == "maxAmp" and args[2][1] <= 0: raise RecipeValidationError("maxAmp must be positive")
        return
    if address == "/var":
        if len(args) != 2 or args[0][0] != "s": raise RecipeValidationError("invalid /var signature")
        name = _identifier(args[0][1], "shared variable")
        _number(args[1], "shared variable")
        if name not in shared:
            if args[1][0] != "f": raise RecipeValidationError("shared expression requires a numeric bootstrap")
            shared.add(name)
        else:
            if args[1][0] != "s" or name in expressions:
                raise RecipeValidationError("shared variable permits one expression after its bootstrap")
            expressions[name] = _expression_dependencies(args[1][1], shared, inputs)
        return
    layer = _layer(args, count)
    tail = args[1:]
    if address in {"/shader", "/tex"}:
        if len(tail) != 1 or tail[0][0] != "asset" or tail[0][1] not in assets:
            raise RecipeValidationError(f"invalid {address} asset")
        expected = "fragment" if address == "/shader" else "image"
        kind = assets[tail[0][1]][0]
        if address == "/tex" and kind == "feedback_fragment":
            if feedback: raise RecipeValidationError("only one feedback object is supported")
            feedback[layer] = {"controls": set(), "settings": set()}
        elif kind != expected: raise RecipeValidationError("asset kind mismatch")
        target = shaders if address == "/shader" else defaults
        if layer in target or (address == "/tex" and (layer, "tex") in samplers):
            raise RecipeValidationError("duplicate source declaration")
        target.add(layer); return
    if address == "/tex/var":
        if layer not in feedback or not 2 <= len(tail) <= 5 or tail[0][0] != "s":
            raise RecipeValidationError("invalid feedback control signature or ordering")
        name = _identifier(tail[0][1], "feedback uniform")
        if name.startswith("gl_") or name in {"time", "resolution", "srctex", "size", "speed", "color", "tint", "timePct", "randomSeed", "random", "mouse", "frame", "modelViewProjectionMatrix", "modelViewMatrix", "projectionMatrix", "textureMatrix", "globalColor", "usingTexture", "usingColors"}:
            raise RecipeValidationError("feedback built-in override")
        if any(arg[0] != "f" for arg in tail[1:]):
            raise RecipeValidationError("feedback controls require constant floats")
        controls = feedback[layer]["controls"]
        if name in controls or len(controls) >= 64:
            raise RecipeValidationError("duplicate or excessive feedback controls")
        controls.add(name)
        return
    if address == "/shader/var":
        if layer not in shaders or not 2 <= len(tail) <= 5 or tail[0][0] != "s":
            raise RecipeValidationError("invalid /shader/var signature or ordering")
        _identifier(tail[0][1], "uniform name")
        for arg in tail[1:]:
            if arg[0] not in {"f", "i", "s"}: raise RecipeValidationError("invalid uniform value")
            if arg[0] == "s": _expression_dependencies(arg[1], shared, inputs)
        numeric = {arg[0] for arg in tail[1:] if arg[0] != "s"}
        if len(numeric) > 1: raise RecipeValidationError("uniform numeric components must have one type")
        return
    if address == "/shader/texture":
        if layer not in shaders or len(tail) != 2 or tail[0][0] != "s" or tail[1][0] != "asset":
            raise RecipeValidationError("invalid /shader/texture signature")
        name = _identifier(tail[0][1], "sampler name")
        key = (layer, name)
        if (key in samplers or (name == "tex" and layer in defaults) or
                tail[1][1] not in assets or assets[tail[1][1]][0] != "image"):
            raise RecipeValidationError("invalid or duplicate sampler asset")
        samplers.add(key); return
    if address in {"/tex/size", "/tex/speed", "/tex/set"}:
        if layer not in defaults: raise RecipeValidationError("default texture control precedes /tex")
        if layer in feedback:
            state = feedback[layer]
            if address == "/tex/set" and (len(tail) != 2 or tail[0][0] != "s"):
                raise RecipeValidationError("invalid feedback setting signature")
            key = address if address != "/tex/set" else tail[0][1]
            if key in state["settings"]: raise RecipeValidationError("duplicate feedback setting")
            if address == "/tex/size":
                if len(tail) != 2 or any(a[0] != "f" for a in tail) or any(not (1 <= a[1] <= limit and a[1].is_integer()) for a, limit in zip(tail, (800, 600))):
                    raise RecipeValidationError("feedback dimensions must be integral and bounded")
            elif address == "/tex/speed":
                if tail != [("f", 1.0)]: raise RecipeValidationError("feedback speed must be one")
            else:
                fixed = {"internalformat": 32856, "textureTarget": 3553, "filter": 9729,
                         "minFilter": 9729, "magFilter": 9729, "aspectRatio": 1,
                         "blendMode": 0, "noClear": 0}
                if len(tail) != 2 or tail[0][0] != "s" or tail[1][0] != "i" or not (
                    (key == "static" and tail[1][1] in {0, 1}) or (key in fixed and tail[1][1] == fixed[key])):
                    raise RecipeValidationError("unsupported feedback setting")
            state["settings"].add(key)
            return
        _validate_texture_tail(address, tail, shared, inputs); return
    if address in {"/shader/texture/size", "/shader/texture/speed", "/shader/texture/set"}:
        if not tail or tail[0][0] != "s" or (layer, tail[0][1]) not in samplers:
            raise RecipeValidationError("named texture control precedes binding")
        _validate_texture_tail(address.replace("/shader/texture", "/tex"), tail[1:], shared, inputs); return
    vector_counts = {"/layer/pos": 3, "/layer/rot": 3, "/layer/scale": 3, "/layer/pivot": 3,
                     "/layer/size": 3, "/layer/tint": 4}
    if address in vector_counts:
        if len(tail) != vector_counts[address]: raise RecipeValidationError(f"invalid {address} arity")
        for index, arg in enumerate(tail):
            _number(arg, address, positive=address == "/layer/size" and index < 2)
            if arg[0] == "s": _expression_dependencies(arg[1], shared, inputs)
        if address == "/layer/size" and tail[2][0] == "f" and tail[2][1] != 0: raise RecipeValidationError("layer size z must be zero")
        return
    if address in {"/layer/alpha", "/layer/bri", "/layer/speed"}:
        if len(tail) != 1: raise RecipeValidationError(f"invalid {address} arity")
        _number(tail[0], address)
        if tail[0][0] == "s": _expression_dependencies(tail[0][1], shared, inputs)
        return
    if address == "/layer/visible":
        if len(tail) != 1 or not ((tail[0][0] == "i" and tail[0][1] in {0, 1}) or tail[0][0] == "s"):
            raise RecipeValidationError("invalid /layer/visible")
        if tail[0][0] == "s": _expression_dependencies(tail[0][1], shared, inputs)
        return
    if address == "/layer/align":
        if len(tail) != 2 or tail[0][0] != "i" or tail[1][0] != "i" or tail[0][1] not in {0,1,2,4} or tail[1][1] not in {0,16,32,64}:
            raise RecipeValidationError("invalid /layer/align")
        return
    if address == "/layer/blend":
        if len(tail) != 1 or tail[0][0] != "i" or tail[0][1] not in range(6): raise RecipeValidationError("invalid /layer/blend")
        return
    raise RecipeValidationError(f"unsupported setup address: {address}")


def _validate_texture_tail(address: str, tail: list[tuple[str, Any]], shared: set[str], inputs: set[str]) -> None:
    if address == "/tex/size":
        if len(tail) != 2 or any(arg[0] != "f" for arg in tail): raise RecipeValidationError("invalid texture size")
        for arg in tail: _float(arg[1], "texture size", positive=True)
    elif address == "/tex/speed":
        if len(tail) != 1: raise RecipeValidationError("invalid texture speed")
        _number(tail[0], "texture speed")
        if tail[0][0] == "s": _expression_dependencies(tail[0][1], shared, inputs)
    elif address == "/tex/set":
        if len(tail) != 2 or tail[0][0] != "s" or tail[1][0] != "i": raise RecipeValidationError("invalid texture setting")
        prop, value = tail[0][1], tail[1][1]
        if (prop in {"aspectRatio", "static"} and value not in {0,1}) or (prop == "blendMode" and value not in range(6)) or prop not in {"aspectRatio", "static", "blendMode"}:
            raise RecipeValidationError("unsupported texture setting")
