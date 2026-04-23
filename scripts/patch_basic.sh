#!/bin/bash
sed -i 's/^int gotobol(int f, int n)$/#include "cursor.h"\n\nint gotobol(int f, int n)/' basic.c
