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

runcmd aclocal
runcmd autoheader
runcmd automake --add-missing --no-force
runcmd autoconf

CONFOPTS="--enable-maintainer-mode --enable-compile-warnings $@"
runcmd ./configure $CONFOPTS
