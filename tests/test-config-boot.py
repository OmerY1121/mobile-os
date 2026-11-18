import hashlib
import shutil
import subprocess
import tempfile
from pathlib import Path

root = Path.cwd()
original = root / "build/selyra-data-test.img"

assert original.is_file() and not original.is_symlink()

def checksum(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

before = checksum(original)

with tempfile.TemporaryDirectory(
    prefix="selyra-config-boot-"
) as directory:

    work = Path(directory)
    disk = work / "config.img"

    shutil.copyfile(original, disk)

    for phase in (1, 2, 3):
        readonly = phase == 3
        mode = "ro" if readonly else "rw"
        disk_before = checksum(disk)

        stage = work / f"rootfs-{phase}"
        image = work / f"phase-{phase}.cpio.gz"

        shutil.copytree(root / "build/rootfs", stage)
        (stage / "init").rename(stage / "selyra-init")

        subprocess.run([
            "gcc", "-static", "-O2",
            "-Wall", "-Wextra", "-Werror",
            f"-DPHASE={phase}",
            "tests/config-boot-probe.c",
            "-o", str(stage / "init")
        ], check=True)

        pack = r'''
set -euo pipefail
cd "$1"
mkdir -p dev
rm -f dev/console dev/null
mknod -m 600 dev/console c 5 1
mknod -m 666 dev/null c 1 3
chown -R 0:0 .
find . -print0 |
    cpio --null --quiet -o -H newc |
    gzip -9 > "$2"
'''

        subprocess.run([
            "fakeroot", "bash", "-c", pack,
            "bash", str(stage), str(image)
        ], check=True)

        print(f"Starting configuration boot {phase}...",
              flush=True)

        result = subprocess.run([
            "qemu-system-x86_64",
            "-m", "512",
            "-accel", "tcg",
            "-nographic",
            "-nic", "none",
            "-kernel", "build/vmlinuz",
            "-initrd", str(image),
            "-append",
            f"console=ttyS0 rdinit=/init selyra.storage={mode}",
            "-drive",
            f"file={disk},format=raw,if=virtio" +
            (",readonly=on" if readonly else ""),
            "-no-reboot"
        ], stdin=subprocess.DEVNULL,
           stdout=subprocess.PIPE,
           stderr=subprocess.STDOUT,
           text=True, errors="replace",
           timeout=120)

        output = result.stdout

        expected = [
            f"PASS: Configuration phase {phase} complete",
            "PASS: Unauthorized settings update rejected",
            "init: storage unmounted safely",
            "reboot: Power down"
        ]

        if phase == 1:
            expected.append("PASS: Settings saved for next boot")
        else:
            expected.extend([
                "PASS: Saved settings restored and kernel hostname verified",
                "PASS: 16 KiB log rotation verified"
            ])

        if readonly:
            expected.append("PASS: Read-only settings protected")

        if (
            result.returncode != 0 or
            not all(m in output for m in expected) or
            "CONFIG-FAIL:" in output or
            "Kernel panic" in output
        ):
            print(output[-10000:])
            raise SystemExit(f"FAIL: Configuration boot {phase}")

        saved = subprocess.check_output([
            "debugfs", "-R", "cat /config/settings.conf",
            str(disk)
        ], text=True)

        expected_file = "\n".join([
            "system.hostname=selyra-config-test",
            "logging.max_kib=16",
            ""
        ])

        if saved != expected_file:
            raise AssertionError(
                f"Expected: {expected_file!r}\nActual: {saved!r}"
            )

        for line in output.splitlines():
            if line.startswith("PASS:"):
                print(line)

        if readonly:
            assert checksum(disk) == disk_before
            print("PASS: Read-only disk unchanged")

        subprocess.run(
            ["e2fsck", "-fn", str(disk)],
            check=True
        )

        print(f"PASS: Configuration boot {phase}")

assert checksum(original) == before

print("PASS: Original disk unchanged")
print("All Selyra service configuration boot tests passed.")
