# Snitt

A small screenshot and screen-recording app for macOS and Windows. Select a region,
edit or combine screenshots, or record a video. Built with C++17, Qt 6 Quick/QML,
and Material controls.

The product is **Snitt**; the executable, repository, and Scoop package are named
`snitt`. macOS ships `Snitt.app`, containing the `snitt` executable.

Snitt's original code is available under the [MIT license](LICENSE).
Third-party code and bundled dependencies retain their own licenses, including
the notices in `src/OMACUT-LICENSE` and `src/icons/LICENSE`. Windows packaging
builds an [LGPL-only FFmpeg playback runtime](docs/windows-multimedia-runtime.md)
and includes its source and build instructions. See the
[public release preparation plan](docs/releasing.md) for GitHub Actions and code
signing setup.

## Upgrading from xshot

Finish any capture or recording, then quit and uninstall the old xshot app before
installing Snitt so its login entry and global shortcut do not remain active.
Use the old checkout's `bin/uninstall` on macOS, `scoop uninstall xshot` for Scoop,
or `%LOCALAPPDATA%\Programs\xshot\Uninstall.cmd` for the standalone Windows install.
Snitt's uninstaller only removes Snitt.

Snitt uses a new settings directory and new default media folders. Existing
screenshots, recordings, and xshot settings stay in their original locations.
To retain custom settings, quit both apps and copy the old `settings.ini` into
Snitt's configuration directory, backing up any Snitt settings first:

| Platform | Old settings directory | Snitt settings directory |
| --- | --- | --- |
| macOS | `~/Library/Preferences/xshot/xshot` | `~/Library/Preferences/snitt/snitt` |
| Windows | `%LOCALAPPDATA%\xshot\xshot` | `%LOCALAPPDATA%\snitt\snitt` |

Copied `Save/picturesRoot` and `Save/videosRoot` values keep their existing paths;
clear those values to use Snitt's new defaults. macOS requires a new screen
recording permission for Snitt. Build environment variables now use the `SNITT_`
prefix, such as `SNITT_QMAKE`, `SNITT_QT_BIN`, and `SNITT_SIGN_IDENTITY`.

## Install and keep it ready

Snitt lives in the menu bar on macOS or the system tray on Windows. It starts at
login and stays running after you finish editing. **Ctrl+Print Screen** starts
region selection from any app. **C** copies the screenshot and finishes the session
when you are not typing an annotation. Finalizing a video reveals it in Finder or Explorer.
Choose **Quit Snitt** from its icon menu to stop it until the next launch/login.
Only one instance runs per user desktop session; launching it again requests a
capture from the existing instance.

### macOS 15+

From this checkout:

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

On a PC keyboard, Print Screen usually arrives as **F13** on macOS, so the binding
is **Control+F13**. This uses the actual Control key, not Command.
macOS may request Screen & System
Audio Recording permission for Snitt or the terminal launching a development build.
If capture opens an error dialog, enable the installed `~/Applications/Snitt.app`
in System Settings → Privacy & Security → Screen & System Audio Recording, then
quit and reopen Snitt. The installer creates a persistent local signing identity
in `~/Library/Application Support/snitt/signing`; macOS may authenticate its first
setup. Keep that directory across rebuilds so updates retain the same identity.
Trust is limited to code signing by Apple's `codesign` tool. This is a local
development certificate, not an Apple Developer ID or notarized release. Set
`SNITT_SIGN_IDENTITY` to use your own signing identity instead. Migrating from
an older ad-hoc signed build requires renewing screen-recording permission once.

### Windows 10/11 x64

Video recording requires Windows 10 version 2004 (build 19041) or later. On
older Windows 10 versions, Snitt stops before recording because the startup
indicator and controls cannot be excluded from the captured screen. Screenshot
capture and editing still work.

The releases are currently private. With Scoop and GitHub CLI installed, sign in
with a GitHub account that can access this repository, then run:

```powershell
gh auth login
gh release download --repo isaksky/snitt --pattern Install-Scoop.ps1
& .\Install-Scoop.ps1 -Repository isaksky/snitt
```

