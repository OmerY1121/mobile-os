#!/usr/bin/env python3

from pathlib import Path

image = Path("build/selyra-data-test.img")

if not image.is_file() or image.is_symlink():
    raise SystemExit("FAIL: Storage image missing or invalid")

expected_size = 64 * 1024 * 1024

if image.stat().st_size != expected_size:
    raise SystemExit("FAIL: Unexpected image size")

with image.open("rb") as f:
    f.seek(1024)
    superblock = f.read(1024)

if superblock[56:58] != b"\x53\xef":
    raise SystemExit("FAIL: Invalid ext4 signature")

label = superblock[120:136].split(b"\0", 1)[0]

if label != b"SELYRA_DATA":
    raise SystemExit("FAIL: Wrong filesystem label")

print("PASS: Storage image size")
print("PASS: ext4 filesystem signature")
print("PASS: Selyra filesystem label")
print("All storage image checks passed.")
