# Typed Qt properties

`glimmer-qt.ffi/set-property!` and `read-property` operate on live managed object
handles on the GUI thread. Reads return values, not display strings. Existing
`set-string`, `set-number` and integer-boolean `set-bool` callers use the same
marshaller; numeric callers no longer coerce integers through double.

```clojure
(require '[glimmer-qt.ffi :as q] '[glimmer-qt.value :as v])
(q/set-property! object "duration" 9223372036854775807)
(q/set-property! object "source" (v/local-file "/tmp/日本語 # ?.png"))
(q/read-property object "source") ; Value with :url and fully encoded URL
(q/local-file-path (q/read-property object "source")) ; original local path
(q/set-property! object "rectF" (v/rect-f -1920.25 -75.5 640.0 480.125))
```

The accepted vocabulary is deliberately small:

| Managed value | Qt representation |
| --- | --- |
| boolean, string | bool, QString (including Unicode and embedded NUL) |
| integer in signed 64-bit range | qint64; checked narrowing to int/uint |
| finite floating point | double |
| vector, string-keyed map | QVariantList, QVariantMap, recursively |
| vector of strings for QStringList property | QStringList |
| `v/color` | QColor from a valid color string; reads canonical `#aarrggbb` |
| `v/url`, `v/local-file` | QUrl; reads a fully encoded `v/url` |
| `v/point`, `v/size`, `v/rect` | QPoint, QSize, QRect with int32 components |
| `v/point-f`, `v/size-f`, `v/rect-f` | QPointF, QSizeF, QRectF |
| `v/enum "Busy"`, `v/flags "First" "Second"` | Property-declared enum/flags, validated symbolic keys |
| `v/object handle`, nil | Registered QObject reference, null reference |

Nil also represents an invalid QVariant inside containers. Ordinary maps never
act as type tags. Unknown types, sets, keyword keys, ratios, nonfinite numbers,
out-of-range integers and implicit string-to-color/URL conversions fail.
Integer-to-double conversion is limited to the exact ±2^53 range; doubles never
silently narrow to integers. Qt enum keys use the property's own metadata;
numeric enum values and keys from other enum types are rejected. Flag reads use
Qt's canonical key order/aliases. QColor reads use 8-bit ARGB, matching Snitt's
settings colors; extended/HDR color spaces are outside this vocabulary.

References do not transfer ownership or extend lifetime. Reads of unregistered
or logically disposed references fail, including references nested in containers.
Native reads consult the registry before exposing identities, and writes resolve
identities before accessing pointers. A native QPointer property may instead
return nil after physical destruction. No raw address is exposed or auto-adopted.

Missing/read-only properties and wrong value types fail before writing, even
when the attempted read-only value equals the current value. Attached and value
subproperties (`Layout.margins`, `font.pixelSize`) use QQmlProperty metadata.
Native setters remain authoritative and can normalize values; read back the
property to observe their result. Equal-write suppression compares nested types
as well as values. Failures throw managed exceptions through the checked ABI; native conversion
exceptions are caught before crossing FFI. Unsupported Qt return types fail
instead of falling back to `toString()`.

## Reset and bindings

`(q/reset-property! handle property default)` writes an explicit typed default.
Widget prop removal continues to restore the defaults declared in `widget.clj`.
`(q/reset-property! handle property)` invokes Qt's native RESET and fails if the
property does not support it. For example, resetting a Quick item's width restores
implicit sizing and subsequent implicit-width changes continue to update it.

Equal writes preserve existing QML bindings and controlled-input cursor state.
A changed write can remove a QML binding. RESET only restores behavior provided
by that property's Qt implementation; arbitrary user-supplied QML bindings cannot
be reconstructed. Use the explicit default form when RESET is unavailable.

## Wire format and verification

The internal C ABI exchanges tagged arrays containing only arrays and strings,
the JSON/EDN common subset. Numeric payloads are decimal strings: JSON number
conversion cannot round qint64. Escaping covers all control characters, including
NUL, before crossing the null-terminated ABI. Qt-specific values have explicit
tags. The native decoder bounds nesting at 64 and rejects malformed payloads.
This is an internal protocol, not a general JSON or EDN interface.

Run `./glimmer-qt/test-values` using Jolt 0.8.16 and the supported local Qt SDK.
It enables a test-only native Q_PROPERTY fixture; normal builds omit it. The
build runner cleans generated objects when the SDK path or qmake arguments change.
The focused suite exercises real native properties, failure preservation, typed
returns and reset behavior. Existing lifecycle and hello-world tests also use
typed reads. These tests use Qt's offscreen platform and make no physical-input
claims. Windows execution is deferred and was not tested for this change.
