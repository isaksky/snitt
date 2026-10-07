#!/usr/bin/env python3
"""Compare shared editor commands through C++ and Jolt; run retained media guards."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib


ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "tests/parity-fixtures.json"
FIELDS = ("id", "step", "op", "result", "has_image", "width", "height",
          "undo", "redo", "tool", "selected_region", "arranging", "saved")


def run(command, *, cwd=ROOT, env=None, capture=False):
    completed = subprocess.run(command, cwd=cwd, env=env, text=True,
                               stdout=subprocess.PIPE if capture else None,
                               stderr=subprocess.STDOUT if capture else None)
    if completed.returncode:
        if capture:
            print(completed.stdout, file=sys.stderr)
        raise RuntimeError(f"{' '.join(map(str, command))} exited {completed.returncode}")
    return completed.stdout if capture else ""


def make_png(path, width, height):
    def chunk(tag, payload):
        return struct.pack(">I", len(payload)) + tag + payload + struct.pack(">I", zlib.crc32(tag + payload))

    raw = bytearray()
    for y in range(height):
        raw.append(0)
        for x in range(width):
            raw.extend(((x * 3 + y * 7) % 256, (x * 11 + y * 5) % 256,
                        (x * 13 + y * 17) % 256, 255))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(bytes(raw))) + chunk(b"IEND", b""))


def png_pixels(path):
    """Decode the 8-bit RGB/RGBA PNGs produced by Qt, without extra packages."""
    data = path.read_bytes()
    if not data.startswith(b"\x89PNG\r\n\x1a\n"):
        raise ValueError(f"Not a PNG: {path}")
    offset, compressed, dimensions = 8, bytearray(), None
    while offset < len(data):
        length = struct.unpack_from(">I", data, offset)[0]
        tag = data[offset + 4:offset + 8]
        payload = data[offset + 8:offset + 8 + length]
        if zlib.crc32(tag + payload) != struct.unpack_from(">I", data, offset + 8 + length)[0]:
            raise ValueError(f"PNG CRC mismatch: {path}")
        if tag == b"IHDR":
            width, height, depth, color, compression, filtering, interlace = struct.unpack(">IIBBBBB", payload)
            if (depth, color, compression, filtering, interlace) not in ((8, 2, 0, 0, 0), (8, 6, 0, 0, 0)):
                raise ValueError(f"Unsupported PNG format in {path}")
            dimensions = width, height, 3 if color == 2 else 4
        elif tag == b"IDAT":
            compressed.extend(payload)
        offset += 12 + length
    if dimensions is None:
        raise ValueError(f"Missing PNG header: {path}")
    width, height, channels = dimensions
    stream = zlib.decompress(compressed)
    stride = width * channels
    previous = bytearray(stride)
    pixels = bytearray()
    position = 0
    for _ in range(height):
        filter_type = stream[position]
        position += 1
        row = bytearray(stream[position:position + stride])
        position += stride
        for index in range(stride):
            left = row[index - channels] if index >= channels else 0
            above = previous[index]
            upper_left = previous[index - channels] if index >= channels else 0
            if filter_type == 1:
                predictor = left
            elif filter_type == 2:
                predictor = above
            elif filter_type == 3:
                predictor = (left + above) // 2
            elif filter_type == 4:
                base = left + above - upper_left
                distances = (abs(base - left), abs(base - above), abs(base - upper_left))
                predictor = (left, above, upper_left)[distances.index(min(distances))]
            elif filter_type == 0:
                predictor = 0
            else:
                raise ValueError(f"Unsupported PNG filter {filter_type}: {path}")
            row[index] = (row[index] + predictor) & 255
        for index in range(0, stride, channels):
            pixels.extend(row[index:index + 3])
            pixels.append(row[index + 3] if channels == 4 else 255)
        previous = row
    return width, height, hashlib.sha256(pixels).hexdigest()


def read_trace(path, root):
    rows = []
    for line in path.read_text().splitlines():
        values = line.split("\t")
        if len(values) == len(FIELDS) - 1:
            values.append("")
        if len(values) != len(FIELDS):
            raise ValueError(f"Malformed trace row: {line}")
        row = dict(zip(FIELDS, values))
        saved = row.pop("saved")
        row["pixels"] = None
        if saved:
            image = Path(saved).resolve()
            if not image.is_relative_to(root.resolve()) or image.suffix.lower() != ".png":
                raise ValueError(f"Saved file escaped isolated root: {image}")
            row["pixels"] = png_pixels(image)
        rows.append(row)
    return rows


def compare(label, actual, expected):
    differences = []
    for index in range(max(len(actual), len(expected))):
        left = actual[index] if index < len(actual) else None
        right = expected[index] if index < len(expected) else None
        if left != right:
            keys = sorted(set(left or {}) | set(right or {}))
            for key in keys:
                if (left or {}).get(key) != (right or {}).get(key):
                    differences.append(f"{label} step {index} {key}: actual={(left or {}).get(key)!r} expected={(right or {}).get(key)!r}")
    return differences


def build():
    run([str(ROOT / "jolt-snitt/build-native")])
    qmake = Path((ROOT / "glimmer-qt/build/qmake-path").read_text().strip())
    for suite in ("editor_tests", "recording_tests"):
        directory = ROOT / "build/tests" / ("" if suite == "editor_tests" else suite)
        directory.mkdir(parents=True, exist_ok=True)
        run([str(qmake), str(ROOT / "tests" / f"{suite}.pro")], cwd=directory)
        run(["make", "-j", "4"], cwd=directory)
    return qmake


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture", type=Path, default=FIXTURE, help="Shared command fixture JSON")
    parser.add_argument("--skip-build", action="store_true", help="Use current local binaries")
    parser.add_argument("--skip-recording", action="store_true", help="Run only editor parity")
    parser.add_argument("--show-pixels", action="store_true", help="Print saved pixel digests for fixture review")
    args = parser.parse_args()
    fixture = json.loads(args.fixture.read_text())
    if fixture.get("version") != 1:
        raise ValueError("Unsupported parity fixture version")
    qmake = (Path((ROOT / "glimmer-qt/build/qmake-path").read_text().strip())
             if args.skip_build else build())
    with tempfile.TemporaryDirectory(prefix="snitt-parity-") as temporary:
        base = Path(temporary)
        commands = base / "commands.tsv"
        commands.write_text("\n".join("\t".join(map(str, [item["id"], item["op"], *item.get("args", [])]))
                                      for item in fixture["commands"]) + "\n")
        traces = {}
        for implementation, command, cwd in (
            ("cpp", [str(ROOT / "build/tests/editor_tests"), "parityFixture", "-platform", "offscreen"], ROOT),
            ("jolt", [os.environ.get("JOLT", "jolt"), "-A:test", "-m", "snitt.parity-test"], ROOT / "jolt-snitt"),
        ):
            isolated = base / implementation
            isolated.mkdir()
            for name, dimensions in fixture["images"].items():
                make_png(isolated / name, *dimensions)
            for name in ("config", "data", "tmp"):
                (isolated / name).mkdir()
            trace = isolated / "trace.tsv"
            env = os.environ.copy()
            env.update({"SNITT_PARITY_COMMANDS": str(commands), "SNITT_PARITY_TRACE": str(trace),
                        "SNITT_PARITY_ROOT": str(isolated), "SNITT_JOLT_DATA_DIR": str(isolated),
                        "SNITT_PARITY_APP_ID": "snitt-jolt-parity-" + hashlib.sha256(str(base).encode()).hexdigest()[:12],
                        "XDG_CONFIG_HOME": str(isolated / "config"), "XDG_DATA_HOME": str(isolated / "data"),
                        "TMPDIR": str(isolated / "tmp"), "QT_QPA_PLATFORM": "offscreen",
                        "QT_PLUGIN_PATH": run([str(qmake), "-query", "QT_INSTALL_PLUGINS"], capture=True).strip(),
                        "QML_IMPORT_PATH": run([str(qmake), "-query", "QT_INSTALL_QML"], capture=True).strip(),
                        "JOLT_AOT_CACHE": "0"})
            run(command, cwd=cwd, env=env, capture=True)
            traces[implementation] = read_trace(trace, isolated)
        errors = compare("Jolt/C++", traces["jolt"], traces["cpp"])
        if len(traces["cpp"]) != len(fixture["commands"]):
            errors.append(f"fixture has {len(fixture['commands'])} commands but C++ emitted {len(traces['cpp'])} states")
        if args.show_pixels:
            for index, row in enumerate(traces["cpp"]):
                if row["pixels"]:
                    print(f"step {index} saved pixels: {row['pixels']}")
        for index, item in enumerate(fixture["commands"]):
            row = traces["cpp"][index] if index < len(traces["cpp"]) else {}
            for key, expected in (("id", item["id"]), ("step", str(index)), ("op", item["op"]),
                                  ("selected_region", "-1"), ("arranging", "false")):
                if row.get(key) != expected:
                    errors.append(f"fixture step {index} {key}: actual={row.get(key)!r} expected={expected!r}")
            for key, value in item.get("expect", {}).items():
                actual = row.get({"saved": "pixels"}.get(key, key))
                if key == "saved":
                    actual = actual is not None
                elif key == "pixels":
                    actual = actual[2] if actual else None
                elif isinstance(value, bool):
                    actual = actual == "true"
                elif isinstance(value, int):
                    actual = int(actual) if actual is not None else None
                if actual != value:
                    errors.append(f"fixture step {index} {key}: actual={actual!r} expected={value!r}")
        if errors:
            print("\n".join(errors), file=sys.stderr)
            return 1
        if not args.skip_recording:
            recording = ROOT / "build/tests/recording_tests/recording_tests"
            recording_env = env.copy()
            # A retained test binary may use a different local Qt SDK than the
            # Jolt bridge. Let that binary resolve its own matching plugins.
            recording_env.pop("QT_PLUGIN_PATH", None)
            recording_env.pop("QML_IMPORT_PATH", None)
            recording_tests = fixture["retained_recording_tests"] + (
                fixture.get("mac_recording_tests", []) if sys.platform == "darwin" else [])
            for test in recording_tests:
                output = run([str(recording), test, "-platform", "offscreen"], env=recording_env, capture=True)
                if f"PASS   : RecordingTests::{test}()" not in output:
                    raise RuntimeError(f"Retained recording test did not pass: {test}\n{output}")
        print(f"PASS: {len(traces['cpp'])} C++/Jolt command states and pixel outputs match")
        if not args.skip_recording:
            print(f"PASS: {len(recording_tests)} retained recording cancellation guards")
        return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError) as error:
        print(f"parity: {error}", file=sys.stderr)
        sys.exit(1)
