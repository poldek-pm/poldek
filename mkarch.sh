#! /bin/sh
make -f Makefile.extra mclean
make -f Makefile.extra backup cparch=1 backupdir=/z/
