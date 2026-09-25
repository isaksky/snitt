# xshot

A small screenshot and screen-recording app for macOS and Windows. Select a region,
edit or combine screenshots, or record a video. Built with C++17, Qt 6 Quick/QML,
and Material controls.

## Install and keep it ready

xshot lives in the menu bar on macOS or the system tray on Windows. It starts at
login and stays running after you finish editing. **Ctrl+Print Screen** starts
region selection from any app. **Command+C** on macOS or **Ctrl+C** on Windows
copies the result and finishes the session. Videos copy their saved file path.
Choose **Quit xshot** from its icon menu to stop it until the next launch/login.
Only one instance runs per user desktop session; launching it again requests a
capture from the existing instance.

### macOS 15+

From this checkout:

```sh
brew install qtdeclarative  # development dependency, if needed
brew install ffmpeg         # required for video recording
./bin/install
```

This builds and bundles Qt into `~/Applications/xshot.app`, installs a per-user
LaunchAgent at `~/Library/LaunchAgents/local.xshot.plist`, and starts xshot in the
background. Qt is bundled; recording uses a separately installed FFmpeg executable.
Re-run the same command to update; use `./bin/uninstall` to remove the app and login
entry. Screenshot capture and editing do not need FFmpeg.

On a PC keyboard, Print Screen usually arrives as **F13** on macOS, so the binding
is **Control+F13**. **Control+Shift+X** also works for keyboards without that key.
These use the actual Control key, not Command. macOS may request Screen & System
Audio Recording permission for xshot or the terminal launching a development build.
If capture opens an error dialog, enable the installed `~/Applications/xshot.app`
in System Settings → Privacy & Security → Screen & System Audio Recording, then
quit and reopen xshot. The installer creates a persistent local signing identity
in `~/Library/Application Support/xshot/signing`; macOS may authenticate its first
setup. Keep that directory across rebuilds so updates retain the same identity.
Trust is limited to code signing by Apple's `codesign` tool. This is a local
development certificate, not an Apple Developer ID or notarized release. Set
`XSHOT_SIGN_IDENTITY` to use your own signing identity instead. Migrating from
an older ad-hoc signed build requires renewing screen-recording permission once.

### Windows 10/11 x64

The releases are currently private. With Scoop and GitHub CLI installed, sign in
with a GitHub account that can access this repository, then run:

```powershell
gh auth login
gh release download --repo isaksky/xshot --pattern Install-Scoop.ps1
& .\Install-Scoop.ps1 -Repository isaksky/xshot
```

This installs xshot through Scoop, including FFmpeg, bundled Qt, and a login entry.
The installer checks the release's SHA-256 hashes and uses GitHub CLI's existing
authentication without copying a token. Run `Install-Scoop.ps1` again to update;
ordinary `scoop update xshot` cannot download private release assets. Uninstall
with `scoop uninstall xshot`. If switching from the standalone installer, run its
**Uninstall.cmd** first.

For a standalone installation, download the ZIP from the GitHub release instead.
Extract `xshot_0.1.0_windows_amd64.zip` and double-click **Install.cmd**. It installs the
app and bundled runtime into `%LOCALAPPDATA%\Programs\xshot`, adds a Start menu
shortcut and a per-user login entry, and starts the tray app. No administrator
rights or Qt installation are needed. Run **Uninstall.cmd** in the installed
folder to remove it. To run without installing, launch `xshot.exe --background`
from the extracted folder.

Install FFmpeg for video recording:

```powershell
scoop install ffmpeg
```

Before updating, finish the current session and quit xshot from its tray menu,
or run `xshot --quit`.

**Ctrl+Print Screen** is registered with Windows. If another app owns it, xshot
reports the conflict; disable that binding in the other app, then restart xshot.
The tray's **New screenshot** action is always available. The Windows region
picker supports selecting within any connected monitor and accounts for display
scaling. A region currently stays within one monitor.

## Multiple regions

Start a capture and press **M**, or choose **Multiple regions** in the editor.
Drag each rectangle in the order you want it to appear. Numbered selections stay
on the frozen screen; click one to remove it, or use Backspace to remove the last.
**Command/Ctrl+C** opens the arrangement preview; Escape cancels the capture.

The preview defaults to two columns. Press **+** or **−** to change the column
count (one to six, limited by the number of regions), or use the **Columns** buttons.
Drag tiles to reorder them, or select a tile and use **Move left**, **Move right**,
or **Remove**.
Original pixels are kept without resizing or cropping, with 24-pixel neutral
spacing. Numbers and selection outlines appear only in the preview.
Choose **Annotate (Enter)** to use the drawing tools, or **Copy (Command/Ctrl+C)**
to finish directly. Enter only switches from arranging to annotating; it does not
copy or finish an edited screenshot.
Rearranging after annotation asks before resetting annotations and cuts; the
original regions are retained until the session is finished. Captures are limited
to 24 regions and 256 MiB, and each rectangle stays within one monitor.

## Recording

Start a capture with **Ctrl+Print Screen**, press **V**, then drag one region to
start recording. **Record video (V)** in the editor also opens this picker.
Video mode selects one rectangle on one monitor; **M** is unavailable in this mode.
The recording controls show elapsed time. Choose **Stop and copy path**, or press
**Command/Ctrl+C** while those controls are active, to stop and save the clip.
The clipboard receives the full file path as text, not the video contents.
**Escape** cancels the recording and removes that clip.
Use **Hide** to move the controls out of the way; **Ctrl+Print Screen** brings
them back while recording. Windows excludes the controls from captured video.
On macOS, keep them outside the selected region or hide them.

