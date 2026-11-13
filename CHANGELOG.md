# Changelog

## v0.0.2 - Basic service management

### Added
- First test service (statusd).
- Service startup and exit monitoring.
- Automatic restart with a failure limit.

### Tested
- Normal service startup in QEMU.
- Repeated failure using exit code 1.
- Restart limit after three failed runs.
- Normal service restored and verified.

### Limitations
- Only one service is supported.
- No service configuration or IPC yet.


## v0.0.1 - Initial boot

### Added
- Minimal init process written in C.
- Basic initramfs.
- Early boot messages.

### Tested
- Static compilation with GCC.
- Boot with Linux 6.8.0-149-generic in QEMU.
- Init successfully started as PID 1.

### Limitations
- No service management.
- No graphical interface.
- No networking or application support.
- Only tested on x86-64.

### Next
- Basic service management in v0.0.2.
