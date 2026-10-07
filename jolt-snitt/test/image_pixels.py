#!/usr/bin/env python3
"""Generate a deterministic image and check pixels exported by the Jolt UI."""
import struct
import sys
import zlib

SIZE = 800


def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)


def generate(path):
    row = bytes([0]) + bytes([255, 255, 255]) * SIZE
    pixels = row * SIZE
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", SIZE, SIZE, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(pixels))
           + chunk(b"IEND", b""))
    with open(path, "wb") as output:
        output.write(png)


def pixels(path):
    with open(path, "rb") as source:
        data = source.read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    position = 8
    compressed = bytearray()
    while position < len(data):
        length = struct.unpack_from(">I", data, position)[0]
        kind = data[position + 4:position + 8]
        payload = data[position + 8:position + 8 + length]
        position += 12 + length
        if kind == b"IHDR":
            width, height, depth, color, _, _, _ = struct.unpack(">IIBBBBB", payload)
            assert (width, height, depth) == (SIZE, SIZE, 8)
            assert color in (2, 6)
            channels = 3 if color == 2 else 4
        elif kind == b"IDAT":
            compressed.extend(payload)
        elif kind == b"IEND":
            break
    raw = zlib.decompress(compressed)
    stride = width * channels
    rows = []
    offset = 0
    previous = bytearray(stride)
    for _ in range(height):
        filter_type = raw[offset]
        offset += 1
        row = bytearray(raw[offset:offset + stride])
        offset += stride
        for index in range(stride):
            left = row[index - channels] if index >= channels else 0
            above = previous[index]
            upper_left = previous[index - channels] if index >= channels else 0
            if filter_type == 1:
                prediction = left
            elif filter_type == 2:
                prediction = above
            elif filter_type == 3:
                prediction = (left + above) // 2
            elif filter_type == 4:
                base = left + above - upper_left
                distances = (abs(base - left), abs(base - above), abs(base - upper_left))
                prediction = (left, above, upper_left)[distances.index(min(distances))]
            else:
                assert filter_type == 0, filter_type
                prediction = 0
            row[index] = (row[index] + prediction) & 255
        rows.append(row)
        previous = row
    return rows, channels


def verify(path):
    rows, channels = pixels(path)

    def rgb(x, y):
        start = x * channels
        return tuple(rows[y][start:start + 3])

    assert rgb(100, 100) == (255, 255, 255), ("outside", rgb(100, 100))
    assert rgb(300, 300) == (255, 255, 255), ("inside", rgb(300, 300))
    assert rgb(300, 200) == (239, 68, 68), ("top edge", rgb(300, 200))
    assert rgb(200, 300) == (239, 68, 68), ("left edge", rgb(200, 300))
    print("PASS: exported PNG has expected rectangle pixels and untouched image areas")


if __name__ == "__main__":
    mode, path, *extra = sys.argv[1:]
    if mode == "generate":
        generate(path)
    elif mode == "verify":
        verify(path)
    elif mode == "compare":
        assert pixels(path) == pixels(extra[0]), "Copy and save pixels differ"
        print("PASS: clipboard and saved image pixels match")
    else:
        raise SystemExit("Usage: image_pixels.py generate|verify PATH")
