#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

ROOTFS="build/s3/rootfs"
OUTPUT="build/s3/mobile-os-initramfs.cpio.gz"
REFS="build/s3/references"

echo "Building current ARMv7 programs..."
bash scripts/build-s3.sh

for program in init statusd statusctl; do
    file "build/armv7/$program" | grep -q ARM || {
        echo "Not an ARM binary: $program"
        exit 1
    }
done

echo "Fetching Galaxy S3 device information..."

mkdir -p "$REFS"

BASE="https://gitlab.com/postmarketOS/pmaports/-/raw/master/device/community/device-samsung-m0"

curl -fsSL --retry 3 \
    "$BASE/deviceinfo" \
    -o "$REFS/deviceinfo"

curl -fsSL --retry 3 \
    "$BASE/APKBUILD" \
    -o "$REFS/APKBUILD"

grep -Fxq \
    'deviceinfo_codename="samsung-m0"' \
    "$REFS/deviceinfo"

grep -Fxq \
    'deviceinfo_dtb="exynos4412-i9300"' \
    "$REFS/deviceinfo"

grep -Fxq \
    'deviceinfo_flash_method="heimdall-bootimg"' \
    "$REFS/deviceinfo"

echo "PASS: Galaxy S3 device metadata verified"

echo "Preparing Mobile OS initramfs..."

rm -rf "$ROOTFS"

mkdir -p \
    "$ROOTFS/bin" \
    "$ROOTFS/dev" \
    "$ROOTFS/proc" \
    "$ROOTFS/sys" \
    "$ROOTFS/tmp" \
    "$ROOTFS/run/mobile-os" \
    "$ROOTFS/var/lib/mobile-os/system" \
    "$ROOTFS/var/lib/mobile-os/statusd"

chmod 1777 "$ROOTFS/tmp"

install -m 0755 build/armv7/init \
    "$ROOTFS/init"

install -m 0755 build/armv7/statusd \
    "$ROOTFS/bin/statusd"

install -m 0755 build/armv7/statusctl \
    "$ROOTFS/bin/statusctl"

fakeroot bash -c '
set -euo pipefail
cd "$1"

mknod -m 600 dev/console c 5 1
mknod -m 666 dev/null c 1 3

find . -print0 |
    cpio --null --quiet -o -H newc |
    gzip -9 > "$2"
' bash "$ROOTFS" "$(pwd)/$OUTPUT"

echo
echo "Checking initramfs contents..."

gzip -dc "$OUTPUT" |
    cpio --quiet -t |
    grep -E \
    '^(\./)?(init|bin/statusd|bin/statusctl|dev/console)$'

echo
sha256sum "$OUTPUT"

echo
echo "PASS: Galaxy S3 initramfs prepared"
echo "Output: $OUTPUT"
echo "No phone partitions were modified."
