#!/usr/bin/env python3
"""Check the relocated production bundle and measure startup resources."""

import argparse
import os
from pathlib import Path
import statistics
import subprocess
import sys
import tempfile
import time


ROOT = Path(__file__).resolve().parents[1]
ARCHIVE = ROOT / "build/release/snitt_0.2.0_macos_arm64.zip"


def environment(home, jolt=False):
    result = ({"PATH": "/usr/bin:/bin:/usr/sbin:/sbin"} if jolt else os.environ.copy())
    result.update({"HOME": str(home), "QT_QPA_PLATFORM": "cocoa"})
    if jolt:
        result["TMPDIR"] = "/tmp"
    if jolt:
        result["SNITT_JOLT_FAKE_HOTKEY"] = "1"
        result["SNITT_JOLT_IPC_ID"] = str(home)
        result["SNITT_JOLT_DATA_DIR"] = str(home)
    else:
        result["SNITT_LEGACY_TEST_IPC_ID"] = str(home)
        result["SNITT_LEGACY_TEST_DATA_DIR"] = str(home)
        qmake = Path((ROOT / "build/.snitt-qmake").read_text().strip())
        result["QT_PLUGIN_PATH"] = subprocess.check_output(
            [str(qmake), "-query", "QT_INSTALL_PLUGINS"], text=True).strip()
        result["QML_IMPORT_PATH"] = subprocess.check_output(
            [str(qmake), "-query", "QT_INSTALL_QML"], text=True).strip()
    return result


def exercise(bundle, home, jolt=False):
    executable = bundle / "Contents/MacOS/snitt"
    env = environment(home, jolt)
    log = home / "launch.log"
    with log.open("wb") as output:
        owner = subprocess.Popen([str(executable), "--background"], cwd=home,
                                 env=env, stdout=output, stderr=subprocess.STDOUT)
        started = time.monotonic()
        try:
            deadline = started + 20
            while time.monotonic() < deadline:
                if owner.poll() is not None:
                    raise RuntimeError(f"Owner exited early: {log.read_text(errors='replace')[-2000:]}")
                if (home / "settings.ini").is_file():
                    break
                time.sleep(0.02)
            else:
                raise RuntimeError(f"Settings never became ready: {log.read_text(errors='replace')[-2000:]}")
            ready = time.monotonic() - started
            subprocess.run([str(executable), "--background"], cwd=home,
                           env=env, check=True, stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL, timeout=5)
            subprocess.run([str(executable), "--show"], cwd=home, env=env,
                           check=True, stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL, timeout=5)
            time.sleep(0.4)
            rss = int(subprocess.check_output(["ps", "-o", "rss=", "-p", str(owner.pid)],
                                              text=True).strip())
            subprocess.run([str(executable), "--quit"], cwd=home, env=env,
                           check=True, stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL, timeout=5)
            if owner.wait(timeout=10) != 0:
                raise RuntimeError(f"Owner failed shutdown: {log.read_text(errors='replace')[-2000:]}")
            return ready, rss
        finally:
            if owner.poll() is None:
                owner.terminate()
                try:
                    owner.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    owner.kill()
                    owner.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, default=ARCHIVE)
    parser.add_argument("--legacy-bundle", type=Path,
                        help="Optional pre-cutover C++ bundle for a local comparison")
    args = parser.parse_args()
    if sys.platform != "darwin":
        raise RuntimeError("macOS is required")
    if not args.archive.is_file():
        raise RuntimeError(f"Package the app first: {args.archive}")
    with tempfile.TemporaryDirectory(prefix="snitt-package-smoke-") as directory:
        root = Path(directory)
        relocated = root / "relocated/Snitt.app"
        relocated.parent.mkdir()
        subprocess.run(["ditto", "-x", "-k", str(args.archive), str(relocated.parent)], check=True)
        image = root / "source.png"
        subprocess.run([sys.executable, str(ROOT / "jolt-snitt/test/image_pixels.py"),
                        "generate", str(image)], check=True)
        smoke_home = root / "smoke"
        smoke_home.mkdir()
        smoke_env = environment(smoke_home, True)
        smoke_env["SNITT_JOLT_DATA_DIR"] = str(smoke_home)
        smoke_env["SNITT_JOLT_IMAGE"] = str(image)
        subprocess.run([str(relocated / "Contents/MacOS/snitt"), "--smoke-test"],
                       cwd=smoke_home, env=smoke_env, check=True, timeout=30)
        saved = list((smoke_home / "pictures").rglob("*.png"))
        if len(saved) != 1 or saved[0].stat().st_size == 0:
            raise RuntimeError("Packaged Jolt app did not save its image in the Pictures root")
        candidates = [("jolt", relocated, True)]
        if args.legacy_bundle:
            candidates.insert(0, ("legacy", args.legacy_bundle.resolve(), False))
        for label, bundle, jolt in candidates:
            if not bundle.is_dir():
                raise RuntimeError(f"Bundle is missing: {bundle}")
            runs = []
            for index in range(2):
                home = root / f"{label}-{index}"
                home.mkdir()
                runs.append(exercise(bundle, home, jolt))
            print(f"{label}: ready median {statistics.median(t for t, _ in runs):.3f}s; "
                  f"RSS median {statistics.median(r for _, r in runs) / 1024:.1f} MiB")
        print("PASS: relocated Jolt smoke, forwarding, show, quit and repeated teardown")


if __name__ == "__main__":
    main()
