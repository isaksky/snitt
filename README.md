# xshot

A Hello World desktop app using C++17, Qt 6, Qt Quick/QML, and Material controls.
Click **Say hello** to call the C++ backend and update the greeting through a Qt
property and its change signal.

## Dependencies

A C++17 compiler, make, and Qt 6 with Core, Gui, Qml, Quick, and Quick Controls 2.
On macOS with Homebrew:

```sh
brew install qtdeclarative
```

On Arch Linux:

```sh
sudo pacman -S --needed base-devel qt6-base qt6-declarative
```

No ffmpeg or external media tools are used.

## Run

```sh
./bin/run
```

This builds and launches the app. To build only, run `./bin/build`.
The output is `build/xshot.app` on macOS or `build/xshot` on Linux.
QML is embedded through Qt resources; the installed Qt libraries and QML modules
are still needed at runtime. This is a development build, not a standalone
distribution bundle.

## Structure

- `xshot.pro`: qmake project and Qt dependencies.
- `src/main.cpp`: application startup, Material style, and QML engine.
- `src/backend.h`: C++ object exposed to QML as `backend`.
- `src/Main.qml`: window, greeting, and button.
- `src/resources.qrc`: embedded QML resources.
