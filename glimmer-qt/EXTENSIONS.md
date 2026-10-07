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

## Images and application resources

The built-in `:image` displays a `glimmer-qt.value/url` such as
`(value/url "qrc:/icons/check.svg")` or a `value/local-file` from an unescaped
filesystem path. Use `value/local-file` for paths with spaces, `#`, or Unicode;
passing a raw path or a manually assembled `file:` URL loses Qt's path encoding.
Compile application SVGs and other resources into the application's own qrc
library. Keep that library loaded and register its resources before rendering.
No Snitt icons are linked into `glimmer_qt`.

`:source-width` and `:source-height` bound image decoding in physical pixels;
omit them to retain Qt's intrinsic size and `@2x` logical sizing. Both must be
positive integers at most 32768. `:fill-mode` is `"fit"` (default), `"crop"`,
`"stretch"`, or `"pad"`. `:asynchronous` and `:cache` default to true; thumbnail
filmstrips should pass bounded source dimensions, `:asynchronous true`, and
`:cache false`. The image is hidden while loading or after a failure, so a changed
source cannot show the old thumbnail. Standard layout hints and item geometry
apply to `:image`.

`"loadState"` reads as `"null"`, `"loading"`, `"ready"`, or `"error"` through
`glimmer-qt.ffi/read-property`. `"loadError"` includes the failing source URL.
`:on-status` receives each status string, `:on-loaded` receives no arguments,
and `:on-error` receives the error string. An owned zero-delay timer reports
initial synchronous completion after the handle is published. Unmounting drops
the timer, events, image object, and native handle.

Native extensions can register an `image://` provider with
`gqt_register_image_provider` in `native/extensions.h` on the GUI thread during
an active session. Registration is per session and must happen before a URL uses
the provider. The callback may run on a Qt loader thread for asynchronous images;
it must be thread-safe, avoid GUI objects and Jolt, and return a `QImage` that
owns or retains its pixel storage. For an external buffer, use `QImage`'s cleanup
callback rather than returning a view of temporary memory. Qt retains implicit
image copies through rendering; the provider's release callback runs when the
session engine is destroyed. Keep the extension library loaded through that
point. The application fixture in `experiment/native/extensions.cpp` demonstrates
the ownership pattern; `./experiment/test-images` verifies its release.

## Native factories and ownership

Application native libraries link against the same `glimmer_qt` shared library
and include `native/extensions.h`. On the Qt thread, register a unique factory
name with `gqt_register_factory(name, create, complete)`, then use
`:source "native:app.factory"` in a managed descriptor. Registration returns
zero with `gqt_error()` on failure. The example registers during the first root
render, before its native elements are created.

`create` returns a fresh, unparented QObject, QQuickItem, or standalone QWidget.
QWidget services are retained explicitly until disposal or application quit;
they cannot be composed as children in a Qt Quick tree. The bridge owns it,
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

## Layout and visual primitives

`:vbox` and `:hbox` own their children's geometry through Qt layouts. `:flow`
wraps controls at the available width, with an implicit height that follows its
rows. All three accept `:spacing`, `:padding`, and per-edge `:padding-left`,
`:padding-right`, `:padding-top`, `:padding-bottom`. Omitted edges inherit padding.
Their internal child target is owned by the container; child coordinates are
relative to that padded target. Flow children use intrinsic sizes (or explicit
sizes on primitives); Layout hints on them are rejected. Give the toolbar enough
width for its widest individual control; Flow does not shrink or split controls.

Layout children accept `:hexpand`, `:vexpand`, `:min-width`, `:min-height`,
`:preferred-width`, `:preferred-height`, `:max-width`, `:max-height`, `:margin` and
per-edge margins. Omitted margin edges inherit `:margin`. `:alignment` takes
`(glimmer-qt.value/flags "AlignRight" "AlignBottom")`, for example. Omit size hints
to restore intrinsic sizing; default maxima are 16777215. Dimensions and spacing
must be nonnegative; fractional values are preserved.

`:item` and `:overlay` are transparent, non-layout containers; `:rectangle` adds
`:color`, `:radius`, `:border-width`, and `:border-color`. `:spacer` is a leaf with
implicit dimensions, useful with `:hexpand`/`:vexpand` in boxes. These primitives
accept `:implicit-width` and `:implicit-height` (default zero). Non-layout
containers deliberately do not derive their size from children, avoiding cycles
between parent sizing and child anchors.

Inside a non-layout container, primitives accept fractional pixel `:x`, `:y`,
`:width`, `:height`, and parent-relative `:relative-x`, `:relative-y`,
`:relative-width`, `:relative-height` (1.0 means the parent's full dimension).
Offsets add to relative positions. `:anchor "fill"` fills the parent;
`:anchor "center"` centers the child's dimensions with optional offsets;
omitting the anchor restores normal positioning. The stable local reference is
the owned visual parent, so references cannot outlive it, cross windows, or form
sibling cycles. Nest an item to establish another local coordinate system.
Absolute and relative dimensions on the same axis are rejected, as are fill
anchors with explicit geometry. Layout parents reject explicit positions,
anchors and sizes; their primitive children's positioning bindings are disabled.
Non-layout parents reject Layout hints rather than silently ignoring them.

```clojure
[:vbox {:hexpand true :vexpand true :padding 8}
 [:flow {:hexpand true :spacing 4}
  [:button {:label "Arrow"}]
  [:button {:label "Text"}]]
 [:overlay {:hexpand true :vexpand true :clip true}
  [:rectangle {:anchor "fill" :color "#223344"}]
  [:item {:relative-x 0.25 :relative-y 0.25
          :relative-width 0.5 :relative-height 0.5 :z 2}
   [:label {:label "Annotation"}]]]]
```

All built-ins accept `:clip`, `:opacity` (0–1), and `:z`. Equal-z children follow
reconciled visual order. Controls retain their intrinsic sizes; compose them
inside positioned items when an overlay needs controls. Geometry notifications
use `:qt/subscriptions` with `:property "width"`, `"height"`, `"x"`, `"y"`,
`"implicitWidth"`, or `"implicitHeight"`, and inherit the existing queued delivery
and disposal semantics. Do not write layout-owned geometry from observers.

Run `./glimmer-qt/test-layout` for offscreen geometry/reconciliation checks, or
`QT_QPA_PLATFORM=cocoa ./glimmer-qt/test-layout` for the macOS desktop backend.
Select the repository Qt SDK with `SNITT_QMAKE` as in `docs/development.md`.
