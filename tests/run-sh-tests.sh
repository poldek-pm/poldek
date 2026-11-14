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

# each check-sh keeps its files in a directory of its own, removed when every
# run passes; mktemp makes it at mode 700, as the runs install packages from it
TOP=$(mktemp -d "${TMP:-${TMPDIR:-/tmp}}/poldek-sh-tests.XXXXXX") || exit 1

# the packages of the source repository the scripts install from, built once;
# each run gets a copy, as its script builds the sh package into it again
SOURCE_REPO=$TOP/repo
mkdir -p $SOURCE_REPO $TOP/build
TMP=$TOP/build TMPDIR=$TOP/build sh -c '. ./sh/lib/repo-setup; SOURCE_REPO=$1; build_repo_packages' \
    sh $SOURCE_REPO > $TOP/repo.log 2>&1 ||
    echo "$0: building the source repository failed, see $TOP/repo.log" >&2

nth=0
nok=0
for i in sh/[0-9][0-9]*; do
    [ -f $i ] || continue
    compr="gz"
    suffixed_compr=""
    suffixed_imode=""
    install_modes=""

    # run with each compression method if test uses indexes
    if grep -q compr-setup $i; then
        compr="gz zst none"
        suffixed_compr="1"
    fi

    if grep -q INSTALL_MODE $i; then
        install_modes="dir rpm"
        suffixed_imodel="1"
    else
        install_modes="none"
        suffixed_imodel=""
    fi

    for m in $install_modes; do
        if [ "$m" = "none" ]; then
           unset INSTALL_MODE
        else
            INSTALL_MODE="$m"
            export INSTALL_MODE
        fi
        isuffix=""
        [ -n "$suffixed_imodel" ] && isuffix=" (mode=$INSTALL_MODE)"

        for c in $compr; do
	    COMPR="$c"
            export COMPR
	    csuffix=""
            [ -n "$suffixed_compr" ] && csuffix=" (compr=$COMPR)"

	    nth=$(expr $nth + 1)
	    d=$TOP/$(basename $i)${suffixed_compr:+.$COMPR}${suffixed_imodel:+.$INSTALL_MODE}
	    mkdir $d && cp -a $SOURCE_REPO $d/repo
	    TMP=$d TMPDIR=$d sh $i -n 6 --source-repo $d/repo >> $LOG
	    if [ $? -eq 0 ]; then
	        nok=$(expr $nok + 1)
	        echo "${GREEN}PASS: $i$csuffix$isuffix$NC"
	    else
	        echo "${RED} FAIL: $i$csuffix$isuffix$NC (files in $d)"
	    fi
        done
    done
done

echo "====================================="
echo " Passed $nok tests of total $nth"
echo "====================================="

[ "$nth" -eq 0 -o "$nth" != "$nok" ] && exit 1
# the scripts make some of their files as root when they test with sudo
sh -c '. ./sh/lib/repo-setup; SUDO_INFO_LOGGED=1; sudo_up; $SUDO rm -rf "$1"' sh $TOP ||
    echo "$0: could not remove $TOP" >&2
exit 0
