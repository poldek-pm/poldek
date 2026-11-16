#!/bin/sh
# Check poldek's own sources with cppcheck; run as "make lint".
#
#   make lint        fast checks (warnings, performance, portability)
#   make lint-full   also reports functions unused anywhere in the tree
#
#   make lint LINT_JOBS=4   run four cppcheck instances at a time
#
# Direct use:
#
#   tests/cppcheck/cppcheck.sh [-j JOBS] [--unused] [SRCDIR [BUILDDIR]]
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
# The analysis cache lives in $CPPCHECK_CACHE, or in ${TMPDIR:-/tmp}/
# poldek-cppcheck-build when it is not set.  CI points it at a persisted path
# (see .github/workflows/build.yml); deleting the directory only costs a full
# re-analysis.  lint-full uses a "-full" directory of its own: the enabled
# checks are part of the cache key, and whole-program analysis (unusedFunction)
# is not cached at all, so a shared directory would make each switch between
# the two targets re-analyse the whole tree.

set -e

usage() {
    echo "usage: $0 [-j JOBS] [--unused] [SRCDIR [BUILDDIR]]" >&2
    exit 2
}

jobs=1
unused=0
while [ $# -gt 0 ]; do
    case $1 in
        -j)          [ $# -ge 2 ] || usage
                     jobs=$2
                     shift 2
                     ;;
        -j*)         jobs=${1#-j}
                     shift
                     ;;
        -u|--unused) unused=1
                     shift
                     ;;
        --)          shift
                     break
                     ;;
        -*)          usage
                     ;;
        *)           break
                     ;;
    esac
done

if ! [ "$jobs" -ge 1 ] 2>/dev/null; then
    echo "$0: -j takes a number of cppcheck instances, 1 or more" >&2
    exit 2
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
    \( -path ./python -o -path ./po \
       -o -path ./Cellar -o -path ./tmp -o -path ./tests \
       -o -path ./vfile/tests \) -prune \
    -o -name 'test_*.c' -prune \
    -o -name 'sample.c' -prune \
    -o -name '*.c' -print | sort > "$files"

# cppcheck's analysis cache; remove it to force a full re-analysis
cache=${CPPCHECK_CACHE:-"${TMPDIR:-/tmp}/poldek-cppcheck-build"}

# the enabled checks are part of cppcheck's cache key, and whole-program
# analysis (--unused) is not cached, so keep lint-full out of lint's cache
if [ "$unused" = 1 ]; then
    cache="$cache-full"
fi
mkdir -p "$cache"

# alloca() is used on purpose; NULL as the sentinel of the trurl varargs
# helpers is the project idiom (on supported platforms every pointer has the
# same representation)
suppressions="
    --suppress=allocaCalled
    --suppress=varFuncNullUB
"

set -- \
    -j"$jobs" \
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
