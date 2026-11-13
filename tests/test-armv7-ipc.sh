#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

command -v qemu-arm >/dev/null || {
    echo "FAIL: qemu-arm is missing"
    exit 1
}

for program in init statusd statusctl; do
    binary="build/armv7/$program"

    if ! file "$binary" | grep -q "ARM"; then
        echo "FAIL: $binary is not an ARM binary"
        exit 1
    fi
done

echo "PASS: ARMv7 binaries detected"

work=$(mktemp -d)
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

qemu-arm build/armv7/statusd \
    > "$work/statusd.log" 2>&1 &
server_pid=$!

for i in {1..100}; do
    [[ -S "$MOBILE_OS_SOCKET" ]] && break

    if ! kill -0 "$server_pid" 2>/dev/null; then
        cat "$work/statusd.log"
        echo "FAIL: ARM statusd exited"
        exit 1
    fi

    sleep 0.1
done

if [[ ! -S "$MOBILE_OS_SOCKET" ]]; then
    cat "$work/statusd.log"
    echo "FAIL: ARM IPC socket not created"
    exit 1
fi

echo "PASS: ARM statusd started"

check_command() {
    local command="$1"
    local expected="$2"
    local actual

    actual=$(qemu-arm build/armv7/statusctl "$command")

    if [[ "$actual" != "$expected" ]]; then
        echo "FAIL: $command"
        echo "Received: $actual"
        exit 1
    fi

    echo "PASS: $command"
}

check_command status "statusd: running"
check_command ping "statusd: pong"

uptime=$(qemu-arm build/armv7/statusctl uptime)

if [[ "$uptime" =~ ^statusd:\ uptime\ [0-9]+s$ ]]; then
    echo "PASS: uptime"
else
    echo "FAIL: uptime ($uptime)"
    exit 1
fi

echo
echo "All ARMv7 IPC tests passed."
