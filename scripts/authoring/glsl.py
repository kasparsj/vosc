"""Bounded fragment-source conversion for the standard VOSC draw shader."""

from __future__ import annotations

from dataclasses import dataclass
import re


LEGACY_VERSION = "vosc-glsl120-to-330-v1"
LEGACY_OUTPUT = "voscFragmentColor"
_IDENTIFIER = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
_COMMENTS = re.compile(r"//[^\r\n]*|/\*[\s\S]*?\*/|/\*")
_VERSION = re.compile(r"[ \t]*#[ \t]*version[ \t]+(120)[ \t]*(?=\r?$)", re.MULTILINE)
_DIRECTIVE = re.compile(r"^[ \t]*#[ \t]*(\w+)[^\r\n]*", re.MULTILINE)


@dataclass(frozen=True)
class Conversion:
    source: bytes
    version: str
    diagnostic: str


def mask_comments(source: str) -> str:
    """Keep source offsets and line breaks while hiding comments from token rules."""
    def mask(match: re.Match[str]) -> str:
        if match[0] == "/*":
            raise ValueError("unterminated GLSL block comment")
        return re.sub(r"[^\r\n]", " ", match[0])

    return _COMMENTS.sub(mask, source)


def convert_legacy_fragment(source: bytes) -> Conversion:
    """Convert GLSL 120 standalone fragments using gl_FragColor to GLSL 330."""
    text = source.decode("utf-8")
    code = mask_comments(text)
    if "\x00" in text or re.search(r"\\\r?\n", text):
        raise ValueError("NUL and continued source lines are unsupported")
    version = _VERSION.match(code, len(code) - len(code.lstrip()))
    if not version:
        raise ValueError("expected a GLSL 120 version directive before shader code")

    directives = list(_DIRECTIVE.finditer(code))
    if any(directive[1] not in {"version", "define", "undef", "if", "ifdef",
                               "ifndef", "elif", "else", "endif"} or
           "gl_FragColor" in _IDENTIFIER.findall(directive[0])
           for directive in directives):
        raise ValueError("unsupported GLSL preprocessor directive")
    if sum(directive[1] == "version" for directive in directives) != 1:
        raise ValueError("expected one GLSL version directive")

    tokens = list(_IDENTIFIER.finditer(code))
    identifiers = {token[0] for token in tokens}
    unsupported = identifiers & {"varying", "attribute", "gl_FragData", "gl_TexCoord", LEGACY_OUTPUT}
    if unsupported:
        raise ValueError("unsupported legacy GLSL identifiers: " + ", ".join(sorted(unsupported)))

    outputs = [token for token in tokens if token[0] == "gl_FragColor"]
    if not outputs:
        raise ValueError("legacy fragment does not use gl_FragColor")

    end_of_version = text.find("\n", version.end(1))
    if end_of_version < 0:
        raise ValueError("expected a fragment body after the version directive")
    after_version = code[end_of_version + 1:]
    body_start = len(code) - len(after_version.lstrip())
    newline = "\r\n" if text[end_of_version - 1] == "\r" else "\n"
    original_line = text.count("\n", 0, body_start) + 1
    declaration = f"out vec4 {LEGACY_OUTPUT};{newline}#line {original_line}{newline}"
    parts = [text[:version.start(1)], "330", text[version.end(1):body_start], declaration]
    cursor = body_start
    for token in outputs:
        parts.extend((text[cursor:token.start()], LEGACY_OUTPUT))
        cursor = token.end()
    parts.append(text[cursor:])
    return Conversion(
        "".join(parts).encode("utf-8"),
        LEGACY_VERSION,
        "GLSL 120 version token promoted to 330 and gl_FragColor mapped to a declared vec4 output",
    )
