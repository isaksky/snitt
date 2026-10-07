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
