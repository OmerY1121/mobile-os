#!/usr/bin/env python3

import os
import re
import selectors
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path

TIMEOUT = 45

checks = {
    "System version": r"Mobile OS 0\.0\.8(?!-dev)",
    "Private filesystem": r"statusd private tmpfs ready",
    "Restricted proc": r"statusd restricted /proc ready",
    "PID namespace": r"namespace supervisor running as PID 1",
    "Service identity":
        r"statusd UID=1100 GID=1100 namespace PID=2 PPID=1",
    "Privilege protection": r"statusd: no_new_privs=1",
    "Service state":
        r"init: service state published",
    "Service configuration":
        r"init: service configuration loaded",
    "Logging service": r"logd: local logging ready",
    "Boot event stored":
        r"init: boot event logged",
    "Statusd event stored":
        r"statusd: boot event logged",
    "IPC service": r"statusd: local IPC ready",
}

kernel = Path("build/vmlinuz")
initramfs = Path("build/initramfs.cpio.gz")

if not kernel.is_file() or not initramfs.is_file():
    sys.exit("Missing kernel or initramfs. Run make first.")

qemu = shutil.which("qemu-system-x86_64")

if not qemu:
    sys.exit("QEMU is not installed.")

command = [
    qemu,
    "-m", "512",
    "-accel", "tcg",
    "-nographic",
    "-nic", "none",
    "-kernel", str(kernel),
    "-initrd", str(initramfs),
    "-append", "console=ttyS0 rdinit=/init",
    "-no-reboot",
]

print("Starting Mobile OS in QEMU...", flush=True)

proc = subprocess.Popen(
    command,
    stdin=subprocess.DEVNULL,
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
    start_new_session=True,
)

output = ""
deadline = time.monotonic() + TIMEOUT
selector = selectors.DefaultSelector()

try:
    selector.register(proc.stdout, selectors.EVENT_READ)

    while time.monotonic() < deadline:
        events = selector.select(timeout=0.25)

        for key, _ in events:
            chunk = os.read(key.fileobj.fileno(), 4096)

            if not chunk:
                selector.unregister(key.fileobj)
                break

            output += chunk.decode("utf-8", errors="replace")
            output = output[-200000:]

        if all(re.search(pattern, output)
               for pattern in checks.values()):
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

failed = []

for name, pattern in checks.items():
    if re.search(pattern, output):
        print(f"PASS: {name}")
    else:
        print(f"FAIL: {name}")
        failed.append(name)

if failed:
    print("\nLast QEMU output:")
    print(output[-4000:])
    sys.exit(1)

print("\nAll QEMU boot checks passed.")
