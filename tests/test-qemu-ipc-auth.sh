#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

for tool in gcc fakeroot cpio gzip qemu-system-x86_64; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "STOP: missing tool: $tool" >&2
        exit 1
    fi
done

test -s build/vmlinuz || {
    echo "STOP: build/vmlinuz missing"
    exit 1
}

test -x build/rootfs/bin/statusd || {
    echo "STOP: statusd image missing"
    exit 1
}

work=$(mktemp -d "${TMPDIR:-/tmp}/selyra-ipc-auth.XXXXXX")
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
    -o "$work/rootfs/init" tests/qemu-ipc-auth-init.c

fakeroot bash -c '
set -euo pipefail
cd "$1"
mkdir -p dev
rm -f dev/console dev/null
mknod -m 600 dev/console c 5 1
mknod -m 666 dev/null c 1 3
find . -print0 | cpio --null --quiet -o -H newc |
    gzip -9 > "$2"
' bash "$work/rootfs" "$work/initramfs.gz"

qemu-system-x86_64 \
    -m 512 -accel tcg -nographic -nic none \
    -kernel build/vmlinuz \
    -initrd "$work/initramfs.gz" \
    -append "console=ttyS0 rdinit=/init" \
    -no-reboot \
    > "$work/qemu.log" 2>&1 &

qemu_pid=$!

passed=0

for ((i=0; i<600; i++)); do
    if grep -Fq \
        "All QEMU IPC peer authorization checks passed." \
        "$work/qemu.log" &&
       grep -Fq \
        "statusd: unauthorized peer" \
        "$work/qemu.log"; then
        passed=1
        break
    fi

    if grep -Fq "FAIL:" "$work/qemu.log"; then
        break
    fi

    if ! kill -0 "$qemu_pid" 2>/dev/null; then
        break
    fi

    sleep 0.1
done

if [[ "$passed" -ne 1 ]]; then
    echo "FAIL: QEMU IPC peer authorization"
    tail -n 60 "$work/qemu.log"
    exit 1
fi

grep -F "PASS:" "$work/qemu.log"
echo "PASS: server logged unauthorized peer"
echo "All QEMU IPC authorization tests passed."
