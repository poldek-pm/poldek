#!/bin/sh
# Network fault test runner
# Starts the test servers, creates test data dynamically, runs tests, cleans up

set -e

VERBOSE="${VERBOSE:-}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PYTHON="${PYTHON:-python3}"

vprint() {
    if [ -n "$VERBOSE" ]; then
        echo "$@"
    fi
}

TEST_DATA_DIR="$(mktemp -d)"
PORT_FILE="$(mktemp)"

cleanup() {
    if [ -n "$SRV_PID" ]; then
        kill "$SRV_PID" 2>/dev/null || true
        wait "$SRV_PID" 2>/dev/null || true
    fi
    rm -rf "$TEST_DATA_DIR" "$PORT_FILE"
}
trap cleanup EXIT

printf 'Hello, World!\n' > "$TEST_DATA_DIR/small.txt"

if ! "$PYTHON" --version >/dev/null 2>&1; then
    echo "ERROR: Python 3 not found. Set PYTHON environment variable."
    exit 1
fi

vprint "Starting test servers..."
"$PYTHON" "$SCRIPT_DIR/net_servers.py" \
    --data-dir "$TEST_DATA_DIR" \
    --write-ports "$PORT_FILE" &
SRV_PID="$!"

sleep 1

if [ ! -s "$PORT_FILE" ]; then
    echo "ERROR: test servers did not start" >&2
    exit 1
fi
FTP_PORT=$(cut -d' ' -f1 "$PORT_FILE")
HTTP_PORT=$(cut -d' ' -f2 "$PORT_FILE")
vprint "FTP: ftp://127.0.0.1:$FTP_PORT  HTTP: http://127.0.0.1:$HTTP_PORT"

# vfile honours the proxy environment, which would bypass the test servers
unset http_proxy https_proxy ftp_proxy
unset HTTP_PROXY HTTPS_PROXY FTP_PROXY

export TEST_SERVER_HOST="127.0.0.1"
export TEST_FTP_PORT="$FTP_PORT"
export TEST_HTTP_PORT="$HTTP_PORT"
export TEST_DATA_DIR="$TEST_DATA_DIR"

if [ $# -eq 0 ]; then
    cd "$SCRIPT_DIR"
    if [ -x ./test_vfnet ]; then
        ./test_vfnet
    elif [ -x ./.libs/test_vfnet ]; then
        ./.libs/test_vfnet
    else
        echo "ERROR: test_vfnet not found. Run 'make test_vfnet' first."
        exit 1
    fi
else
    "$@"
fi
