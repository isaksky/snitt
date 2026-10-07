# Jolt Snitt development entry point

Run `./jolt-snitt/run [image-path | --show | --background | --quit]` from the repository root. This builds the
Glimmer Qt bridge and the separate Snitt native library, then starts a Jolt-owned
editor window. `./jolt-snitt/test-app` exercises the real canvas and retained
services offscreen, then builds and smoke-tests both `jolt build --dev` and the
default release build. `./jolt-snitt/build-app --dev`, `--release`, or `--all`
builds the corresponding standalone executables under `build/jolt-snitt/`.
Launch those from `jolt-snitt/` with the matching Qt SDK's `QT_PLUGIN_PATH` and
`QML_IMPORT_PATH` (as the scripts set them). The hello-world experiment remains
at `./experiment/run`; the C++ application remains at `./bin/run`.

This development process uses Qt application ID `snitt-jolt-development` and
its own user-scoped IPC identity. It never contacts `AppService::serverName()`
or the installed Snitt. A second Jolt invocation forwards its normalized
arguments to the running instance. The first invocation captures by default,
opens a path, shows the editor, or stays in the tray according to its arguments.
The app stays alive after its last editor closes; the tray and application hotkey
can create another editor. Its settings and default picture/video output live
below Qt's application-local data directory for that ID.
Set `SNITT_JOLT_DATA_DIR` to an absolute disposable directory for tests or an
isolated workspace. Set `SNITT_JOLT_IPC_ID` to a unique value when testing
multiple processes. Automated tests set `SNITT_JOLT_FAKE_HOTKEY=1` to exercise
hotkey dispatch without registering a system shortcut. The C++ production entry
point and its IPC identity are unchanged.

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

`./jolt-snitt/test-app` also runs startup/forwarding policy tests and launches
the release executable as several isolated processes. Those cases cover command
forwarding, stale and unresponsive sockets, and simultaneous startup using
temporary settings.

On an unlocked macOS desktop, `./jolt-snitt/test-desktop` checks the actual
menu-bar tray, its Show and Quit callbacks, and survival after closing the last
editor. `./jolt-snitt/test-capture-desktop` uses real screenshot acquisition and
region selectors, with test-controlled selection, to check capture routing on
every connected display and the captured pixel scale. Both use disposable
settings directories and fake global hotkeys.

`snitt.native/install!` registers `snitt-jolt.canvas` and
`snitt-jolt.services` with the generic Glimmer factory seam on the Qt thread.
The native library links the existing `EditorCanvas`, `AppSettings`, `Backend`,
and their C++ dependencies against the same shared `glimmer_qt` and Qt runtime.
`snitt.app` owns each editor's Glimmer root, toolbar, pointer commands, and
reactive state. `snitt.sessions` owns session identity, active-window selection,
capture admission, visibility snapshots, and teardown. Each session has its own
retained native services root and canvas. `snitt.native/create-services!` returns
that owned root plus borrowed settings/backend handles and a Jolt trim owner;
disposing the root invalidates its handles and terminates its media jobs. The native adapter retains captured pixels
before the backend removes its temporary file. Jolt advances each display capture,
then consumes or discards the retained result, positions the new editor, and
restores prior editor visibility on completion or failure. Canvas mount/unmount
owns its native document. Application namespaces call
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
against generated clips. The retained recorder/media helpers continue using these C++ regression gates. The text edit in the fixture is undone
before pixel comparison; font rendering is not treated as bit-identical across
platforms. Use `--skip-build` for a quick rerun after the binaries are current.

`snitt.recording` owns recording phases, controls, startup indicator, outline,
stop-before-readiness, cancellation, and generation fencing. The native adapter
retains the capture source and platform recorder only; it rejects starts until
all overlay windows have been prepared for exclusion. Windows verifies capture
affinity before start; macOS ScreenCaptureKit excludes the entire application
before streaming. Jolt restores editor visibility/focus once on completion and
opens the Jolt trim owner for a saved file. The original backend/QML owner
remains active only in the reference launcher.

Run `./jolt-snitt/test-recording` for fake-recorder transition and UI tests,
including protection failure and stale callbacks. On an unlocked macOS desktop,
`./jolt-snitt/test-recording-desktop` records a real region, stops, checks review
and restoration, cancels during a second startup, then probes and decodes the
generated MP4. Both use disposable settings/output roots and fake global hotkeys.

`snitt.trim` owns trim ranges, progress parsing, output verification policy,
replacement sequencing, filmstrip windows, EOF retries, and cancellation recovery.
Each process attempt has a distinct generation; queued results from retired jobs
are ignored. `MediaJobs` provides Qt-thread process events and narrow file/probe
operations. The MP4 duration editor and atomic platform replacement helper remain
native, shared with the reference C++ launcher.

Run `./jolt-snitt/test-trim` for generated audio/video, reordered-frame and short
clips, packet/timing checks, thumbnail windows and EOF fallback, cancellation,
process/probe/replacement failures, retry, teardown, and macOS held-file behavior.
`./jolt-snitt/test-review` exercises the review UI against the same Jolt owner.
