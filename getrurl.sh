#! /bin/sh

if [ ! -f capreq.h ]; then
    cd `dirname $0`;
    if [ ! -f capreq.h ]; then
	echo "`pwd`: not a poldek directory"
	exit 1
    fi 	
fi

if [ -d trurlib ]; then
    echo "./trurlib/ presented"
    exit 1
fi	

if [ -d ../trurlib ]; then 
     TRURLIB_VERSION=$(egrep 'T_AUTOMAKE\(trurlib, ' ../trurlib/configure.in | cut -d ' ' -f 2 | sed 's|)||')
     
     if [ -z "$TRURLIB_VERSION" ]; then
	echo "error extracting TRURLIB_VERSION";
	exit 1
     fi	

     if [ ! -f ../trurlib/trurlib-${TRURLIB_VERSION}.tar.gz ]; then
	(cd ../trurlib && ./autogen.sh && ./make dist)
     fi
     
     if [ ! -f  ../trurlib/trurlib-${TRURLIB_VERSION}.tar.gz ]; then
	exit 1
     fi
     
     echo "Unpacking ../trurlib/trurlib-${TRURLIB_VERSION}.tar.gz"
     tar xvpzf ../trurlib/trurlib-${TRURLIB_VERSION}.tar.gz && \
     mv trurlib-${TRURLIB_VERSION} trurlib
fi
