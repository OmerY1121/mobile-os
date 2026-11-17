#!/usr/bin/env python3

import hashlib
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

root = Path.cwd()
kernel = root / "build/vmlinuz"
rootfs = root / "build/rootfs"
original = root / "build/selyra-data-test.img"

assert original.is_file() and not original.is_symlink()

def checksum(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

before = checksum(original)

with tempfile.TemporaryDirectory(
    prefix="selyra-persistence-"
) as directory:

    work = Path(directory)
    disk = work / "persistent.img"

    shutil.copyfile(original, disk)

    for phase in ("write", "read", "read-ro"):
        readonly = phase == "read-ro"
        access = "ro" if readonly else "rw"
        disk_before = checksum(disk)
        stage = work / f"rootfs-{phase}"
        image = work / f"{phase}.cpio.gz"

        shutil.copytree(rootfs, stage)
        (stage / "init").rename(stage / "selyra-init")

        shutil.copy2(
            root / f"build/storage-{phase}-probe",
            stage / "init"
        )

        pack = r'''
set -euo pipefail
cd "$1"
mkdir -p dev
rm -f dev/console dev/null
mknod -m 600 dev/console c 5 1
mknod -m 666 dev/null c 1 3
find . -print0 |
    cpio --null --quiet -o -H newc |
    gzip -9 > "$2"
'''

        subprocess.run([
            "fakeroot", "bash", "-c", pack,
            "bash", str(stage), str(image)
        ], check=True)

        print(f"Starting storage {phase} boot...",
              flush=True)

        result = subprocess.run([
            "qemu-system-x86_64",
            "-m", "512",
            "-accel", "tcg",
            "-nographic",
            "-nic", "none",
            "-kernel", str(kernel),
            "-initrd", str(image),
            "-append",
            f"console=ttyS0 rdinit=/init selyra.storage={access}",
            "-drive",
            f"file={disk},format=raw,if=virtio" +
            (",readonly=on" if readonly else ""),
            "-no-reboot"
        ], stdin=subprocess.DEVNULL,
           stdout=subprocess.PIPE,
           stderr=subprocess.STDOUT,
           text=True,
           errors="replace",
           timeout=90)

        output = result.stdout

        expected = [
            ("init: storage mounted read-only at /data"
             if readonly else
             "init: storage mounted read-write at /data"),
            "init: poweroff shutdown requested",
            "init: services stopped; power transition pending",
            *([] if readonly else ["init: storage synchronized"]),
            "init: storage unmounted safely",
            "reboot: Power down",
            "PASS: Secure storage mount flags",
            "PASS: Unauthorized UID 1000 rejected",
            "PASS: Unauthorized UID 1100 rejected",
            "PASS: Unauthorized UID 1101 rejected",
            "PASS: Unauthorized UID 1200 rejected",
            f"storage: device=/dev/vda mount=/data fs=ext4 access={access}",
            "PASS: Storage permission checks completed"
        ]

        if phase == "write":
            expected.append(
                "PASS: Persistent file written and synchronized"
            )
        else:
            expected.extend([
                "PASS: Persistent file survived shutdown",
                "PASS: Persistent file contents verified"
            ])

        if readonly:
            expected.append(
                "PASS: Root write access rejected in read-only mode"
            )

        if (
            result.returncode != 0
            or not all(s in output for s in expected)
            or "STORAGE-FAIL:" in output
            or "Kernel panic" in output
        ):
            print(output[-8000:])
            raise SystemExit(f"FAIL: Storage {phase} boot")

        for line in output.splitlines():
            if line.startswith(("PASS:", "DATA-INFO:", "storage:")):
                print(line)

        shutdown_order = [
            "init: services stopped; power transition pending",
            *([] if readonly else ["init: storage synchronized"]),
            "init: storage unmounted safely",
            "init: requesting kernel power transition",
            "reboot: Power down"
        ]

        report = re.search(
            r"total=(\d+)KiB used=(\d+)KiB available=(\d+)KiB",
            output
        )

        if not report:
            raise SystemExit("FAIL: Storage capacity report missing")

        total, used, available = map(int, report.groups())

        if total <= 0 or used + available > total:
            raise SystemExit("FAIL: Invalid storage capacity values")

        print("PASS: Storage capacity values consistent")

        if readonly:
            if checksum(disk) != disk_before:
                raise SystemExit("FAIL: Read-only disk modified")

            print("PASS: Read-only disk unchanged")

        positions = [
            output.index(message)
            for message in shutdown_order
        ]

        if positions != sorted(positions):
            raise SystemExit("FAIL: Incorrect shutdown order")

        print("PASS: Storage shutdown order verified")

        subprocess.run(
            ["e2fsck", "-fn", str(disk)],
            check=True
        )

        print("PASS: ext4 filesystem integrity verified")

        print(f"PASS: Guest {phase} poweroff")

if checksum(original) != before:
    raise SystemExit("FAIL: Original disk modified")

print("PASS: Original disk unchanged")
print("All Selyra persistence tests passed.")
