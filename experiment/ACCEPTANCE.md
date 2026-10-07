# Snitt binding acceptance fixture

Run `./experiment/test-acceptance` from the repository root. It builds the
test-enabled Glimmer Qt bridge and Snitt extension, creates disposable image and
three-second MP4 fixtures, then drives `hello.acceptance` through one Qt event
loop. A failed assertion or timeout exits nonzero. `QT_QPA_PLATFORM=cocoa
SNITT_ACCEPTANCE_SCREENSHOT=/absolute/path.png ./experiment/test-acceptance`
runs the same sequence on the macOS desktop and captures the first editor.
`./experiment/run --smoke-test` remains the separate hello smoke command.

| Exercised capability | Snitt behavior reference | Acceptance checks |
| --- | --- | --- |
| Two independent editor roots, native canvases and image edits | `src/Main.qml`, `tests/editor_tests.cpp` | Isolated image load/cut, reactive toolbar commands, independent annotation and repeat window reload/close |
| Responsive annotation toolbar and multiline text | `src/Main.qml` annotation toolbar, `tests/editor_tests.cpp` | Narrow-window Flow wrap, real paste and keyboard activation, controlled annotation state |
| Confirm/file dialogs and capture overlay | `src/Main.qml` dialogs/capture flow | Pointer accept/reject, nonblocking file result, topmost input-transparent overlay and shared `RegionSelector` QWidget |
| Review filmstrip, trim handles, playhead and playback | `src/Main.qml`, `src/TrimBar.qml`, `tests/recording_tests.cpp` | Four image tiles, real handle and seek drags, local MP4 load/play/pause/first frame |
| Progress, error, cancel and background lifetime | `src/Main.qml`, `tests/recording_tests.cpp`, `src/appservice.cpp` | Worker-posted review update, cancel control, native tray action, zero-window lifetime and tray reopening, canceled owned timer and zero live handles after quit |

On 2026-10-07, the local macOS offscreen run passed 91 assertions. The Cocoa
desktop run passed 94 assertions, including a saved window image at
[`evidence/acceptance-macos.png`](evidence/acceptance-macos.png). The desktop
run exercised native windows, tray availability, Qt input delivery, and video
decoding. Windows was not tested, as requested.

This is a binding acceptance fixture. The filmstrip repeats a generated still
image; it does not call asynchronous `TrimSession` thumbnail extraction. Export
progress and error are controlled fixture state, and the Snitt adapter uses a
fake capture backend, so this command does not prove real screen capture,
recording, or encoding. The file chooser result is injected into the live Qt
dialog by the test helper; a human did not navigate a platform file panel.
