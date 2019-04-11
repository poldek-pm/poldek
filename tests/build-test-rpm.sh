#!/bin/sh
# $Id$
# Script for quick build fake foo-packages

name=
version=
release="1"
requires=
provides=
suggests=
obsoletes=
conflicts=
files=
arch="noarch"
rpmdir="repo"

COMMAND="$0 $@"

usage_and_exit() {
    echo "Usage $0 -n NAME -v [EPOCH:]VERSION-[RELEASE] [-a ARCH] [-p PROVIDES] [-r REQUIRES] [-s SUGGESTS] [-f file] "
    exit 1
}

while test $# -gt 0 ; do
    case "${1}" in
        -n)
            shift; name="${1}"; shift ;;

        -v)
            shift; version="${1}"; shift ;;

        -p)
            shift; provides="$provides ${1},"; shift ;;

        -r)
            shift; requires="$requires ${1},"; shift ;;

        -s)
            shift; suggests="$suggests ${1},"; shift ;;

        -o)
            shift; obsoletes="$obsoletes ${1},"; shift ;;

        -c)
            shift; conflicts="$conflicts ${1},"; shift ;;

        -f)
            shift; files="$files ${1} "; shift ;;

	-a)
	    shift; arch="${1}"; shift;;

        -d)
            shift; rpmdir="${1}"; shift;;

        *)
            echo "unknown option ${1}"; exit 1;
    esac
done

[ -n "$name" -a -n "$version" ] || usage_and_exit;

if echo $version | grep -q '-'; then
    release=$(echo $version | cut -f 2 -d -)
    version=$(echo $version | cut -f 1 -d -)
    [ -n "$version" -a -n "$release" ] || exit 1;
fi

if echo $version | grep -q ':'; then
    epoch=$(echo $version | cut -f 1 -d :)
    version=$(echo $version | cut -f 2 -d :)
    [ -n "$version" -a -n "$epoch" ] || exit 1;
fi

TMPDIR="${TMPDIR:-/tmp}"

SPEC="$TMPDIR/$name.$$.spec"
echo "Building $name-$version-$release.$arch.rpm"

echo > $SPEC
echo "%define _noautoreq libc.so.6 rtld" >> $SPEC
echo "AutoReqProv: no" >> $SPEC # rpmorg
echo "Name: $name" >> $SPEC
echo "Version: $version" >> $SPEC
echo "Release: $release" >> $SPEC
if [ -n "$epoch" ]; then echo "Epoch: $epoch" >> $SPEC; fi
echo "Summary: $name" >> $SPEC
echo "Summary(pl): pl $name" >> $SPEC
echo "Summary(de): de $name" >> $SPEC
echo "Group: System" >> $SPEC
echo "License: foo" >> $SPEC
#echo "BuildArch: $arch" >> $SPEC
echo "BuildRoot: /tmp/%{name}-%{version}-root-%(id -u -n)" >> $SPEC
[ -n "$provides" ] &&  echo "Provides: $provides" >> $SPEC
[ -n "$requires" ] && echo "Requires: $requires" >> $SPEC
[ -n "$suggests" ] && echo "Suggests: $suggests" >> $SPEC
[ -n "$obsoletes" ] && echo "Obsoletes: $obsoletes" >> $SPEC
[ -n "$conflicts" ] && echo "Conflicts: $conflicts" >> $SPEC

echo -e "%description\nPackage build by $COMMAND\n" >> $SPEC

echo -e "%description -l pl\n(pl)Package build by $COMMAND\n" >> $SPEC

echo -e "%description -l de\n(de)Package build by $COMMAND\n" >> $SPEC

echo -e "%prep\n%pre\n" >> $SPEC

if [ -n "$files" ]; then
    echo "%install" >> $SPEC
    for f in $files; do
        dn=$(dirname $f)
        echo "mkdir -p \$RPM_BUILD_ROOT/$dn" >> $SPEC
        bn=$(basename $f)
        if [ -f $bn ]; then
            echo "cp $(pwd)/$bn \$RPM_BUILD_ROOT$dn" >> $SPEC
        else
            echo "touch \$RPM_BUILD_ROOT/$f" >> $SPEC
        fi
    done
fi

echo -e "%files\n%defattr(644,root,root,755)" >> $SPEC
if [ -n "$files" ]; then
    for f in $files; do
        dn=$(dirname $f)
        echo "%dir $dn" >> $SPEC
        echo "$f" >> $SPEC
    done
fi

echo -e "%clean\nrm -rf \$RPM_BUILD_ROOT" >> $SPEC
[ ! -d "$rpmdir" ] && rpmdir="$TMPDIR"
rpmbuild --define 'debug_package nil' --define '__spec_install_post_chrpath echo' --define "_rpmdir $rpmdir" --target $arch -bb $SPEC

# rpmorg stores packages in $arch subdir
if [ -d "$rpmdir/$arch" ]; then
    mv -f $rpmdir/$arch/$name-*.rpm $rpmdir/ 2>/dev/null
fi
