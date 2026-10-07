#!/usr/bin/env python3
"""Alpha fixtures with distinct secrets but identical privacy-block averages."""
from pathlib import Path
import struct
import sys
import zlib
from image_pixels import chunk, pixels


def generate(directory):
    width, height = 96, 80
    for name, variant in (("source.png", 0), ("secret.png", 1)):
        raw = bytearray()
        for y in range(height):
            raw.append(0)
            for x in range(width):
                # Swap pairs inside a single 16px cell; leave erase's sample
                # (0, 0) unchanged. The average premultiplied pixel is identical.
                sx = x ^ 1 if variant and 32 <= x < 48 and 32 <= y < 48 else x
                raw.extend((sx * 2, y * 3, (sx + y) % 256, 80 + (sx + y) % 176))
        (directory / name).write_bytes(
            b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))

    (directory / "large.png").write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", 2048, 2048, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress((b"\0" + b"\xff\xff\xff\xff" * 2048) * 2048))
        + chunk(b"IEND", b""))


def compare(directory):
    files = list(directory.glob("migrated-*.png"))
    assert len(files) >= 170, len(files)
    for migrated in files:
        reference = directory / migrated.name.replace("migrated-", "reference-", 1)
        assert pixels(migrated) == pixels(reference), migrated.name
    print(f"PASS: {len(files)} exact RGBA export/preview comparisons, including privacy independence")


if __name__ == "__main__":
    {"generate": generate, "compare": compare}[sys.argv[1]](Path(sys.argv[2]))
