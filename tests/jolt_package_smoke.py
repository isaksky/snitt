#!/usr/bin/env python3
"""Check the relocated production bundle and measure startup resources.

On Windows, run this in an unlocked, signed-in desktop session."""

import argparse
import os
from pathlib import Path
import shutil
import statistics
import subprocess
import sys
import tempfile
import time
import zipfile


ROOT = Path(__file__).resolve().parents[1]
WINDOWS = sys.platform == "win32"
ARCHIVE = ROOT / ("build/release/snitt_0.2.0_windows_amd64.zip" if WINDOWS
                  else "build/release/snitt_0.2.0_macos_arm64.zip")


def system_environment():
    """Only the operating system's own directories, so the bundle must be self-contained."""
    if not WINDOWS:
        return {"PATH": "/usr/bin:/bin:/usr/sbin:/sbin", "TMPDIR": "/tmp"}
    system = os.environ["SystemRoot"]
    names = ("SystemRoot", "SystemDrive", "WINDIR", "TEMP", "TMP", "USERPROFILE",
             "LOCALAPPDATA", "APPDATA", "USERNAME", "COMPUTERNAME")
    result = {name: os.environ[name] for name in names if name in os.environ}
    result["PATH"] = os.pathsep.join([str(Path(system) / "System32"), system])
    return result


def resident_kib(pid):
    if not WINDOWS:
        return int(subprocess.check_output(["ps", "-o", "rss=", "-p", str(pid)], text=True).strip())
    # The launcher waits on the Jolt runtime; measure that child.
    script = (f"(Get-CimInstance Win32_Process -Filter 'ParentProcessId={pid}' | "
              "ForEach-Object { (Get-Process -Id $_.ProcessId).WorkingSet64 } | Measure-Object -Sum).Sum")
    return int(subprocess.check_output(["powershell", "-NoProfile", "-Command", script],
                                       text=True).strip()) // 1024


def runtime_children(pid):
    script = f"@(Get-CimInstance Win32_Process -Filter 'ParentProcessId={pid}').Count"
    return int(subprocess.check_output(["powershell", "-NoProfile", "-Command", script], text=True).strip())


def environment(home, jolt=False):
    result = system_environment() if jolt else os.environ.copy()
    result.update({"HOME": str(home), "QT_QPA_PLATFORM": "windows" if WINDOWS else "cocoa"})
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


def launcher(bundle):
    return bundle / "snitt.exe" if WINDOWS else bundle / "Contents/MacOS/snitt"


def exercise(bundle, home, jolt=False, through_path=False):
    executable = launcher(bundle)
    env = environment(home, jolt)
    if through_path:
        if WINDOWS:
            # Scoop shims and PATH entries name the launcher, never the runtime.
            # CreateProcess searches this process's PATH, so resolve the name here.
            env["PATH"] = str(bundle) + os.pathsep + env["PATH"]
            executable = shutil.which("snitt", path=env["PATH"])
        else:
            cli = home / "bin"
            cli.mkdir()
            (cli / "snitt").symlink_to(executable)
            env["PATH"] = str(cli) + os.pathsep + env["PATH"]
            executable = "snitt"
        subprocess.run([executable, "--quit"], cwd=home, env=env, check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=5)
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
            rss = resident_kib(owner.pid)
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


def launcher_owns_runtime(bundle, home):
    """Ending the Windows launcher must also end its Jolt runtime."""
    env = environment(home, True)
    owner = subprocess.Popen([str(launcher(bundle)), "--background"], cwd=home, env=env,
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    deadline = time.monotonic() + 20
    while not (home / "settings.ini").is_file() and time.monotonic() < deadline:
        time.sleep(0.05)
    if runtime_children(owner.pid) != 1:
        owner.kill()
        raise RuntimeError("Launcher did not start exactly one Jolt runtime")
    runtime = subprocess.check_output(["powershell", "-NoProfile", "-Command",
        f"(Get-CimInstance Win32_Process -Filter 'ParentProcessId={owner.pid}').ProcessId"], text=True).strip()
    owner.kill()
    owner.wait(timeout=5)
    for _ in range(50):
        alive = subprocess.run(["powershell", "-NoProfile", "-Command", f"Get-Process -Id {runtime}"],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
        if not alive:
            return
        time.sleep(0.1)
    raise RuntimeError("Jolt runtime outlived its launcher")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, default=ARCHIVE)
    parser.add_argument("--legacy-bundle", type=Path,
                        help="Optional pre-cutover C++ bundle for a local comparison")
    args = parser.parse_args()
    if sys.platform not in ("darwin", "win32"):
        raise RuntimeError("macOS or Windows is required")
    if not args.archive.is_file():
        raise RuntimeError(f"Package the app first: {args.archive}")
    with tempfile.TemporaryDirectory(prefix="snitt-package-smoke-") as directory:
        root = Path(directory)
        if WINDOWS:
            relocated = root / "relocated"
            with zipfile.ZipFile(args.archive) as archive:
                archive.extractall(relocated)
        else:
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
        subprocess.run([str(launcher(relocated)), "--smoke-test"],
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
                runs.append(exercise(bundle, home, jolt, through_path=jolt and index == 1))
            print(f"{label}: ready median {statistics.median(t for t, _ in runs):.3f}s; "
                  f"RSS median {statistics.median(r for _, r in runs) / 1024:.1f} MiB")
        if WINDOWS:
            ended = root / "launcher-ended"
            ended.mkdir()
            launcher_owns_runtime(relocated, ended)
        print("PASS: relocated Jolt smoke, direct/PATH launch, forwarding, show, quit and repeated teardown")


if __name__ == "__main__":
    main()
