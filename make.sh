#!/bin/sh

cd $(dirname $0) || exit 1
make CFLAGS="-fno-builtin-log -O0 -g -Wall -W -pipe" $@
