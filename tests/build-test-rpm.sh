#!/bin/sh
# $Id$
# Script for quick build fake foo-packages

name=
version=
release="1"
requires=
provides=

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

        *)
            echo "unknown option ${1}"; exit 1;
    esac
done

[ -n "$name" -a -n "$version" ] || exit 1;

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
[ -n "$provides" ] &&  echo "Provides: $provides" >> $SPEC
[ -n "$requires" ] && echo "Requires: $requires" >> $SPEC

echo -e "%description\n$name" >> $SPEC

echo -e "%prep\n%pre\n%files\n%defattr(644,root,root,755)" >> $SPEC
echo -e "%clean\nrm -rf $$RPM_BUILD_ROOT" >> $SPEC
rpmbuild -bb $SPEC
