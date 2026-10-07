# Snitt native extension experiment

Run `./experiment/test-snitt-extensions` from the repository root. It builds the
application extension with the same Snitt C++ sources as the production target,
then runs Jolt and Qt offscreen with a disposable settings, image, and media
directory. The production `src/main.cpp` launcher remains separate.

`hello.snitt/install!` runs on glimmer-qt's existing QApplication thread before
the first render. It registers three application-owned factories. The bridge
owns each unparented factory result and invalidates its handle on unmount or
session close. The canvas is a real `EditorCanvas` mounted as a QQuickItem.
The services root owns isolated `AppSettings`, a `Backend` subclass, and its
real `TrimSession`. It exposes them as borrowed handles only after the root is
registered. Destroying the root disconnects subscriptions and invalidates all
three descendants. The selector factory returns a QWidget host with a real
`RegionSelector` child and isolated settings. It is retained by the same
application session, outside Qt Quick visual composition, until disposed.

| Native API | Jolt call |
| --- | --- |
| `EditorCanvas::load`, `begin`, `end`, `undo` | `hello.snitt/load-image!`, `drag!`, `undo!` |
| `AppSettings::reloadNow` | `hello.snitt/reload-settings!` on the services root |
| `TrimSession::open` | `hello.snitt/open-trim!` on the services root |
| QObject properties, invokables, and signals | `glimmer-qt.ffi` and `glimmer-qt.object` with explicit signatures |

The test's Backend override emits capture and recording signals without
touching the screen or encoder. Its real TrimSession opens a temporary dummy
file and rejects an invalid range, exercising `problem` and `progress`
notifications without exporting media. The services root connects a
`GlobalHotkey::activated` signal and a private `QLocalServer` socket to
GUI-thread signals observed by Jolt. The test fires the hotkey callback and
sends a local socket command, while leaving system-wide registration unused.
An application launcher can replace those fixture triggers with
`AppSettings::attachGlobalHotkey` and the `AppService` command policy on the
same QApplication. Native callbacks remain QObject-context connections;
destroying the service root removes them before the bridge closes the session.
