#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

DIR="build/s3"
REF="$DIR/references/deviceinfo"
RAMDISK="$DIR/mobile-os-initramfs.cpio.gz"
PACKAGE="$DIR/linux-postmarketos-exynos4-7.1.1-r0.apk"
OUTPUT="$DIR/UNTESTED-mobile-os-i9300.img"

URL="https://mirror.math.princeton.edu/pub/postmarketos/master/armv7/linux-postmarketos-exynos4-7.1.1-r0.apk"

echo "Checking Galaxy S3 configuration..."

test -f "$REF"
test -s "$RAMDISK"

grep -Fxq 'deviceinfo_codename="samsung-m0"' "$REF"
grep -Fxq 'deviceinfo_dtb="exynos4412-i9300"' "$REF"
grep -Fxq 'deviceinfo_append_dtb="true"' "$REF"
grep -Fxq 'deviceinfo_flash_pagesize="2048"' "$REF"
grep -Fxq 'deviceinfo_flash_offset_base="0x10000000"' "$REF"
grep -Fxq 'deviceinfo_flash_offset_kernel="0x00008000"' "$REF"
grep -Fxq 'deviceinfo_flash_offset_ramdisk="0x01000000"' "$REF"

gzip -t "$RAMDISK"

echo "Downloading Exynos4 kernel package..."

if [[ ! -s "$PACKAGE" ]]; then
    curl -fL --retry 3 "$URL" -o "$PACKAGE"
fi

WORK=$(mktemp -d "$DIR/exynos4.XXXXXX")
trap 'rm -rf "$WORK"' EXIT

gzip -dc "$PACKAGE" |
    tar --ignore-zeros --no-same-owner \
        -xf - -C "$WORK"

KERNEL="$WORK/boot/vmlinuz"
DTB="$WORK/boot/dtbs/exynos4412-i9300.dtb"
CONFIG="$WORK/boot/config"

for path in "$KERNEL" "$DTB" "$CONFIG"; do
    test -s "$path" || {
        echo "Missing kernel component: $path"
        exit 1
    }
done

echo "Checking kernel features..."

for option in \
    CONFIG_NAMESPACES \
    CONFIG_PID_NS \
    CONFIG_PROC_FS \
    CONFIG_TMPFS
do
    if ! grep -Fxq "$option=y" "$CONFIG"; then
        echo "Missing kernel support: $option"
        exit 1
    fi
    echo "PASS: $option"
done

mkdir -p "$DIR/kernel"

cp "$KERNEL" "$DIR/kernel/zImage"
cp "$DTB" "$DIR/kernel/exynos4412-i9300.dtb"
cp "$CONFIG" "$DIR/kernel/config"

cat "$KERNEL" "$DTB" > "$DIR/kernel/zImage-dtb"

echo "Creating Android boot image candidate..."

python3 "$DIR/tools/mkbootimg.py" \
    --header_version 0 \
    --kernel "$DIR/kernel/zImage-dtb" \
    --ramdisk "$RAMDISK" \
    --base 0x10000000 \
    --kernel_offset 0x00008000 \
    --ramdisk_offset 0x01000000 \
    --second_offset 0x00f00000 \
    --tags_offset 0x00000100 \
    --pagesize 2048 \
    --cmdline "console=ttySAC2,115200 rdinit=/init loglevel=7" \
    --output "$OUTPUT"

python3 - "$OUTPUT" <<'PY'
import struct
import sys
from pathlib import Path

path = Path(sys.argv[1])
data = path.read_bytes()
limit = 8 * 1024 * 1024

if data[:8] != b"ANDROID!":
    raise SystemExit("FAIL: invalid boot image header")

fields = struct.unpack_from("<10I", data, 8)

kernel_size = fields[0]
kernel_addr = fields[1]
ramdisk_size = fields[2]
ramdisk_addr = fields[3]
page_size = fields[7]

assert page_size == 2048
assert kernel_addr == 0x10008000
assert ramdisk_addr == 0x11000000
assert kernel_size > 0
assert ramdisk_size > 0

print("PASS: Android boot image header")
print(f"Kernel size: {kernel_size:,} bytes")
print(f"Ramdisk size: {ramdisk_size:,} bytes")
print(f"Image size: {len(data):,} bytes")

if len(data) > limit:
    raise SystemExit(
        "FAIL: image exceeds the 8 MiB BOOT partition limit"
    )

print("PASS: image fits within 8 MiB")
PY

echo
sha256sum "$OUTPUT"
echo
echo "Boot image candidate created:"
echo "$OUTPUT"
echo "NOT VERIFIED FOR FLASHING"
