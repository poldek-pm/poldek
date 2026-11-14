#!/bin/sh
# Check poldek's own sources with cppcheck; run as "make lint".
#
# Tests are skipped: they use the check macros cppcheck cannot follow, and
# cppcheck-suppress comments there would only add noise.  ENABLE_TRACE is 0,
# so trace-only code is not analysed.
#
# cppcheck/trurl/{nassert,ndie}.h are shims that tell cppcheck n_assert() and
# n_die() do not return; without them every guarded dereference looks unsafe.
#
# cppcheck does not define __GNUC__; without it compiler.h turns
# __attribute__ into a no-op and the real function attributes are lost.
#
# The suppressions below cover the categories that are deliberate or noise in
# this tree; there are no inline suppressions in the sources.

set -e

command -v cppcheck >/dev/null || {
    echo "cppcheck is not installed" >&2
    exit 1
}

srcdir=${1:-.}
builddir=${2:-.}

builddir=$(cd "$builddir" && pwd)
cd "$srcdir"

files=$(mktemp "${TMPDIR:-/tmp}/poldek-lint.XXXXXX")
trap 'rm -f "$files"' EXIT

find . \
    \( -path ./trurlib -o -path ./tndb -o -path ./python -o -path ./po \
       -o -path ./Cellar -o -path ./tmp -o -path ./tests \
       -o -path ./vfile/tests \) -prune \
    -o -name 'test_*.c' -prune \
    -o -name 'sample.c' -prune \
    -o -name '*.c' -print > "$files"

# alloca() is used on purpose, n_malloc() aborts on OOM, and the trurlib
# headers belong to the submodule
suppressions="
    --suppress=allocaCalled
    --suppress=nullPointerOutOfMemory
    --suppress=nullPointerOutOfResources
    --suppress=varFuncNullUB
    --suppress=*:trurlib/*
"

set -- \
    --quiet \
    --std=c99 --language=c --inline-suppr \
    --suppress=missingIncludeSystem \
    -DENABLE_TRACE=0 -DHAVE_CONFIG_H -D__GNUC__=1 \
    -Itests/cppcheck -I. -I"$builddir" -Icli -Iconf -Iinstall3 -Ipkgdir -Ipm -Isigint -Ivfile \
    -Itrurlib/include -Itndb -I/usr/include/rpm \
    --file-list="$files"

# shellcheck disable=SC2086 -- the list is a set of options, not a filename
cppcheck --enable=warning,performance,portability --error-exitcode=1 \
    $suppressions "$@"

# staticFunction is style noise; the unused functions that are left are
# autoconf markers and are suppressed where they are defined
# shellcheck disable=SC2086
cppcheck --enable=unusedFunction --error-exitcode=1 \
    --suppress=staticFunction $suppressions "$@"
