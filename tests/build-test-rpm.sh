#!/bin/sh
# $Id$
# Script for quick build fake foo-packages

name=
version=
release="1"
requires=
provides=
files=

usage_and_exit() {
    echo "Usage $0 -n NAME -v VERSION-[RELEASE [-p PROVIDES] [-r REQUIRES] [-f file] "
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

        -f)
            shift; files="$files ${1} "; shift ;;

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

SPEC="/tmp/$name.spec"
> $SPEC
echo "Building $name $version-$release"
echo "Name: $name" >> $SPEC
echo "Version: $version" >> $SPEC
echo "Release: $release" >> $SPEC
echo "Summary: $name" >> $SPEC
echo "Group: System" >> $SPEC
echo "License: foo" >> $SPEC
echo "BuildArch: noarch" >> $SPEC
echo "BuildRoot: /tmp/%{name}-%{version}-root-%(id -u -n)" >> $SPEC
[ -n "$provides" ] &&  echo "Provides: $provides" >> $SPEC
[ -n "$requires" ] && echo "Requires: $requires" >> $SPEC

echo -e "%description\n$name" >> $SPEC
echo -e "%prep\n%pre\n" >> $SPEC

if [ -n "$files" ]; then
    echo "%install" >> $SPEC
    for f in $files; do
        dn=$(dirname $f)
        echo "mkdir -p \$RPM_BUILD_ROOT/$dn" >> $SPEC
        echo "touch \$RPM_BUILD_ROOT/$f" >> $SPEC
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
rpmbuild -bb $SPEC
