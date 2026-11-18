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
    prefix="selyra-policy-boot-"
) as directory:

    work = Path(directory)
    disk = work / "policy.img"

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
            "tests/service-policy-probe.c",
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

        print(f"Starting policy boot {phase}...",
              flush=True)

        try:
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


        except subprocess.TimeoutExpired as exc:
            output = exc.stdout or b""
            if isinstance(output, bytes):
                output = output.decode(errors="replace")
            print(output[-12000:])
            raise SystemExit(f"FAIL: Policy boot {phase} timed out")

        output = result.stdout

        expected = [
            f"PASS: Policy phase {phase} completed",
            "PASS: Current service state preserved",
            "init: storage unmounted safely",
            "reboot: Power down"
        ]

        if phase == 1:
            expected += [
                "init: default service policy active",
                "PASS: At least one service remains enabled"
            ]
        elif phase == 2:
            expected += [
                "init: persistent service policy loaded",
                "init: logd disabled by config",
                "PASS: Disabled service policy restored",
                "PASS: Disabled service started manually",
                "PASS: Automatic startup policy unchanged",
                "PASS: Manually started service stopped"
            ]
        else:
            expected += [
                "init: persistent service policy loaded",
                "PASS: Enabled service policy restored",
                "PASS: Read-only policy update rejected"
            ]

        if (
            result.returncode != 0 or
            not all(m in output for m in expected) or
            "POLICY-FAIL:" in output or
            "Kernel panic" in output
        ):
            print(output[-10000:])
            raise SystemExit(f"FAIL: Policy boot {phase}")

        saved = subprocess.check_output([
            "debugfs", "-R", "cat /services.conf",
            str(disk)
        ], text=True)

        policy = dict(
            line.split("=", 1)
            for line in saved.splitlines()
            if "=" in line
        )

        assert policy["logd.enabled"] == (
            "0" if phase == 1 else "1"
        )
        assert policy["statusd.enabled"] == "1"

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

        print(f"PASS: Policy boot {phase}")

assert checksum(original) == before

print("PASS: Original disk unchanged")
print("All Selyra service policy boot tests passed.")
