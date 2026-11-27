"""Supported standalone draw-fragment profiles for the authoring importer."""

from __future__ import annotations

import re

from .acquire import ImportFailure, MAX_SOURCE_BYTES
from .glsl import Conversion, convert_legacy_fragment, mask_comments


NATIVE_VERSION = "vosc-native-fragment-v1"
SHADERTOY_VERSION = "vosc-shadertoy-image-v1"
_WORD = r"[A-Za-z_][A-Za-z0-9_]*"
_IDENTIFIER = re.compile(_WORD)
_DIRECTIVE = re.compile(r"^[ \t]*#[ \t]*(\w+)[^\r\n]*", re.MULTILINE)
_VERSION = re.compile(r"^[ \t]*#[ \t]*version[ \t]+(\d+)(?:[ \t]+(\w+))?[ \t]*(?=\r?$)",
                      re.MULTILINE)
_NATIVE_VERSIONS = {"150", "330", "400", "410"}
_DIRECTIVES = {"version", "define", "undef", "if", "ifdef", "ifndef", "elif", "else", "endif", "line"}
_TEXTURE_TARGETS = ("1D", "2D", "3D", "Cube", "Buffer", "1DArray", "2DArray",
                    "CubeArray", "2DRect", "2DMS", "2DMSArray")
_TEXTURE_TYPES = {
    kind + target
    for kind in ("sampler", "isampler", "usampler", "image", "iimage", "uimage")
    for target in _TEXTURE_TARGETS
} | {"sampler" + target + "Shadow"
     for target in ("1D", "2D", "Cube", "1DArray", "2DArray", "CubeArray", "2DRect")}
_MAIN = re.compile(r"\bvoid\s+main\s*\(\s*(?:void\s*)?\)\s*\{")
_MAIN_IMAGE = re.compile(
    rf"\bvoid\s+mainImage\s*\(\s*out\s+vec4\s+{_WORD}\s*,\s*in\s+vec2\s+{_WORD}\s*\)\s*\{{")
_ST_REPLACE = {"iTime": "(time)", "iResolution": "(vec3(resolution, 1.0))"}
_ST_RESERVED = {"main", "time", "resolution", "voscImageColor", "gl_FragColor", "gl_FragData"}
_ST_HOST = {"iTime", "iResolution", "iDate", "iSampleRate", "iMouse", "iFrame",
            "iTimeDelta", "iFrameRate", "iGlobalTime", "iAudio", "iOffset", "iChannel"}


def syntax(source):
    if not source or len(source) > MAX_SOURCE_BYTES:
        raise ImportFailure("malformed_source", "Fragment source is empty or exceeds the engine source limit")
    try:
        text = source.decode("utf-8")
        code = mask_comments(text)
    except (UnicodeError, ValueError) as error:
        raise ImportFailure("malformed_source", str(error)) from None
    if any(ord(ch) < 32 and ch not in "\t\r\n" for ch in text) or re.search(r"\\\r?\n", text):
        raise ImportFailure("malformed_source", "Control characters and continued source lines are unsupported")
    if re.search(r"\r(?!\n)", text):
        raise ImportFailure("malformed_source", "Use LF or CRLF line endings")
    directives = list(_DIRECTIVE.finditer(code))
    if "#" in _DIRECTIVE.sub("", code) or "##" in code:
        raise ImportFailure("unsupported_profile", "Unrecognized GLSL preprocessor syntax")
    for directive in directives:
        name = directive[1]
        if name in {"include", "extension"}:
            raise ImportFailure("unsupported_dependency", f"External GLSL #{name} is unsupported")
        if name not in _DIRECTIVES:
            raise ImportFailure("unsupported_profile", f"GLSL #{name} is unsupported")
    return text, code, directives


def top_level(code):
    """Keep tokens outside argument lists and bodies for interface checks."""
    body = _DIRECTIVE.sub("", code)
    depth = 0
    for symbol in re.findall(rf"{_WORD}|[(){{}}\[\];,]", body):
        if symbol in "({[":
            depth += 1
        elif symbol in ")}]":
            depth -= 1
        elif depth == 0:
            yield symbol


