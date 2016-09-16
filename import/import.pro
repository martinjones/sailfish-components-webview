TEMPLATE=lib

TARGET=sailfishwebviewplugin
TARGET = $$qtLibraryTarget($$TARGET)

MODULENAME = Sailfish/WebView
TARGETPATH = $$[QT_INSTALL_QML]/$$MODULENAME

CONFIG += qt link_pkgconfig plugin
QT += core qml quick
PKGCONFIG += qt5embedwidget

QMAKE_CXXFLAGS += -fPIC

HEADERS += plugin.h
SOURCES += plugin.cpp
OTHER_FILES += qmldir WebView.qml

include(translations.pri)

import.files = qmldir WebView.qml
import.path = $$TARGETPATH
target.path = $$TARGETPATH

INSTALLS += target import translations_install engineering_english_install