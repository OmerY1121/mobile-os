#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
mkdir -p build

gcc -std=gnu11 -O2 -Wall -Wextra -Werror \
    src/service_config.c \
    tests/service-config-check.c \
    -o build/service-config-check

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cp config/services.conf "$work/services.conf"
chmod 600 "$work/services.conf"

./build/service-config-check \
    "$work/services.conf" > "$work/result"

grep -qx "statusd=1, retries=3" "$work/result"
grep -qx "logd=1, retries=3" "$work/result"

echo "PASS: valid service configuration"

cat >> "$work/services.conf" <<'BAD'
statusd.enabled=0
BAD

if ./build/service-config-check \
    "$work/services.conf" >/dev/null 2>&1; then
    echo "FAIL: duplicate setting accepted"
    exit 1
fi

echo "PASS: duplicate setting rejected"

printf 'statusd.enabled=2\n' \
    > "$work/services.conf"

if ./build/service-config-check \
    "$work/services.conf" >/dev/null 2>&1; then
    echo "FAIL: invalid configuration accepted"
    exit 1
fi

echo "PASS: invalid configuration rejected"

cp config/services.conf "$work/services.conf"
chmod 666 "$work/services.conf"

if ./build/service-config-check \
    "$work/services.conf" >/dev/null 2>&1; then
    echo "FAIL: unsafe file permissions accepted"
    exit 1
fi

echo "PASS: unsafe file permissions rejected"

echo "All service configuration tests passed."
