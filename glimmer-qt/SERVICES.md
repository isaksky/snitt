# Clipboard and desktop services

`glimmer-qt.services` provides synchronous GUI-thread calls during an active
`glimmer-qt.core/run-application!` session. Every call returns either
`{:ok? true :value ...}` or `{:ok? false :error "..."}`. Callers should retain
their document when publication or reveal fails. A successful desktop open means
the platform accepted the request; it does not prove another application finished
opening the URL or selecting the file.

| Operation | Result value |
| --- | --- |
| `read-text`, `publish-text!` | Unicode string; `true` |
| `read-image`, `load-image path`, `publish-image! image` | Owned `Image`; owned `Image`; `true` |
| `save-image! image path`, `same-image? a b`, `release-image! image` | `true`; boolean; `true` |
| `open-url! absolute-url`, `reveal-file! existing-path` | `true`; `true` |

An `Image` is an opaque session-owned native QImage copy. Clipboard reads detach
pixel storage from Qt's MIME owner, so clipboard replacement cannot invalidate a
document. Release each image explicitly; session teardown releases any remaining
images and invalidates their handles. PNG file load/save is for fixtures and
application interchange and preserves the image pixels and alpha. Text, URLs and
paths use UTF-8 at the native boundary. Strings containing NUL are rejected.

An application library may register a `GqtClipboardPublisher`, `GqtUrlOpener`,
or `GqtFileRevealer` from `native/extensions.h` for the active session. The
publisher always takes ownership of its `std::unique_ptr<QMimeData>`, including
when it returns failure. Hooks run on Qt's GUI thread, must not throw, and their
library must stay loaded through session close. On macOS, the default publisher
checks Qt's MIME ownership, URL opening uses `QDesktopServices`, and file reveal
uses `/usr/bin/open -R`. On Windows, publication requires a registered native
publisher, so the facade cannot mistake an optimistic Qt copy for success. The
example adapter in `experiment/native/extensions.cpp` registers Snitt's existing
`snittClipboard::publish` implementation, retaining its Windows retry and image
formats. Applications can likewise register their own robust file revealer.

`./glimmer-qt/test-services` uses Qt's private offscreen clipboard, a small RGBA
fixture, and test hooks for rejected publication and desktop calls. It never
asserts anything about the user's desktop clipboard. The existing Windows
contention tests in `tests/editor_tests.cpp` remain separate.
