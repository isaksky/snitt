# Controls

## Slider and progress

`:slider` accepts numeric `:from`, `:to`, `:value`, and `:step` (default 0 for
continuous movement). The range may be a single value; reversed ranges,
negative steps, and nonpositive handle/track dimensions are rejected. Qt clamps
the displayed value to the range. `:on-change` receives a number from real
pointer or keyboard movement; controlled prop writes never call it. A zero range
cannot emit a value change. `:on-interacting-change` receives `true` on press and
`false` on release or cancellation, and `pressed` remains readable on the live
control. Playback pause/resume policy belongs to the application.

The slider has a horizontal track and handle. `:track-color`, `:fill-color`,
`:handle-color`, `:pressed-color`, `:handle-size`, and `:track-height` control its
appearance. Layout hints such as `:hexpand` and `:preferred-height` can size it
inside a box. `:sensitive false` disables input.

`:progress` uses the same numeric range and value and accepts `:indeterminate`
for preparation before a measured total is available. `:track-color`,
`:fill-color`, and `:track-height` style the bar. `:busy-indicator` is a separate
spinner controlled by `:running`. These controls accept `:accessible-name`;
the application supplies user-facing status text and export policy.

`./glimmer-qt/test-controls` exercises controlled writes, real pointer drag,
keyboard input, disabled and short/zero ranges, and determinate/indeterminate
progress on the local macOS Qt build. It defaults to offscreen input testing.

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

## Scrolling and multiline text

`:scroll` owns one content root (use an item/box to group multiple children).
It clips a Flickable viewport and exposes `contentWidth` and `contentHeight` for
property subscriptions. `:padding` insets the viewport. Content width follows
its viewport unless `:content-width` specifies a logical pixel width; height
follows the child's bounds. `:horizontal-scrollbar` and `:vertical-scrollbar`
accept `"as-needed"` (default), `"always"`, or `"never"`. Layout hints size the
scroll frame, not its content. Primitive content can use `:relative-width 1`;
`:text-area` automatically follows the content width and grows with its text.

```clojure
[:scroll {:hexpand true :vexpand true :padding 4
          :on-wheel (fn [event]
                      (when (font-wheel? event)
                        (adjust-font! event)
                        true))}
 [:text-area {:text @draft :on-change #(reset! draft %)
              :font-size @font-size :color "#ffffff" :padding 6
              :on-cancel cancel! :on-commit commit!
              :qt/subscriptions
              {:height {:property "contentHeight" :handler layout-height!}}}]]
```

`:text-area` is plain text with mouse selection, multiline editing, and `Wrap`
by default. `:wrap` takes a Qt symbolic enum (e.g. `Wrap`, `WordWrap`, `NoWrap`).
It supports the shared font properties, `:color`, `:selection-color`, and
`:selected-text-color`. Equal controlled text writes do not reset the document,
selection, cursor, or preedit. Style-only `textChanged` notifications are filtered;
programmatic prop assignment does not call `:on-change`. Keep the controlled
`:text` atom current in `:on-change`; an actual external text replacement owns
the document and may end an active edit/composition.

Optional `:cursor-position` and `:selection-anchor` use zero-based **Unicode
codepoint** offsets, not UTF-16 units or grapheme clusters. Positions beyond the
text clamp to its end. Set both for a directed selection, or only the cursor to
collapse it. Selection commands apply when those props or text change; unrelated
renders leave the user's current selection intact. Removing them leaves selection
uncontrolled. Read/observe `cursorCodepoint`, `selectionStartCodepoint`, and
`selectionEndCodepoint` through the property API. Native `cursorPosition` and
`selectionStart`/`selectionEnd` remain Qt UTF-16 values; do not pass them directly
to Jolt string functions. Combining marks count separately, surrogate pairs once.

Escape calls `:on-cancel`; Command+Return on macOS (Control+Return on Windows)
calls `:on-commit`, also for keypad Enter. Both consume the key and ignore repeats;
IME composition retains its own key handling. Plain Return inserts a newline.
Use the handlers to implement application policy and restore focus as appropriate.
`:on-wheel` on the scroll frame can consume wheel events before scrolling;
`:text-area` also supports it when used independently. The payload includes
angle/pixel deltas, modifiers, and inversion. Only literal `true` consumes it.
The active editor keeps its cursor visible in its containing scroll viewport.

`./glimmer-qt/test-text` sends Qt key, clipboard paste, input-method and wheel
events through a real Qt window using the offscreen platform. For macOS desktop
verification use `QT_QPA_PLATFORM=cocoa ./glimmer-qt/test-text`. Clipboard tests
restore the original MIME formats. These are distinct execution modes; neither
claims Windows validation.
