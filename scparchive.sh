#! /bin/sh
# $Id$ 

ROOTDIR="public_html/poldek"
HOST="team.pld.org.pl"

if [ -z "$1" -o -z "$2" ]; then
    echo "usage: $(basename $0) FILE (${ROOTDIR}/)DESTDIR"
    exit;
fi

DESTDIR="${ROOTDIR}/$2"

scp -p $1 "${HOST}:${DESTDIR}" && \
ssh $HOST \
"(cd $DESTDIR && find . -type f | xargs md5sum > md5sums && \
    \$HOME/mkindexpage.pl *.* > index.html && find . -type f | xargs chmod 644 && ls -l)"
