#!/bin/sh

POLDEK="$HOME/poldek/cli/poldek"
[ -f $POLDEK ] || POLDEK="$HOME/poldek/poldek/cli/poldek"
TMPDIR="${TMPDIR:-/tmp}"

echo "***************************************************************"
echo "** If you want to do single test only if package is movable do:"
echo "** poldek -n SOURCE --dn DESTINATION -uvt [PACKAGE...]"
echo "***************************************************************"
echo

SESSIONDIR=""
ARGV=""
MKIDX_ARGV=""

while test $# -gt 0 ; do
    case "${1}" in
        --destination)
            echo "use --dn"; exit 1
            shift;;

        --dn)
            shift
            SESSIONDIR="$SESSIONDIR ${1}"
            MKIDX_ARGV="$MKIDX_ARGV -n ${1}" 
            shift;;


        -n|-s)
            ARGV="$ARGV ${1}"
            shift
            SESSIONDIR="$SESSIONDIR ${1}"
            ARGV="$ARGV ${1}" 
            shift;;

        *)
            ARGV="$ARGV ${1}"; shift ;;
    esac
done

# create unique sessionid based on sources and destination
SESSIONDIR=$(echo $SESSIONDIR | md5sum | cut -f1 -d' ')
SESSIONDIR="$TMPDIR/mvpkg-$SESSIONDIR"

if [ -d $SESSIONDIR ]; then
    echo "** Using uncommitted move session (remove $SESSIONDIR to reset)"
else 
    echo "** Starting new move session $SESSIONDIR"
    mkdir -p $SESSIONDIR
    [ -d $SESSIONDIR ] || exit 1
    # create uncompressed index - zfseek() slooows down with random seeks
    $POLDEK $MKIDX_ARGV --mo=nodesc,nocompress --mkidx="$SESSIONDIR/packages.ndir"
fi

$POLDEK --destination $SESSIONDIR/packages.ndir -Oconflicts=no --nofetch --justdb \
 --log $SESSIONDIR/log.$$ $ARGV

[ $? -ne 0 ] && rm $SESSIONDIR/log.$$

echo
echo "** Session transacions:"
for i in $SESSIONDIR/log.*; do
    [ -f $i ] || continue
    echo "--"
    egrep '%(add|del)' $i
done
