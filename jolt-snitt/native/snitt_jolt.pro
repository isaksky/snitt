QT += core gui widgets network qml quick multimedia svg
TEMPLATE = lib
TARGET = snitt_jolt
CONFIG += c++17 shared release unversioned_libname unversioned_soname
CONFIG -= lib_bundle
macx: QMAKE_MACOSX_DEPLOYMENT_TARGET = 15.0
macx: QMAKE_SONAME_PREFIX = @rpath

SOURCES += snitt_jolt.cpp \
    ../../src/clipboard.cpp ../../src/imagedocument.cpp ../../src/editorcanvas.cpp \
    ../../src/screenshotsave.cpp ../../src/appsettings.cpp ../../src/globalhotkey.cpp \
    ../../src/backend.cpp ../../src/regionselector.cpp ../../src/trimsession.cpp \
    ../../src/mp4duration.cpp ../../src/videorecorder.cpp
HEADERS += media_jobs.h ../../src/recordingfile.h ../../glimmer-qt/native/extensions.h ../../src/clipboard.h \
    ../../src/imagedocument.h ../../src/editorcanvas.h ../../src/appsettings.h \
    ../../src/globalhotkey.h ../../src/backend.h ../../src/regionselector.h \
    ../../src/trimsession.h ../../src/videorecorder.h
RESOURCES += ../../src/resources.qrc
macx {
    HEADERS += ../../src/macrecorder.h
    SOURCES += ../../src/macrecorder.mm
    QMAKE_CXXFLAGS += -fobjc-arc
    LIBS += -framework Carbon -framework CoreGraphics -framework CoreMedia -framework ScreenCaptureKit -framework AVFoundation -framework AppKit
}
win32: LIBS += -L$$PWD/../../glimmer-qt/build/release
win32: LIBS += -luser32 -ldwmapi -lshell32 -lole32
LIBS += -L$$PWD/../../glimmer-qt/build -lglimmer_qt
QMAKE_RPATHDIR += $$PWD/../../glimmer-qt/build
