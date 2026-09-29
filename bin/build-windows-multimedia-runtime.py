#!/usr/bin/env python3
"""Build and validate an LGPL-only FFmpeg runtime for the MSYS2 Qt plugin.

Uses the existing UCRT64 SDK without modifying it. Run with that SDK's Python.
The staged DLLs, source archive, and build instructions are packaged together.
"""

import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tarfile
import urllib.request


ROOT = Path(__file__).resolve().parent.parent
VERSION = "9.0.2"
SOURCE_URL = f"https://ffmpeg.org/releases/ffmpeg-{VERSION}.tar.xz"
SOURCE_SHA256 = "8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e"
QT_VERSION = "6.11.2"
LICENSE = "LGPL version 2.1 or later"
LIBRARIES = {"avcodec": 63, "avformat": 63, "avutil": 61,
             "swresample": 7, "swscale": 10}
DLLS = {f"{name}-{major}.dll" for name, major in LIBRARIES.items()}
COMPILER_DLLS = {"libgcc_s_seh-1.dll", "libwinpthread-1.dll", "libstdc++-6.dll"}
FLAGS = [
    "--target-os=mingw32", "--arch=x86_64", "--cc=gcc", "--cxx=g++",
    "--enable-shared", "--disable-static", "--disable-programs", "--disable-doc",
    "--disable-debug", "--disable-autodetect", "--disable-network",
    "--disable-gpl", "--disable-version3", "--disable-nonfree",
    "--disable-everything", "--enable-avcodec", "--enable-avformat",
    "--enable-avutil", "--enable-swresample", "--enable-swscale",
    "--disable-avdevice", "--disable-avfilter", "--enable-protocol=file",
    "--enable-demuxer=mov", "--enable-parser=h264,hevc",
    "--enable-decoder=h264,hevc,mjpeg,aac",
    "--enable-bsf=h264_mp4toannexb,hevc_mp4toannexb",
    "--enable-d3d11va", "--enable-dxva2",
    "--enable-hwaccel=h264_d3d11va,h264_d3d11va2,h264_dxva2,hevc_d3d11va,hevc_d3d11va2,hevc_dxva2",
]


def digest(path):
    with Path(path).open("rb") as handle:
        return hashlib.file_digest(handle, "sha256").hexdigest()


def capture(*command):
    return subprocess.check_output(list(map(str, command)), text=True).strip()


def imports(binary, qt_bin):
    return set(re.findall(r"DLL Name:\s*(\S+)",
                          capture(qt_bin / "objdump.exe", "-p", binary)))


def check_codec_imports(names):
    codecs = {name.lower() for name in names
              if re.match(r"(?:lib)?(?:avcodec|avformat|avutil|swresample|swscale|avfilter|avdevice|postproc)[-\d.]",
                          name, re.I)}
    if codecs != DLLS:
        raise RuntimeError(f"Qt plugin FFmpeg ABI mismatch: expected {sorted(DLLS)}, found {sorted(codecs)}")


def check_configuration(configuration, license_text):
    if license_text != LICENSE:
        raise RuntimeError(f"FFmpeg did not report the required LGPL license: {license_text}")
    actual = shlex.split(configuration)
    if any(flag not in actual for flag in FLAGS):
        raise RuntimeError("FFmpeg configure flags differ from the approved build")
    if any(flag in actual for flag in ("--enable-gpl", "--enable-version3", "--enable-nonfree")):
        raise RuntimeError("FFmpeg enables GPL, version3, or nonfree components")
    if sorted(flag for flag in actual if not flag.startswith("--prefix=")) != sorted(FLAGS):
        raise RuntimeError("FFmpeg contains unexpected configure options")


