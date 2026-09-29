# Development

Snitt is built with C++17, Qt 6 Quick/QML, and Material controls.
The product is Snitt; the executable and repository are named `snitt`.
macOS ships `Snitt.app`, containing the `snitt` executable.

## macOS

Build and install from this checkout:

```sh
brew install cmake pkgconf python  # build tools; Xcode command-line tools are also required
./bin/setup-macos-qt             # private, pinned Qt 6.11.2 SDK in build/tools
SNITT_QMAKE="$PWD/build/tools/qt-sdk/6.11.2/macos/bin/qmake6" ./bin/install
```

The setup command downloads the official Qt 6.11.2 SDK into this checkout; it
does not replace a Homebrew Qt installation. Use the same `SNITT_QMAKE` setting
for `./bin/build` or `./bin/stage-macos` when testing without installing.
The build checks that the selected Qt frameworks and the private playback plugin
match before compiling. Qt 6.11.1 is also supported when an existing SDK is
selected explicitly; a newer unpinned Qt release is rejected with setup guidance.

The install command builds and bundles Qt into `~/Applications/Snitt.app`, installs a per-user
LaunchAgent at `~/Library/LaunchAgents/local.snitt.plist`, and starts Snitt in the
background. Qt is bundled; macOS 15+ recording uses native ScreenCaptureKit.
The first build also fetches and compiles small LGPL FFmpeg helpers for thumbnails, stream-copy trimming,
and probing, plus a private Qt Multimedia playback plugin with LGPL FFmpeg libraries. This
can take several minutes; later builds reuse the compiled sources. Both are
bundled with the app, so no separate FFmpeg install is needed to use Snitt.
Re-run the same command to update; use `./bin/uninstall` to remove the app and login
entry.

macOS may request Screen & System Audio Recording permission for Snitt or the
terminal launching a development build.
If capture opens an error dialog, enable the installed `~/Applications/Snitt.app`
in System Settings → Privacy & Security → Screen & System Audio Recording, then
quit and reopen Snitt. The installer creates a persistent local signing identity
in `~/Library/Application Support/snitt/signing`; macOS may authenticate its first
setup. Keep that directory across rebuilds so updates retain the same identity.
Trust is limited to code signing by Apple's `codesign` tool. This is a local
development certificate, not an Apple Developer ID or notarized release. Set
`SNITT_SIGN_IDENTITY` to use your own signing identity instead. Migrating from
an older ad-hoc signed build requires renewing screen-recording permission once.

Run and test:

```sh
./bin/run                    # capture now, then stay in the menu bar
./bin/run --background       # just start the background app
./bin/run --show             # show the editor
./bin/run /path/to/image.png # edit an existing image
./bin/run --quit             # quit the running instance
./bin/test
```

## Windows

From PowerShell with MSYS2's UCRT64 GCC/Qt toolchain:

```powershell
.\bin\build.ps1 -Test
.\bin\package-windows.ps1
```

The scripts find Scoop's MSYS2 installation automatically. Alternatively set
`SNITT_QT_BIN` or pass `-QtBin` with a matching MinGW Qt 6 bin directory on PATH
alongside `mingw32-make`. The package script uses `windeployqt` and follows PE
imports to include MSYS2's non-Qt runtime libraries. Release packaging currently
requires UCRT64 Qt 6.11.2 and builds pinned LGPL-only FFmpeg 9.0.2 DLLs for playback;
it excludes the SDK's GPL-configured FFmpeg DLLs. The first runtime build takes
several minutes; subsequent builds reuse it. The package includes the exact
FFmpeg source and build instructions. It produces
`build/release/snitt_0.1.0_windows_amd64.zip` and its SHA-256 checksum file.
Pass `-Version` to package a different release version.

MSYS2 UCRT64 dependencies:

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-make \
  mingw-w64-ucrt-x86_64-qt6-base mingw-w64-ucrt-x86_64-qt6-declarative \
  mingw-w64-ucrt-x86_64-qt6-multimedia mingw-w64-ucrt-x86_64-python \
  mingw-w64-ucrt-x86_64-nasm make diffutils
```

## Testing

Tests cover exact pixel joins, undo/redo, annotations, privacy-mask independence,
erase sampling, source-resolution clipboard output, keyboard commands, grid
arrangement, recording lifecycle, scaled region selection, and Windows hotkey
registration. Offscreen tests use a private Qt clipboard and cannot prove physical
keyboard input or capture from a real unlocked desktop. Interactive capture and
recording tests run separately on an unlocked desktop. The recording tests and
startup comparison benchmark use FFmpeg and FFprobe to decode or compare clips;
those developer tools are not macOS application runtime dependencies.

See the [code map](code-map.md), [release guide](releasing.md), and
[Windows multimedia runtime guide](windows-multimedia-runtime.md).
