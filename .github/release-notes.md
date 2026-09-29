Windows x64 and macOS Apple silicon builds of Snitt.

Windows binaries are unsigned. The macOS app uses an ad-hoc signature and is
not notarized. The archives include dependency notices and source/build material.

This draft passed the CI headless tests and archive integrity checks. Before
publishing, run the expensive checks locally against these exact archives:

- Desktop capture, recording, playback, seeking, thumbnails, and trimming.
- Installation, upgrade from the previous release, and uninstall.
- Hotkeys, login startup, and macOS screen-recording permissions.
- Hardware decoding, multiple displays, and performance where applicable.

Record those results here and remove this checklist before publishing.
Publishing opens a PR updating the Scoop bucket and Homebrew cask; merge it
after the package-manager installation checks pass.
