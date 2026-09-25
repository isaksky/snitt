QT += core gui widgets network qml quick quickcontrols2

CONFIG += c++17 release
TARGET = xshot
TEMPLATE = app

# Match the minimum macOS version of the Homebrew Qt Quick libraries.
macx: QMAKE_MACOSX_DEPLOYMENT_TARGET = 15.0

HEADERS += src/backend.h src/imagedocument.h src/editorcanvas.h
SOURCES += src/main.cpp src/backend.cpp src/imagedocument.cpp src/editorcanvas.cpp
HEADERS += src/globalhotkey.h src/appservice.h src/regionselector.h
SOURCES += src/globalhotkey.cpp src/appservice.cpp src/regionselector.cpp
HEADERS += src/videorecorder.h
SOURCES += src/videorecorder.cpp
macx {
    LIBS += -framework Carbon -framework CoreGraphics
    QMAKE_INFO_PLIST = platform/macos/Info.plist
}
win32: LIBS += -luser32 -ldwmapi
RESOURCES += src/resources.qrc
