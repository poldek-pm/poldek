#! /bin/sh

PATH="/bin/:/usr/bin:/sbin:/usr/sbin"
if [ ! -f capreq.h ]; then
    cd `dirname $0`;
    if [ ! -f capreq.h ]; then
	echo "$0: `pwd`: not a poldek directory"
	exit 1
    fi 	
fi

name=$1
mode=$2

[ -n "$name" ] || exit 1

if [ -n "$mode" -a $mode = "link" ]; then 
    if [ -d ../$name ]; then
		ln ../$name -sf 
    fi
fi

if [ -d $name ]; then 
    [ -f $name/configure ] || (cd $name && ./autogen.sh)
    exit;
fi

if [ -d ../$name ]; then 
     LIB_VERSION=$(egrep "(AC_INIT|AM_INIT_AUTOMAKE)\($name, " ../$name/configure.in | cut -d ' ' -f 2 | sed 's|)||')
     if [ -z "$LIB_VERSION" ]; then
	echo "$0: error extracting $name's VERSION";
	exit 1
     fi	

     if [ ! -f ../${name}/${name}-${LIB_VERSION}.tar.gz ]; then
	(cd ../${name} && ./autogen.sh && make dist)
     fi
     
     if [ ! -f ../${name}/${name}-${LIB_VERSION}.tar.gz ]; then
	"../${name}/${name}-${LIB_VERSION}.tar.gz: no such file"
	exit 1
     fi
     
     echo "Unpacking ../${name}/${name}-${LIB_VERSION}.tar.gz"
     tar xvpzf ../${name}/${name}-${LIB_VERSION}.tar.gz && \
     mv ${name}-${LIB_VERSION} ${name}
fi
