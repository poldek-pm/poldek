#!/bin/sh
# Check poldek's own sources with cppcheck; run as "make lint".
#
#   make lint        fast checks (warnings, performance, portability)
#   make lint-full   also reports functions unused anywhere in the tree
#
# Direct use:
#
#   tests/cppcheck/cppcheck.sh [--unused] [SRCDIR [BUILDDIR]]
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
# The analysis cache lives in $CPPCHECK_CACHE, or in tmp/cppcheck/build below
# the build directory when it is not set.  CI points it at a persisted path;
# deleting the directory only costs a full re-analysis.

set -e

unused=0
if [ "${1:-}" = "--unused" ] || [ "${1:-}" = "-u" ]; then
    unused=1
    shift
fi

srcdir=${1:-.}
builddir=${2:-.}

command -v cppcheck >/dev/null || {
    echo "cppcheck is not installed" >&2
    exit 1
}

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

# cppcheck's analysis cache; remove it to force a full re-analysis
cache=${CPPCHECK_CACHE:-"${TMPDIR:-/tmp}/poldek-cppcheck-build"}
mkdir -p "$cache"

# alloca() is used on purpose; NULL as the sentinel of the trurl varargs
# helpers is the project idiom (on supported platforms every pointer has the
# same representation); the trurlib headers belong to the submodule
suppressions="
    --suppress=allocaCalled
    --suppress=varFuncNullUB
    --suppress=*:trurlib/*
    --suppress=unusedFunction:trurlib/*
"

set -- \
    --quiet \
    --std=c99 --language=c --inline-suppr \
    --suppress=missingIncludeSystem \
    --cppcheck-build-dir="$cache" \
    -DENABLE_TRACE=0 -DHAVE_CONFIG_H -D__GNUC__=1 \
    -Itests/cppcheck -I. -I"$builddir" -Icli -Iconf -Iinstall3 -Ipkgdir -Ipm -Isigint -Ivfile \
    -Itrurlib/include -Itndb -I/usr/include/rpm \
    --file-list="$files"

# shellcheck disable=SC2086 -- the list is a set of options, not a filename
cppcheck --enable=warning,performance,portability --error-exitcode=1 \
    $suppressions "$@"

# staticFunction is style noise; the unused functions that are left are
# autoconf markers and are suppressed where they are defined
if [ "$unused" = 1 ]; then
    # shellcheck disable=SC2086
    cppcheck --enable=unusedFunction --error-exitcode=1 \
        --suppress=staticFunction $suppressions "$@"
fi
