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

`snitt.settings` parses and validates the INI, owns the published settings
snapshot, and applies accepted values to every editor session. The native
`AppSettings` object runs in externally controlled mode for this launcher: it
provides isolated paths, default-file creation, file/directory watching, Qt
shortcut spelling, and a projection for retained native consumers. The native
global hotkey adapter registers a candidate before Jolt publishes it, so failed
registration keeps the previous settings and shortcut active. Editor shortcut
labels, key bindings, colors, save roots, and tray hints follow the same Jolt
snapshot. The original C++ entry point continues using its existing settings
loader until the application cutover.

Run `./jolt-snitt/test-settings` for isolated macOS settings cases, including
ordinary saves, atomic replacement, deletion/recreation, validation errors,
multi-editor projection, and native hotkey rollback.

`snitt.native/install!` registers `snitt-jolt.canvas` and
`snitt-jolt.services` with the generic Glimmer factory seam on the Qt thread.
The native library links the existing `EditorCanvas`, `AppSettings`, `Backend`,
and their C++ dependencies against the same shared `glimmer_qt` and Qt runtime.
`snitt.app` owns each editor's Glimmer root, toolbar, pointer commands, and
reactive state. `snitt.sessions` owns session identity, active-window selection,
capture admission, visibility snapshots, and teardown. Each session has its own
retained native services root and canvas. `snitt.native/create-services!` returns
that owned root plus borrowed settings, backend, and trim handles; disposing the
root invalidates all borrowed handles. The native adapter loads temporary capture
files into the session's canvas before the backend removes them, while Jolt owns
the decision to show or close that editor. Canvas mount/unmount owns its native
document. Application namespaces call
`snitt.native` methods and `glimmer-qt.object` with explicit Qt signatures;
image pixels and undo history stay in `EditorCanvas`, never in reactive state.

`snitt.commands` decides which editor commands are available from a live session
snapshot and returns effect requests. Toolbar controls, Qt shortcuts, and callers
using `snitt.app/dispatch!` share that policy. `dispatch!` rereads native canvas
and backend properties before every effect. Settings supplies Qt-normalized key
sequences and hints; removing or replacing a binding updates the mounted shortcut
and its label. Run `jolt -M:commands` from `jolt-snitt/` for the pure context tests;
`./jolt-snitt/test-app` also runs these and drives representative keys through Qt.

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
