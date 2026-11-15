#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

VERSION=$(tr -d '\r\n' < VERSION)
BASE="build/s3"
PACKAGE="$BASE/candidate-$VERSION"
ARCHIVE="$BASE/selyra-os-s3-$VERSION-UNTESTED.tar.gz"

echo "Building Selyra OS for Galaxy S3..."

bash scripts/build-s3.sh
bash scripts/prepare-s3.sh
bash scripts/build-s3-boot.sh

echo "Running offline image checks..."
python3 tests/test-s3-image.py

echo "Preparing test package..."

rm -rf "$PACKAGE"
mkdir -p "$PACKAGE"

cp "$BASE/UNTESTED-selyra-i9300.img" \
   "$PACKAGE/UNTESTED-selyra-i9300.img"

cp "$BASE/selyra-initramfs.cpio.gz" \
   "$PACKAGE/initramfs.cpio.gz"

cp "$BASE/kernel/exynos4412-i9300.dtb" \
   "$PACKAGE/exynos4412-i9300.dtb"

cp "$BASE/kernel/config" \
   "$PACKAGE/kernel-config"

cp "$BASE/references/deviceinfo" \
   "$PACKAGE/deviceinfo"

cat > "$PACKAGE/README.txt" <<NOTES
Selyra OS v$VERSION
Galaxy S3 GT-I9300 hardware test candidate

This package is not verified for flashing.

Target: Samsung Galaxy S III GT-I9300
Architecture: ARMv7
Kernel: postmarketOS Exynos4
Init: Selyra OS

Verified:
- ARMv7 binaries compile
- ARM QEMU boot
- Local IPC tests
- Android boot image structure
- Kernel and initramfs contents
- Image size within 8 MiB

Not verified:
- Actual phone model and partition layout
- Bootloader compatibility on the target phone
- Booting on physical hardware
- Display, USB and debugging functionality
- Recovery and restoration procedure

DO NOT FLASH THIS IMAGE YET.

Before hardware testing:
1. Confirm the phone is GT-I9300.
2. Back up the existing firmware and data.
3. Preserve EFS and recovery information.
4. Establish a verified restoration procedure.
5. Prepare a way to diagnose boot failures.
NOTES

(
    cd "$PACKAGE"
    sha256sum \
        UNTESTED-selyra-i9300.img \
        initramfs.cpio.gz \
        exynos4412-i9300.dtb \
        kernel-config \
        deviceinfo \
        > SHA256SUMS

    sha256sum -c SHA256SUMS
)

tar -C "$BASE" -czf "$ARCHIVE" \
    "candidate-$VERSION"

tar -tzf "$ARCHIVE" > /dev/null

echo
echo "Galaxy S3 test package prepared:"
echo "$ARCHIVE"
sha256sum "$ARCHIVE"
echo
echo "Status: OFFLINE VERIFIED - NOT FLASH READY"
