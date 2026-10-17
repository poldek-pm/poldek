#!/bin/sh
# Every function declared EXPORT in poldek's headers and defined in one of its
# shared libraries is exported by it (they are built with -fvisibility=hidden).
LC_ALL=C; export LC_ALL       # byte order for sort/comm, no perl locale warnings
libs="../.libs/libpoldek.so ../cli/.libs/libpoclidek.so ../vfile/.libs/libvfile.so"
for lib in $libs; do
    [ -f "$lib" ] || { echo "$lib: not built, skipped"; exit 77; }
done

tmp=${TMPDIR:-/tmp}/poldek-exports.$$
trap 'rm -f $tmp.*' EXIT

(cd "${srcdir:-.}/.." &&
    find . -name '*.h' ! -path './trurlib/*' ! -path './tndb/*' ! -path './tests/*' ! -path './Cellar/*' |
    xargs perl -0ne 's{/\*.*?\*/}{}gs; print "$1\n" while /^\s*EXPORT\s[^;{(]*?\b(\w+)\s*\(/mg') |
    sort -u > $tmp.declared
[ -s $tmp.declared ] || { echo "no EXPORT declarations found"; exit 1; }
nm $libs | awk '$2 == "t" { print $3 }' | sort -u > $tmp.hidden

comm -12 $tmp.declared $tmp.hidden > $tmp.bad
if [ -s $tmp.bad ]; then
    echo "declared EXPORT but not exported:"
    cat $tmp.bad
    exit 1
fi
exit 0
