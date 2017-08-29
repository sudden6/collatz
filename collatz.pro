TEMPLATE = app
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt
QMAKE_CFLAGS += -std=c99 -msse3 -msse -msse2
QMAKE_CFLAGS_RELEASE += -Ofast -march=native -mtune=native

SOURCES += \
    collatz_sieb_multistep.c

DISTFILES += \
    worktodo.txt

copydata.commands = $(COPY_DIR) $$PWD/worktodo.txt $$OUT_PWD
first.depends = $(first) copydata
export(first.depends)
export(copydata.commands)
QMAKE_EXTRA_TARGETS += first copydata

