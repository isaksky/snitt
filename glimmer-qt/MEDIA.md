# Qt review playback

Require `glimmer-qt.media` before mounting a `:video-player`. The tag is a visual
Qt Quick surface with an owned `MediaPlayer`, `AudioOutput`, `VideoOutput`, and
`QVideoSink`. It accepts `:id`, `:hexpand`, `:vexpand`, `:fill-mode` (`"fit"`,
`"crop"`, or `"stretch"`), and `:on-first-frame`. The callback takes no frame
data and fires once for the first valid decoded frame of each nonempty source.
Clearing and loading another source rearms it.

Use `glimmer-qt.media/player`, `audio-output`, `video-output`, or `video-sink` to
borrow typed handles for inspection. The surface owns them; unmounting it
invalidates every handle and disconnects its observers. Playback mutations run
on the Qt GUI thread through `source!`, `position!`, `rate!`, `mute!`, `play!`,
`pause!`, `stop!`, and `clear!`. `clear!` stops and clears the source; call it
before trimming or replacing the current file. The media controls are
imperative so an unrelated Glimmer render cannot reset a seek, rate, or source.

`state` returns `:source`, `:position`, `:duration`, `:playback`, `:media`,
`:error`, `:error-string`, `:rate`, and `:muted`. Enum fields are typed
`glimmer-qt.value/enum` values, such as `(value/enum "PlayingState")`.
`observe!` delivers typed source, position, duration, playback, media, and error
changes through the owned QObject subscription path. Read initial state with
`state`; notifications report subsequent changes. For muted priming, mute,
start playback, then pause and seek in `:on-first-frame` before unmuting. A
short fallback timer can finish priming if the backend does not produce a frame.

`./glimmer-qt/test-media` generates a static-frame MP4 and uses Snitt's private
macOS FFmpeg playback plugin. It checks playback, seek, rate, first-frame
delivery, typed states and errors, source replacement, and teardown. The
default offscreen run checks decoding and sink delivery. Run with
`QT_QPA_PLATFORM=cocoa SNITT_MEDIA_SCREENSHOT=/tmp/media.png` to capture the
window on the macOS desktop.
