#!/bin/sh
# development helper script

cd $(dirname $0) || exit 1
#make LDFLAGS="-Wl,--as-needed"

OPT="-O0"

if [ "$1" == "profiled" ]; then
    #OPT="-O2 "
   OPT="$OPT -pg -static"
   shift
fi

CFLAGS="-fno-builtin-log $OPT -g -Wall -W -Wextra -Wformat-security -Wshadow -pipe -std=gnu99 -D_GNU_SOURCE=1 -fbounds-check -Wformat -Werror=format-security"

make CFLAGS="$CFLAGS -DWITH_TIMETHIS=1 -DWITH_THREADS=1" $@
