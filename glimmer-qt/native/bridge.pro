QT += core gui widgets qml quick quickcontrols2 multimedia
TEMPLATE = lib
TARGET = glimmer_qt
CONFIG += c++17 shared release unversioned_libname unversioned_soname
CONFIG -= lib_bundle
macx: QMAKE_MACOSX_DEPLOYMENT_TARGET = 15.0
macx: QMAKE_SONAME_PREFIX = @rpath
DEFINES += GQT_BUILD
SOURCES += bridge.cpp
RESOURCES += resources.qrc
HEADERS += values.h objects.h extensions.h
contains(CONFIG, property_tests) {
    QT += testlib
    DEFINES += GQT_PROPERTY_TESTS
    HEADERS += ../test/native/value_fixture.h ../test/native/object_fixture.h
}
