# Selyra OS

An experimental Linux-based mobile operating system.

The project is still in early development.
The long-term goal is to build an open-source
mobile platform focused on privacy, user control
and long-term device support.

Starting with v0.0.9, Mobile OS is named Selyra OS.
Earlier releases retain their original name.
Selyra OS 0.1.0 is the latest published release.

Version 0.1.0 focuses on fixes, internal naming and test improvements.
It also introduces standard SemVer numbering for future releases.

See [0.1.0 release notes](docs/releases/v0.1.0.md).

## Current state

- Custom init process
- Basic service management
- Local IPC using Unix domain sockets
- Basic service recovery
- Automated IPC tests
- QEMU boot support

There is no graphical interface or mobile
hardware support yet.

## Principles

Selyra is an independent Linux-based operating
system, not an Android fork.

Its design goals include privacy, minimal software,
user-controlled updates and downgrades, and freedom
from mandatory Google services.

See [Design Principles](docs/PRINCIPLES.md).

## Building

Requirements: GCC, Make, cpio, gzip, fakeroot,
QEMU and a compatible Linux kernel image.

Build:

    make

Run in QEMU:

    make run

Run tests:

    make test

A readable kernel image must be available at
build/vmlinuz before running QEMU.

## Releases

Release notes are available in docs/releases/.


## Latest release (v0.1.0)

This update focuses on fixes, naming and test improvements. See [release notes](docs/releases/v0.1.0.md).

## Previous release (v0.0.9)

Selyra OS v0.0.9 is an experimental release.

This version adds RAM and CPU monitoring,
system information, and controlled power management.

Poweroff and reboot have been tested in QEMU.
Physical mobile hardware operation remains untested.

See [v0.0.9 release notes](docs/releases/v0.0.9.md).

## Previous release (v0.0.8)

v0.0.8 adds local logging, configurable service management,
service status reporting and health checks. The system boots
under x86-64 and ARMv7 QEMU.

The Galaxy S3 boot image has only passed offline checks. It
has not been tested on the device and is not ready for flashing.

See [v0.0.8 release notes](docs/releases/v0.0.8.md).
