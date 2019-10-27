TEMPLATE = app
CONFIG -= app_bundle
CONFIG -= qt
CONFIG += console
CONFIG += c11


HEADERS += \
	json.h \
	marena.h


SOURCES += \
	json.c \
	main.c \
	marena.c
