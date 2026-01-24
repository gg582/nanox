#!/bin/sh

make CC=gcc \
CFLAGS="-Os -D_GNU_SOURCE $(pkg-config --cflags hunspell ncurses)" \
LDFLAGS="-static" \
LDLIBS="$(pkg-config --libs hunspell ncurses) -lstdc++ -lm"
