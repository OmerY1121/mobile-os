#!/usr/bin/env python3
"""Real power transition test inside isolated QEMU guests."""

import argparse
import os
import re
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
HELPER = ROOT / "tests/power-qemu-transition-init.c"

parser = argparse.ArgumentParser()
parser.add_argument("action", choices=("poweroff", "reboot"))
parser.add_argument("--stubborn-logd", action="store_true")
args = parser.parse_args()
action = args.action

qemu = shutil.which("qemu-system-x86_64")

required = [
    KERNEL,
    ROOTFS / "init",
    ROOTFS / "bin/powerctl",
    HELPER
]

if not qemu or any(not p.is_file() for p in required):
    sys.exit("STOP: QEMU, kernel, rootfs or helper missing")

if not shutil.which("fakeroot") or not shutil.which("cpio"):
    sys.exit("STOP: fakeroot or cpio missing")

with tempfile.TemporaryDirectory(
    prefix="selyra-kernel-power-"
) as tmp:
    work = Path(tmp)
    stage = work / "rootfs"

    shutil.copytree(ROOTFS, stage, symlinks=True)
    (stage / "init").rename(stage / "selyra-init")

    if args.stubborn_logd:
        subprocess.run([
            "gcc", "-static", "-O2",
            "-Wall", "-Wextra", "-Werror",
            "-o", str(stage / "bin/logd"),
            str(ROOT / "tests/stubborn-logd.c")
        ], check=True)

    subprocess.run([
        "gcc", "-static", "-O2",
        "-Wall", "-Wextra", "-Werror",
        f'-DSELYRA_TEST_ACTION="{action}"',
        "-o", str(stage / "init"), str(HELPER)
    ], check=True)

    image = work / "guest.cpio.gz"

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
        "-kernel", str(KERNEL.resolve()),
        "-initrd", str(image),
        "-append", "console=ttyS0 rdinit=/init",
        "-no-reboot"
    ]

    print(f"Starting isolated QEMU {action} test...",
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
        selector.register(proc.stdout, selectors.EVENT_READ)
        deadline = time.monotonic() + 90

        while time.monotonic() < deadline:
            for key, _ in selector.select(timeout=0.25):
                chunk = os.read(
                    key.fileobj.fileno(), 4096
                )

                if chunk:
                    output += chunk.decode(
                        "utf-8", errors="replace"
                    )
                    output = output[-300000:]
                else:
                    selector.unregister(key.fileobj)

            if proc.poll() is not None and not selector.get_map():
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

        proc.stdout.close()

checks = {
    "Guest request sent":
        f"SEL-POWER-E2E: requesting {action}" in output,

    "Client acknowledged":
        "SEL-POWER-E2E: request acknowledged" in output,

    "PID 1 accepted":
        f"init: {action} shutdown requested" in output,

    "SIGTERM sent":
        "init: shutdown SIGTERM sent" in output,

    "Services stopped":
        "init: services stopped; power transition pending"
        in output,

    "Kernel transition invoked":
        "init: requesting kernel power transition" in output,

    "Guest kernel confirmed transition":
        bool(re.search(
            r"reboot:\s*Power down"
            if action == "poweroff"
            else r"reboot:\s*Restarting system",
            output,
            re.I
        )),

    "QEMU exited successfully":
        proc.returncode == 0,

    "No kernel panic":
        not re.search(r"Kernel panic", output, re.I),

    "No transition syscall error":
        "kernel power transition failed" not in output
}

if args.stubborn_logd:
    request_marker = f"init: {action} shutdown requested"

    after_request = (
        output.split(request_marker, 1)[1]
        if request_marker in output else ""
    )

    checks["Stubborn service started"] = (
        "SEL-POWER-E2E: stubborn logd running" in output
    )

    checks["SIGKILL escalation"] = (
        "init: shutdown SIGKILL escalation" in output
    )

    checks["No logd restart after request"] = (
        bool(after_request) and
        "init: logd started (PID" not in after_request
    )

for label, success in checks.items():
    print(
        ("PASS" if success else "FAIL")
        + ": " + label
    )

if not all(checks.values()):
    print("\nLast QEMU output:\n" + output[-8000:])
    sys.exit(1)

print(f"PASS: real QEMU {action} transition verified")
