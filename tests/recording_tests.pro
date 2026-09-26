QT += core gui widgets svg testlib
CONFIG += c++17 testcase console
CONFIG -= app_bundle
TARGET = recording_tests
TEMPLATE = app
macx: QMAKE_MACOSX_DEPLOYMENT_TARGET = 15.0
INCLUDEPATH += ../src
HEADERS += ../src/backend.h ../src/regionselector.h ../src/videorecorder.h ../src/trimsession.h
SOURCES += recording_tests.cpp ../src/backend.cpp ../src/regionselector.cpp ../src/videorecorder.cpp ../src/trimsession.cpp
macx {
    HEADERS += ../src/macrecorder.h
    HEADERS += ../src/macclip.h
    SOURCES += ../src/macrecorder.mm ../src/macclip.mm
    QMAKE_CXXFLAGS += -fobjc-arc
    LIBS += -framework CoreGraphics -framework CoreMedia -framework ScreenCaptureKit -framework AVFoundation -framework AppKit
}
win32: LIBS += -luser32 -ldwmapi -lshell32 -lole32
