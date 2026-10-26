#!/bin/sh
# Test poldek --no-ssl-verify against the HTTPS server started by run_http_tests.sh.
# Environment set by run_http_tests.sh: TEST_SERVER_HOST, TEST_HTTPS_PORT, TEST_DATA_DIR.

set -e

POLDEK_BIN="../../cli/poldek"
if [ ! -x "$POLDEK_BIN" ]; then
    echo "SKIP: poldek binary not found at $POLDEK_BIN"
    exit 0
fi

if [ -z "$TEST_HTTPS_PORT" ] || [ "$TEST_HTTPS_PORT" -eq 0 ]; then
    echo "SKIP: HTTPS server not available"
    exit 0
fi

host="${TEST_SERVER_HOST:-127.0.0.1}"
url="https://${host}:${TEST_HTTPS_PORT}/"

echo "Testing poldek CLI SSL verification..."

# Without --no-ssl-verify the self-signed cert must be rejected.
out=$("$POLDEK_BIN" --noconf -s "$url" --st pndir --cmd ls 2>&1) || true
if ! echo "$out" | grep -qi "certificate verify failed"; then
    echo "FAIL: poldek without --no-ssl-verify did not reject self-signed cert"
    echo "$out"
    exit 1
fi
echo "  without --no-ssl-verify: rejected self-signed cert (OK)"

# With --no-ssl-verify the certificate check must be bypassed.
out=$("$POLDEK_BIN" --noconf --no-ssl-verify -s "$url" --st pndir --cmd ls 2>&1) || true
if echo "$out" | grep -qi "certificate verify failed"; then
    echo "FAIL: poldek --no-ssl-verify still failed on self-signed cert"
    echo "$out"
    exit 1
fi
echo "  with --no-ssl-verify: accepted self-signed cert (OK)"
