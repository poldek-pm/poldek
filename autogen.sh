#!/bin/sh

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
CONFOPTS="$CONFOPTS $@"

runcmd git submodule init
runcmd git submodule update

# run autogen in submodules
(cd trurlib && ./autogen.sh)
(cd tndb && ./autogen.sh)

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
