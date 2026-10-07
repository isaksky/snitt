# Glimmer Qt lifetimes

This Jolt backend supports `:vbox`, `:hbox`, `:label`, `:entry`, and `:button`.
`glimmer.core/run` starts an application with one initial window, and
`glimmer.core/reload!` remounts that initial root. Closing the final window exits
this convenience runner by default; pass `:keep-alive true` for background use.
An application can be run again after normal closure or an exception. The hello-world entry point is
`./experiment/run`; `./experiment/run --smoke-test` remains available.

## Independent windows and background applications

`glimmer-qt.core/run-application!` takes a startup function and keyword options
`:app-id`, `:on-ready`, `:auto-quit-ms`, and `:keep-alive` (default true). Startup
runs on the GUI thread and can create zero or more windows and retained services.
The one Qt event loop runs until `quit!`; with `:keep-alive false`, accepting the
last editor close also quits. Hiding a window never quits the runtime.

```clojure
(qt/run-application!
  (fn []
    (let [editor (qt/open-window! editor-root :title "Editor")
          review (qt/open-window! review-root :title "Review" :visible false)]
      (qt/activate! editor)
      (qt/later! 1000 #(qt/show! review)))))
```

`open-window!` returns an opaque window handle. Each root has independent
subscriptions, callbacks, and reload state. `reload-window!` takes that handle
and an optional replacement root function. It uses Glimmer's public `mount`,
`unmount!`, and `abandon!` contract; no private reconciler state is inspected.
`find-by-id` in `glimmer-qt.core` takes `[window id]`. The widget namespace also
accepts `[mount-point id]`; its original unscoped arity rejects duplicate IDs.

Window commands are `show!`, `hide!`, `minimize!`, `restore!`, `raise!`, and
`request-activate!`. `activate!` combines show, minimized restoration, raise,
and an activation request (the desktop window manager controls focus).
`window-container`, `window-alive?`, and `window-count` expose lifetime state.
Window mutation and lookup require the GUI thread; use `later!` from workers.

`screens` returns current monitor descriptors with a session ID, name, logical
desktop `:geometry` and `:available` rectangles, `:device-pixel-ratio`, and
`:primary?`. Refresh the snapshot after display changes; an ID for a removed
monitor fails placement. `screen-containing` selects from a supplied snapshot
using absolute logical coordinates, including negative origins. `place-window!`
sets a chosen screen and an absolute `[x y width height]` rectangle in logical
coordinates. The application can use the reported DPR when it needs explicit
physical-pixel conversion. `open-window!` accepts `:screen` with `:x` and `:y`
for initial placement; minimized windows do not change the monitor's available
geometry.

Window style options are `:tool`, `:frameless`, `:topmost`,
`:input-transparent`, `:no-focus`, `:transparent-background`, and `:opacity`
(0–1). They can be supplied at creation or applied with `style-window!`.
Input-transparent windows also reject focus; omit that option for interactive
recording controls. `with-native-window!` synchronously lends a live `QWindow*`
to an application hook after Qt creates the platform window. The pointer must
not escape the callback; returning false or throwing propagates failure. The
application owns capture exclusion and platform policy. Native extensions can
use the C declaration in `native/window_hook.h`.

`:on-close (fn [window] ...)` is optional. Returning exactly `true` accepts the
request; any other result rejects/defers it. Without a callback, closure is
accepted. Native close events are consumed, and policy delivery is queued after
the event returns; pending duplicate requests coalesce. `request-close!` uses
that same event path, including hidden windows. Call `close-window!` to accept
later without invoking policy again. It is idempotent and queues destruction,
so even calls from control callbacks leave the native sender alive until return.
Logical invalidation cancels window-owned jobs; physical deletion is deferred.
Closing or reloading one root does not change other roots or retained services.

A window handle can own `later!`/`every!` jobs: they survive its root reload and
cancel at window disposal. Widget-owned jobs cancel with their widget subtree.
Unowned jobs and standalone services live until explicit removal or final quit.
`quit!` stops scheduling and leaves the event loop; the runner then unmounts all
roots and destroys native objects before returning. Callback errors abandon all
root subscriptions, perform the same cleanup, and rethrow outside C++.

Native factories may return an unparented QWidget for a retained service. Create
it with `widget/create!` during startup/callbacks, use the checked QObject API,
and release it with `widget/dispose!` or final quit. QWidget services are retained
explicitly without a QObject engine parent and cannot be children in a Qt Quick
composition. Their native QWidget descendants retain normal Qt ownership.

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
The public `glimmer.core/abandon!` operation takes an instance returned by
`mount`, immediately detaches subscriptions without native calls, and clears
that instance. It is idempotent. This explicit portable extension supports
independent backend-owned roots; their owner must release the native arena.

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
Each application session explicitly owns a relay, QML engine, independent
windows, retained QWidget services, and every created control, including
unattached controls. They are destroyed before `run` returns.
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

## Owned scheduling and playback clocks

