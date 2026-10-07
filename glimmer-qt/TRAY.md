# Native tray and menu

`glimmer-qt.tray` owns one `QSystemTrayIcon` and its native `QMenu` per `create!`
call. Create them inside `run-application!` on the GUI thread. The application
runner disposes remaining trays, actions, icons and callbacks on shutdown.
`destroy!` can release one earlier. Handles become invalid after disposal.

```clojure
(require '[glimmer-qt.core :as qt]
         '[glimmer-qt.tray :as tray])

(qt/run-application!
 (fn []
   (let [icon (tray/create! "/absolute/path/to/icon.png" :tooltip "Capture")]
     (tray/add-action! icon :label "Capture region" :shortcut-hint "⌘⇧5"
                       :on-trigger capture!)
     (tray/add-action! icon :label "Open editor" :on-trigger show-editor!)
     (tray/add-separator! icon)
     (tray/add-action! icon :label "Quit" :on-trigger qt/quit!)
     (tray/show! icon))))
```

`show!` throws when the desktop has no system tray; `available?` supports an
earlier check. `notify!` throws when desktop notifications are unsupported.
`update-action!` changes label, shortcut hint, enabled/checkable/checked state,
or callback in place. The hint is display text and does not register a global
shortcut. `set-handlers!` replaces tray activation and notification-click
callbacks. Activation receives `:context`, `:double-click`, `:trigger`,
`:middle-click` or `:unknown`. Tray actions, like other Qt mutations, must run
on the GUI thread; use `qt/later!` to schedule work from a worker.

Run `./test-tray` for offscreen native/model checks. On macOS, run
`SNITT_TRAY_DESKTOP_CHECK=1 QT_QPA_PLATFORM=cocoa ./test-tray` to verify the
actual menu-bar tray and popup menu. The short-lived manual demo is
`QT_QPA_PLATFORM=cocoa jolt -A:tray-test -m glimmer-qt.tray-desktop`.
