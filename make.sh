#!/bin/sh
# development helper script

cd $(dirname $0) || exit 1
#make LDFLAGS="-Wl,--as-needed"

CFLAGS="-fno-builtin-log -O0 -g -Wall -W -Wextra -Wformat-security -Wshadow -pipe -std=gnu99 -D_GNU_SOURCE=1 -fbounds-check -Wformat -Werror=format-security"

if [ "$(whoami)" != "mis" ]; then
    make CFLAGS="$CFLAGS" $@

elif grep -q "HAVE_RPMORG 1" config.h; then
    echo "*** build on Fedora ***"
    unset LC_CTYPE
    [ -f ~/.a ] && . ~/.a
    ssh mis@10.1.1.177 "cd poldek && make CFLAGS='$CFLAGS' $@"
else
    make CFLAGS="$CFLAGS" $@
fi
