#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

if [[ ! -f build/vmlinuz ||
      ! -d build/rootfs ]]; then
    echo "Run make first."
    exit 1
fi

work=$(mktemp -d "$PWD/build/restart.XXXXXX")
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

rm -f "$work/rootfs/bin/statusd"

gcc -static -Wall -Wextra -Werror -O2 \
    -o "$work/rootfs/bin/statusd" \
    tests/crash_statusd.c

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
' bash "$work/rootfs" "$work/crash-initramfs.gz"

echo "Starting QEMU crash recovery test..."

qemu-system-x86_64 \
    -m 512 \
    -accel tcg \
    -nographic \
    -nic none \
    -kernel build/vmlinuz \
    -initrd "$work/crash-initramfs.gz" \
    -append "console=ttyS0 rdinit=/init" \
    -no-reboot \
    > "$work/qemu.log" 2>&1 &

qemu_pid=$!

found=0

for ((i=0; i<750; i++)); do
    if grep -qF \
        "init: statusd restart limit reached" \
        "$work/qemu.log"; then
        found=1
        sleep 1
        break
    fi

    if ! kill -0 "$qemu_pid" 2>/dev/null; then
        break
    fi

    sleep 0.1
done

count() {
    grep -cF "$1" "$work/qemu.log" || true
}

launches=$(count "init: statusd launcher started")
crashes=$(count "statusd: injected crash UID=1100 PID=2 no_new_privs=1")
exits=$(count "init: namespace statusd exited with code 23")
limits=$(count "init: statusd restart limit reached")
orphans=$(count "init: namespace cleanup reaped 1 orphan(s)")
forced=$(count "init: namespace cleanup escalating to SIGKILL")

failed=0

check() {
    if [[ "$2" == "$3" ]]; then
        echo "PASS: $1"
    else
        echo "FAIL: $1 (expected $3, got $2)"
        failed=1
    fi
}

check "Three launch attempts" "$launches" 3
check "Three isolated crashes" "$crashes" 3
check "Crash exit codes propagated" "$exits" 3
check "Restart limit enforced" "$limits" 1
check "Orphan processes reaped" "$orphans" 3
check "Stubborn processes killed" "$forced" 3

if [[ "$found" != 1 ]]; then
    echo "FAIL: restart limit was not reached"
    failed=1
fi

if [[ "$failed" != 0 ]]; then
    echo
    echo "Last QEMU output:"
    tail -n 50 "$work/qemu.log"
    exit 1
fi

echo
echo "All QEMU restart checks passed."
