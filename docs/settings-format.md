# xshot settings file

xshot reads one UTF-8 INI file at `QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)/settings.ini`. The application exposes the resolved path as `settingsFile`; this is a per-user configuration location on both macOS and Windows. This guide is installed with the app and copied beside the INI as `settings-format.md` on first launch.

Edit the file with a text editor while xshot is running. Preserve unrelated keys and comments. xshot does not serialize the file while loading or reloading, so comments and unknown settings remain as written. Save in UTF-8 INI syntax. Comments begin with `;` or `#` at the start of a line. Section and key names below are case-sensitive in this documented format.

## Shortcuts

Shortcuts use Qt portable key-sequence spelling: one key, optionally preceded by modifiers such as `Ctrl+`, `Alt+`, `Shift+`, or `Meta+`. Separate alternate keys with `|`. Each sequence must be a single key press; multi-step chords are not supported. On macOS, `Meta` means Command; `Ctrl` remains Control. On Windows, `Meta` is the Windows key. The global shortcut must include a modifier and use a key supported by the operating system. `Print` means Print Screen on Windows and F13/Print Screen on macOS.

Bindings may be empty to disable that optional shortcut. `globalCapture` is required because it captures a region and stops an active recording. Missing shortcut keys use the defaults below. Unknown keys are ignored. xshot rejects unsupported sequences and collisions between actions that can run in the same context. Region-picker and trim-review shortcuts have their own contexts; the global capture shortcut conflicts with every other binding.

| Key | Action | Default on Windows | Default on macOS | Context |
| --- | --- | --- | --- | --- |
| `globalCapture` | Capture a region; stop an active recording | `Ctrl+Print` | `Ctrl+Print` | Global |
| `capture` | Capture a region | `Ctrl+N` | `Meta+N` | Editor |
| `captureMultiple` | Capture multiple regions | `M` | `M` | Editor |
| `captureVideo` | Record a region | `V` | `V` | Editor |
| `openImage` | Open an image | `Ctrl+O` | `Meta+O` | Editor |
| `pasteImage` | Paste an image | `Ctrl+V` | `Meta+V` | Editor |
| `copyClose` | Copy and close | `C` | `C` | Editor |
| `saveClose` | Save and close | `S` | `S` | Editor |
| `dismiss` | Cancel current action or close editor | `Escape` | `Escape` | Editor |
| `undo` / `redo` | Undo / redo edits | `Ctrl+Z` / `Ctrl+Y` | `Meta+Z` / `Meta+Shift+Z` | Annotate |
| `toolCut`, `toolRectangle`, `toolHighlight`, `toolText`, `toolArrow`, `toolPixelate`, `toolErase` | Choose the named editor tool | `X`, `R`, `H`, `T`, `A`, `P`, `E` | Same | Annotate |
| `goodMode` / `badMode` | Choose Good / Bad annotation color | `G` / `B` | Same | Annotate |
| `arrange` | Continue to annotation | `Return|Enter` | Same | Arrange |
| `columnsMore` / `columnsLess` | Add / remove a layout column | `Shift+=|+|=` / `-` | Same | Arrange |
| `removeRegion` | Remove selected region | `Backspace|Delete` | Same | Arrange |
| `moveRegionLeft` / `moveRegionRight` | Move selected region | `Left` / `Right` | Same | Arrange |
| `regionCancel` | Cancel capture | `Escape` | Same | Region picker |
| `regionMultiple` / `regionVideo` | Choose multiple / video capture | `M` / `V` | Same | Region picker |
| `regionArrange` | Continue from multiple capture | `Return|Enter` | Same | Region picker |
| `regionRemoveLast` | Remove the most recent region | `Backspace|Delete` | Same | Region picker |
| `reviewPlayPause` | Play or pause a recording | `Space` | Same | Trim review |
| `reviewSeekBackward` / `reviewSeekForward` | Seek one second | `Left` / `Right` | Same | Trim review |
| `reviewSeekBackwardFar` / `reviewSeekForwardFar` | Seek five seconds | `Shift+Left` / `Shift+Right` | Same | Trim review |
| `reviewToggleZoom` | Toggle trim timeline zoom | `Z` | Same | Trim review |
| `reviewMarkStart` / `reviewMarkEnd` | Set trim start / end at the playhead | `Ctrl+Space` / `Alt+Space` | Same | Trim review |
| `reviewCancel` | Cancel export or keep the original recording | `Escape` | Same | Trim review |

For example, this changes the Pixelate tool and preserves the other shortcut entries:

```ini
[Shortcuts]
toolPixelate=Shift+P
```

Use the current platform's default modifiers if changing standard editor actions. The documented key names are the settings-file names; button hints and tooltips use each active binding.

## Annotation colors

`[Colors]` contains `good` and `bad` values in six-digit `#RRGGBB` hex form. Defaults are `#22c55e` and `#ef4444`. Good and Bad are logical modes, not color comparisons. Changing a color updates the selected mode and future annotations; annotations already drawn keep their saved color. Good-mode highlights keep the existing translucent yellow fill, and Bad-mode highlights keep the existing translucent red fill.

```ini
[Colors]
good=#36b37e
bad=#d94c63
```

## Save roots

`[Save]` has `picturesRoot` and `videosRoot`. Each value is an absolute directory containing media. Defaults are the platform Pictures folder followed by `xshot`, and the platform Movies/Videos folder followed by `xshot`; if the Movies/Videos location is unavailable, recordings use the home folder followed by `xshot`. An empty value restores that platform default. A leading `~/` expands to the current user's home folder. Relative paths are rejected.

xshot creates `YEAR/MONTH` below the configured root, with an unpadded month number. For example, a configured root of `/data/captures` produces `/data/captures/2026/9/...`; a default Pictures folder produces `Pictures/xshot/2026/9/...`. xshot creates missing directories when saving. If the destination cannot be used, it reports the error and does not silently save elsewhere. New roots apply to later saves and recording starts; an active recording and its trim flow keep the path allocated when recording began.

```ini
[Save]
picturesRoot=~/Pictures/work-captures
videosRoot=/Volumes/archive/video-captures
```

## Validation and live reload

xshot validates a complete candidate before applying it. It watches the file and its containing directory, debounces edits, and reloads both ordinary saves and atomic file replacements without restarting. A missing key uses its default; removing a key therefore restores that key's default after a successful reload. Unknown keys are ignored and preserved. A malformed INI file, invalid color or path, unsupported shortcut, conflicting binding, or unavailable native global hotkey leaves the last valid settings active and reports the specific key to fix. Correct the file and save it again to reload. If the file is removed while xshot is running, the last valid values remain active; recreate the file to resume watching. xshot creates the default file only when starting with no settings file and never replaces an existing file.
