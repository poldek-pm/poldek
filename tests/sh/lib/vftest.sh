#! /bin/sh
# $Id$

# support smb://[login[:passwd]@]host/service/[path/]file
# requires: basename, grep, sed, smbclient

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

cp $src $dest


#if echo $src | grep -q toc; then zcat $src; fi
