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
