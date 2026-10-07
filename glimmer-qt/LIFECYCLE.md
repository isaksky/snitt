# Glimmer Qt lifetimes

This Jolt backend supports `:vbox`, `:hbox`, `:label`, `:entry`, and `:button`.
`glimmer.core/run` owns one session/window at a time. `glimmer.core/reload!`
remounts the current root inside that window. A session can be run again after
normal closure or an exception. The hello-world entry point is
`./experiment/run`; `./experiment/run --smoke-test` remains available.

## Explicit Glimmer contract extension

`vendor/glimmer` is an explicitly maintained copy of
`jolt-lang/glimmer` at `09b8ab08705fa1e6f20145c2e99eaaba9ea812cc`, with its MIT
license and upstream tests. `deps.edn` selects this local dependency deliberately;
the upstream revision alone does **not** supply the lifetime contract below.
The separate Glimmer reference checkout is not changed. These portable changes
are isolated here for review/upstream coordination, rather than monkey-patching
private reconciler state at runtime.

The optional backend `:run-lifecycle` operation receives options and a map of
three callbacks. Their return values are unspecified:

- `:mount!` takes a container and tag and mounts the root.
- `:unmount!` disposes the **current** root, including any reloads. Call it on the
  GUI thread before native teardown, in `finally`. Repeated calls are harmless.
- `:abandon!` immediately drops subscriptions without accessing native widgets,
  for unexpected native destruction. The backend must then close its arena.

Legacy `:run` backends retain their existing mount callback contract. The core
also detaches their subscriptions when `run` returns, but such backends need the
explicit seam to unmount native objects before destroying their containers.
`:dispose!` is the optional backend operation to release a just-created widget
whose attachment failed.

Component subscription scopes detach **all** tracked reactive cells immediately
on unmount, replacement, reload, or failed mount. Re-rendering refreshes the
scope, dropping dependencies no longer read. This uses the public
`IReactiveCell` protocol, including custom cell implementations. Queued watcher
work checks disposal before rendering, and queued reloads belong to their
original run. Newly allocated child slots are recorded before rendering so
partial creation can be unwound. Form-2 state resets on component replacement;
rendering `nil` removes the previous child. These changes do not dispose
application-owned ratoms, cursors, or reactions; their independent lifetimes
remain with the application.

## Native and managed ownership

There is one process-owned `QApplication`, created on the main thread. It remains
available across sessions. It is reclaimed by the OS at process exit: Jolt may
exit from a worker, where a static C++ QApplication destructor is unsafe.
Each session explicitly owns a relay, QML engine, window, and every created
control, including unattached controls. They are destroyed before `run` returns.
There is no second Qt event loop or application object.

Session and object handles are monotonically allocated `uint64` identities,
never raw addresses and never reused across sessions. The native registry uses
`QPointer` and observes `QObject::destroyed`. Checked FFI operations throw managed
errors for invalid/disposed handles and reject off-thread calls. `alive?` is an
explicit lifetime probe. The binding additionally validates tags, parentage,
container rules, and property types before writes. A handle is valid only for
its owning session; `find-by-id` does not extend its lifetime.

Subtree removal immediately invalidates its handles and clears every stored
handler. Physical deletion is deferred until Qt can safely return from the
current signal; the engine still owns those objects until deletion or session
closure. Keyed children retain identity and move in place. Tag replacement
creates a fresh handle and disposes the previous subtree. Prop removal restores
documented defaults from the widget spec; handler replacement changes the
single current managed handler. Controlled text is reapplied even when the
prop has not changed, while equal native writes preserve cursor/selection.

Unexpected native destruction of a registered widget or mount point fails the
session closed: subscriptions are abandoned immediately and the remaining
arena is released. A handler exception follows the same policy and is rethrown
from `run` after cleanup; it never unwinds through C++. Direct session closure
while executing a callback/event loop is rejected; use `quit!` or window close.

`later!` returns a cancellation token (or `nil` after close). `cancel!` releases
the managed thunk immediately. Queued native timers are inert after cancel;
Qt cancels them when their relay dies. Worker posts include the session identity,
so a race with close cannot deliver old work to a later session. Window close
cancels all remaining work and releases the native callback before returning.

## Verification

Use Jolt 0.8.16 or newer and Snitt's supported Qt SDK (macOS Qt 6.11.2/6.11.1;
Windows UCRT64 Qt 6.11.2). For example, from the repository root:

```sh
export SNITT_QMAKE="$PWD/build/tools/qt-sdk/6.11.2/macos/bin/qmake6"
./glimmer-qt/build-native
./glimmer-qt/test-lifecycle
./experiment/run --smoke-test
(cd glimmer-qt/vendor/glimmer && jolt -M:test)
```

The lifecycle runner defaults to the offscreen Qt platform. Set
`QT_QPA_PLATFORM=cocoa` for a macOS desktop run. Tests exercise real Qt signals,
object destruction, queued work, repeated reload/remount, invalid handles,
property resets, keyed movement, and creation/callback failures. They check both
logical handle counts and physical QObject destruction. They do not claim to
verify physical keyboard input, focus, or hit testing.

On Windows, build `native/bridge.pro` with UCRT64 `qmake6` and
`mingw32-make` from `glimmer-qt/build`, put that Qt SDK's `bin` on `PATH`, set
`QT_PLUGIN_PATH` and `QML_IMPORT_PATH` from the same SDK, and run
`QT_QPA_PLATFORM=offscreen jolt -M:test` from `glimmer-qt` (using PowerShell
syntax for environment variables). The dependency declares both qmake's
`build/release/glimmer_qt.dll` and `build/glimmer_qt.dll` locations.
