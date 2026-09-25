# xshot

A small screenshot app for macOS and Windows: select a region, cut out the noise,
mark what matters, and copy the result. C++17, Qt 6 Quick/QML, and Material controls.

## Install and keep it ready

xshot lives in the menu bar on macOS or the system tray on Windows. It starts at
login and stays running after you finish editing. **Ctrl+Print Screen** starts
region selection from any app. Done copies the result and hides the editor.
Choose **Quit xshot** from its icon menu to stop it until the next launch/login.
Only one instance runs per user desktop session; launching it again requests a
capture from the existing instance.

### macOS 15+

From this checkout:

```sh
brew install qtdeclarative  # development dependency, if needed
./bin/install
```

This builds and bundles Qt into `~/Applications/xshot.app`, installs a per-user
LaunchAgent at `~/Library/LaunchAgents/local.xshot.plist`, and starts xshot in the
background. The installed app does not need Homebrew to run. Re-run the same
command to update; use `./bin/uninstall` to remove the app and login entry.

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

Extract `xshot-windows-x64.zip` and double-click **Install.cmd**. It installs the
app and bundled runtime into `%LOCALAPPDATA%\Programs\xshot`, adds a Start menu
shortcut and a per-user login entry, and starts the tray app. No administrator
rights or Qt installation are needed. Run **Uninstall.cmd** in the installed
folder to remove it. To run without installing, launch `xshot.exe --background`
from the extracted folder.

**Ctrl+Print Screen** is registered with Windows. If another app owns it, xshot
reports the conflict; disable that binding in the other app, then restart xshot.
The tray's **New screenshot** action is always available. The Windows region
picker supports selecting within any connected monitor and accounts for display
scaling. A region currently stays within one monitor.

## Multiple regions

Start a capture and press **M**, or choose **Multiple regions** in the editor.
Drag each rectangle in the order you want it to appear. Numbered selections stay
on the frozen screen; click one to remove it, or use Backspace to remove the last.
**Enter** opens the arrangement preview; Escape cancels the capture.

The preview defaults to two columns. Change **Columns**, drag tiles to reorder
them, or select a tile and use **Move left**, **Move right**, or **Remove**.
Original pixels are kept without resizing or cropping, with 24-pixel neutral
spacing. Numbers and selection outlines appear only in the preview.
Choose **Annotate** (Enter) to use the drawing tools, or **Copy** to finish directly.
Rearranging after annotation asks before resetting annotations and cuts; the
original regions are retained until the session is finished. Captures are limited
to 24 regions and 256 MiB, and each rectangle stays within one monitor.

## Editing

| Action | Shortcut | Gesture |
| --- | --- | --- |
| Cut | X | Drag sideways to remove a vertical strip, or up/down to remove a horizontal strip. Release to join the remaining edges. |
| Rectangle | R | Click and drag around an area. |
| Text | T | Click to place a textbox, then type. Command/Ctrl+Enter places it; Enter adds a line; Escape cancels. |
| Arrow | A | Drag from the tail toward the tip. |
| Green / good | G | Applies to new rectangles, arrows, and text. |
| Red / bad | B | Applies to new rectangles, arrows, and text. |
| Finish | Enter | Copies the full-resolution image, hides the editor, and clears its editing history. |
| Copy without finishing | Command/Ctrl+C | Copies the current image. |
| Undo / redo | Command/Ctrl+Z / Shift+Command/Ctrl+Z | Reverts or reapplies the last edit. |
| New capture | Command/Ctrl+N | Starts a new region selection. |
| Open / paste | Command/Ctrl+O / Command/Ctrl+V | Opens an existing image for editing. |

Use Command on macOS, Ctrl on Windows. Escape cancels region selection or an
in-progress drawing gesture. Tool shortcuts are inactive while entering text.
The view fits the screenshot to the window; edits and clipboard output retain
the source pixels. Cut also removes annotations in the selected strip.
Annotations are flattened when placed; use undo to correct them. History is
limited to 50 images or 256 MiB.

No ffmpeg or third-party capture utility is used. macOS snapshots the desktop
with its built-in screenshot tool; Windows uses Qt screen capture. Both use
xshot's frozen selection overlay. Nothing is uploaded.

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
`build/package/windows/xshot-windows-x64.zip`.

MSYS2 UCRT64 dependencies:

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-make \
  mingw-w64-ucrt-x86_64-qt6-base mingw-w64-ucrt-x86_64-qt6-declarative
```

Tests cover exact pixel joins, undo/redo, annotations, source-resolution clipboard
output, the QML editing/session flow, scaled region selection, and Windows hotkey
registration. Offscreen tests use a private Qt clipboard and cannot prove physical
keyboard input or capture from a real unlocked desktop.

## Code

- `src/Main.qml`: editing controls, shortcuts, and show/hide session flow.
- `src/appservice.cpp`: tray menu and single-instance local IPC.
- `src/globalhotkey.cpp`: native Windows/macOS global hotkey registration.
- `src/backend.cpp`: platform capture lifecycle.
- `src/regionselector.cpp`: single- and multiple-region selection overlay.
- `src/editorcanvas.cpp`: image display, gesture mapping, and clipboard access.
- `src/imagedocument.cpp`: full-resolution image edits and bounded undo history.