def inspect_dlls(directory, qt_bin):
    """Check actual loaded DLL licensing, ABI and imports, not only a manifest."""
    records = {}
    with os.add_dll_directory(str(directory)), os.add_dll_directory(str(qt_bin)):
        for name, major in LIBRARIES.items():
            dll = directory / f"{name}-{major}.dll"
            dependencies = imports(dll, qt_bin)
            for dependency in dependencies:
                lower = dependency.lower()
                if lower in DLLS | COMPILER_DLLS or lower.startswith(("api-ms-", "ext-ms-")):
                    continue
                if not (Path(os.environ["WINDIR"]) / "System32" / dependency).is_file():
                    raise RuntimeError(f"Unexpected FFmpeg dependency: {dll.name}: {dependency}")
            library = ctypes.CDLL(str(dll))
            license_fn = getattr(library, name + "_license")
            license_fn.restype = ctypes.c_char_p
            config_fn = getattr(library, name + "_configuration")
            config_fn.restype = ctypes.c_char_p
            version_fn = getattr(library, name + "_version")
            version_fn.restype = ctypes.c_uint
            check_configuration(config_fn().decode(), license_fn().decode())
            if version_fn() >> 16 != major:
                raise RuntimeError(f"Unexpected ABI in {dll.name}")
            records[dll.name] = {"sha256": digest(dll), "imports": sorted(dependencies)}
    return records


def validate(stage, qt_bin):
    manifest = json.loads((stage / "manifest.json").read_text())
    if (manifest.get("ffmpeg_version") != VERSION
            or manifest.get("ffmpeg_license") != "LGPL-2.1-or-later"
            or manifest.get("source_sha256") != SOURCE_SHA256
            or manifest.get("configure") != FLAGS
            or manifest.get("qt_version") != QT_VERSION):
        raise RuntimeError("Unexpected Windows multimedia runtime manifest")
    if set(manifest.get("libraries", {})) != DLLS:
        raise RuntimeError("Runtime manifest has missing or unexpected DLLs")
    if digest(stage / "sources" / f"ffmpeg-{VERSION}.tar.xz") != SOURCE_SHA256:
        raise RuntimeError("FFmpeg corresponding source archive is missing or changed")
    plugin = qt_bin.parent / "share/qt6/plugins/multimedia/ffmpegmediaplugin.dll"
    check_codec_imports(imports(plugin, qt_bin))
    if digest(plugin) != manifest["qt_plugin_sha256"]:
        raise RuntimeError("Qt playback plugin changed; rebuild the private runtime")
    if inspect_dlls(stage, qt_bin) != manifest["libraries"]:
        raise RuntimeError("Staged FFmpeg DLLs differ from the verified build")
    return manifest


