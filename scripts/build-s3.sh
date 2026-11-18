#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

mkdir -p build/armv7

for program in init statusd statusctl logd logctl powerctl configd configctl servicectl; do
    echo "Building $program for Galaxy S3..."

    sources=("src/$program.c")

    if [[ "$program" == "init" ]]; then
        sources+=(src/service_config.c src/service_state.c src/power_control.c src/power_socket.c src/shutdown_state.c src/power_transition.c src/service_control.c src/config_engine.c src/config_store.c)
    fi

    if [[ "$program" == "configd" ]]; then
        sources+=(src/config_engine.c src/config_store.c)
    fi

    if [[ "$program" == "configctl" ]]; then
        sources+=(src/config_engine.c)
    fi

    if [[ "$program" == "statusd" ]]; then
        sources+=(src/cpu_info.c)
    fi

    arm-linux-gnueabihf-gcc \
        -static -O2 -Wall -Wextra -Werror \
        -DSELYRA_VERSION="\"$(cat VERSION)\"" \
        -march=armv7-a \
        -mfpu=vfpv3-d16 \
        -mfloat-abi=hard \
        -o "build/armv7/$program" \
        "${sources[@]}"
done

echo
file build/armv7/*
echo "Galaxy S3 ARMv7 build completed."
