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
runcmd ./getrurl.sh

runcmd aclocal
runcmd autoheader
runcmd autoconf
if [ ! -f ABOUT-NLS ]; then
    if which -- autopoint >/dev/null 2>&1 ; then
        runcmd autopoint --force
    else
        runcmd gettextize -f
    fi
fi
runcmd automake --add-missing 

# w/o
if [ ! -f depcomp ]; then 
    runcmd automake --add-missing Makefile
    (cd vfile && ln -sf ../depcomp .)
    (cd shell && ln -sf ../depcomp .)
fi

if [ -x ./trurlib/autogen.sh ]; then
	cd trurlib
	runcmd ./autogen.sh $CONFOPTS
	cd ..
fi

if [ -x ./tndb/autogen.sh ]; then
	cd tndb
	runcmd ./autogen.sh $CONFOPTS
	cd ..
fi

runcmd ./configure $CONFOPTS
