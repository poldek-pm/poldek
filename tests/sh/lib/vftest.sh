#! /bin/sh
# $Id$

# test://path schema
# requires: sed, grep

if [ "$#" != "2" ]; then
    echo "usage: `basename $0` test://path/FILE DESTFILE"
    exit 1
fi

src=$1
src=$(echo $src | sed 's|test:/||')
dest=$2

if [ ! -f $src ]; then
    echo "$src: no such file"
    exit 1
fi

POLDEK_TESTING_DENIED_FILES=${POLDEK_TESTING_DENIED_FILES:-""}
for f in $POLDEK_TESTING_DENIED_FILES; do
    if echo $src | grep -qP "$f$"; then
       echo "DENIED FILE $f reqested ($src)"
       exit 1
    fi
done

POLDEK_TESTING_DENIED_DEST=${POLDEK_TESTING_DENIED_DEST:-""}
for f in $POLDEK_TESTING_DENIED_DEST; do
    if echo $dest | grep -qE "$f"; then
       echo "DENIED DEST $f reqested"
       exit 1
    fi
done


#echo $src $dest
# Symlink packages, copy other files
if echo $src | grep -qE '.rpm$'; then
    ln -sf $src $dest
else
    cp $src $dest
fi

# display toc
#if echo $src | grep -q toc; then zcat $src; fi
