#!/bin/sh
# $Id$

if [ ! -f capreq.h ]; then
    cd `dirname $0`;
    if [ ! -f capreq.h ]; then
        echo "`pwd`: not a poldek directory"
        exit 1
    fi 
fi

runcmd () {
    echo "$@"
    $@
}

CONFOPTS="--enable-maintainer-mode --enable-compile-warnings $@"

getlib_mode="link"
if [ -n "$1" -a "$1" = "makedist" ]; then
    rm -f trurlib tndb
    getlib_mode="cp"
fi

runcmd ./getlib.sh trurlib $getlib_mode
runcmd ./getlib.sh tndb    $getlib_mode

# generate po/POTFILES.in
make -f Makefile.extra POTFILES_in

runcmd libtoolize --force --automake
runcmd aclocal -I m4
runcmd autoheader
runcmd autoconf

if which -- autopoint >/dev/null 2>&1 ; then
    runcmd autopoint --force
else
    runcmd gettextize -f
fi
runcmd automake --add-missing -a -c -f --foreign

# w/o
if [ ! -f depcomp ]; then 
    runcmd automake --add-missing Makefile
    (cd vfile && ln -sf ../depcomp .)
    (cd shell && ln -sf ../depcomp .)
fi

runcmd ./configure $CONFOPTS
