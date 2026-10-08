#!/usr/bin/env python3
"""Compare complete editor renders against an immutable C++/QML reference.

This complements tests/parity.py (document commands/exported pixels). It does not
replace the live desktop workflows and final package inspection in docs/ui-parity.md.
Run with the same Qt SDK for both implementations. Pillow supplies difference
images. Only Qt compositing rounding is allowed: 1/255, or 2/255 for selectors.
"""
import argparse
import json
import os
from pathlib import Path
import runpy
import subprocess

from parity import png_pixels

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / "build/ui-parity"
STATES = ("empty-wide", "empty-small", "populated-wide", "populated-small",
          "populated-large", "tool-hover", "tool-pressed", "copy-hover", "copy-pressed", "ink-focus", "text-draft", "text-committed", "arrangement-wide", "arrangement-small",
          "arrangement-annotated", "dialog-rearrange", "dialog-reveal", "dialog-capture-permission", "dialog-capture-error", "review-empty",
          "review-loaded", "review-trimmed", "review-small", "review-exporting", "review-error",
          "recording-starting", "recording-running", "recording-finishing", "recording-ready",
          "selector-single", "selector-multiple", "selector-small", "selector-video", "selector-drag", "selector-fractional")


def selector_geometry(evidence, name):
    """Rendering tolerance never substitutes for exact control geometry."""
    reference = json.loads((evidence / "reference" / f"{name}.json").read_text())
    actual = {}
    for line in (evidence / "jolt" / f"{name}.tsv").read_text().splitlines():
        control, prop, value = line.split("\t")
        actual.setdefault(control, {})[prop] = value
    panel = reference["captureToolbar"]
    for control, props in actual.items():
        if props["visible"] == "false":
            continue
        expected = reference[control.replace("Button", "CaptureButton")]
        for prop in ("x", "y", "width", "height"):
            offset = panel[prop] if prop in ("x", "y") else 0
            if float(props[prop]) + offset != expected[prop]:
                return False
    return True


def selector_rounding(expected, actual):
    """Allow only the extra rounding of native artwork composited by Quick.

    No spatial tolerance, blurred comparison, or excluded pixels. Geometry is
    checked independently, and alpha must match exactly.
    """
    from PIL import ImageChops
    if expected.size != actual.size or expected.getchannel("A").tobytes() != actual.getchannel("A").tobytes():
        return False
    return max(high for low, high in ImageChops.difference(expected, actual).getextrema()) <= 2


