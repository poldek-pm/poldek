#!/bin/sh

cd $(dirname $0) || exit 1
make CFLAGS="-O0 -g -Wall -W" $@
