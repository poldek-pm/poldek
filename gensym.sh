#!/bin/sh

libNAME=libpoldek
if [ -n "$1" ]; then libNAME="lib$1"; fi

LIB=.libs/${libNAME}_allsyms.a
if [ ! -f $LIB ]; then echo "$LIB: no such file"; exit 1; fi

INCLUDES=$(grep ^libHEADERS Makefile.am | perl -ne 's|^libHEADERS\s*=\s*||; print')

HH="/tmp/hh";
> /tmp/hh
for i in $INCLUDES; do 
    gcc -E $i 2>/dev/null >>$HH;
done


symlist=$(nm --defined-only $LIB | pcregrep '^\w+\s+[tT]' | awk '{print $3}' | sort -u)

#out=$(basename $LIB .so)
#out="$out.sym"
out="${libNAME}.sym"
> $out
for s in $symlist; do
    if pcregrep -s "\b$s\(" $HH; then
       echo "+ $s"
       echo $s >> $out
    fi
done  
  
# libpoldek hack - add constans
if [ "$libNAME" = "libpoldek" ]; then
    nm --defined-only $LIB | pcregrep '^\w+\s+[RB]' | awk '{print $3}' | grep -v poldek_conf_| grep poldek_ | sort -u >> $out
fi


if [ "$libNAME" = "libpoclidek" ]; then
    nm --defined-only $LIB | pcregrep '^\w+\s+[D]' | awk '{print $3}' | grep poclidek_ | sort -u >> $out
fi
