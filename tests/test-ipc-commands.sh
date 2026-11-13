#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

TEST_DIR=$(mktemp -d "$PWD/build/commands.XXXXXX")
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
    echo "FAIL: statusd did not start"
    exit 1
fi

[[ "$(./build/statusctl status)" == "statusd: running" ]]
echo "PASS: status command"

[[ "$(./build/statusctl ping)" == "statusd: pong" ]]
echo "PASS: ping command"

UPTIME=$(./build/statusctl uptime)

if [[ ! "$UPTIME" =~ ^statusd:\ uptime\ [0-9]+s$ ]]; then
    echo "FAIL: uptime command"
    exit 1
fi

echo "PASS: uptime command"
echo "All command tests passed."
