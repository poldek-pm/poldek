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
[ -d ../$name ] || exit 1

if [ ! -d $name ]; then 
	if [ -n "$mode" -a $mode == "link" ]; then 
		ln ../$name -sf 
	else 
		cp -a ../$name .
    fi
fi
	
[ -d $name ] || exit 1
[ -f $name/configure ] || (cd $name && ./autogen.sh --no-configure)


