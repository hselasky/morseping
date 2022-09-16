TEMPLATE	= app
CONFIG		+= qt warn_on release
QT		+= core gui network
greaterThan(QT_MAJOR_VERSION, 4) {
QT += widgets
}

HEADERS		+= morseping.h
SOURCES		+= morseping.cpp
RESOURCES	+= morseping.qrc
TARGET		= MorsePing

target.path	= $${PREFIX}/bin
INSTALLS	+= target

icons.path	= $${PREFIX}/share/pixmaps
icons.files	= MorsePing.png
INSTALLS	+= icons

desktop.path	= $${PREFIX}/share/applications
desktop.files	= MorsePing.desktop
INSTALLS	+= desktop