def run(args, *, cwd=ROOT, env=None, log=None):
    if log:
        with log.open("w") as stream:
            result = subprocess.run(list(map(str, args)), cwd=cwd, env=env,
                                    stdout=stream, stderr=subprocess.STDOUT)
        if result.returncode:
            raise RuntimeError(f"Command failed ({result.returncode}); see {log}")
    else:
        subprocess.run(list(map(str, args)), cwd=cwd, env=env, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", default="4fd3106", help="Reference Git commit")
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--desktop", action="store_true", help="Use actual macOS windows instead of offscreen Qt")
    parser.add_argument("--scale", default="2", choices=("1", "2"))
    parser.add_argument("--output", type=Path, default=WORK / "evidence", help="Keep each DPI/platform evidence set separately")
    args = parser.parse_args()
    commit = subprocess.check_output(["git", "rev-parse", args.reference], cwd=ROOT, text=True).strip()
    WORK.mkdir(parents=True, exist_ok=True)
    reference = WORK / "reference-src"
    identity = WORK / "reference-commit.txt"
    if reference.exists():
        if not identity.exists() or identity.read_text().strip() != commit:
            raise RuntimeError("Reference archive identity differs; choose a separate audit directory")
    else:
        reference.mkdir()
        archive = WORK / "reference.tar"
        run(["git", "archive", "--output", archive, commit])
        run(["tar", "-xf", archive, "-C", reference])
        archive.unlink()
        identity.write_text(commit + "\n")

    if not args.skip_build:
        run([ROOT / "jolt-snitt/build-native", "CONFIG+=property_tests"], log=WORK / "native-build.log")
    qmake = Path((ROOT / "glimmer-qt/build/qmake-path").read_text().strip())
    ref_build = WORK / "reference-ui"
    ref_build.mkdir(exist_ok=True)
    project = (reference / "tests/editor_tests.pro").read_text()
    project = (project.replace("../src", str(reference / "src"))
               .replace("editor_tests.cpp", str(ROOT / "tests/ui_reference.cpp"))
               .replace("TARGET = editor_tests", "TARGET = ui_reference"))
    (ref_build / "ui_reference.pro").write_text(project)
    if not args.skip_build:
        run([qmake, "ui_reference.pro"], cwd=ref_build, log=WORK / "reference-ui-qmake.log")
        run(["make", "-j4"], cwd=ref_build, log=WORK / "reference-ui-build.log")

    evidence = args.output.resolve()
    evidence.mkdir(exist_ok=True, parents=True)
    image = evidence / "source.png"
    runpy.run_path(str(ROOT / "jolt-snitt/test/image_pixels.py"))["generate"](image)
    env = dict(os.environ, QT_QPA_PLATFORM="cocoa" if args.desktop else "offscreen",
               QT_SCALE_FACTOR=args.scale, SNITT_UI_IMAGE=str(image),
               SNITT_JOLT_FAKE_HOTKEY="1", JOLT_AOT_CACHE="0",
               QT_PLUGIN_PATH=subprocess.check_output([qmake, "-query", "QT_INSTALL_PLUGINS"], text=True).strip(),
               QML_IMPORT_PATH=subprocess.check_output([qmake, "-query", "QT_INSTALL_QML"], text=True).strip())
    for kind, command, directory in (
        ("reference", [ref_build / "ui_reference"], ref_build),
        ("jolt", [os.environ.get("JOLT", "jolt"), "-A:test", "-m", "snitt.ui-parity-test"], ROOT / "jolt-snitt"),
    ):
        output = evidence / kind
        output.mkdir(exist_ok=True)
        # A failed run must not be mistaken for a previous successful snapshot.
        for name in STATES:
            (output / f"{name}.png").unlink(missing_ok=True)
        run(command, cwd=directory,
            env=dict(env, SNITT_UI_OUTPUT=str(output),
                     SNITT_LEGACY_TEST_DATA_DIR=str(WORK / "reference-data"),
                     SNITT_JOLT_DATA_DIR=str(WORK / "jolt-data")),
            log=WORK / f"{kind}-ui-snapshots.log")

    failures = []
    comparisons = {}
    for name in STATES:
        expected, actual = (evidence / kind / f"{name}.png" for kind in ("reference", "jolt"))
        try:
            from PIL import Image, ImageChops
            with Image.open(expected) as e, Image.open(actual) as a:
                matches = e.size == a.size and e.convert("RGBA").tobytes() == a.convert("RGBA").tobytes()
                exact = matches
                rounding = (not exact and e.size == a.size
                            and max(high for low, high in ImageChops.difference(e.convert("RGBA"), a.convert("RGBA")).getextrema()) <= 1)
                if rounding:
                    matches = True
                if name.startswith("selector-"):
                    matches = selector_geometry(evidence, name) and selector_rounding(e.convert("RGBA"), a.convert("RGBA"))
        except ImportError:
            matches = png_pixels(expected) == png_pixels(actual)
            exact = matches
            rounding = False
        comparison = ("pixel/geometry mismatch" if not matches else "exact RGBA" if exact
                      else "RGBA rounding ≤1/255" if rounding else "RGBA rounding ≤2/255 + exact geometry")
        comparisons[name] = comparison
        print(f"{'PASS' if matches else 'FAIL'} {name}: {comparison}")
        if not matches:
            failures.append(name)
        if not exact:
            try:
                from PIL import Image, ImageChops
                ImageChops.difference(Image.open(expected).convert("RGB"),
                                      Image.open(actual).convert("RGB")).save(evidence / f"diff-{name}.png")
            except ImportError:
                pass
    result = (
        f"reference={commit}\nplatform={env['QT_QPA_PLATFORM']}\nscale={args.scale}\n"
        + "\n".join(f"{name}={'FAIL' if name in failures else 'PASS'} ({comparisons[name]})" for name in STATES) + "\n")
    (WORK / "editor-render-result.txt").write_text(result)
    (evidence / "render-result.txt").write_text(result)
    if failures:
        raise RuntimeError(f"UI render differences: {', '.join(failures)}; inspect {evidence}")


if __name__ == "__main__":
    main()
