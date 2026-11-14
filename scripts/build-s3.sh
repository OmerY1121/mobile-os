#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

mkdir -p build/armv7

for program in init statusd statusctl logd logctl; do
    echo "Building $program for Galaxy S3..."

    sources=("src/$program.c")

    if [[ "$program" == "init" ]]; then
        sources+=(src/service_config.c src/service_state.c)
    fi

    arm-linux-gnueabihf-gcc \
        -static -O2 -Wall -Wextra -Werror \
        -march=armv7-a \
        -mfpu=vfpv3-d16 \
        -mfloat-abi=hard \
        -o "build/armv7/$program" \
        "${sources[@]}"
done

echo
file build/armv7/*
echo "Galaxy S3 ARMv7 build completed."
