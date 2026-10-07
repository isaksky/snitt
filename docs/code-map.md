# Code map

- [jolt-snitt/src/snitt/app.clj](../jolt-snitt/src/snitt/app.clj): production UI, editor commands, tray, and launch dispatch.
- [jolt-snitt/src/snitt/lifecycle.clj](../jolt-snitt/src/snitt/lifecycle.clj): CLI command policy and path normalization.
- [jolt-snitt/src/snitt/sessions.clj](../jolt-snitt/src/snitt/sessions.clj): editor lifetime, capture routing, and visibility restoration.
- [jolt-snitt/src/snitt/document.clj](../jolt-snitt/src/snitt/document.clj): document commands and bounded undo history.
- [jolt-snitt/src/snitt/recording.clj](../jolt-snitt/src/snitt/recording.clj): recording state and safe completion.
- [jolt-snitt/src/snitt/trim.clj](../jolt-snitt/src/snitt/trim.clj): review and trim orchestration.
- [jolt-snitt/native/snitt_jolt.cpp](../jolt-snitt/native/snitt_jolt.cpp): narrow native services and image handle bridge.
- [jolt-snitt/native/package_launcher.c](../jolt-snitt/native/package_launcher.c): packaged Jolt entry point and bundled Qt paths.
- [src/globalhotkey.cpp](../src/globalhotkey.cpp): native Windows/macOS global hotkey registration.
- [src/backend.cpp](../src/backend.cpp): platform screenshot and recorder adapters.
- [src/macrecorder.mm](../src/macrecorder.mm): native macOS ScreenCaptureKit recording.
- [src/videorecorder.cpp](../src/videorecorder.cpp): Windows FFmpeg recorder adapter.
- [src/regionselector.cpp](../src/regionselector.cpp): native capture acquisition support.
- [src/editorcanvas.cpp](../src/editorcanvas.cpp): image rendering and clipboard bridge.
- [src/imagedocument.cpp](../src/imagedocument.cpp): retained C++ parity reference and native image operations.

`src/main.cpp`, `src/Main.qml`, `src/appservice.cpp`, and `src/editorsessions.cpp`
remain as the Windows build and C++ parity reference. They are not macOS launch
targets.

See [development](development.md) for build and test instructions.
