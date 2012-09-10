#! /bin/sh
#
# Put this script on the first disk of dist set
# 

PATH="/bin/:/usr/bin:/sbin:/usr/sbin"

whereiam=$(dirname $0)

if [ "${whereiam}." = ".." ]; then
    whereiam=$(pwd)
fi

cmd=poldek

if [ -f "${whereiam}/.poldekrc" ]; then
    cmd="$cmd --conf ${whereiam}/.poldekrc"
fi

if [ -f "${whereiam}/.poldekpri.conf" ]; then
    cmd="$cmd --priconf ${whereiam}/.poldekpri.conf"
fi

VF_MNTPOINT="$whereiam"
export VF_MNTPOINT
$cmd $@
