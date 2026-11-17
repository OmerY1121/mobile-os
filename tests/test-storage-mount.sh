#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

disk="build/selyra-data-test.img"

test -f "$disk" && test ! -L "$disk" || {
    echo "STOP: Test disk missing or invalid"
    exit 1
}

test -s build/vmlinuz || {
    echo "STOP: Kernel missing"
    exit 1
}

work=$(mktemp -d /tmp/selyra-storage.XXXXXX)
qemu_pid=""

cleanup() {
    if [[ -n "$qemu_pid" ]]; then
        kill "$qemu_pid" 2>/dev/null || true
        wait "$qemu_pid" 2>/dev/null || true
    fi
    rm -rf "$work"
}

trap cleanup EXIT

mkdir -p "$work/rootfs/dev" "$work/rootfs/data"

cp build/storage-mount-probe "$work/rootfs/init"

fakeroot bash -c '
set -euo pipefail
cd "$1"
mknod -m 600 dev/console c 5 1
mknod -m 666 dev/null c 1 3
find . -print0 |
    cpio --null --quiet -o -H newc |
    gzip -9 > "$2"
' bash "$work/rootfs" "$work/initramfs.gz"

qemu-system-x86_64 \
    -m 512 \
    -accel tcg \
    -nographic \
    -nic none \
    -kernel build/vmlinuz \
    -initrd "$work/initramfs.gz" \
    -append "console=ttyS0 rdinit=/init" \
    -drive "file=$disk,format=raw,if=virtio,readonly=on" \
    -no-reboot \
    > "$work/qemu.log" 2>&1 &

qemu_pid=$!

passed=0

for ((i=0; i<450; i++)); do
    if grep -Fq \
        "All Selyra read-only storage checks passed." \
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
    echo "FAIL: QEMU storage mount test"
    tail -n 60 "$work/qemu.log"
    exit 1
fi

grep -E "PASS:|All Selyra read-only storage checks passed\." "$work/qemu.log"
echo "PASS: QEMU ext4 read-only mount verified"
