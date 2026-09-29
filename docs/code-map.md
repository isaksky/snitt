# Code map

- [src/Main.qml](../src/Main.qml): editing controls, shortcuts, and show/hide session flow.
- [src/appservice.cpp](../src/appservice.cpp): tray menu and single-instance local IPC.
- [src/globalhotkey.cpp](../src/globalhotkey.cpp): native Windows/macOS global hotkey registration.
- [src/backend.cpp](../src/backend.cpp): platform capture lifecycle.
- [src/macrecorder.mm](../src/macrecorder.mm): native macOS ScreenCaptureKit recording and saved-file lifecycle.
- [src/videorecorder.cpp](../src/videorecorder.cpp): Windows FFmpeg discovery, recording, and saved-file lifecycle.
- [src/regionselector.cpp](../src/regionselector.cpp): single- and multiple-region selection overlay.
- [src/editorcanvas.cpp](../src/editorcanvas.cpp): image display, gesture mapping, and clipboard access.
- [src/imagedocument.cpp](../src/imagedocument.cpp): full-resolution image edits and bounded undo history.

See [development](development.md) for build and test instructions.
