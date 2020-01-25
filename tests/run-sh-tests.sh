#!/bin/sh

GREEN='\033[1;32m'
RED='\033[0;31m'
NC='\033[0m'

[ ! -t 1 ] && {  echo "not tty"; GREEN=""; RED=""; NC=""; }

#echo "Running *.sh tests";
find sh -name \*~ | xargs -r rm -f

LOG=sh-tests.log
> $LOG

nth=0
nok=0
for i in sh/[0-9][0-9]*; do
    [ -f $i ] || continue
    compr="gz"
    suffixed=""

    # run with each compression method if test uses indexes
    grep -q compr-setup $i
    [ $? -eq 0 ] && compr="gz zst none" && suffixed="1"

    for c in $compr; do
	COMPR="$c"; export COMPR
	suffix=""
        [ -n "$suffixed" ] && suffix=" (compr=$c)"

	nth=$(expr $nth + 1)
	sh $i -n 4 >> $LOG
	if [ $? -eq 0 ]; then
	    nok=$(expr $nok + 1)
	    echo "${GREEN}PASS: $i$suffix$NC"
	else
	    echo "${RED} FAIL: $i$suffix$NC"
	fi
    done
done

echo "====================================="
echo " Passed $nok tests of total $nth"
echo "====================================="
