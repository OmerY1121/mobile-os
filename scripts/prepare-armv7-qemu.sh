#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

kernel="build/armv7/vmlinuz"
rootfs="build/armv7/rootfs"
initramfs="build/armv7/initramfs.cpio.gz"

base="https://deb.debian.org/debian/dists/trixie/main/installer-armhf/current/images"

for program in init statusd statusctl logd logctl; do
    test -f "build/armv7/$program" || {
        echo "Missing ARM binary: $program"
        exit 1
    }
done

echo "Preparing ARMv7 Linux kernel..."

mkdir -p build/armv7

curl -fL --retry 3 \
    "$base/netboot/vmlinuz" \
    -o "$kernel"

curl -fL --retry 3 \
    "$base/SHA256SUMS" \
    -o build/armv7/SHA256SUMS

expected=$(awk '
    $2 == "./netboot/vmlinuz" ||
    $2 == "netboot/vmlinuz" {
        print $1
        exit
    }
' build/armv7/SHA256SUMS)

if [[ -z "$expected" ]]; then
    echo "Kernel checksum not found"
    exit 1
fi

printf '%s  %s\n' "$expected" "$kernel" |
    sha256sum -c -

echo "Preparing Selyra OS rootfs..."

rm -rf "$rootfs"

mkdir -p \
    "$rootfs/bin" \
    "$rootfs/proc" \
    "$rootfs/sys" \
    "$rootfs/dev" \
    "$rootfs/tmp" \
    "$rootfs/run/mobile-os" \
    "$rootfs/var/lib/mobile-os/system" \
    "$rootfs/var/lib/mobile-os/statusd"

chmod 1777 "$rootfs/tmp"

cp build/armv7/init "$rootfs/init"
cp build/armv7/statusd "$rootfs/bin/statusd"
cp build/armv7/statusctl "$rootfs/bin/statusctl"
cp build/armv7/logd "$rootfs/bin/logd"
cp build/armv7/logctl "$rootfs/bin/logctl"

install -D -m 0600 config/services.conf \
    "$rootfs/etc/mobile-os/services.conf"

echo "Creating ARMv7 initramfs..."

fakeroot bash -c '
set -euo pipefail
cd "$1"
chown 0:0 etc/mobile-os/services.conf
chmod 0600 etc/mobile-os/services.conf

mknod -m 600 dev/console c 5 1
mknod -m 666 dev/null c 1 3

find . -print0 |
    cpio --null --quiet -o -H newc |
    gzip -9 > "$2"
' bash "$rootfs" "$(pwd)/$initramfs"

echo "ARMv7 boot image ready."
