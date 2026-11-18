#!/usr/bin/env python3
"""Verify ARMv7 QEMU image freshness and consistency."""

import gzip
import subprocess
import sys
from pathlib import Path

version = Path("VERSION").read_text().strip()
base = Path("build/armv7")
image = base / "initramfs.cpio.gz"

if not image.is_file():
    sys.exit(
        "FAIL: ARM image missing. Rebuild ARMv7 first."
    )

sources = (
    list(Path("src").glob("*.c"))
    + list(Path("src").glob("*.h"))
    + [
        Path("VERSION"),
        Path("config/services.conf"),
        Path("scripts/build-s3.sh"),
        Path("scripts/prepare-armv7-qemu.sh"),
    ]
)

image_time = image.stat().st_mtime_ns

changed = [
    str(p) for p in sources
    if p.is_file() and p.stat().st_mtime_ns > image_time
]

if changed:
    sys.exit(
        "FAIL: ARM image is older than source files: "
        + ", ".join(changed[:6])
        + "\nRebuild the ARMv7 image."
    )

try:
    archive = gzip.decompress(image.read_bytes())
except (OSError, EOFError) as exc:
    sys.exit(f"FAIL: Cannot read ARM image: {exc}")

programs = [
    "init", "statusd", "statusctl",
    "logd", "logctl", "powerctl",
    "configd", "configctl", "servicectl"
]

for program in programs:
    binary = base / program
    member = (
        "init" if program == "init"
        else f"bin/{program}"
    )

    if not binary.is_file():
        sys.exit(f"FAIL: Missing ARM binary: {binary}")

    result = subprocess.run(
        ["cpio", "-i", "--quiet", "--to-stdout", member],
        input=archive,
        capture_output=True,
        check=False,
    )

    if (
        result.returncode != 0
        or result.stdout != binary.read_bytes()
    ):
        sys.exit(
            f"FAIL: ARM image has an outdated or "
            f"missing {program} binary."
        )

expected = f"Selyra OS {version}".encode()

if expected not in (base / "init").read_bytes():
    sys.exit(
        f"FAIL: ARM init version does not match {version}."
    )

config = Path("config/services.conf")

result = subprocess.run(
    [
        "cpio", "-i", "--quiet", "--to-stdout",
        "etc/selyra/services.conf"
    ],
    input=archive,
    capture_output=True,
    check=False,
)

if (
    result.returncode != 0
    or result.stdout != config.read_bytes()
):
    sys.exit(
        "FAIL: ARM service configuration is outdated."
    )

print(
    f"PASS: ARMv7 image matches current "
    f"binaries, config and Selyra OS {version}"
)
