"""Pixel-exact comparisons of the visible annotation preview, not exported images."""
import pathlib
import struct
import sys
import zlib

from image_pixels import chunk, pixels


def generate(directory):
    # Single-pixel detail makes unwanted enlargement/interpolation immediately visible.
    w, h = 401, 301
    raw = b"".join(b"\0" + bytes(v for x in range(w)
                                  for v in ((255 if (x + y) % 2 else 0), (x * 17) % 256, (y * 29) % 256))
                   for y in range(h))
    (directory / "source.png").write_bytes(
        b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))
    (directory / "large.png").write_bytes(
        b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 1200, 700, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress((b"\0" + b"\xff\xff\xff" * 1200) * 700)) + chunk(b"IEND", b""))


def verify(directory):
    source, sc = pixels(directory / "source.png")
    sw, sh = len(source[0]) // sc, len(source)
    for name in ("original", "vertical", "both", "undo"):
        actual, ac = pixels(directory / f"{name}.png")
        w, h = len(actual[0]) // ac, len(actual)
        x, y, iw, ih = map(int, (directory / f"{name}.tsv").read_text().split())
        xs = list(range(sw)) if name == "original" else list(range(101)) + list(range(119, sw))
        ys = list(range(79)) + list(range(91, sh)) if name == "both" else list(range(sh))
        assert (iw, ih) == (len(xs), len(ys)), (name, iw, ih)
        # Qt rounds the canvas origin and its texture separately at fractional
        # DPR. Allow one pixel of placement rounding, never any pixel-color error.
        origins = [(x + ox, y + oy) for oy in (0, -1, 1) for ox in (0, -1, 1)]
        candidates = [(px, py) for px, py in origins
                      if 0 <= px <= w - iw and 0 <= py <= h - ih
                      and all(actual[py + dy][(px + dx) * ac:(px + dx) * ac + 3]
                              == source[ys[dy]][xs[dx] * sc:xs[dx] * sc + 3]
                              for dx, dy in ((0, 0), (1, 0), (0, 1)))]
        assert candidates, (name, "Preview pixels were resampled at the image origin")
        x, y = candidates[0]
        assert 0 <= x <= w - iw and 0 <= y <= h - ih
        for dy, sy in enumerate(ys):
            for dx, sx in enumerate(xs):
                expected = source[sy][sx * sc:sx * sc + 3]
                offset = (x + dx) * ac
                found = actual[y + dy][offset:offset + 3]
                assert found == expected, (name, dx, dy, expected, found)
    print("PASS: all visible source pixels are exact before cuts, after cuts, and after undo")


if __name__ == "__main__":
    globals()[sys.argv[1]](pathlib.Path(sys.argv[2]))
