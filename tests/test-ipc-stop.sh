#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

work=$(mktemp -d "$PWD/build/stop-ipc.XXXXXX")
server_pid=""

cleanup() {
    if [[ -n "$server_pid" ]]; then
        kill "$server_pid" 2>/dev/null || true
        wait "$server_pid" 2>/dev/null || true
    fi
    rm -rf "$work"
}

trap cleanup EXIT

export MOBILE_OS_SOCKET="$work/status.sock"

./build/statusd > "$work/server.log" 2>&1 &
server_pid=$!

for i in {1..50}; do
    [[ -S "$MOBILE_OS_SOCKET" ]] && break
    sleep 0.1
done

if [[ ! -S "$MOBILE_OS_SOCKET" ]]; then
    cat "$work/server.log"
    exit 1
fi

if [[ "$(id -u)" != 0 ]]; then
    result=$(./build/statusctl stop)

    if [[ "$result" != "statusd: permission denied" ]]; then
        echo "FAIL: unprivileged stop was accepted"
        exit 1
    fi

    echo "PASS: unprivileged stop denied"

    result=$(./build/statusctl ping)

    if [[ "$result" != "statusd: pong" ]]; then
        echo "FAIL: service unavailable after denied stop"
        exit 1
    fi

    echo "PASS: service remains available"
fi

if [[ "$(id -u)" == 0 ]]; then
    result=$(./build/statusctl stop)
elif sudo -n true 2>/dev/null; then
    result=$(sudo -n env \
        MOBILE_OS_SOCKET="$MOBILE_OS_SOCKET" \
        "$PWD/build/statusctl" stop)
else
    echo "SKIP: root stop test (sudo unavailable)"
    echo "Remaining test requires CI privileges."
    exit 0
fi

if [[ "$result" != "statusd: stopping" ]]; then
    echo "FAIL: authorized stop"
    exit 1
fi

echo "PASS: authorized stop accepted"

wait "$server_pid"
server_pid=""

echo "PASS: service exited successfully"
echo "All IPC stop tests passed."