def reject_dependencies(code, *, shadertoy=False):
    identifiers = set(_IDENTIFIER.findall(code))
    textures = identifiers & _TEXTURE_TYPES
    if textures:
        raise ImportFailure("unsupported_dependency",
                            "Shader requires texture or external image inputs: " + ", ".join(sorted(textures)))
    shadertoy_globals = (_ST_HOST & identifiers) | {name for name in identifiers if name.startswith("iChannel")}
    if shadertoy:
        shadertoy_globals -= _ST_REPLACE.keys()
    if shadertoy_globals:
        raise ImportFailure("unsupported_dependency",
                            "Unsupported shader host inputs: " + ", ".join(sorted(shadertoy_globals)))
    if {"varying", "attribute", "gl_TexCoord", "gl_Color", "gl_SecondaryColor",
        "gl_FogFragCoord", "gl_FragData"} & identifiers or "in" in top_level(code):
        raise ImportFailure("unsupported_dependency", "Shader requires a custom vertex input or output interface")


def standalone(source):
    _, code, directives = syntax(source)
    if not code.strip():
        raise ImportFailure("malformed_source", "Fragment source has no code")
    reject_dependencies(code)
    version = _VERSION.search(code)
    if not version or code[:version.start()].strip() or sum(item[1] == "version" for item in directives) != 1:
        raise ImportFailure("unsupported_profile", "Expected one explicit desktop GLSL version before shader code")
    number, profile = version.groups()
    if (number not in _NATIVE_VERSIONS | {"120"} or
            profile not in (None, "core") or (number == "120" and profile is not None)):
        raise ImportFailure("unsupported_profile", f"GLSL {number} {profile or ''} is unsupported".strip())
    if not _MAIN.search(code):
        raise ImportFailure("malformed_source", "Expected a standalone fragment entry point void main()")
    if number == "120":
        try:
            return convert_legacy_fragment(source)
        except ValueError as error:
            raise ImportFailure("unsupported_profile", str(error)) from None
    tokens = list(top_level(code))
    if tokens.count("out") != 1 or "gl_FragColor" in _IDENTIFIER.findall(code):
        raise ImportFailure("unsupported_profile", "Native fragment requires one explicit color output")
    return Conversion(source, NATIVE_VERSION, f"GLSL {number} standalone fragment retained byte for byte")


def wrap_shadertoy(source):
    text, code, directives = syntax(source)
    reject_dependencies(code, shadertoy=True)
    identifiers = set(_IDENTIFIER.findall(code))
    collisions = identifiers & _ST_RESERVED
    if collisions:
        raise ImportFailure("unsupported_profile", "Shadertoy wrapper name conflict: " + ", ".join(sorted(collisions)))
    signature = _MAIN_IMAGE.search(code)
    if not signature or len(re.findall(r"\bvoid\s+mainImage\b", code)) != 1:
        raise ImportFailure("unsupported_profile", "Expected one mainImage(out vec4, in vec2) definition")
    # Supported globals appear only as expression tokens. Do not replace
    # member names, declaration names or tokens in preprocessor directives.
    for directive in directives:
        if directive[1] in {"version", "line"} or set(_IDENTIFIER.findall(directive[0])) & _ST_REPLACE.keys():
            raise ImportFailure("unsupported_profile", "Shadertoy preprocessor conflicts with the wrapper")
    for name in _ST_REPLACE:
        if re.search(rf"(?:{_WORD}\s+|\.\s*)\b{name}\b", code):
            # Return expressions are valid identifier uses.
            candidate = re.sub(r"\breturn\s+", " ", code)
            if re.search(rf"(?:{_WORD}\s+|\.\s*)\b{name}\b", candidate):
                raise ImportFailure("unsupported_profile", f"Shadertoy host identifier is redeclared: {name}")
    parts = ["#version 330\nuniform float time;\nuniform vec2 resolution;\n"
             "out vec4 voscImageColor;\n#line 1\n"]
    cursor = 0
    for token in _IDENTIFIER.finditer(code):
        if token[0] in _ST_REPLACE:
            parts.extend((text[cursor:token.start()], _ST_REPLACE[token[0]]))
            cursor = token.end()
    parts.extend((text[cursor:], "\nvoid main() { mainImage(voscImageColor, gl_FragCoord.xy); }\n"))
    return Conversion("".join(parts).encode("utf-8"), SHADERTOY_VERSION,
                      "Channel-free Image wrapped with VOSC time and resolution")
