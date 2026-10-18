#!/bin/sh
# Two checks on the functions and variables declared EXPORT in poldek's headers
# and defined in one of its shared libraries (built with -fvisibility=hidden):
#
#   - declared, but the definition is hidden: the library promises the symbol and
#     does not deliver it, so a program linking it fails with an undefined reference
#   - declared, but defined nowhere: the header promises a symbol that no library has,
#     which reads like a missing export rather than a missing function
#
# Names inside `#if 0` are ignored (nothing builds them, e.g. pkgdir_clean_cache()).
LC_ALL=C; export LC_ALL       # byte order for sort/comm, no perl locale warnings
libs="../.libs/libpoldek.so ../cli/.libs/libpoclidek.so ../vfile/.libs/libvfile.so"
for lib in $libs; do
    [ -f "$lib" ] || { echo "$lib: not built, skipped"; exit 77; }
done
# without nm there is nothing to compare against and the test would pass vacuously
command -v nm >/dev/null || { echo "nm: not found, skipped"; exit 77; }

tmpdir=${TMPDIR:-/tmp}
if command -v mktemp >/dev/null; then
    # a name of our own: $$ alone collides when two test runs share a $TMPDIR
    tmpdir=$(mktemp -d "$tmpdir/polkdek-exports.XXXXXX") || exit 77
else
    tmpdir="$tmpdir/polkdek-exports.$$"
    mkdir "$tmpdir" || exit 77
fi
trap 'rm -rf "$tmpdir"' EXIT
tmp=$tmpdir/list

# EXPORT declarations of functions (name followed by '(') and of variables
# (name followed by '[' or ';')
(cd "${srcdir:-.}/.." &&
    find . -name '*.h' ! -path './trurlib/*' ! -path './tndb/*' ! -path './tests/*' ! -path './Cellar/*' |
    xargs perl -0ne 's{/\*.*?\*/}{}gs; s{^[ \t]*#[ \t]*if[ \t]+0\b.*?^[ \t]*#[ \t]*endif\b}{}gms;
                   print "$1\n" while /^\s*EXPORT\s[^;{(]*?\b(\w+)\s*(?:[\(\[]|;)/mg') |
    sort -u > $tmp.declared
[ -s $tmp.declared ] || { echo "no EXPORT declarations found"; exit 1; }

nm $libs | awk 'NF == 3 { print $3 }' | sort -u > $tmp.defined
[ -s $tmp.defined ] || { echo "nm found no symbols"; exit 1; }
# any local symbol: 't' functions, but also 'r'/'d'/'b' for hidden variables
nm $libs | awk '$2 ~ /^[a-z]$/ { print $3 }' | sort -u > $tmp.hidden

comm -12 $tmp.declared $tmp.hidden > $tmp.not_exported
comm -23 $tmp.declared $tmp.defined > $tmp.nowhere

rc=0
if [ -s $tmp.not_exported ]; then
    echo "declared EXPORT but not exported:"
    cat $tmp.not_exported
    rc=1
fi
if [ -s $tmp.nowhere ]; then
    echo "declared EXPORT but defined nowhere:"
    cat $tmp.nowhere
    rc=1
fi
exit $rc
