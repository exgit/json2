TEMPLATE = app
CONFIG -= app_bundle
CONFIG -= qt
CONFIG += console
CONFIG += c11


HEADERS += \
    ../json.h


SOURCES += \
    test.c \
    ../json.c
