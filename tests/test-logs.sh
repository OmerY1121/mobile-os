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

./build/logd > "$work/server.log" 2>&1 &
pid=$!

for i in {1..100}; do
    [[ -S "$MOBILE_OS_LOG_SOCKET" ]] && break
    sleep 0.1
done

if [[ ! -S "$MOBILE_OS_LOG_SOCKET" ]]; then
    cat "$work/server.log"
    echo "FAIL: logd startup"
    exit 1
fi

echo "PASS: logd startup"

[[ "$(./build/logctl write 'boot started')" == "OK" ]]
[[ "$(./build/logctl write 'statusd ready')" == "OK" ]]
echo "PASS: log writing"

output=$(./build/logctl show)

grep -q 'boot started' <<< "$output"
grep -q 'statusd ready' <<< "$output"
echo "PASS: log reading"

[[ "$(./build/logctl write 'init: boot services starting')" == "OK" ]]
[[ "$(./build/logctl write 'statusd: IPC ready')" == "OK" ]]

boot_output=$(./build/logctl boot)

grep -qF 'init: boot services starting' <<< "$boot_output"
grep -qF 'statusd: IPC ready' <<< "$boot_output"

if grep -qF 'statusd ready' <<< "$boot_output"; then
    echo "FAIL: unrelated event in boot log"
    exit 1
fi

echo "PASS: boot event filtering"

grep -Eq \
    '^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z ' \
    <<< "$output"
echo "PASS: UTC timestamps"

[[ "$(stat -c %a "$MOBILE_OS_LOG_FILE")" == 600 ]]
echo "PASS: private file permissions"

for i in {1..1100}; do
    ./build/logctl write \
        "Log rotation test entry number $i, sample data" \
        > /dev/null
done

[[ -s "$MOBILE_OS_LOG_FILE.1" ]]
[[ $(stat -c %s "$MOBILE_OS_LOG_FILE") -le 65536 ]]
[[ $(stat -c %s "$MOBILE_OS_LOG_FILE.1") -le 65536 ]]
echo "PASS: log rotation and size limits"

kill "$pid"
wait "$pid"
pid=""

[[ ! -e "$MOBILE_OS_LOG_SOCKET" ]]
echo "PASS: clean shutdown"

echo "All logging tests passed."
