#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

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

export MOBILE_OS_LOG_SOCKET="$work/log.sock"
export MOBILE_OS_LOG_FILE="$work/current.log"

for program in logd logctl; do
    file "build/armv7/$program" | grep -q ARM
done

echo "PASS: ARMv7 logging binaries"

qemu-arm build/armv7/logd \
    > "$work/server.log" 2>&1 &
server_pid=$!

for i in {1..100}; do
    [[ -S "$MOBILE_OS_LOG_SOCKET" ]] && break

    if ! kill -0 "$server_pid" 2>/dev/null; then
        cat "$work/server.log"
        exit 1
    fi

    sleep 0.1
done

[[ -S "$MOBILE_OS_LOG_SOCKET" ]] || {
    cat "$work/server.log"
    exit 1
}

echo "PASS: ARMv7 logd startup"

result=$(qemu-arm build/armv7/logctl \
    write "ARMv7 logging test")

[[ "$result" == "OK" ]]
echo "PASS: ARMv7 log writing"

result=$(qemu-arm build/armv7/logctl show)

grep -q "ARMv7 logging test" <<< "$result"
echo "PASS: ARMv7 log reading"

[[ "$(stat -c %a "$MOBILE_OS_LOG_FILE")" == 600 ]]
echo "PASS: private log permissions"

kill "$server_pid"
wait "$server_pid"
server_pid=""

[[ ! -e "$MOBILE_OS_LOG_SOCKET" ]]
echo "PASS: clean shutdown"

echo
echo "All ARMv7 logging tests passed."
