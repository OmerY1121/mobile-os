#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

test -x build/statusd
test -x build/statusctl

work=$(mktemp -d /tmp/selyra-env.XXXXXX)
server_pid=""

stop_server() {
    if [[ -n "$server_pid" ]]; then
        kill "$server_pid" 2>/dev/null || true
        wait "$server_pid" 2>/dev/null || true
        server_pid=""
    fi
}

cleanup() {
    stop_server
    rm -rf "$work"
}
trap cleanup EXIT

start_server() {
    env -u SELYRA_SOCKET -u MOBILE_OS_SOCKET \
        "$@" ./build/statusd \
        > "$work/server.log" 2>&1 &
    server_pid=$!
}

wait_for_socket() {
    local socket="$1"

    for i in {1..50}; do
        [[ -S "$socket" ]] && return 0
        sleep 0.1
    done

    echo "FAIL: Socket not created: $socket"
    cat "$work/server.log"
    exit 1
}

new_socket="$work/new.sock"
old_socket="$work/old.sock"

# New variable must take priority.
start_server \
    "SELYRA_SOCKET=$new_socket" \
    "MOBILE_OS_SOCKET=$old_socket"

wait_for_socket "$new_socket"
test ! -S "$old_socket"

response=$(
    env -u SELYRA_SOCKET -u MOBILE_OS_SOCKET \
        "SELYRA_SOCKET=$new_socket" \
        "MOBILE_OS_SOCKET=$old_socket" \
        ./build/statusctl
)

test "$response" = "statusd: running"
echo "PASS: Selyra variable takes priority"

stop_server
rm -f "$new_socket" "$old_socket"

# Old variable must remain supported.
start_server "MOBILE_OS_SOCKET=$old_socket"

wait_for_socket "$old_socket"

response=$(
    env -u SELYRA_SOCKET -u MOBILE_OS_SOCKET \
        "MOBILE_OS_SOCKET=$old_socket" \
        ./build/statusctl
)

test "$response" = "statusd: running"
echo "PASS: Legacy variable remains supported"

echo "All Selyra environment compatibility tests passed."
