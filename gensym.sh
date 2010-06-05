#!/bin/sh

set -e

# $0 libpoldek sourcedir targetdir
libNAME=libpoldek
if [ -n "$1" ]; then
	libNAME="lib$1"
fi
if [ -n "$2" ]; then
	sourcedir="$2"
else
	sourcedir="."
fi
if [ -n "$3" ]; then
	targetdir="$3"
else
	targetdir="."
fi
if [ -n "$4" ]; then
	topsourcedir="$4"
else
	topsourcedir="."
fi

pcregrep --help > /dev/null || echo "error: pcregrep not found, failing!" && exit 1

LIB="$targetdir/.libs/${libNAME}_allsym.a"
out="$targetdir/${libNAME}.sym"

if [ ! -f $LIB ]; then echo "$LIB: no such file"; exit 1; fi

INCLUDES=$(grep "^libHEADERS" $sourcedir/Makefile.am | perl -ne 's|^libHEADERS\s*=\s*||; print')

HH="$(mktemp gensym.XXXXXXXXXX)"
> $HH
for i in $INCLUDES; do 
    gcc -E -I$topsourcedir $sourcedir/$i 2>/dev/null >>$HH || :
done


symlist=$(nm --defined-only $LIB | pcregrep '^\w+\s+[tT]' | awk '{print $3}' | sort -u)

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

# provide rpmlog to cover rpmlib's one -- haaack 
    nm --defined-only $LIB | pcregrep '\brpmlog\b' | awk '{print $3}' >> $out

# hack, this is not found automaticly
    echo source_TYPE_GROUP >> $out
fi


if [ "$libNAME" = "libpoclidek" ]; then
    nm --defined-only $LIB | pcregrep '^\w+\s+[D]' | awk '{print $3}' | grep poclidek_ | sort -u >> $out
fi

rm $HH
