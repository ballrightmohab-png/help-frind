#!/usr/bin/env python3
"""Generates assets/icon.png (a small, dependency-free PNG writer).

The icon is intentionally simple: a dark rounded panel, a rising bar chart and
an upward arrow in LeviBoost green.  Run it only when the icon needs to change:

    python3 scripts/make_icon.py
"""

import struct
import zlib
from pathlib import Path

SIZE = 256
BACKGROUND = (16, 20, 26, 255)
PANEL = (24, 30, 38, 255)
GREEN = (102, 224, 122, 255)
GREEN_DARK = (56, 156, 82, 255)
WHITE = (240, 246, 240, 255)


def blend(base, top, alpha):
    return tuple(
        int(top[i] * alpha + base[i] * (1.0 - alpha)) for i in range(4)
    )


def rounded_mask(x, y, radius, size=SIZE):
    """Anti-aliased rounded-rectangle coverage in [0, 1]."""
    cx = min(max(x, radius), size - 1 - radius)
    cy = min(max(y, radius), size - 1 - radius)
    distance = ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5
    return max(0.0, min(1.0, radius - distance + 0.5))


def build_pixels():
    pixels = []
    for y in range(SIZE):
        row = []
        for x in range(SIZE):
            color = (0, 0, 0, 0)
            coverage = rounded_mask(x, y, 44)
            if coverage > 0.0:
                color = blend(color, BACKGROUND, coverage)
                # inner panel
                inner = rounded_mask(x - 10, y - 10, 34, SIZE - 20)
                if 10 <= x < SIZE - 10 and 10 <= y < SIZE - 10:
                    color = blend(color, PANEL, coverage * inner)

            # rising bars
            for index, (left, top) in enumerate(
                ((52, 168), (96, 138), (140, 108), (184, 76))
            ):
                width = 34
                if left <= x < left + width and top <= y < 204:
                    shade = GREEN_DARK if index < 2 else GREEN
                    color = blend(color, shade, 1.0)

            # upward arrow: a chevron over a short stem, drawn on the right
            centre = 168
            if 120 <= y < 168:
                half = (y - 120) * 0.85
                if abs(x - centre) <= half:
                    color = blend(color, WHITE, 0.95)
            elif 168 <= y < 216 and abs(x - centre) <= 11:
                color = blend(color, WHITE, 0.95)

            row.append(color)
        pixels.append(row)
    return pixels


def write_png(path: Path, pixels):
    raw = bytearray()
    for row in pixels:
        raw.append(0)  # filter type 0
        for pixel in row:
            raw.extend(bytes(pixel))

    def chunk(tag: bytes, payload: bytes) -> bytes:
        return (
            struct.pack(">I", len(payload))
            + tag
            + payload
            + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)
        )

    header = struct.pack(">IIBBBBB", SIZE, SIZE, 8, 6, 0, 0, 0)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", header)
        + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + chunk(b"IEND", b"")
    )


def main() -> None:
    path = Path(__file__).resolve().parent.parent / "assets/icon.png"
    write_png(path, build_pixels())
    print(f"wrote {path} ({path.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
