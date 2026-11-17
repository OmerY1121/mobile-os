#!/usr/bin/env python3

import hashlib
import shutil
import re
import subprocess
import tempfile
import time
from pathlib import Path

version = Path("VERSION").read_text().strip()
kernel = Path("build/vmlinuz")
initramfs = Path("build/initramfs.cpio.gz")
disk = Path("build/selyra-data-test.img")

for path in (kernel, initramfs, disk):
    if not path.is_file() or path.is_symlink():
        raise SystemExit(f"STOP: Invalid file: {path}")

DISABLED = "init: storage disabled"
MOUNTED = "init: storage mounted read-only at /data"
UNAVAILABLE = (
    "init: storage unavailable; "
    "continuing without persistent data"
)

WRITABLE = "init: storage mounted read-write at /data"
outcomes = [DISABLED, MOUNTED, UNAVAILABLE, WRITABLE]

def checksum(path):
    digest = hashlib.sha256()

    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            digest.update(chunk)

    return digest.hexdigest()


def test_boot(name, work, enabled, expected, image=None,
              extra=None, writable=False):

    logfile = work / f"{name}.log"

    cmdline = "console=ttyS0 rdinit=/init"

    if enabled:
        cmdline += (" selyra.storage=rw" if writable
                    else " selyra.storage=ro")

    command = [
        "qemu-system-x86_64",
        "-m", "512",
        "-accel", "tcg",
        "-nographic",
        "-nic", "none",
        "-kernel", str(kernel),
        "-initrd", str(initramfs),
        "-append", cmdline,
        "-no-reboot",
    ]

    if image is not None:
        command += [
            "-drive",
            f"file={image},format=raw,if=virtio," +
            ("snapshot=on" if writable else "readonly=on")
        ]

    required = [
        expected,
        "init: service manager ready",
        "logd: local logging ready",
        "statusd: local IPC ready",
    ]

    version_pattern = re.compile(
        rf"(?m)^Selyra OS {re.escape(version)}"
        r"(?=[ \t]*(?:$|\[\s*\d+\.\d+\]))"
    )

    if extra:
        required.append(extra)

    print(f"Testing: {name}", flush=True)

    passed = False
    output = ""

    with logfile.open("wb") as log:
        proc = subprocess.Popen(
            command,
            stdin=subprocess.DEVNULL,
            stdout=log,
            stderr=subprocess.STDOUT,
        )

        try:
            deadline = time.monotonic() + 60

            while time.monotonic() < deadline:
                output = logfile.read_text(errors="replace")

                if (version_pattern.search(output) and
                    all(marker in output for marker in required)):
                    seen = [
                        marker for marker in outcomes
                        if marker in output
                    ]

                    if seen == [expected]:
                        passed = True

                    break

                if proc.poll() is not None:
                    break

                time.sleep(0.1)

        finally:
            if proc.poll() is None:
                proc.terminate()

                try:
                    proc.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait()

    if not passed:
        print(output[-6000:])
        raise SystemExit(f"FAIL: {name}")

    print(f"PASS: {name}", flush=True)


before = checksum(disk)

with tempfile.TemporaryDirectory(
    prefix="selyra-storage-"
) as directory:

    work = Path(directory)

    invalid = work / "invalid.img"

    with invalid.open("xb") as f:
        f.truncate(8 * 1024 * 1024)

    test_boot(
        "storage-disabled",
        work, False, DISABLED, disk
    )

    test_boot(
        "valid-readonly-storage",
        work, True, MOUNTED, disk
    )

    test_boot(
        "missing-storage",
        work, True, UNAVAILABLE
    )

    test_boot(
        "invalid-storage",
        work, True, UNAVAILABLE, invalid,
        extra="init: storage signature or label invalid"
    )


    unsafe_mode = work / "unsafe-mode.img"
    unsafe_owner = work / "unsafe-owner.img"

    shutil.copyfile(disk, unsafe_mode)
    shutil.copyfile(disk, unsafe_owner)

    for image, command, pattern in [
        (
            unsafe_mode,
            "set_inode_field / mode 040755",
            r"Mode:\s+0755"
        ),
        (
            unsafe_owner,
            "set_inode_field / uid 1000",
            r"User:\s+1000"
        ),
    ]:
        subprocess.run(
            ["debugfs", "-w", "-R", command, str(image)],
            check=True,
            capture_output=True
        )

        result = subprocess.check_output(
            ["env", "LC_ALL=C", "debugfs", "-R",
             "stat /", str(image)],
            text=True
        )

        if not re.search(pattern, result):
            raise SystemExit("FAIL: Unsafe test image preparation")

    test_boot(
        "unsafe-permissions-ro",
        work, True, UNAVAILABLE, unsafe_mode,
        extra="init: storage root permissions invalid"
    )

    test_boot(
        "unsafe-permissions-rw",
        work, True, UNAVAILABLE, unsafe_mode,
        extra="init: storage root permissions invalid",
        writable=True
    )

    test_boot(
        "unsafe-owner",
        work, True, UNAVAILABLE, unsafe_owner,
        extra="init: storage root permissions invalid"
    )

    test_boot(
        "valid-writable-storage",
        work, True, WRITABLE, disk,
        writable=True
    )

if checksum(disk) != before:
    raise SystemExit("FAIL: Storage image was modified")

print("PASS: Original disk contents unchanged")
print("All Selyra storage boot tests passed.")
