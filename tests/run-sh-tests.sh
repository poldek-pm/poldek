#!/bin/sh

GREEN=""
RED=""
NC=""

if [ -t 1 ]; then
    [ -z "$TERM" ] && export TERM=xterm
    nc=$(tput colors)
    if [ -n "$nc" ] && [ "$nc" -ge 8 ]; then
        GREEN="$(tput setaf 2)"
        RED="$(tput setaf 1)"
        NC="$(tput sgr0)"
    fi
fi

#echo "Running *.sh tests";
rm -f sh/*~

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

[ "$nth" -eq 0 -o "$nth" != "$nok" ] && exit 1
exit 0