This installs Snitt through Scoop, including FFmpeg, bundled Qt, and a login entry.
The installer checks the release's SHA-256 hashes and uses GitHub CLI's existing
authentication without copying a token. Run `Install-Scoop.ps1` again to update;
ordinary `scoop update snitt` cannot download private release assets. Uninstall
with `scoop uninstall snitt`. If switching from the standalone installer, run its
**Uninstall.cmd** first.

For a standalone installation, download the ZIP from the GitHub release instead.
Extract `snitt_0.1.0_windows_amd64.zip` and double-click **Install.cmd**. It installs the
app and bundled runtime into `%LOCALAPPDATA%\Programs\snitt`, adds a Start menu
shortcut and a per-user login entry, and starts the tray app. No administrator
rights or Qt installation are needed. Run **Uninstall.cmd** in the installed
folder to remove it. To run without installing, launch `snitt.exe --background`
from the extracted folder.

Install FFmpeg for video recording:

```powershell
scoop install ffmpeg
```

Before updating, finish the current session and quit Snitt from its tray menu,
or run `snitt --quit`.

**Ctrl+Print Screen** is registered with Windows. If another app owns it, Snitt
reports the conflict; disable that binding in the other app, then restart Snitt.
The tray's **New screenshot** action is always available. The Windows region
picker supports selecting within any connected monitor and accounts for display
scaling. A region currently stays within one monitor.

## Multiple regions

The workflow has four phases: **Capture → Arrange → Annotate (optional) → Finish**.
Use **Enter** to advance through selection and arrangement, then **C** to copy or
**S** to save the finished image. You can skip annotation and finish from Arrange.

### Phase 1: Capture

- Start a capture with **Ctrl+Print Screen** (**Control+F13** on macOS), then press
  **M**, or choose **Multiple (M)** in the editor.
- Drag rectangles on the frozen desktop. Selections remain highlighted and
  numbered in the order you draw them.
- The fixed capture toolbar shows **Region**, **Multi**, and **Video**, with their
  first letters underlined for the **R**, **M**, and **V** mode shortcuts.
  Switching to Multiple keeps the controls in place; dimensions appear beside the
  selection while dragging.
- Click a selection's **×** control (or the selection itself) to remove it, or press
  **Backspace/Delete** to remove the latest.
- Click **Arrange → Enter** or press **Enter** to finish selecting and open Arrange.
  Arrange is enabled after the first region. **Esc** cancels the capture.

All regions come from the same frozen desktop; this phase does not collect new
shots after scrolling or changing windows. Captures are limited to 24 regions and
256 MiB, and each rectangle stays within one monitor.

### Phase 2: Arrange

The editor highlights **Arrange** in its **Arrange → Annotate (optional)** indicator
and shows layout controls. Drawing tools are hidden during this phase.

- Regions start in a two-column grid. Use **+ / −** or the **Columns** buttons to
  choose one to six columns, limited by the number of regions.
- Drag tiles to reorder them, or select a tile and use **← / →** or the
  **Move left / Move right** buttons. **Backspace/Delete** or **Remove** removes it.
- Original pixels are preserved without resizing or cropping, with 24-pixel
  neutral spacing. Numbers and selection outlines are preview guides only.
- Choose **Annotate → (Enter)** for Phase 3, or **Save and close (S)**
  or **Copy and close (C)** to finish directly from Arrange.

### Phase 3: Annotate — optional

The indicator highlights **Annotate**, and drawing tools replace the layout
controls. Tile numbers and selection outlines disappear; you now edit one combined image.

- Use the normal drawing, text, Pixelate, smart erase, and cut tools.
- Choose **← Back to arrange** to revise the layout. If edits exist, choose
  **Keep editing** to preserve them or **Discard edits** to reset
  annotations and cuts. The captured regions are kept.
- Press **S** or **C**, or choose **Save** or **Copy** under **Finish (will close)**,
  when ready. While typing an annotation, these shortcuts type text; the buttons
  place the active text before finishing.

Enter does not copy or finish an edited screenshot. While typing an annotation,
Enter adds a line.

### Phase 4: Finish

