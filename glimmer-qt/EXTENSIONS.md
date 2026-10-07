# Qt control extensions

`glimmer-qt.widget/register!` registers application tags before `ui/run`.
Built-in controls use this same API. Registrations are process-scoped and cannot
be replaced or changed during a session. Use namespaced tags to avoid collisions.

```clojure
(w/register! :app/spinner
  {:source "qrc:/app/Spinner.qml"
   :props {:id ["objectName" :string ""]
           :angle ["angle" :number 0]}
   :required #{:angle}
   :events {:on-activate :string}
   :relay "relay"
   :children "content"
   :dispose #(release-application-state!)})
```

Each property declares its Qt name, type (`:string`, `:number`, `:bool`, or
`:color` string), and reset default. Required keys must be supplied on every
render. Unknown keys and invalid values fail; Qt metadata checks writability,
conversion and numeric ranges. Defaults and supplied values are applied before
QML component completion, including grouped properties such as `font.pixelSize`.
Creation errors include the component URL and Qt diagnostics; partial objects
are destroyed. A required QML property must be represented by the schema or the
injected relay. Completion-time events happen before publication of the handle
and are not delivered as user events.

Sources can be absolute `file:/` URLs or `qrc:/` resources provided by an
application library. Such QML may import application types registered with
`qmlRegisterType` before its creation. Keep the resource/type library loaded.
The backend does not search an application-specific directory or infer filenames
from custom tag names.

Events explicitly declare either `:none` (zero arguments) or `:string` (one text
argument). QML declares the relay property and calls
`relay.dispatch(control, "on-activate", text)`. Native controls can declare a
QObject relay property and invoke its `dispatch(QObject*,QString,QString)`
method on the GUI thread. Only declared handlers are delivered; each render
refreshes closures. Scheduling and lifecycle event names are reserved. Native
connections should use the control as their QObject context so deletion
disconnects them. Late events for invalidated handles are ignored. Managed
exceptions are caught by the session callback and stop the session safely.

## Native factories and ownership

Application native libraries link against the same `glimmer_qt` shared library
and include `native/extensions.h`. On the Qt thread, register a unique factory
name with `gqt_register_factory(name, create, complete)`, then use
`:source "native:app.factory"` in a managed descriptor. Registration returns
zero with `gqt_error()` on failure. The example registers during the first root
render, before its native elements are created.

`create` returns a fresh, unparented QObject or QQuickItem. The bridge owns it,
writes schema properties, then calls the optional `complete(QObject*)` hook.
Hooks must not throw, destroy the object, or enter a nested event loop. They can
initialize native services after required values are available. Factories remain
registered across sessions, so register once and keep their library loaded.
Use destructors/QObject-context connections for native resource cleanup.

`:dispose` is a zero-argument managed notification called once per instance,
including subtree removal, replacement, native destruction, and session cleanup.
It must not assume the native handle is still usable. A throwing hook does not
prevent the remaining subtree from being released. Handles are invalidated
immediately; physical deletion may be deferred until signal delivery finishes.

## Composition

`:children :none` is the default. `:self` parents children to the root.
A string such as `"content"` selects a QQuickItem property referencing an owned
descendant. This composition target is resolved once during creation and must
stay alive for the control's lifetime. It can point to a background or content
item; separate wrapper tags can expose different composition policies for the
same QML source. Arbitrary external objects cannot become child targets.

Glimmer retains logical ownership of children while the bridge parents them to
the composition target. Keyed reuse, order changes, replacement and recursive
disposal work through the normal backend contract. QObject children have no
visual stacking order; visual children require a QQuickItem parent.

## Application example and verification

`experiment/src/hello/extensions.clj`, `experiment/qml/Spinner.qml`, and
`experiment/native/extensions.cpp` provide a QML spinner with a content slot,
a native item with a completion hook, and a nonvisual QObject factory. The QML
resource is compiled into the application extension library, not the backend.

From the repository root:

```sh
./experiment/test-extensions
QT_QPA_PLATFORM=cocoa ./experiment/test-extensions --demo
```

The first command runs automated Qt integration checks offscreen. The demo
rotates the spinner using an owner-scoped repeating job. Offscreen signal and
lifetime checks do not establish physical keyboard/mouse behavior. Validation
for this change is local macOS only; Windows execution is deferred.
