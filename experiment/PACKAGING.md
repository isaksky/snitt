# Standalone macOS experiment

From the repository root, use the same supported Qt 6.11.1 or 6.11.2 SDK that
provides Snitt's private LGPL playback runtime. The package command builds the
Glimmer bridge and the `hello.packaged` Jolt entry point, deploys the Qt/QML
runtime, and signs a separate `Glimmer Experiment.app`:

```sh
SNITT_QMAKE=/path/to/qt/bin/qmake6 ./bin/package-glimmer-macos
./bin/smoke-glimmer-macos
```

The default SDK is the one last used by `glimmer-qt/build-native`. Jolt, a C
compiler, the matching `macdeployqt`, and the existing private multimedia stage
under `build/tools/mac-multimedia-runtime/stage` are required. A missing or
mismatched bridge, Qt SDK, or playback runtime stops packaging with an error.
The output is `build/package/glimmer-macos/Glimmer Experiment.app`; packaging
does not install or register it, publish it, or alter `Snitt.app`.

`smoke-glimmer-macos` copies the signed bundle to a temporary directory, clears
developer Qt/Jolt paths, launches it on the local Cocoa desktop, and checks a
decoded frame from the bundled MP4 through the private LGPL FFmpeg plugin. The
app can also be launched normally from Finder. The fixture is a small greeting
form and a paused video preview; it does not activate Snitt's production tray,
hotkey, recording, or file services.

The bundle uses a distinct `local.snitt.glimmer-experiment` identity and the
repository's local macOS signing helper. Its Qt frameworks and LGPL FFmpeg
libraries remain dynamically linked. The included `Resources/DEPENDENCIES.json`,
`Resources/licenses/`, and `Resources/Sources/` identify licenses, exact source
archives, and build material for the selected playback runtime. A replacement
compatible library requires locally signing the modified bundle again. This is
a local development package; it is not Developer ID signed or notarized.

Windows packaging and execution are deferred. The existing Windows native
library candidates remain in `deps.edn`; this command runs only on macOS.
