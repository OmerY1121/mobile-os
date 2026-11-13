#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

TEST_DIR=$(mktemp -d /tmp/mobile-os-auth.XXXXXX)
SERVER_PID=""

cleanup() {
    if [[ -n "$SERVER_PID" ]]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -rf "$TEST_DIR"
}

trap cleanup EXIT

chmod 755 "$TEST_DIR"
export MOBILE_OS_SOCKET="$TEST_DIR/status.sock"

./build/statusd > "$TEST_DIR/server.log" 2>&1 &
SERVER_PID=$!

for i in {1..50}; do
    [[ -S "$MOBILE_OS_SOCKET" ]] && break
    sleep 0.1
done

if [[ ! -S "$MOBILE_OS_SOCKET" ]]; then
    echo "FAIL: server did not start"
    exit 1
fi

# Authorized user
RESPONSE=$(./build/statusctl)

if [[ "$RESPONSE" != "statusd: running" ]]; then
    echo "FAIL: authorized connection"
    exit 1
fi

echo "PASS: authorized connection"

# Temporarily allow access to test peer credentials.
chmod 777 "$MOBILE_OS_SOCKET"

# Unauthorized user
if sudo -n -u nobody env \
    MOBILE_OS_SOCKET="$MOBILE_OS_SOCKET" \
    "$PWD/build/statusctl" \
    > "$TEST_DIR/client.log" 2>&1; then
    echo "FAIL: unauthorized connection accepted"
    exit 1
fi

if ! grep -q "unauthorized peer" "$TEST_DIR/server.log"; then
    echo "FAIL: peer rejection not verified"
    cat "$TEST_DIR/client.log"
    exit 1
fi

echo "PASS: unauthorized connection rejected"
echo "All authentication tests passed."
