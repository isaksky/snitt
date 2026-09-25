# xshot

A small screenshot editor for macOS: select a region, cut out the noise, mark what
matters, and copy the result. Built with C++17, Qt 6 Quick/QML, and Material controls.

## Use

```sh
./bin/run
```

Drag to select a screen region. The editor opens immediately after selection.
Escape cancels capture; the start screen lets you try again, open an image, or
paste one. **New capture** starts another selection with the editor hidden.

| Action | Shortcut | Gesture |
| --- | --- | --- |
| Cut | X | Drag sideways to remove a vertical strip, or up/down to remove a horizontal strip. Release to join the remaining edges. |
| Rectangle | R | Click and drag around an area. |
| Text | T | Click to place a textbox, then type. ⌘Enter places it; Enter adds a line; Escape cancels. |
| Arrow | A | Drag from the tail toward the tip. |
| Green / good | G | Applies to new rectangles, arrows, and text. |
| Red / bad | B | Applies to new rectangles, arrows, and text. |
| Finish | Enter | Copies the full-resolution edited image to the clipboard and closes the app. |
| Copy without closing | ⌘C | Copies the current image. |
| Undo / redo | ⌘Z / ⇧⌘Z | Reverts or reapplies the last edit. |
| New capture | ⌘N | Starts a new region selection. |
| Open / paste | ⌘O / ⌘V | Opens an existing image for editing. |

Escape cancels an in-progress drawing gesture. Tool shortcuts are inactive while
entering text. The view fits the screenshot to the window; edits and clipboard
output retain the source pixels. Cut removes the selected strip from both the
image and any annotations already on it. Annotations are flattened when placed;
use undo to correct them. History is limited to 50 images or 256 MiB.

You can also open an image directly:

```sh
./bin/run /absolute/path/to/screenshot.png
```

No ffmpeg or third-party capture utility is used. macOS capture uses the built-in
`/usr/sbin/screencapture` region picker. macOS may request Screen & System Audio
Recording permission for xshot or its launching terminal. Other platforms can
open and paste images, but screen capture is currently implemented only for macOS.

## Build and check

Requires a C++17 compiler, make, and Qt 6 Core, Gui, Qml, Quick, Quick Controls 2,
and Quick Dialogs. Tests also use Qt Test. On macOS 15+ with Homebrew:

```sh
brew install qtdeclarative
./bin/build
./bin/test
```

The tests verify exact pixel joins for cuts in both directions, invalid cuts,
undo/redo, red and green annotations, text, scaled mouse coordinates, clipboard
image dimensions, and failed image loads. They use Qt's offscreen platform, so
the test clipboard does not replace the system clipboard.

The output is `build/xshot.app` on macOS. QML is embedded through Qt resources;
the installed Qt libraries and QML modules are still needed at runtime. This is
a development build, not a standalone distribution bundle.

## Code

- `src/Main.qml`: toolbar, keyboard shortcuts, text entry, and session flow.
- `src/backend.cpp`: asynchronous macOS region capture and cancellation.
- `src/editorcanvas.cpp`: image display, gesture mapping, and clipboard access.
- `src/imagedocument.cpp`: full-resolution image edits and bounded undo history.
- `src/main.cpp`: application startup and the C++/QML bridge.
