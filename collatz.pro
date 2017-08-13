TEMPLATE = app
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt
QMAKE_CFLAGS += -std=c99
QMAKE_CFLAGS_RELEASE += -Ofast -march=native -mtune=native

SOURCES += \
    collatz_sieb_multistep.c \
    collatz_sieb_multistep_kopie.c

DISTFILES += \
    worktodo.txt
