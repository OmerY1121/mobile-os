#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

mkdir -p build/armv7

for program in init statusd statusctl; do
    echo "Building $program for Galaxy S3..."

    arm-linux-gnueabihf-gcc \
        -static -O2 -Wall -Wextra -Werror \
        -march=armv7-a \
        -mfpu=vfpv3-d16 \
        -mfloat-abi=hard \
        -o "build/armv7/$program" \
        "src/$program.c"
done

echo
file build/armv7/*
echo "Galaxy S3 ARMv7 build completed."
