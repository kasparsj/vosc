#!/usr/bin/env python3
"""Synthetic stdio server for the vosc MCP compatibility spike.

This is deliberately not the milestone 3 engine adapter. It proves the selected
SDK's subprocess transport, schemas, mixed image/metadata result, and lifecycle.
"""

from __future__ import annotations

import base64
import binascii
import struct
import zlib
from typing import Annotated

from mcp.server import MCPServer
from mcp.types import CallToolResult, ImageContent, TextContent, ToolAnnotations
from pydantic import BaseModel, Field


FIXTURE_WIDTH = 48
FIXTURE_HEIGHT = 32
FIXTURE_PROVENANCE = "synthetic_stdlib_png_v1"


class ProbeReceipt(BaseModel):
    """Machine-readable metadata returned beside the native image block."""

    request_id: str
    status: str
    width: int
    height: int
    provenance: str


def _chunk(kind: bytes, payload: bytes) -> bytes:
    checksum = binascii.crc32(kind + payload) & 0xFFFFFFFF
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", checksum)


def _fixture_png() -> bytes:
    """Build a deterministic, synthetic RGB test card with no image dependency."""

    rows: list[bytes] = []
    for y in range(FIXTURE_HEIGHT):
        row = bytearray([0])  # PNG filter type: None.
        for x in range(FIXTURE_WIDTH):
            if x < 5 or y < 4:
                pixel = (18, 26, 48)
            elif 7 <= x < 22 and 8 <= y < 25:
                pixel = (238, 71, 93)
            elif (x - 35) ** 2 + (y - 11) ** 2 <= 42:
                pixel = (61, 214, 198)
            elif x > 28 and y > 19 and (x + 2 * y) % 7 < 3:
                pixel = (250, 205, 72)
            else:
                pixel = (82, 67, 142)
            row.extend(pixel)
        rows.append(bytes(row))

    header = struct.pack(">IIBBBBB", FIXTURE_WIDTH, FIXTURE_HEIGHT, 8, 2, 0, 0, 0)
    scanlines = b"".join(rows)
    return (
        b"\x89PNG\r\n\x1a\n"
        + _chunk(b"IHDR", header)
        + _chunk(b"IDAT", zlib.compress(scanlines, level=9))
        + _chunk(b"IEND", b"")
    )


FIXTURE_PNG = _fixture_png()

mcp = MCPServer(
    "vosc-mcp-compatibility-spike",
    instructions=(
        "Compatibility-only synthetic image probe. This server does not connect "
        "to, launch, stop, or mutate the vosc renderer."
    ),
)


@mcp.tool(
    annotations=ToolAnnotations(
        readOnlyHint=True,
        destructiveHint=False,
        idempotentHint=True,
        openWorldHint=False,
    )
)
async def compatibility_probe(
    request_id: Annotated[
        str,
        Field(strict=True, min_length=1, max_length=64, pattern=r"^[A-Za-z0-9._:-]+$"),
    ],
) -> Annotated[CallToolResult, ProbeReceipt]:
    """Return a deterministic synthetic PNG plus a structured compatibility receipt."""

    receipt = ProbeReceipt(
        request_id=request_id,
        status="synthetic_fixture",
        width=FIXTURE_WIDTH,
        height=FIXTURE_HEIGHT,
        provenance=FIXTURE_PROVENANCE,
    )
    return CallToolResult(
        content=[
            ImageContent(
                type="image",
                data=base64.b64encode(FIXTURE_PNG).decode("ascii"),
                mime_type="image/png",
            ),
            TextContent(
                type="text",
                text=f"Synthetic compatibility receipt for request {request_id}.",
            ),
        ],
        structured_content=receipt.model_dump(),
    )


def main() -> None:
    mcp.run(transport="stdio")


if __name__ == "__main__":
    main()
