QT += core gui widgets svg testlib
CONFIG += c++17 testcase console
CONFIG -= app_bundle
TARGET = recording_tests
TEMPLATE = app
macx: QMAKE_MACOSX_DEPLOYMENT_TARGET = 15.0
INCLUDEPATH += ../src
HEADERS += ../src/backend.h ../src/regionselector.h ../src/videorecorder.h ../src/trimsession.h ../src/savepaths.h ../src/appsettings.h ../src/globalhotkey.h
SOURCES += recording_tests.cpp ../src/backend.cpp ../src/regionselector.cpp ../src/videorecorder.cpp ../src/trimsession.cpp ../src/appsettings.cpp ../src/globalhotkey.cpp
macx {
    HEADERS += ../src/macrecorder.h
    SOURCES += ../src/macrecorder.mm
    QMAKE_CXXFLAGS += -fobjc-arc
    LIBS += -framework Carbon -framework CoreGraphics -framework CoreMedia -framework ScreenCaptureKit -framework AVFoundation -framework AppKit
}
win32: LIBS += -luser32 -ldwmapi -lshell32 -lole32
RESOURCES += ../src/resources.qrc
