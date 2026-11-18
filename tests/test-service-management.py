#!/usr/bin/env python3
import os
import selectors
import shutil
import signal
import subprocess
import tempfile
import time
from pathlib import Path

ROOT = Path.cwd()
KERNEL = ROOT / "build/vmlinuz"
ROOTFS = ROOT / "build/rootfs"

GUEST = Path("tests/service-management-probe.c").read_text()

if not KERNEL.is_file() or not (ROOTFS / "init").is_file():
    raise SystemExit("Run make first.")

expected = [
    "MO-E2E: PASS unauthorized command rejected",
    "MO-E2E: PASS service stopped",
    "MO-E2E: PASS service started",
    "MO-E2E: PASS service restarted with new PID",
    "MO-E2E: PASS duplicate start rejected",
    "MO-E2E: PASS logd management",
    "MO-E2E: PASS statusd management",
    "MO-E2E: COMPLETE",
]

with tempfile.TemporaryDirectory(
    prefix="mo-e2e-", dir=ROOT / "build"
) as tmp:
    work = Path(tmp)
    guest_root = work / "rootfs"

    shutil.copytree(ROOTFS, guest_root, symlinks=True)

    source = work / "probe.c"
    binary = guest_root / "init"
    original = guest_root / "init-real"
    source.write_text(GUEST)

    binary.rename(original)

    subprocess.run([
        "gcc", "-static", "-O2",
        "-std=gnu11", "-Wall", "-Wextra", "-Werror",
        str(source), "-o", str(binary)
    ], check=True)

    image = work / "initramfs.gz"

    pack = r'''
set -euo pipefail
cd "$1"
mkdir -p dev
rm -f dev/console dev/null
mknod -m 600 dev/console c 5 1
mknod -m 666 dev/null c 1 3
chown 0:0 etc/selyra/services.conf
chmod 0600 etc/selyra/services.conf
find . -print0 |
    cpio --null --quiet -o -H newc |
    gzip -9 > "$2"
'''

    subprocess.run([
        "fakeroot", "bash", "-c", pack,
        "bash", str(guest_root), str(image)
    ], check=True)

    command = [
        "qemu-system-x86_64",
        "-m", "512",
        "-accel", "tcg",
        "-nographic",
        "-nic", "none",
        "-kernel", str(KERNEL),
        "-initrd", str(image),
        "-append", "console=ttyS0 rdinit=/init",
        "-no-reboot"
    ]

    print("Starting QEMU end-to-end test...", flush=True)

    proc = subprocess.Popen(
        command,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        start_new_session=True,
    )

    output = ""
    selector = selectors.DefaultSelector()
    deadline = time.monotonic() + 120

    try:
        selector.register(proc.stdout, selectors.EVENT_READ)

        while time.monotonic() < deadline:
            for key, _ in selector.select(0.25):
                chunk = os.read(key.fileobj.fileno(), 4096)

                if not chunk:
                    selector.unregister(key.fileobj)
                    continue

                output += chunk.decode(
                    "utf-8", errors="replace"
                )
                output = output[-200000:]

            if ("MO-E2E: COMPLETE" in output or
                "MO-E2E: FAIL" in output):
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

    missing = [item for item in expected
               if item not in output]

    for item in expected:
        if item in output:
            print(item)

    if missing or "MO-E2E: FAIL" in output:
        print("FAIL: QEMU end-to-end integration")
        print("Missing checks:", missing)
        print("\nLast guest output:")
        print(output[-6000:])
        raise SystemExit(1)

print("All QEMU end-to-end checks passed.")
