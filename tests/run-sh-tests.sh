#!/bin/sh
# usage: run-sh-tests.sh [-j JOBS]
# Runs the scripts in sh/, JOBS runs at a time (1 by default); the results and
# sh-tests.log keep the order of the runs, whatever order the runs end in.

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

jobs=1
while getopts j: opt; do
    case $opt in
        j) jobs=$OPTARG ;;
        *) echo "usage: $0 [-j JOBS]" >&2; exit 2 ;;
    esac
done
if ! [ "$jobs" -ge 1 ] 2>/dev/null; then
    echo "$0: -j takes a number of runs, 1 or more" >&2
    exit 2
fi

# no scripts means the runner was started from the wrong directory: say so and
# do not build the source repository for nothing
have_scripts=
for i in sh/[0-9][0-9]*; do
    if [ -f "$i" ]; then
        have_scripts=1
        break
    fi
done
[ -n "$have_scripts" ] || {
    echo "$0: no test scripts in sh/" >&2
    exit 1
}

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
    sh $SOURCE_REPO > $TOP/repo.log 2>&1 || {
    echo "$0: building the source repository failed, see $TOP/repo.log" >&2
    tail -20 $TOP/repo.log >&2
    exit 1
}
RUNS=$TOP/runs
> $RUNS

# one line for each run: <directory> <script> <install mode> <compression> <label>
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
        isuffix=""
        [ -n "$suffixed_imodel" ] && isuffix=" (mode=$m)"

        for c in $compr; do
	    csuffix=""
            [ -n "$suffixed_compr" ] && csuffix=" (compr=$c)"

	    d=$TOP/$(basename $i)${suffixed_compr:+.$c}${suffixed_imodel:+.$m}
	    mkdir $d
	    echo "$d $i $m $c$csuffix$isuffix" >> $RUNS
        done
    done
done

# the result of each run, once it and all runs before it have ended
report() {
    nth=0
    nok=0
    while read d i m c label; do
        while [ ! -s $d/rc ]; do sleep 1; done
        nth=$(expr $nth + 1)
        cat $d/log >> $LOG
        cat $d/err >&2
        if [ "$(cat $d/rc)" = 0 ]; then
            nok=$(expr $nok + 1)
            echo "${GREEN}PASS: $i${label:+ $label}$NC"
        else
            echo "${RED} FAIL: $i${label:+ $label}$NC (files in $d)"
        fi
    done < $RUNS

    echo "====================================="
    echo " Passed $nok tests of total $nth"
    echo "====================================="

    [ "$nth" -ne 0 ] && [ "$nth" = "$nok" ]
}

report &
reporter=$!

[ -s $RUNS ] && xargs -P $jobs -L 1 sh -c '
    d=$1
    cp -a ${d%/*}/repo $d/repo
    if [ $3 = none ]; then unset INSTALL_MODE; else INSTALL_MODE=$3; export INSTALL_MODE; fi
    COMPR=$4 TMP=$d TMPDIR=$d sh $2 -n 6 --source-repo $d/repo > $d/log 2> $d/err
    echo $? > $d/rc' sh < $RUNS

# no exit code from a run xargs did not get to or that got killed
while read d rest; do
    [ -s $d/rc ] || echo none > $d/rc
done < $RUNS

wait $reporter || exit 1
# the scripts make some of their files as root when they test with sudo
sh -c '. ./sh/lib/repo-setup; SUDO_INFO_LOGGED=1; sudo_up; $SUDO rm -rf "$1"' sh $TOP ||
    echo "$0: could not remove $TOP" >&2
