# Focus and keyboard input

`glimmer-qt.widget/focus!` requests active focus for an attached live item.
`focus-next!` traverses Qt's Tab chain; its optional second argument is `false`
for backward traversal. Both require the GUI thread and reject stale handles.
The built-in visual tags accept `:tab-focus`; entries and buttons default to
true, other tags to false. Actual activation remains controlled by the OS.

Visual items accept `:on-key-press` and `:on-key-release` handlers. They receive
`{:key int :text string :modifiers int :auto-repeat boolean :count int}` using
Qt key codes and modifier bits. Return exactly `true` to consume the event;
other results preserve Qt's normal editing, activation and propagation. Handlers
are refreshed on each render and removed when omitted. Exceptions stop the
session through the existing managed callback boundary, never through C++.
Input-method events pass through untouched; preedit, unknown and dead keys do
not invoke command handlers. Do not synthesize text from key codes: use
`:on-change` for committed text.

## Scoped shortcuts

Mount a `:shortcut` under the owning window's tree:

```clojure
[:shortcut {:sequences ["Ctrl+S" "Ctrl+Shift+S"]
            :scope "window"
            :enabled (and (not editing?) (not dialog-open?))
            :on-activate save!}]
```

`:sequences` is a vector of Qt key sequence strings (empty disables all
registrations). `:scope` is `"window"` (default) or `"application"`. Qt owns
sequence parsing, alternate/multi-stroke matching and context resolution.
Sequence changes replace old registrations in place; handler changes do not
add connections. Unmounting removes registrations. `:auto-repeat` defaults to
false. Conflicts invoke optional `:on-ambiguous`, never `:on-activate`.

Qt's platform convention is intentional: `Ctrl`/ControlModifier means Command
on macOS and Control on Windows; `Meta`/MetaModifier means physical Control on
macOS and the Windows key on Windows. Shift and Alt retain Qt semantics. No
OS-global hotkeys are registered; those belong to a native extension.

To suppress an editor command group, derive each member's `:enabled` from the
same reactive application state, including text editing and modal state. This
also covers application-scoped commands that otherwise remain active in other
windows. Key handlers run after shortcut matching, so consuming a key is not a
replacement for disabling an editor shortcut. After commit/cancel, request
canvas focus explicitly with `focus!` (use `later!` if the target is remounted).

## Verification

`./glimmer-qt/test-keyboard` uses QtTest key delivery through the window and
input-method events through the text control; it defaults to offscreen.
`QT_QPA_PLATFORM=cocoa ./glimmer-qt/test-keyboard` exercises the local macOS
desktop. Neither is evidence of a physical IME candidate-window interaction or
Windows execution.
