#!/usr/bin/env python3
"""Write three deterministic, uneven RGB capture fixtures."""
import pathlib
import struct
import sys
import zlib


def chunk(kind, data):
    return (struct.pack(">I", len(data)) + kind + data
            + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff))


def image(path, width, height, color):
    row = b"\0" + bytes(color) * width
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(row * height))
           + chunk(b"IEND", b""))
    path.write_bytes(png)


root = pathlib.Path(sys.argv[1])
for index, (width, height, color) in enumerate([
        (80, 60, (255, 0, 0)),
        (32, 90, (0, 255, 0)),
        (50, 20, (0, 0, 255))]):
    image(root / f"region-{index}.png", width, height, color)
    image(root / f"wide-{index}.png", 11000, 1, color)
