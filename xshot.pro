QT += core gui qml quick quickcontrols2

CONFIG += c++17 release
TARGET = xshot
TEMPLATE = app

# Match the minimum macOS version of the Homebrew Qt Quick libraries.
macx: QMAKE_MACOSX_DEPLOYMENT_TARGET = 15.0

HEADERS += src/backend.h src/imagedocument.h src/editorcanvas.h
SOURCES += src/main.cpp src/backend.cpp src/imagedocument.cpp src/editorcanvas.cpp
RESOURCES += src/resources.qrc
