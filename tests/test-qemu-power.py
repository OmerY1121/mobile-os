#!/usr/bin/env python3
"""QEMU-only shutdown test. No kernel power transition."""

import os
import selectors
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
KERNEL = ROOT / "build/vmlinuz"
ROOTFS = ROOT / "build/rootfs"
HELPER = ROOT / "tests/power-qemu-init.c"

qemu = shutil.which("qemu-system-x86_64")

if not qemu or not KERNEL.is_file() or not (
    ROOTFS / "bin/powerctl"
).is_file():
    sys.exit(
        "STOP: QEMU, kernel or powerctl image missing"
    )

checks = {
    "Root request sent":
        "SEL-E2E: sending reboot request",
    "Client received success": "SEL-E2E: reboot request acknowledged",
    "Duplicate request rejected": "SEL-E2E: duplicate power request rejected",
    "PID 1 accepted reboot":
        "init: reboot shutdown requested",
    "Graceful termination started":
        "init: shutdown SIGTERM sent",
    "Service shutdown completed":
        "init: services stopped; power transition pending",
}

with tempfile.TemporaryDirectory(
    prefix="selyra-power-"
) as tmp:
    work = Path(tmp)
    stage = work / "rootfs"
    stage.mkdir()

    subprocess.run([
        "cp", "-a", str(ROOTFS) + "/.", str(stage)
    ], check=True)

    (stage / "init").rename(stage / "selyra-init")
    shutil.copy2(ROOT / "build/init-dry-run", stage / "selyra-init")

    subprocess.run([
        "gcc", "-static", "-O2",
        "-Wall", "-Wextra", "-Werror",
        "-o", str(stage / "init"), str(HELPER)
    ], check=True)

    image = work / "test-initramfs.cpio.gz"

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

    command = [
        qemu,
        "-m", "512",
        "-accel", "tcg",
        "-nographic",
        "-nic", "none",
        "-kernel", str(KERNEL),
        "-initrd", str(image),
        "-append", "console=ttyS0 rdinit=/init",
        "-no-reboot"
    ]

    print("Starting isolated Selyra power test...",
          flush=True)

    proc = subprocess.Popen(
        command,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        start_new_session=True
    )

    output = ""
    selector = selectors.DefaultSelector()

    try:
        selector.register(
            proc.stdout, selectors.EVENT_READ
        )

        deadline = time.monotonic() + 85

        while time.monotonic() < deadline:
            for key, _ in selector.select(timeout=0.25):
                chunk = os.read(
                    key.fileobj.fileno(), 4096
                )

                if not chunk:
                    selector.unregister(key.fileobj)
                    break

                output += chunk.decode(
                    "utf-8", errors="replace"
                )
                output = output[-300000:]

            if all(
                marker in output
                for marker in checks.values()
            ):
                break

            if proc.poll() is not None:
                break

    finally:
        selector.close()

        if proc.poll() is None:
            os.killpg(proc.pid, signal.SIGTERM)

        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            os.killpg(proc.pid, signal.SIGKILL)
            proc.wait()

    errors = []

    for label, marker in checks.items():
        success = marker in output

        print(
            ("PASS" if success else "FAIL")
            + ": " + label
        )

        if not success:
            errors.append(label)

    accepted = checks["PID 1 accepted reboot"]

    if accepted in output:
        after_request = output.split(accepted, 1)[1]

        restarted = (
            "init: statusd launcher started" in after_request
            or "init: logd started" in after_request
        )

        print(
            ("FAIL" if restarted else "PASS")
            + ": No service restart after request"
        )

        if restarted:
            errors.append("Unexpected restart")

    if "init: poweroff shutdown requested" in output:
        print("FAIL: Duplicate changed shutdown action")
        errors.append("Duplicate changed shutdown action")
    else:
        print("PASS: Original reboot action preserved")

    if errors:
        print("\nLast QEMU output:\n" + output[-5000:])
        sys.exit(1)

print("All QEMU service-shutdown request checks passed.")
