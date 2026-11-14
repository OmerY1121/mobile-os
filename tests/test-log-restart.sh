#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

work=$(mktemp -d)
pid=""

cleanup() {
    if [[ -n "$pid" ]]; then
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    fi
    rm -rf "$work"
}
trap cleanup EXIT

export MOBILE_OS_LOG_SOCKET="$work/log.sock"
export MOBILE_OS_LOG_FILE="$work/current.log"

start_server() {
    local attempt="$1"

    ./build/logd > "$work/server-$attempt.log" 2>&1 &
    pid=$!

    for i in {1..60}; do
        if [[ -S "$MOBILE_OS_LOG_SOCKET" ]]; then
            if [[ "$(./build/logctl write \
                "restart test $attempt" 2>/dev/null || true)" == "OK" ]]; then
                return 0
            fi
        fi

        if ! kill -0 "$pid" 2>/dev/null; then
            cat "$work/server-$attempt.log"
            return 1
        fi

        sleep 0.1
    done

    cat "$work/server-$attempt.log"
    return 1
}

start_server 1
echo "PASS: initial logd startup"

kill -KILL "$pid"
wait "$pid" 2>/dev/null || true
pid=""

[[ -S "$MOBILE_OS_LOG_SOCKET" ]]
echo "PASS: stale socket detected after crash"

start_server 2
echo "PASS: logd restarted after crash"

grep -qF "logd: removed stale socket" \
    "$work/server-2.log"

echo "PASS: stale socket recovered"

output=$(./build/logctl show)

grep -qF "restart test 1" <<< "$output"
grep -qF "restart test 2" <<< "$output"

echo "PASS: existing logs preserved"

kill -TERM "$pid"
wait "$pid"
pid=""

[[ ! -e "$MOBILE_OS_LOG_SOCKET" ]]
echo "PASS: clean shutdown"

echo
echo "All logging recovery tests passed."
