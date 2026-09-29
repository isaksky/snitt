# Windows playback runtime

Snitt's original code is MIT licensed. Windows release packaging builds FFmpeg
with GPL, nonfree components, and external library autodetection disabled. The
resulting five shared libraries are LGPL-2.1-or-later. Qt and other dependencies
retain their own licenses.

The private build uses FFmpeg 9.0.2 with the MSYS2 UCRT64 Qt 6.11.2 playback
plugin. The plugin is reused without modification: its five FFmpeg DLL imports
must match the private build's ABI exactly. The compiler SDK and its original
FFmpeg DLLs are not changed. Source builds using the SDK directly may still load
the SDK's FFmpeg; the private runtime is selected when making a release package.

## Build

In addition to the app's UCRT64 Qt/GCC dependencies, install these MSYS2 build
tools from the MSYS2 terminal:

```sh
pacman -S --needed make diffutils mingw-w64-ucrt-x86_64-nasm \
  mingw-w64-ucrt-x86_64-python
```

From PowerShell, package normally:

```powershell
.\bin\package-windows.ps1 -Version 0.1.0
```

Packaging runs the private runtime builder before deploying dependencies. The
first build downloads checksum-pinned FFmpeg source and compiles the required
libraries. Subsequent builds reuse the configured compilation directory. A
changed compiler, source hash, prefix, or configure flags causes a clean rebuild.

To build or verify just the runtime:

```powershell
$qt = "$env:USERPROFILE\scoop\apps\msys2\current\ucrt64\bin"
& "$qt\python.exe" .\bin\build-windows-multimedia-runtime.py --qt-bin $qt
& "$qt\python.exe" .\bin\build-windows-multimedia-runtime.py --qt-bin $qt `
  --verify .\build\tools\windows-multimedia-runtime\stage
```

Use the UCRT64 Python executable, not the Windows Store alias. The default build
directory is `build/tools/windows-multimedia-runtime`. `--build-dir` can select a
shorter directory when the checkout's path would exceed Windows tool limits.
Configure and compilation logs are saved as `configure.log` and `build.log`.

## Package checks and source

The builder queries each actual DLL's exported license, configure options, and
ABI version. It also inspects PE imports; the codec DLLs may depend only on the
other approved codec DLLs, Windows system libraries, and the named GCC runtime
libraries. The configured playback supports MP4/MOV, H.264, HEVC, MJPEG, and AAC.
H.264/HEVC retain Windows D3D11VA and DXVA2 acceleration where the GPU supports
them, with software decoding available as a fallback.
FFmpeg's network protocols and external codec integrations are disabled.

`windeployqt` is instructed to skip multimedia plugins and FFmpeg libraries. The
package script then adds the validated Qt plugin and private codec DLLs. Missing
codec dependencies cause an error instead of being filled from the SDK. The
final dependency walk verifies private DLL hashes and rejects extra FFmpeg ABIs
and common GPL codec dependencies.
`RUNTIME-DEPENDENCIES.json` records every deployed executable/DLL's PE imports
for inspection alongside the license inventory.

The package's `licenses/ffmpeg` directory contains the LGPL text, FFmpeg's license
explanation, the build manifest, and a `sources` directory with the exact source
archive and build instructions. Users may replace the separate shared libraries
with compatible builds. The normal Qt/MSYS2 attribution and source information
remain in `THIRD-PARTY-NOTICES.txt` and `licenses`.

Recording and trimming still invoke a separately installed `ffmpeg.exe` and
`ffprobe.exe`, normally supplied by Scoop. Those executables are not bundled in
the Snitt ZIP, and their own distributors supply their licensing material.

## Validation when changing the runtime

Run `python3 tests/windows_multimedia_runtime.py` for the configuration and ABI
rejection checks. On Windows, run the app's test suites and
`tests/windows-package-smoke.ps1` against the package in an unlocked desktop.
The package test removes SDK paths and exercises video review, playback, seeking,
trimming, and recording controls. Verify its logs report the LGPL FFmpeg runtime.
Headless build success is not a substitute for these playback checks.

When changing Qt or FFmpeg versions, update the pinned source hash, supported Qt
version, and DLL ABI map deliberately, then repeat package verification. Do not
silently substitute whatever FFmpeg version is currently in MSYS2.
