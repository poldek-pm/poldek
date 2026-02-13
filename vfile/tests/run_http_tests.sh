#!/bin/sh
# HTTP/HTTPS Test Runner
# Starts test servers, creates test data dynamically, runs tests, cleans up

set -e

# Check for verbose mode
VERBOSE="${VERBOSE:-}"

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PYTHON="${PYTHON:-python3}"

# Print only if verbose
vprint() {
    if [ -n "$VERBOSE" ]; then
        echo "$@"
    fi
}

# Create temp directory for test data
TEST_DATA_DIR="$(mktemp -d)"

# Cleanup function
cleanup() {
    vprint "Cleaning up..."
    if [ -n "$HTTP_PID" ]; then
        kill "$HTTP_PID" 2>/dev/null || true
        wait "$HTTP_PID" 2>/dev/null || true
    fi
    if [ -n "$HTTPS_PID" ]; then
        kill "$HTTPS_PID" 2>/dev/null || true
        wait "$HTTPS_PID" 2>/dev/null || true
    fi
    rm -rf "$TEST_DATA_DIR"
}
trap cleanup EXIT

# Create test files dynamically
vprint "Creating test files in $TEST_DATA_DIR..."

# small.txt - simple text file
printf 'Hello, World!\n' > "$TEST_DATA_DIR/small.txt"

# binary.bin - 256 bytes of binary data (0x00-0xFF)
python3 -c "import sys; sys.stdout.buffer.write(bytes(range(256)))" > "$TEST_DATA_DIR/binary.bin"

# large.txt - 64KB of text
python3 -c "print('A' * 65536)" > "$TEST_DATA_DIR/large.txt"

# Verify files were created
vprint "Test files created:"
if [ -n "$VERBOSE" ]; then
    ls -la "$TEST_DATA_DIR/"
fi

# Find available ports
find_free_port() {
    python3 -c "import socket; s=socket.socket(); s.bind(('',0)); print(s.getsockname()[1]); s.close()"
}

HTTP_PORT=$(find_free_port)
HTTPS_PORT=$(find_free_port)

# Temp files for port communication
PORT_FILE_HTTP="$(mktemp)"
PORT_FILE_HTTPS="$(mktemp)"

echo ""
vprint "=== Starting HTTP/HTTPS Test Servers ==="

# Check if Python 3 is available
if ! "$PYTHON" --version >/dev/null 2>&1; then
    echo "ERROR: Python 3 not found. Set PYTHON environment variable."
    exit 1
fi

# Check for cryptography package (needed for HTTPS)
HAVE_CRYPTO=0
if "$PYTHON" -c "import cryptography" 2>/dev/null; then
    HAVE_CRYPTO=1
else
    if [ -n "$VERBOSE" ]; then
        echo "WARNING: cryptography package not found. HTTPS tests will be skipped."
        echo "Install with: pip3 install cryptography"
    fi
fi

# Start HTTPS server first (if available) so HTTP server knows the port
if [ "$HAVE_CRYPTO" -eq 1 ]; then
    vprint "Starting HTTPS server on port $HTTPS_PORT..."
    "$PYTHON" "$SCRIPT_DIR/http_server.py" \
        --port "$HTTPS_PORT" \
        --data-dir "$TEST_DATA_DIR" \
        --https \
        --write-port "$PORT_FILE_HTTPS" &
    HTTPS_PID="$!"
else
    vprint "HTTPS server not started (cryptography package missing)"
    HTTPS_PORT=0
fi

# Wait a moment for HTTPS server to start
sleep 1

# Read HTTPS port if auto-assigned
if [ "$HAVE_CRYPTO" -eq 1 ] && [ -s "$PORT_FILE_HTTPS" ]; then
    HTTPS_PORT=$(cat "$PORT_FILE_HTTPS")
fi

# Start HTTP server (with HTTPS port for upgrade redirects)
vprint "Starting HTTP server on port $HTTP_PORT..."
if [ "$HAVE_CRYPTO" -eq 1 ]; then
    "$PYTHON" "$SCRIPT_DIR/http_server.py" \
        --port "$HTTP_PORT" \
        --data-dir "$TEST_DATA_DIR" \
        --https-port "$HTTPS_PORT" \
        --write-port "$PORT_FILE_HTTP" &
else
    "$PYTHON" "$SCRIPT_DIR/http_server.py" \
        --port "$HTTP_PORT" \
        --data-dir "$TEST_DATA_DIR" \
        --write-port "$PORT_FILE_HTTP" &
fi
HTTP_PID="$!"

# Wait for servers to start
sleep 2

# Read HTTP port if auto-assigned (HTTPS port already read earlier)
if [ -s "$PORT_FILE_HTTP" ]; then
    HTTP_PORT=$(cat "$PORT_FILE_HTTP")
fi

rm -f "$PORT_FILE_HTTP" "$PORT_FILE_HTTPS"

vprint "HTTP server: http://127.0.0.1:$HTTP_PORT"
if [ "$HTTPS_PORT" -ne 0 ]; then
    vprint "HTTPS server: https://127.0.0.1:$HTTPS_PORT"
fi

# Test servers are running
vprint ""
vprint "Pinging servers..."
if ! curl -s "http://127.0.0.1:$HTTP_PORT/ping" >/dev/null 2>&1; then
    echo "ERROR: HTTP server not responding" >&2
    exit 1
fi
vprint "HTTP server OK"

if [ "$HTTPS_PORT" -ne 0 ]; then
    if curl -s -k "https://127.0.0.1:$HTTPS_PORT/ping" >/dev/null 2>&1; then
        vprint "HTTPS server OK"
    else
        vprint "WARNING: HTTPS server not responding (curl with -k may be needed)"
    fi
fi

# Export environment for tests
export TEST_SERVER_HOST="127.0.0.1"
export TEST_HTTP_PORT="$HTTP_PORT"
export TEST_HTTPS_PORT="$HTTPS_PORT"
export TEST_DATA_DIR="$TEST_DATA_DIR"

vprint ""
vprint "=== Running Tests ==="

# Run the actual test binary
if [ $# -eq 0 ]; then
    # Run the test binary directly
    cd "$SCRIPT_DIR"
    if [ -x ./test_vhttp ]; then
        ./test_vhttp
    elif [ -x ./.libs/test_vhttp ]; then
        ./.libs/test_vhttp
    else
        echo "ERROR: test_vhttp not found. Run 'make test_vhttp' first."
        exit 1
    fi
else
    # Run specific test binary or command
    "$@"
fi
