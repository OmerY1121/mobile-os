#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

work=$(mktemp -d "$PWD/build/log-auth.XXXXXX")
qemu_pid=""

cleanup() {
    if [[ -n "$qemu_pid" ]]; then
        kill "$qemu_pid" 2>/dev/null || true
        wait "$qemu_pid" 2>/dev/null || true
    fi
    rm -rf "$work"
}
trap cleanup EXIT

mkdir -p "$work/rootfs"
cp -a build/rootfs/. "$work/rootfs/"

gcc -static -O2 -Wall -Wextra -Werror \
    -o "$work/rootfs/bin/statusd" \
    tests/log-auth-probe.c

fakeroot bash -c '
set -euo pipefail
cd "$1"
mkdir -p dev
rm -f dev/console dev/null
mknod -m 600 dev/console c 5 1
mknod -m 666 dev/null c 1 3
find . -print0 |
    cpio --null --quiet -o -H newc |
    gzip -9 > "$2"
' bash "$work/rootfs" "$work/initramfs.gz"

echo "Starting QEMU log authorization test..."

qemu-system-x86_64 \
    -m 512 \
    -accel tcg \
    -nographic \
    -nic none \
    -kernel build/vmlinuz \
    -initrd "$work/initramfs.gz" \
    -append "console=ttyS0 rdinit=/init" \
    -no-reboot \
    > "$work/qemu.log" 2>&1 &

qemu_pid=$!

passed=0

for ((i=0; i<600; i++)); do
    if grep -qF \
        "All service log authorization checks passed." \
        "$work/qemu.log"; then
        passed=1
        break
    fi

    if grep -qF "FAIL:" "$work/qemu.log"; then
        break
    fi

    if ! kill -0 "$qemu_pid" 2>/dev/null; then
        break
    fi

    sleep 0.1
done

if [[ "$passed" != 1 ]]; then
    echo "FAIL: QEMU log authorization test"
    tail -n 50 "$work/qemu.log"
    exit 1
fi

grep -F "PASS: statusd can write logs" \
    "$work/qemu.log"
grep -F "PASS: statusd cannot read logs" \
    "$work/qemu.log"

echo "All QEMU log authorization tests passed."
