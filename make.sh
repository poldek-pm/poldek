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

LDFLAGS=""
if [ "$1" == "gprofiled" ]; then
   LDFLAGS="-ltcmalloc -lprofiler"
   shift
fi


if [ "$1" == "sane" ]; then
    OPT="-fsanitize=address -fsanitize=undefined -fno-sanitize-recover=all -fsanitize=float-divide-by-zero -fsanitize=float-cast-overflow -fno-sanitize=null -fno-sanitize=alignment"
    shift
fi

CFLAGS="-fno-builtin-log $OPT -g -Wall -W -Wextra -Wformat-security -Wshadow -pipe -std=gnu99 -D_GNU_SOURCE=1 -fbounds-check -Wformat -Werror=format-security"

make CFLAGS="$CFLAGS -DWITH_TIMETHIS=1" LDADD="$LDFLAGS" $@ && ./cli/poldek -l >/dev/null
