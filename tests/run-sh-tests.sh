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
    suffixed_compr=""
    suffixed_imode=""
    install_modes="dir"

    # run with each compression method if test uses indexes
    if grep -q compr-setup $i; then
        compr="gz zst none"
        suffixed_compr="1"
    elif grep -q INSTALL_MODE $i; then
        install_modes="dir rpm"
        suffixed_imodel="1"
    fi

    for m in $install_modes; do
        INSTALL_MODE="$m"
        export INSTALL_MODE
        isuffix=""
        [ -n "$suffixed_imodel" ] && isuffix=" (mode=$INSTALL_MODE)"

        for c in $compr; do
	    COMPR="$c"
            export COMPR
	    csuffix=""
            [ -n "$suffixed_compr" ] && csuffix=" (compr=$COMPR)"

	    nth=$(expr $nth + 1)
	    sh $i -n 6 >> $LOG
	    if [ $? -eq 0 ]; then
	        nok=$(expr $nok + 1)
	        echo "${GREEN}PASS: $i$csuffix$isuffix$NC"
	    else
	        echo "${RED} FAIL: $i$csuffix$isuffix$NC"
	    fi
        done
    done
done

echo "====================================="
echo " Passed $nok tests of total $nth"
echo "====================================="

[ "$nth" -eq 0 -o "$nth" != "$nok" ] && exit 1
exit 0
