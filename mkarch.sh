#! /bin/sh

CPDIR=/z
make -f Makefile.extra mclean

if [ -w $CPDIR ]; then
    make -f Makefile.extra backup cparch=1 backupdir=$CPDIR
else 
    make -f Makefile.extra backup
fi
