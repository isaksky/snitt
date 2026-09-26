QT += core gui widgets network quick quickcontrols2 svg multimedia testlib
CONFIG += c++17 testcase console
CONFIG -= app_bundle
TARGET = editor_tests
TEMPLATE = app
macx: QMAKE_MACOSX_DEPLOYMENT_TARGET = 15.0
INCLUDEPATH += ../src
HEADERS += ../src/imagedocument.h ../src/editorcanvas.h ../src/backend.h ../src/regionselector.h ../src/globalhotkey.h
SOURCES += editor_tests.cpp ../src/imagedocument.cpp ../src/editorcanvas.cpp ../src/backend.cpp ../src/regionselector.cpp ../src/globalhotkey.cpp
HEADERS += ../src/videorecorder.h
SOURCES += ../src/videorecorder.cpp
HEADERS += ../src/screenshotsave.h
SOURCES += ../src/screenshotsave.cpp
HEADERS += ../src/trimsession.h
SOURCES += ../src/trimsession.cpp
macx {
    HEADERS += ../src/macrecorder.h
    HEADERS += ../src/macclip.h
    SOURCES += ../src/macrecorder.mm ../src/macclip.mm
    QMAKE_CXXFLAGS += -fobjc-arc
    LIBS += -framework Carbon -framework CoreGraphics -framework CoreMedia -framework ScreenCaptureKit -framework AVFoundation -framework AppKit
}
win32: LIBS += -luser32 -ldwmapi -lshell32 -lole32
RESOURCES += ../src/resources.qrc
