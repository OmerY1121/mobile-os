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
export MOBILE_OS_STATE_FILE="$TEST_DIR/services"

printf 'logd=launched failures=0\nstatusd=launched failures=0\n' \
    > "$MOBILE_OS_STATE_FILE"
chmod 0600 "$MOBILE_OS_STATE_FILE"

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
actual=$(./build/statusctl services)

expected="services: logd=launched failures=0; statusd=launched failures=0"

if [[ "$actual" != "$expected" ]]; then
    echo "FAIL: services command"
    echo "Received: $actual"
    exit 1
fi

echo "PASS: services command"

chmod 0666 "$MOBILE_OS_STATE_FILE"

actual=$(./build/statusctl services)

if [[ "$actual" != "statusd: state unavailable" ]]; then
    echo "FAIL: unsafe state permissions accepted"
    exit 1
fi

echo "PASS: unsafe state file rejected"

if health_output=$(./build/statusctl health); then
    health_rc=0
else
    health_rc=$?
fi

case "$health_output" in
    "health: statusd=ok logd=ok")
        [[ "$health_rc" -eq 0 ]] || exit 1
        ;;
    "health: statusd=ok logd=unavailable")
        [[ "$health_rc" -eq 2 ]] || exit 1
        ;;
    *)
        echo "FAIL: unexpected health response"
        exit 1
        ;;
esac

echo "PASS: health command and exit status"

echo "All command tests passed."
