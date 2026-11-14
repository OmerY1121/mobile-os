# Mobile OS

An experimental Linux-based mobile operating system.

The project is still in early development.
The long-term goal is to build an open-source
mobile platform focused on privacy, user control
and long-term device support.

## Current state

- Custom init process
- Basic service management
- Local IPC using Unix domain sockets
- Basic service recovery
- Automated IPC tests
- QEMU boot support

There is no graphical interface or mobile
hardware support yet.

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


## Development status (v0.0.8)

v0.0.8 adds local logging, configurable service management,
service status reporting and health checks. The system boots
under x86-64 and ARMv7 QEMU.

The Galaxy S3 boot image has only passed offline checks. It
has not been tested on the device and is not ready for flashing.

See [v0.0.8 release notes](docs/releases/v0.0.8.md).
