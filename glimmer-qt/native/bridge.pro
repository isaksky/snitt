QT += core gui widgets qml quick quickcontrols2
TEMPLATE = lib
TARGET = glimmer_qt
CONFIG += c++17 shared release unversioned_libname unversioned_soname
CONFIG -= lib_bundle
macx: QMAKE_MACOSX_DEPLOYMENT_TARGET = 15.0
SOURCES += bridge.cpp
RESOURCES += resources.qrc
HEADERS += values.h
contains(CONFIG, property_tests) {
    DEFINES += GQT_PROPERTY_TESTS
    HEADERS += ../test/native/value_fixture.h
}