- From Arrange or Annotate, **Copy and close (C)** copies the full-resolution
  combined image to the clipboard. **Save and close (S)** writes a unique PNG to
  `Pictures/snitt/<year>/<month>` and reveals it in Finder or Explorer; month
  folders use unpadded numbers, and saving does not change the clipboard.
- The editor closes and the session's editing history and original regions are cleared.
- Preview numbers and selection outlines are not included in the exported image.
- If saving fails, the editor stays open with the image and edits intact.
- Snitt stays running in the menu bar or system tray, ready for the next capture.

## Recording

Start a capture with **Ctrl+Print Screen**, press **V**, then drag one region to
start recording. **Record video (V)** in the editor also opens this picker.
Video mode selects one rectangle on one monitor; **M** is unavailable in this mode.
The compact recording strip sits at the top center of the selected display and
shows elapsed time. Choose **Stop** or press **Ctrl+Print Screen** (**Control+F13**
on macOS) to save the clip and open its review. Choose **Cancel** to discard it.
A red outline sits just outside the selected region while recording. The outline
and recording controls are excluded from the saved video. The clipboard keeps
its previous contents.
On macOS, ScreenCaptureKit excludes all Snitt windows automatically, including
the startup indicator and controls; they can remain over the selected region.

After Stop, preview the video and drag the filmstrip's blue handles to choose the
portion to keep. Drag the white seek handle to change playback position. The
play/pause button sits below the centered elapsed and total time. Space plays or
pauses; Left/Right seek one second and Shift+Left/Right seek five seconds. **Save trim**
exports the chosen range and replaces the original MP4 only after the new file
passes validation. Trimming copies the existing compressed video and audio without
re-encoding. MP4 edit lists hide the reference frames needed before and after the
selection, keeping playback bounded to the chosen range even with sparse frames.
**Keep original**, Escape, or closing review keeps the full
recording. Canceling an export or an export error also keeps the original. The
final file is selected in Finder or Explorer after Keep original or a successful
trim; previewing and trimming never change the clipboard.

Recordings are silent MP4 files saved under `~/Movies/snitt/<year>/<month>` on
macOS or your Windows **Videos\snitt\<year>\<month>** folder. The year and
unpadded month come from the local time when saving or recording starts. Both
platforms capture the selected region and pointer at 30 fps and encode H.264. macOS 15+ uses native ScreenCaptureKit;
Windows runs FFmpeg as a separate process. Recordings are kept after the session;
Snitt does not upload them or delete completed clips. On Windows, install FFmpeg
with `scoop install ffmpeg`; Snitt checks PATH and the usual Scoop locations,
including when started at login.

## Editing

| Action | Shortcut | Gesture |
| --- | --- | --- |
| Cut | X | Drag sideways to remove a vertical strip, or up/down to remove a horizontal strip. Release to join the remaining edges. |
| Rectangle | R | Click and drag around an area. |
| Text | T | Click to place a textbox, then type. Drag its right-edge grip to change wrapping. Command/Ctrl+Enter places it; Enter adds a line; Escape cancels. |
| Arrow | A | Drag from the tail toward the tip. |
| Pixelate | P | Drag a rectangle to replace its contents with coarse blocks averaged from the image. Use the wheel to change block size. |
| Smart erase | E | Drag a rectangle to fill it with the exact color at the drag's starting point. Works in any drag direction. |
| Green / good | G | Applies to new rectangles, arrows, and text. |
| Red / bad | B | Applies to new rectangles, arrows, and text. |
| Copy and close | C | Copies the full-resolution image, hides the editor, and clears its editing history. Inactive while typing an annotation. |
| Save and close | S | Saves a PNG in Pictures/snitt/<year>/<month>, reveals it in the file manager, and closes the editor. Inactive while typing an annotation. |
| Undo | Command/Ctrl+Z | Reverts the last edit. |
| Redo | Shift+Command+Z on macOS; Ctrl+Y on Windows | Reapplies the last undone edit. |
| New capture | Command/Ctrl+N | Starts a new region selection. |
| Multiple regions | M | Starts a multiple-region capture, or enables it in the screenshot picker. |
| Record video | V | Starts a video capture, or enables it in the screenshot picker. |
| Open / paste | Command/Ctrl+O / Command/Ctrl+V | Opens an existing image for editing. |

