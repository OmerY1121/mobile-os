#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

TEST_DIR=$(mktemp -d "$PWD/build/ipc-test.XXXXXX")
SERVER_PID=""

cleanup() {
    if [[ -n "$SERVER_PID" ]]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -rf "$TEST_DIR"
}
trap cleanup EXIT

export MOBILE_OS_SOCKET="$TEST_DIR/status.sock"

./build/statusd > "$TEST_DIR/server.log" 2>&1 &
SERVER_PID=$!

for i in {1..50}; do
    [[ -S "$MOBILE_OS_SOCKET" ]] && break
    sleep 0.1
done

if [[ ! -S "$MOBILE_OS_SOCKET" ]]; then
    cat "$TEST_DIR/server.log"
    echo "FAIL: socket not created"
    exit 1
fi

RESPONSE=$(./build/statusctl)

if [[ "$RESPONSE" != "statusd: running" ]]; then
    echo "FAIL: incorrect response"
    exit 1
fi

echo "PASS: IPC communication"

python3 - <<'PY'
import os
import stat

path = os.environ["MOBILE_OS_SOCKET"]
mode = os.stat(path).st_mode

assert stat.S_ISSOCK(mode)
assert mode & 0o077 == 0, "Socket permissions too broad"

print("PASS: socket permissions")
PY

python3 tests/test-ipc-errors.py

echo "All IPC tests passed."
