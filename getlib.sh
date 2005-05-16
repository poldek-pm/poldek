#! /bin/sh

#! /bin/sh

do_configure() 
{
   nn="$1"
   [ -d $nn ] || exit 1
   [ -f $nn/configure ] || (cd $nn && ./autogen.sh --no-configure)
}

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

if [ "$mode" = "link" ]; then
        if [ -h $name ]; then do_configure $name; exit 0; fi
        if [ -d $name ]; then rm -rf $name; fi
fi

if [ "$mode" = "copy" ]; then
        if [ -h $name ]; then rm -f $name; fi
        if [ -d $name ]; then do_configure $name; exit 0; fi
fi     

if [ ! -d $name ]; then 
        if [ -n "$mode" -a $mode == "link" ]; then 
                ln ../$name -sf 
        else 
                cp -a ../$name .
    fi
fi

do_configure $name        