New capture, multiple-region capture, video recording, open, and image-paste
shortcuts are disabled while annotating an image. Finish or close the image first.
Pasting text into an annotation still works.

The footer shows preview zoom and the current image's natural pixel dimensions.
Zoom changes only the editor view. Copy and Save keep those dimensions for
unannotated images, including cuts, arranged regions, Pixelate, and erase. When
visible shapes or text remain on a small image, Snitt renders them fresh at a
larger output size: it raises the
shorter edge toward 720 pixels, never more than 3×, 16,384 pixels on either edge,
or 32 million output pixels. Images already at least 720 pixels on the shorter
edge stay at natural size. This improves annotation edges; enlarged screenshot
pixels do not gain missing captured detail. Undoing or completely removing the
annotations restores natural-size export. Copy and Save use the same output.

Use Command on macOS, Ctrl on Windows. Escape cancels region selection or an
in-progress drawing gesture. Tool shortcuts are inactive while entering text.
Annotation tools show their shortcuts beneath the icons. Good and Bad underline
their shortcut letter in the Color Mode group. Concise tool hints sit between
Color Mode and Finish, with Undo/Redo alongside the drawing tools. The groups
wrap onto two rows when the window is narrow.
The view fits the screenshot to the window; edits and clipboard output retain
the source pixels. Cut also removes annotations in the selected strip.
Annotations are flattened when placed; use undo to correct them. History is
limited to 50 images or 256 MiB.

Use the mouse wheel over the image with Rectangle or Arrow selected to change
stroke width, with Text selected to change font size, or with Pixelate selected
to change block size. Each wheel notch changes line width or text size by 2 px,
or pixelation block size by 4 px. The current size appears
in the editor hint. While entering text, wheel over the image or text box to
resize the whole draft; use the scrollbar to move through long text. Widths
and font sizes are source-image pixels and stay selected for the next annotation.
Stroke width ranges from 1 px to the smaller of 64 px and one-eighth of the
image's shorter edge (at least 1 px). Text ranges from 8 px to the smaller of
144 px and one-third of that edge (at least 8 px). Existing annotations keep
their size. Pixelate blocks range from 4 px to the smaller of 64 px and half
the image's shorter edge. Existing
pixelated areas keep their chosen block size.

Pixelate averages source colors into larger blocks. It obscures fine detail but
retains coarse information from the image, so do not use it to hide secrets.
Pixels outside the selection are unchanged. The copied image contains the
flattened result, with no hidden original layer. Undo history and original
regions remain available only during the active session and are cleared when
you copy and finish. Smart erase samples the starting pixel, including its alpha,
and is intended for extending a matching background.

Screenshots use macOS's built-in screenshot tool or Qt screen capture on Windows,
followed by Snitt's frozen selection overlay. FFmpeg is a Windows video runtime
dependency; macOS recording uses ScreenCaptureKit.
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

Tests cover exact pixel joins, undo/redo, annotations, privacy-mask independence,
erase sampling, source-resolution clipboard output, keyboard commands, grid
arrangement, recording lifecycle, scaled region selection, and Windows hotkey
registration. Offscreen tests use a private Qt clipboard and cannot prove physical
keyboard input or capture from a real unlocked desktop. Interactive capture and
recording tests run separately on an unlocked desktop. The recording tests and
startup comparison benchmark use FFmpeg and FFprobe to decode or compare clips;
those developer tools are not macOS application runtime dependencies.

## Code

- `src/Main.qml`: editing controls, shortcuts, and show/hide session flow.
- `src/appservice.cpp`: tray menu and single-instance local IPC.
- `src/globalhotkey.cpp`: native Windows/macOS global hotkey registration.
- `src/backend.cpp`: platform capture lifecycle.
- `src/macrecorder.mm`: native macOS ScreenCaptureKit recording and saved-file lifecycle.
- `src/videorecorder.cpp`: Windows FFmpeg discovery, recording, and saved-file lifecycle.
- `src/regionselector.cpp`: single- and multiple-region selection overlay.
- `src/editorcanvas.cpp`: image display, gesture mapping, and clipboard access.
- `src/imagedocument.cpp`: full-resolution image edits and bounded undo history.
