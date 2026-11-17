#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

IMAGE="build/selyra-data-test.img"
SIZE="64M"

for tool in qemu-img mkfs.ext4; do
    command -v "$tool" >/dev/null || {
        echo "FAIL: Missing $tool"
        exit 1
    }
done

mkdir -p build
umask 077

if [[ -e "$IMAGE" || -L "$IMAGE" ]]; then
    echo "STOP: Storage image already exists."
    echo "Existing data will not be overwritten."
    exit 1
fi

qemu-img create -f raw "$IMAGE" "$SIZE"

mkfs.ext4 -F -q -L SELYRA_DATA "$IMAGE"

echo "PASS: Selyra test storage created"
echo "Image: $IMAGE"
echo "Filesystem: ext4"
echo "Size: $SIZE"
