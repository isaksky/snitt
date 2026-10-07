# Jolt Snitt development entry point

Run `./jolt-snitt/run [image-path]` from the repository root. This builds the
Glimmer Qt bridge and the separate Snitt native library, then starts a Jolt-owned
editor window. `./jolt-snitt/test-app` exercises the real canvas and retained
services offscreen, then builds and smoke-tests both `jolt build --dev` and the
default release build. `./jolt-snitt/build-app --dev`, `--release`, or `--all`
builds the corresponding standalone executables under `build/jolt-snitt/`.
Launch those from `jolt-snitt/` with the matching Qt SDK's `QT_PLUGIN_PATH` and
`QML_IMPORT_PATH` (as the scripts set them). The hello-world experiment remains
at `./experiment/run`; the C++ application remains at `./bin/run`.

This development process uses Qt application ID `snitt-jolt-development`. It
does not contact `AppService::serverName()`, install the production hotkey/tray,
or forward commands to the installed Snitt. Its settings and default picture/
video output live below Qt's application-local data directory for that ID.
Set `SNITT_JOLT_DATA_DIR` to an absolute disposable directory for tests or an
isolated workspace. The C++ production entry point and its IPC identity are
unchanged.

`snitt.native/install!` registers `snitt-jolt.canvas` and
`snitt-jolt.services` with the generic Glimmer factory seam on the Qt thread.
The native library links the existing `EditorCanvas`, `AppSettings`, `Backend`,
and their C++ dependencies against the same shared `glimmer_qt` and Qt runtime.
`snitt.app` owns the window, toolbar, pointer commands, and one retained services
root. `snitt.native/create-services!` returns that owned root plus borrowed
settings, backend, and trim handles; `widget/dispose!` on the root invalidates all
borrowed handles. On session close, Glimmer also destroys any retained root.
Canvas mount/unmount owns its native document. Application namespaces call
`snitt.native` methods and `glimmer-qt.object` with explicit Qt signatures;
image pixels and undo history stay in `EditorCanvas`, never in reactive state.

Only local macOS validation is in scope for this development launcher. Windows
build and execution remain deferred.

Run `python3 tests/parity.py` from the repository root to rebuild the local
test binaries and compare the shared editor fixture through C++ and the Jolt
bridge. `tests/parity-fixtures.json` records commands and expected state; the
runner generates images in separate temporary roots, compares every state and
saved image pixel buffer, and prints the differing command and field on failure.
It also runs the retained recording cancellation and failed replacement tests
against generated clips. Recording has not migrated to Jolt yet, so those media
checks remain a C++ regression gate. The text edit in the fixture is undone
before pixel comparison; font rendering is not treated as bit-identical across
platforms. Use `--skip-build` for a quick rerun after the binaries are current.
