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

Each property declares its Qt name, type (`:string`, `:number`, `:bool`,
`:color` string, or `:value`), and reset default. `:value` accepts the typed
values described in [VALUES.md](VALUES.md), including object references, URLs
and containers. Required keys must be supplied on every
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
A string such as `"content"` selects a QObject property referencing an owned
descendant. This composition target is resolved once during creation and must
stay alive for the control's lifetime. It can point to a background or content
item; separate wrapper tags can expose different composition policies for the
same QML source. Arbitrary external objects cannot become child targets.

Glimmer retains logical ownership of children while the bridge parents them to
the composition target. Keyed reuse, order changes, replacement and recursive
disposal work through the normal backend contract. QObject children have no
visual stacking order; visual children require a QQuickItem parent.

## QObject methods and subscriptions

`glimmer-qt.object/invoke!` takes a live target handle, an explicit Qt method
signature, and an argument vector. It selects a public invokable method or slot;
signals, missing signatures, wrong arity, unsupported types and invalid values
fail before invocation. Signatures use Qt's normalized metadata names (for
example, `qlonglong` for `qint64`); overloads are never guessed from values.

```clojure
(require '[glimmer-qt.object :as object]
         '[glimmer-qt.value :as value])

(object/invoke! trim "exportRange(qlonglong,qlonglong)" [1000 5000])
(object/invoke! canvas "load(QUrl)" [(value/local-file image-path)])
```

The supported method/signal subset is up to ten arguments of `bool`, `int`,
`uint`, `qlonglong`, `double`, `QString`, `QStringList`, `QUrl`, `QColor`,
`QPoint`/`QSize`/`QRect` and their floating variants, `QVariant`, `QVariantList`,
`QVariantMap`, and registered QObject pointer types. Returns use those same
types or `void` (nil). Numeric ranges, object classes, and recursive values use
the property marshaller's checks. Mutable reference parameters, enum/flag method
arguments, arbitrary custom metatypes and automatic argument coercion are outside
this subset. A QVariant with an unsupported runtime value fails at serialization.
Native exceptions become managed errors; a method's side effects cannot be
rolled back if its implementation throws or returns an unsupported runtime value.

Calls and subscription registration require the GUI thread and GUI-affine
objects. Invocation is synchronous and GC-safe. Keep methods short, use native
asynchronous services for long work, and do not enter nested event loops.

Every registered tag accepts a `:qt/subscriptions` map. Its keys identify
connections independently of the callback closure. Each descriptor has exactly
one of `:signal` (full signature) or `:property` (direct property name with
readable NOTIFY metadata), a `:handler`, and optional `:target` (defaults to the
mounted object). Application callbacks can update Glimmer reactive atoms:

```clojure
[:app/capture-service
 {:qt/subscriptions
  {:busy {:property "capturing" :handler #(reset! capturing? %)}
   :images {:signal "regionsCaptured(QVariantList)"
            :handler #(reset! captured-images %)}
   :review {:target trim :property "progress"
            :handler #(reset! export-progress %)}}}]
```

Signal handlers receive positional typed arguments. Property handlers receive
one current value; read the initial value with `ffi/read-property`. Notification
bursts may read the same final value more than once. For transition history,
subscribe to a signal that carries the value. Grouped property paths require an
explicit handle for their owning QObject.

Renders refresh handlers without reconnecting. Changing a descriptor's target,
signal or property disconnects the previous subscription; removing its key or
unmounting its owner does the same. Already queued deliveries are canceled.
Controlled property writes during rendering suppress this object's subscriptions and string relay events
for signals emitted synchronously by its setter, preventing notification feedback.
Imperative `ffi/set-property!` and native method calls remain observable. A service
that emits delayed echoes must identify/deduplicate them in its application policy.

For imperative ownership, use `subscribe! owner target signature handler` or
`observe! owner target property handler`. Both return an opaque token.
`update-handler!` refreshes a live token; `disconnect!` is idempotent. Owners may
be widgets, registered service handles, or `core/owner!` handles. Destroying either
owner or target cancels the subscription once and releases its managed handler.
These APIs all run on the GUI thread; they do not create another event loop.

Native signals may originate on workers. The bridge captures their values and
registered object identities without entering Jolt, then queues delivery onto
the GUI thread through the session's collect-safe callback. A disposed or reused
object reference produces a managed error, never a new object identity. Handler
errors and payload errors stop the session through the normal cleanup path.
One small native capture receiver per sender/signature stays with the sender
until its destruction, even between subscriptions, to protect captures already
executing on workers. It holds no managed closure; the underlying Qt connection
is disconnected when its last subscriber is removed. Native services must stop
and join signal producers before destroying the sender or its referenced objects.

## Native-owned object references

Assigning `value/object` to an object-valued property never reparents or transfers
ownership. Expose a native-created descendant by calling
`gqt_expose_object(QObject*)` on the GUI thread after its ancestor is registered
(for example, from an initialization method invoked after mounting). The native
extension must supply a live pointer; unregistered external objects are rejected.
Exposure returns a stable handle, and repeated exposure returns that same handle.
Native method returns, signal payloads and property reads can then carry the
reference. Unknown pointers fail instead of silently adopting a native service.

Exposed descendants and composition targets are borrowed: bridge removal,
owner destruction, `deleteLater()` invocation and reparenting APIs reject them. Their native parent retains
ownership. Native deletion or ancestor disposal invalidates their handles and
subscriptions. Native pointer properties must follow Qt's lifetime rules (use
`QPointer` for references that can be deleted independently). A registered service
must remain GUI-affine throughout its lifetime.

## Application example and verification

`experiment/src/hello/extensions.clj`, `experiment/qml/Spinner.qml`, and
`experiment/native/extensions.cpp` provide a QML spinner with a content slot,
a native item with a completion hook, and a nonvisual QObject factory. The QML
resource is compiled into the application extension library, not the backend.

From the repository root:

```sh
./experiment/test-extensions
./glimmer-qt/test-objects
QT_QPA_PLATFORM=cocoa ./experiment/test-extensions --demo
```

The first two commands run automated Qt integration checks offscreen. The object
suite uses a test-only native service to check overloads, typed values, reactive
notifications, worker signals, cancellation and borrowed lifetimes. It includes
an external deadline for worker/GC failures. The demo
rotates the spinner using an owner-scoped repeating job. Offscreen signal and
lifetime checks do not establish physical keyboard/mouse behavior. Validation
for this change is local macOS only; Windows execution is deferred.