`later!` takes `[delay-ms thunk]` or `[owner delay-ms thunk]`. It queues even for
zero delay and returns a cancellation token, or `nil` when the session has shut
down or the owner has been disposed. Omit the owner (or pass `nil`) for session
ownership. On the GUI thread, create a dedicated owner with `owner!` and destroy
it with `destroy-owner!`. A live widget handle can also own work; disposing the
widget or its ancestor cancels those jobs. Dedicated owners last until explicit
destruction or session closure; a root reload alone does not destroy them.

`every!` has the same arities but requires a positive interval. Its callback
receives `{:elapsed-seconds total :delta-seconds since-previous-tick}`. Both use
Qt's monotonic clock, starting when the native timer is installed on the GUI
thread. A delayed tick includes the real elapsed duration; missed ticks are not
replayed. This lets a playhead advance independently of decoded frames:

```clojure
(qt/every! owner 33
  (fn [{:keys [delta-seconds]}]
    (swap! playhead-seconds + delta-seconds)))
```

`restart!` resets an active token's delay and clock, optionally taking a new
delay. It returns `true` on success and `false` after completion/cancellation.
Canceled jobs release their closures, so resuming a stopped clock requires a
new `every!` call. `cancel!` is idempotent and may be called from the job itself.
It immediately removes the managed closure and native registration; Qt reclaims
the timer on its next event-loop turn or at session teardown. An invocation
already claimed for delivery may finish. Restart generations suppress stale
deliveries, including a tick racing with worker restart.

Scheduling, cancellation and restart are safe from workers; owner creation,
destruction and all widget operations remain GUI-thread-only. Worker admission
is fenced against native destruction and session shutdown. Native commands use
the relay as their Qt context, and no native lock is held across a managed
callback. Glimmer's existing per-component pending flag coalesces worker ratom
bursts through this same scheduler; there is no separate reconciler path.

Final application quit stops admission and cancels all remaining jobs. No job
survives into a later session. A scheduled exception follows the handler policy:
abandon subscriptions, exit, clean up, and rethrow from `run`. One collect-safe
foreign callable serves all events for a session, including every timer tick;
it is disconnected before being freed. Long event-loop FFI calls remain blocking
so worker threads and GC can progress.

## Verification

Use Jolt 0.8.16 or newer and Snitt's supported Qt SDK (macOS Qt 6.11.2/6.11.1;
Windows UCRT64 Qt 6.11.2). For example, from the repository root:

```sh
export SNITT_QMAKE="$PWD/build/tools/qt-sdk/6.11.2/macos/bin/qmake6"
./glimmer-qt/build-native
./glimmer-qt/test-lifecycle
./glimmer-qt/test-scheduling
./glimmer-qt/test-windows
./experiment/run --smoke-test
(cd glimmer-qt/vendor/glimmer && jolt -M:test)
```

The lifecycle runner defaults to the offscreen Qt platform. Set
`QT_QPA_PLATFORM=cocoa` for a macOS desktop run. Tests exercise real Qt signals,
object destruction, queued work, repeated reload/remount, invalid handles,
property resets, keyed movement, and creation/callback failures. They check both
logical handle counts and physical QObject destruction. They do not claim to
verify physical keyboard input, focus, or hit testing.

The scheduling runner has a 60-second process deadline in addition to per-session
Qt deadlines. It covers canceled/installed/restarted timers, owner destruction,
monotonic elapsed time during a stall, self-cancellation, callback exceptions,
concurrent worker posts and ratom changes under explicit GC, coalesced renders,
main-thread delivery, shutdown races, and zero retained jobs/timers after close.

The window runner exercises monitor snapshots, logical placement, overlay
styles, native hook success/failure and stale handles, plus independent roots
with duplicate control IDs,
per-window reload/close, close rejection and deferred acceptance, background
lifetime with zero editors, a real retained QWidget, failed creation/reload/close,
and full cleanup on quit. QtTest sends mouse and keyboard events through the
window for hit testing and focus routing. Run `QT_QPA_PLATFORM=cocoa ./glimmer-qt/test-windows` with desktop access to additionally verify activation
and minimized restoration on macOS. The Cocoa run separately checks that
topmost controls stack above an ordinary window and remain active while an
input-transparent outline is visible. Offscreen execution cannot prove desktop
focus or stacking. Windows execution is deferred for this change.

On Windows, build `native/bridge.pro` with UCRT64 `qmake6` and
`mingw32-make` from `glimmer-qt/build`, put that Qt SDK's `bin` on `PATH`, set
`QT_PLUGIN_PATH` and `QML_IMPORT_PATH` from the same SDK, and run
`QT_QPA_PLATFORM=offscreen jolt -M:test` from `glimmer-qt` (using PowerShell
syntax for environment variables). Run `jolt -M:scheduling-test` there as well,
with an external process deadline (for example, PowerShell `WaitForExit(60000)`)
to detect worker/GC deadlocks. The dependency declares both qmake's
`build/release/glimmer_qt.dll` and `build/glimmer_qt.dll` locations.
