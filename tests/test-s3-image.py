#!/usr/bin/env python3

import gzip
import struct
import subprocess
from pathlib import Path

base = Path("build/s3")
image = (base / "UNTESTED-mobile-os-i9300.img").read_bytes()
kernel = (base / "kernel/zImage-dtb").read_bytes()
dtb = (base / "kernel/exynos4412-i9300.dtb").read_bytes()
ramdisk = (base / "mobile-os-initramfs.cpio.gz").read_bytes()

def check(condition, message):
    if not condition:
        raise SystemExit("FAIL: " + message)
    print("PASS: " + message)

check(image[:8] == b"ANDROID!", "Android boot header")

fields = struct.unpack_from("<10I", image, 8)
ksize, kaddr, rsize, raddr = fields[:4]
second_size = fields[4]
pagesize = fields[7]
version = fields[8]

check(
    pagesize == 2048 and version == 0,
    "Legacy boot image format"
)

check(
    kaddr == 0x10008000 and raddr == 0x11000000,
    "Galaxy S3 load addresses"
)

check(second_size == 0, "No second-stage payload")

align = lambda n: ((n + pagesize - 1) // pagesize) * pagesize

kernel_start = pagesize
ramdisk_start = kernel_start + align(ksize)

check(
    image[kernel_start:kernel_start + ksize] == kernel,
    "Kernel contents match"
)

check(
    image[ramdisk_start:ramdisk_start + rsize] == ramdisk,
    "Ramdisk contents match"
)

check(
    dtb[:4] == bytes.fromhex("d00dfeed") and
    kernel.endswith(dtb),
    "Device tree appended"
)

command_line = image[64:576].split(b"\0", 1)[0]

check(
    b"rdinit=/init" in command_line,
    "Mobile OS init selected"
)

check(
    b"TRAILER!!!" in gzip.decompress(ramdisk),
    "Initramfs archive readable"
)

arm_init = Path("build/armv7/init").read_bytes()

check(
    b"Mobile OS 0.0.8" in arm_init,
    "ARMv7 release version"
)

check(
    len(image) <= 8 * 1024 * 1024,
    "Image within 8 MiB"
)


archive = gzip.decompress(ramdisk)

listing = subprocess.run(
    ["cpio", "--quiet", "-t"],
    input=archive,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    check=True
)

present = {
    name.removeprefix("./")
    for name in listing.stdout.decode().splitlines()
}

required = {
    "init",
    "bin/statusd",
    "bin/statusctl",
    "bin/logd",
    "bin/logctl",
    "etc/mobile-os/services.conf",
}

check(
    required.issubset(present),
    "Initramfs includes all services"
)

print("\nAll offline S3 image checks passed.")
print("Image is NOT verified for flashing.")
