#!/bin/sh

LIB=.libs/libpoldek.so
INCLUDES="poldek.h poldek_ts.h pkg.h capreq.h pkgdir/pkgdir.h pkgdir/source.h sigint/sigint.h"


HH="/tmp/hh";
> /tmp/hh
for i in $INCLUDES; do 
    gcc -E $i 2>/dev/null >>$HH;
done


symlist=$(nm --defined-only .libs/libpoldek.so | pcregrep '^\w+\s+[tT]' | awk '{print $3}')




out=$(basename $LIB .so)
out="$out.sym"
> $out
for s in $symlist; do
    echo $s    
    if pcregrep -s "\b$s\(" $HH; then
       echo $s >> $out
    fi
done    
