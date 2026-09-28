QT += core gui widgets network qml quick quickcontrols2 svg multimedia

CONFIG += c++17 release
TARGET = xshot
TEMPLATE = app

# Match the minimum macOS version of the Homebrew Qt Quick libraries.
macx: QMAKE_MACOSX_DEPLOYMENT_TARGET = 15.0

HEADERS += src/backend.h src/imagedocument.h src/editorcanvas.h src/annotationfont.h
SOURCES += src/main.cpp src/backend.cpp src/imagedocument.cpp src/editorcanvas.cpp
HEADERS += src/globalhotkey.h src/appservice.h src/appsettings.h src/regionselector.h
SOURCES += src/globalhotkey.cpp src/appservice.cpp src/appsettings.cpp src/regionselector.cpp
HEADERS += src/videorecorder.h
SOURCES += src/videorecorder.cpp
HEADERS += src/screenshotsave.h src/savepaths.h
SOURCES += src/screenshotsave.cpp
HEADERS += src/trimsession.h src/mp4duration.h src/playbackclock.h
SOURCES += src/trimsession.cpp src/mp4duration.cpp
macx {
    HEADERS += src/macrecorder.h
    SOURCES += src/macrecorder.mm
    QMAKE_CXXFLAGS += -fobjc-arc
    LIBS += -framework Carbon -framework CoreGraphics -framework CoreMedia -framework ScreenCaptureKit -framework AVFoundation -framework AppKit
    QMAKE_INFO_PLIST = platform/macos/Info.plist
}
win32: LIBS += -luser32 -ldwmapi -lshell32 -lole32
RESOURCES += src/resources.qrc
