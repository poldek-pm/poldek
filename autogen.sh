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
    echo "Executing: $@"
    $@
    if [ $? -ne 0 ]; then
	echo "failed"
	exit 1
    fi
}

CONFOPTS="--enable-maintainer-mode --enable-compile-warnings"

getlib_mode="link"
if [ -n "$1" -a "$1" = "makedist" ]; then
    rm -f trurlib tndb
    getlib_mode="cp"
    shift
fi
CONFOPTS="$CONFOPTS $@"
runcmd ./getlib.sh trurlib $getlib_mode
runcmd ./getlib.sh tndb    $getlib_mode

# generate po/POTFILES.in
make -f Makefile.extra POTFILES_in
# replaces gettextize
runcmd autopoint --force
runcmd libtoolize --force --automake
runcmd aclocal -I m4
runcmd autoheader
runcmd autoconf
runcmd automake --add-missing -a -c -f --foreign

# w/o
if [ ! -f depcomp ]; then 
    runcmd automake --add-missing Makefile
    (cd vfile && ln -sf ../depcomp .)
    (cd shell && ln -sf ../depcomp .)
fi

runcmd ./configure --sysconfdir=/etc $CONFOPTS
