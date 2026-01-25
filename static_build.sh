#!/bin/sh

make clean

PCRE2_INC="/usr/include/pcre2"
HUNSPELL_INC=$(ls -d /usr/include/hunspell* 2>/dev/null | head -n 1)
LIB_PATH="/usr/lib/x86_64-linux-gnu"

make CC=gcc \
CFLAGS="-Os -D_GNU_SOURCE -I$PCRE2_INC -I$HUNSPELL_INC" \
LDFLAGS="-static" \
LDLIBS="-Wl,--start-group \
$LIB_PATH/libpcre2-8.a \
$LIB_PATH/libhunspell-*.a \
$LIB_PATH/libncursesw.a \
$LIB_PATH/libtinfo.a \
-lpthread -lstdc++ -lm \
-Wl,--end-group"

echo "--- Build Result ---"
ls -lh nanox
ldd nanox 2>&1 | grep "not a dynamic executable" || echo "Warning: Still dynamic!"
