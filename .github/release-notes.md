Windows x64 and macOS Apple silicon builds of Snitt.

Windows binaries are unsigned. The macOS app uses an ad-hoc signature and is
not notarized. The archives include dependency notices and source/build material.

## Opening Snitt on macOS

After installing, try opening Snitt from Applications. If macOS blocks it
because the developer cannot be verified, go to **System Settings > Privacy &
Security > Open Anyway**, then confirm. See [Apple's opening instructions](https://support.apple.com/en-us/102445).

For a copy you trust from Snitt's official release or Homebrew tap, you can
alternatively remove its quarantine attribute and open it from Terminal:

```sh
xattr -dr com.apple.quarantine "/Applications/Snitt.app"
open "/Applications/Snitt.app"
```

If you installed Snitt elsewhere, use that app's path in both commands. These
commands apply only to Snitt's app bundle; they do not notarize it or disable
Gatekeeper globally. Grant screen-recording permission when prompted.

## Release validation (replace before publishing)

This draft passed the CI headless tests and archive integrity checks. Before
publishing, run the expensive checks locally against these exact archives:

- Desktop capture, recording, playback, seeking, thumbnails, and trimming.
- Installation, upgrade from the previous release, and uninstall.
- Hotkeys, login startup, and macOS screen-recording permissions.
- Hardware decoding, multiple displays, and performance where applicable.

Record those results here and remove this checklist before publishing. Keep
the macOS opening instructions above in the published notes.
Publishing a stable release opens a PR updating the Scoop bucket and Homebrew
cask; merge it after the package-manager installation checks pass. Prereleases
use direct downloads and do not update those stable package definitions.
