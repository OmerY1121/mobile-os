#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

work=$(mktemp -d "$PWD/build/stop.XXXXXX")
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

gcc -static -Wall -Wextra -Werror -O2 \
    -o "$work/rootfs/bin/statusd" \
    tests/stop_statusd.c

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
' bash "$work/rootfs" "$work/stop-initramfs.gz"

echo "Starting QEMU clean shutdown test..."

qemu-system-x86_64 \
    -m 512 \
    -accel tcg \
    -nographic \
    -nic none \
    -kernel build/vmlinuz \
    -initrd "$work/stop-initramfs.gz" \
    -append "console=ttyS0 rdinit=/init" \
    -no-reboot \
    > "$work/qemu.log" 2>&1 &

qemu_pid=$!

found=0

for ((i=0; i<450; i++)); do
    if grep -qF \
        "init: statusd stopped normally" \
        "$work/qemu.log"; then
        found=1
        sleep 3
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
exits=$(count "init: statusd exited with code 0")
stops=$(count "init: statusd stopped normally")
cleanups=$(count "init: namespace cleanup started")
tests=$(count "statusd: clean-exit test")

failed=0

check() {
    if [[ "$2" == "$3" ]]; then
        echo "PASS: $1"
    else
        echo "FAIL: $1 (expected $3, got $2)"
        failed=1
    fi
}

check "Single service launch" "$launches" 1
check "Clean service exit" "$tests" 1
check "Exit status propagated" "$exits" 1
check "Namespace cleanup" "$cleanups" 1
check "Normal stop recognized" "$stops" 1

if [[ "$found" != 1 ]]; then
    echo "FAIL: normal stop not detected"
    failed=1
fi

if [[ "$failed" != 0 ]]; then
    echo
    echo "Last QEMU output:"
    tail -n 60 "$work/qemu.log"
    exit 1
fi

echo "All clean shutdown tests passed."
