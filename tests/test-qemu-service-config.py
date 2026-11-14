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

CASES = [
    {
        "name": "logd disabled",
        "setting": "logd.enabled=1",
        "replacement": "logd.enabled=0",
        "expected": [
            "init: service configuration loaded",
            "init: logd disabled by config",
            "init: statusd launcher started",
            "statusd: local IPC ready",
            "init: service manager ready",
        ],
        "forbidden": [
            "logd: local logging ready",
            "init: logd started",
        ],
    },
    {
        "name": "statusd disabled",
        "setting": "statusd.enabled=1",
        "replacement": "statusd.enabled=0",
        "expected": [
            "init: service configuration loaded",
            "init: statusd disabled by config",
            "logd: local logging ready",
            "init: service manager ready",
        ],
        "forbidden": [
            "statusd: local IPC ready",
            "init: statusd launcher started",
        ],
    },
]

def prepare_image(work, case):
    rootfs = work / "rootfs"
    shutil.copytree(ROOTFS, rootfs)

    config = rootfs / "etc/mobile-os/services.conf"
    content = config.read_text()

    if content.count(case["setting"]) != 1:
        raise RuntimeError("Unexpected service configuration")

    config.write_text(
        content.replace(
            case["setting"],
            case["replacement"], 1
        )
    )

    image = work / "initramfs.gz"

    script = r'''
set -euo pipefail
cd "$1"

chown 0:0 etc/mobile-os/services.conf
chmod 0600 etc/mobile-os/services.conf

mkdir -p dev
rm -f dev/console dev/null
mknod -m 600 dev/console c 5 1
mknod -m 666 dev/null c 1 3

find . -print0 |
    cpio --null --quiet -o -H newc |
    gzip -9 > "$2"
'''

    subprocess.run(
        ["fakeroot", "bash", "-c", script,
         "bash", str(rootfs), str(image)],
        check=True,
    )
    return image

def run_qemu(image, case):
    command = [
        "qemu-system-x86_64",
        "-m", "512",
        "-accel", "tcg",
        "-nographic",
        "-nic", "none",
        "-kernel", str(KERNEL),
        "-initrd", str(image),
        "-append", "console=ttyS0 rdinit=/init",
        "-no-reboot",
    ]

    proc = subprocess.Popen(
        command,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        start_new_session=True,
    )

    output = ""
    deadline = time.monotonic() + 60
    ready_at = None
    selector = selectors.DefaultSelector()

    try:
        selector.register(
            proc.stdout, selectors.EVENT_READ
        )

        while time.monotonic() < deadline:
            for key, _ in selector.select(0.25):
                chunk = os.read(key.fileobj.fileno(), 4096)

                if not chunk:
                    selector.unregister(key.fileobj)
                    continue

                output += chunk.decode(
                    "utf-8", errors="replace"
                )

            if all(
                item in output
                for item in case["expected"]
            ):
                if ready_at is None:
                    ready_at = time.monotonic()

                if time.monotonic() - ready_at >= 1:
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

    missing = [
        item for item in case["expected"]
        if item not in output
    ]
    unexpected = [
        item for item in case["forbidden"]
        if item in output
    ]

    if missing or unexpected:
        print(f"FAIL: {case['name']}")
        print("Missing:", missing)
        print("Unexpected:", unexpected)
        print(output[-4000:])
        return False

    print(f"PASS: {case['name']}")
    return True

if not KERNEL.is_file() or not ROOTFS.is_dir():
    raise SystemExit("Run make first.")

with tempfile.TemporaryDirectory(
    prefix="service-config-",
    dir=ROOT / "build",
) as temp:
    base = Path(temp)

    for index, case in enumerate(CASES):
        work = base / str(index)
        work.mkdir()

        image = prepare_image(work, case)

        if not run_qemu(image, case):
            raise SystemExit(1)

print("All QEMU service configuration tests passed.")
