QT += core gui
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = trim_thumbnail_benchmark
TEMPLATE = app
macx: QMAKE_MACOSX_DEPLOYMENT_TARGET = 15.0
INCLUDEPATH += ../src
HEADERS += ../src/trimsession.h ../src/videorecorder.h
SOURCES += trim_thumbnail_benchmark.cpp ../src/trimsession.cpp ../src/videorecorder.cpp
macx {
    HEADERS += ../src/macclip.h
    SOURCES += ../src/macclip.mm
    QMAKE_CXXFLAGS += -fobjc-arc
    LIBS += -framework CoreGraphics -framework CoreMedia -framework AVFoundation -framework AppKit
}
win32: LIBS += -luser32 -lshell32 -lole32