def build(base, qt_bin, jobs):
    msys = qt_bin.parent.parent
    bash = msys / "usr/bin/bash.exe"
    for path in (bash, msys / "usr/bin/make.exe", msys / "usr/bin/cmp.exe", qt_bin / "nasm.exe"):
        if not path.is_file():
            raise RuntimeError("Missing build tool: " + str(path)
                               + "; install MSYS2 make, diffutils, and mingw-w64-ucrt-x86_64-nasm")
    if capture(qt_bin / "qmake6.exe", "-query", "QT_VERSION") != QT_VERSION:
        raise RuntimeError(f"This runtime is validated for Qt {QT_VERSION}; select a matching SDK")
    plugin = qt_bin.parent / "share/qt6/plugins/multimedia/ffmpegmediaplugin.dll"
    check_codec_imports(imports(plugin, qt_bin))
    base.mkdir(parents=True, exist_ok=True)
    source_dir = base / "sources"
    source_dir.mkdir(exist_ok=True)
    archive = source_dir / f"ffmpeg-{VERSION}.tar.xz"
    if not archive.is_file() or digest(archive) != SOURCE_SHA256:
        pending = archive.with_suffix(".pending")
        with urllib.request.urlopen(SOURCE_URL, timeout=60) as response, pending.open("wb") as output:
            shutil.copyfileobj(response, output)
        if digest(pending) != SOURCE_SHA256:
            raise RuntimeError("FFmpeg source checksum mismatch")
        pending.replace(archive)
    source = source_dir / f"ffmpeg-{VERSION}"
    stamp = source / ".snitt-extracted"
    if not stamp.is_file() or stamp.read_text() != SOURCE_SHA256:
        if source.exists():
            shutil.rmtree(source)
        with tarfile.open(archive) as contents:
            contents.extractall(source_dir, filter="data")
        stamp.write_text(SOURCE_SHA256)
    work = base / "compile"
    prefix = base / "prefix"
    fingerprint = json.dumps({"source": SOURCE_SHA256, "flags": FLAGS,
                              "compiler": capture(qt_bin / "gcc.exe", "--version"),
                              "prefix": str(prefix)}, sort_keys=True)
    configured = work / ".snitt-configure"
    if not configured.is_file() or configured.read_text() != fingerprint:
        for path in (work, prefix):
            if path.exists():
                shutil.rmtree(path)
        work.mkdir(parents=True)
    env = dict(os.environ, MSYSTEM="UCRT64", CHERE_INVOKING="1", GIT_DIR="/dev/null")
    env["PATH"] = str(qt_bin) + os.pathsep + str(msys / "usr/bin") + os.pathsep + env["PATH"]

    def posix(path):
        return capture(msys / "usr/bin/cygpath.exe", "-u", path)

    def run_bash(command, logfile):
        script = "set -euo pipefail\nexport PATH=/ucrt64/bin:/usr/bin\ncd " + shlex.quote(posix(work)) + "\n" + command
        print(f"Building Windows FFmpeg; log: {logfile}", flush=True)
        with logfile.open("w") as output:
            result = subprocess.run([str(bash), "-c", script], env=env,
                                    stdout=output, stderr=subprocess.STDOUT)
        if result.returncode:
            raise RuntimeError(f"FFmpeg build failed ({logfile}):\n"
                               + "\n".join(logfile.read_text(errors="replace").splitlines()[-40:]))

    if not configured.exists():
        command = [posix(source / "configure"), "--prefix=" + posix(prefix), *FLAGS]
        run_bash(shlex.join(command), base / "configure.log")
        if "License: " + LICENSE not in (base / "configure.log").read_text():
            raise RuntimeError("Configure did not confirm an LGPL-only build")
        configured.write_text(fingerprint)
    run_bash(f"make -j{jobs} install", base / "build.log")
    stage = base / "stage"
    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir()
    for dll in sorted(DLLS):
        shutil.copy2(prefix / "bin" / dll, stage / dll)
    (stage / "sources").mkdir()
    shutil.copy2(archive, stage / "sources" / archive.name)
    shutil.copy2(__file__, stage / "sources" / "build-windows-multimedia-runtime.py")
    (stage / "licenses").mkdir()
    for name in ("LICENSE.md", "COPYING.LGPLv2.1"):
        shutil.copy2(source / name, stage / "licenses" / name)
    manifest = {"ffmpeg_version": VERSION, "ffmpeg_license": "LGPL-2.1-or-later",
                "source_url": SOURCE_URL, "source_sha256": SOURCE_SHA256,
                "configure": FLAGS, "qt_version": QT_VERSION,
                "qt_plugin_sha256": digest(plugin),
                "compiler": capture(qt_bin / "gcc.exe", "--version"),
                "libraries": inspect_dlls(stage, qt_bin)}
    (stage / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (stage / "sources" / "BUILD.txt").write_text(
        f"FFmpeg {VERSION}, LGPL-2.1-or-later\nSource: {SOURCE_URL}\nSHA256: {SOURCE_SHA256}\n"
        "No source modifications. Built in MSYS2 UCRT64 with GCC, GNU Make, and NASM.\n"
        "From a separate build directory, run the source tree's configure with\n"
        "--prefix=<absolute install directory> and these options, then make -j4 install:\n"
        + shlex.join(FLAGS) + "\n\n" + manifest["compiler"] + "\n"
        "These DLLs replace the GPL-configured MSYS2 FFmpeg DLLs. Qt's existing\n"
        "LGPL plugin is reused with the same FFmpeg ABI; it is not rebuilt.\n")
    validate(stage, qt_bin)
    print(f"Verified LGPL-only Windows runtime: {stage}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qt-bin", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/tools/windows-multimedia-runtime")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--verify", type=Path, help="verify an existing stage instead of building")
    args = parser.parse_args()
    if sys.platform != "win32" or args.jobs < 1:
        parser.error("Requires Windows and a positive job count")
    qt_bin = args.qt_bin.resolve()
    if qt_bin.parent.name.lower() != "ucrt64":
        parser.error("Select the MSYS2 UCRT64 Qt bin directory")
    if args.verify:
        validate(args.verify.resolve(), qt_bin)
        print("Windows multimedia runtime verification passed")
    else:
        build(args.build_dir.resolve(), qt_bin, args.jobs)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
