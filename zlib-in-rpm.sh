#! /bin/sh
# $Id$
# Check if librpmio contains zlib linked in 

PATH="/bin:/sbin:/usr/bin:/usr/sbin"
rpmio=$(ldd /usr/bin/poldek | grep librpmio | awk '{print $3}')

if [ -n "$rpmio" -a -f "$rpmio" ]; then
    if ldd $rpmio | grep libz\.so >/dev/null; then
        exit 1
    fi
    exit 0
fi
exit 1  