Recordings are silent MP4 files saved under `~/Movies/xshot` on macOS or your
Windows **Videos\xshot** folder. FFmpeg runs as a separate process, captures the
selected screen region and pointer at 30 fps, and encodes H.264. Recordings are
kept after the session; xshot does not upload them or delete completed clips.
Install FFmpeg with `brew install ffmpeg` on macOS or `scoop install ffmpeg` on
Windows. xshot checks PATH and the usual Homebrew/Scoop locations, including when
started at login.

## Editing

| Action | Shortcut | Gesture |
| --- | --- | --- |
| Cut | X | Drag sideways to remove a vertical strip, or up/down to remove a horizontal strip. Release to join the remaining edges. |
| Rectangle | R | Click and drag around an area. |
| Text | T | Click to place a textbox, then type. Command/Ctrl+Enter places it; Enter adds a line; Escape cancels. |
| Arrow | A | Drag from the tail toward the tip. |
| Blur | B | Drag a rectangle to replace its contents with an opaque pixelated blur. |
| Smart erase | E | Drag a rectangle to fill it with the exact color at the drag's starting point. Works in any drag direction. |
| Green / good | G | Applies to new rectangles, arrows, and text. |
| Red / bad | D | Applies to new rectangles, arrows, and text. |
| Copy and finish | Command/Ctrl+C | Copies the full-resolution image, hides the editor, and clears its editing history. |
| Undo | Command/Ctrl+Z | Reverts the last edit. |
| Redo | Shift+Command+Z on macOS; Ctrl+Y on Windows | Reapplies the last undone edit. |
| New capture | Command/Ctrl+N | Starts a new region selection. |
| Multiple regions | M | Starts a multiple-region capture, or enables it in the screenshot picker. |
| Record video | V | Starts a video capture, or enables it in the screenshot picker. |
| Open / paste | Command/Ctrl+O / Command/Ctrl+V | Opens an existing image for editing. |

Use Command on macOS, Ctrl on Windows. Escape cancels region selection or an
in-progress drawing gesture. Tool shortcuts are inactive while entering text.
Buttons show their shortcuts in parentheses, for example **Rectangle (R)**.
The view fits the screenshot to the window; edits and clipboard output retain
the source pixels. Cut also removes annotations in the selected strip.
Annotations are flattened when placed; use undo to correct them. History is
limited to 50 images or 256 MiB.

Blur is a privacy mask: the selected source pixels are completely replaced with
an opaque neutral texture that does not depend on their original contents. Cover
the entire sensitive area; pixels outside the selection are unchanged. The copied
image contains the flattened mask, with no hidden original layer. Undo history and
original regions remain available only during the active session and are cleared
when you copy and finish. Smart erase samples the starting pixel, including its
alpha, and is intended for extending a matching background.

Screenshots use macOS's built-in screenshot tool or Qt screen capture on Windows,
followed by xshot's frozen selection overlay. Only video recording needs FFmpeg.
Nothing is uploaded.

## Development

macOS:

```sh
./bin/run                    # capture now, then stay in the menu bar
./bin/run --background       # just start the background app
./bin/run --show             # show the editor
./bin/run /path/to/image.png # edit an existing image
./bin/run --quit             # quit the running instance
./bin/test
```

Windows, from PowerShell with MSYS2's UCRT64 GCC/Qt toolchain:

```powershell
.\bin\build.ps1 -Test
.\bin\package-windows.ps1
```

The scripts find Scoop's MSYS2 installation automatically. Alternatively set
`XSHOT_QT_BIN` or pass `-QtBin` with a matching MinGW Qt 6 bin directory on PATH
alongside `mingw32-make`. The package script uses `windeployqt` and follows PE
imports to include MSYS2's non-Qt runtime libraries. It produces
`build/release/xshot_0.1.0_windows_amd64.zip` and its SHA-256 checksum file.
Pass `-Version` to package a different release version.

MSYS2 UCRT64 dependencies:

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-make \
  mingw-w64-ucrt-x86_64-qt6-base mingw-w64-ucrt-x86_64-qt6-declarative
```

Tests cover exact pixel joins, undo/redo, annotations, privacy-mask independence,
erase sampling, source-resolution clipboard output, keyboard commands, grid
arrangement, recording lifecycle, scaled region selection, and Windows hotkey
registration. Offscreen tests use a private Qt clipboard and cannot prove physical
keyboard input or capture from a real unlocked desktop. Interactive capture and
recording tests run separately on an unlocked desktop.

## Code

- `src/Main.qml`: editing controls, shortcuts, and show/hide session flow.
- `src/appservice.cpp`: tray menu and single-instance local IPC.
- `src/globalhotkey.cpp`: native Windows/macOS global hotkey registration.
- `src/backend.cpp`: platform capture lifecycle.
- `src/videorecorder.cpp`: external FFmpeg discovery, recording, and saved-file lifecycle.
- `src/regionselector.cpp`: single- and multiple-region selection overlay.
- `src/editorcanvas.cpp`: image display, gesture mapping, and clipboard access.
- `src/imagedocument.cpp`: full-resolution image edits and bounded undo history.
