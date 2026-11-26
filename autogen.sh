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

# the submodules at the commits this tree pins, as a release and CI need;
# AUTOGEN_SUBMODULES=remote takes the tips of their branches instead
if [ "$AUTOGEN_SUBMODULES" = "remote" ]; then
    runcmd git submodule update --init --remote
else
    runcmd git submodule update --init
fi

# run autogen in submodules
(cd trurlib && ./autogen.sh --no-configure)
(cd tndb && ./autogen.sh --no-configure)

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
