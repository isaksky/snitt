# Toolbar controls

`:button` keeps one native click relay and refreshes `:on-click` on each render.
`:checkable`, `:checked`, `:sensitive` (enabled), and `:highlighted` are booleans.
A user click toggles a checkable button; a subsequent render restores the supplied
`:checked` value, even if it equals the previous render. Prop writes never click.
`:mode` is `"normal"`, `"good"`, or `"bad"`; the latter two use mint/red accents.
Hover, pressed, disabled, checked and keyboard-focus states are styled separately.

`:icon` takes a QML URL string (`file:/…` or `qrc:/…`); empty clears the icon.
`:icon-only true` requires a nonempty `:accessible-name`. Tab focuses controls,
Space activates them, and the focus outline remains visible with custom backgrounds.
Both buttons and labels accept `:accessible-name`, `:accessible-description`, and
`:tooltip`. An empty name falls back to visible text. Tooltip strings can be
computed from live shortcut settings and appear on hover (also keyboard focus for
buttons). Icon URLs and custom QML sources should be application-owned assets.

Buttons accept `:theme (value/enum "Dark")` (also `"Light"` or `"System"`) and
`:accent "#91bff0"`. Defaults match the dark window. Themes/accents apply per
button; `good`/`bad` intentionally override the action color. This is toolbar
styling, not a replacement for all Material application palettes.

Both controls accept `:font-family`, `:font-size` (logical pixels), `:font-weight`
(Qt numeric weight), and `:font-italic`. Labels also accept `:color`,
`:text-alignment (value/enum "AlignLeft")`, `:wrap (value/enum "Wrap")`, and
`:elide (value/enum "ElideNone")`. For an elided single line, use `"NoWrap"` and
`"ElideRight"` with a constrained layout width. Text is plain text.

## Composition and measurement

`:content-source` and `:background-source` take QML URL strings for Item-based
components loaded inside a button. In each component, `parent.action` is the
owning control: bind to its `text`, `font`, `foreground`, `checked`, `enabled`,
`hovered`, `down`, and `visualFocus` properties. Content is inset eight pixels;
its implicit size contributes to the button's implicit size. Background fills
the button. Empty/removed sources unload the custom items and restore defaults.
Qt owns loaded items and disposes them with the button. Decorative components
should not install input handlers that consume the button's events. For richer
native/custom controls, use `widget/register!` and its explicit property schema.

`ffi/measure-text` takes a live font-bearing control handle and a single-line
string. It returns string-keyed logical-pixel measurements: `advance`, `height`,
`ascent`, `descent`, `underline-position`, `line-width`, and ink bounds `x`, `y`,
`width`. Call on the GUI thread; stale handles fail. Measure the prefix and the
shortcut letter separately to position an underline from the baseline. For a
playback counter, select a monospaced font available on the target platform and
reserve the advance of the widest expected formatted counter. Re-measure when
font properties change. No temporary widgets are allocated.

## Verification

`./glimmer-qt/test-actions [screenshot.png]` builds the test bridge and exercises
real Qt mouse/key events, reactive state/handlers, accessible properties, custom
composition, metrics, and reset behavior. It defaults to offscreen rendering.
Use `QT_QPA_PLATFORM=cocoa` for a local macOS desktop run and `QT_SCALE_FACTOR=2`
for a high-DPI render. Screenshots are optional and should go outside the source
tree. Windows execution is deferred; these checks do not assert screen-reader
speech or replace a platform accessibility audit.

## Pointer input

Compose `:pointer-area` inside an item/overlay (or give it layout hints inside
boxes). It supports the primitive geometry props, children, and common props,
including `:scale`. It covers its bounds without painting. Children appear above
its input surface and can take input themselves.

```clojure
[:pointer-area {:width 240 :height 80 :hover true
                :cursor (value/enum "SizeHorCursor")
                :buttons (value/flags "LeftButton")
                :prevent-stealing true
                :on-pointer-press (fn [event] (begin-drag! event) true)
                :on-pointer-move move-drag!
                :on-pointer-release end-drag!
                :on-pointer-cancel cancel-drag!
                :on-wheel (fn [event]
                            (when (supported-tool-wheel? event)
                              (resize-tool! event)
                              true))}]
```

Handlers receive a keyword-keyed map with `:x`, `:y`, `:global-x`, `:global-y`
in logical pixels, and Qt integer bit fields `:button`, `:buttons`, `:modifiers`.
Wheel maps additionally contain `:angle-x`, `:angle-y` (eighths of a degree),
`:pixel-x`, `:pixel-y` (touchpad pixels), and boolean `:inverted`. Hover entry/exit
use `:on-hover-enter` / `:on-hover-leave`; enable them and unpressed movement
with `:hover true` (default false). Hover entry/exit have zero button/modifier
fields. Cancellation supplies the last pointer sample, not a new release event.

Only literal `true` from press or wheel consumes that event; absent handlers,
false and nil leave it to Qt's normal propagation. An accepted press takes Qt's
exclusive mouse grab, delivering movement/release beyond the bounds. Movement,
release, hover and cancellation return values do not change the grab or replay
input to parents. `:prevent-stealing true` prevents a containing Flickable from
taking an established gesture; set it before pressing. Its default is false.
Wheel consumption is independent of press acceptance and supports pixel deltas;
ignored wheels reach parent scroll containers. There is no synthesized click.

Disabling an active area cancels its gesture. Disabled areas receive no further
input; unmount drops all callbacks before native teardown (including cancel).
Handlers refresh in place without reconnecting native signals. Exceptions use
the backend's managed failure/cleanup path and never unwind through Qt.

`(glimmer-qt.ffi/map-point from to x y)` returns `[x y]` mapped between registered,
attached items, including transforms and different windows. Pass nil for `to`
to obtain global desktop coordinates. Negative origins and fractional pixels
are preserved; stale handles, unattached items and nonfinite points fail.

Run `./glimmer-qt/test-pointer` for offscreen Qt window input tests, or
`QT_QPA_PLATFORM=cocoa ./glimmer-qt/test-pointer` for local macOS desktop delivery.
These send mouse/wheel events through Qt hit testing and grabbing, not QML signal
invocation. The offscreen run also models a negative desktop window origin.
