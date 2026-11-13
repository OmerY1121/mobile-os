# Changelog

## v0.0.5.1 - Service privileges

### Added
- Dedicated UID and GID for statusd.
- Privilege dropping before service execution.
- Restricted service directory access.

### Tested
- Existing IPC tests.
- QEMU boot with reduced service privileges.


## v0.0.5 - Peer authentication

### Added
- SO_PEERCRED-based peer authentication.
- Cross-user authentication tests.
- GitHub Actions CI workflow.

### Tested
- Authorized and unauthorized connections.
- Existing IPC functionality.
- QEMU boot.

### Limitations
- Same-UID processes are not isolated.
- No application permission framework yet.


## v0.0.4 - IPC error handling

### Added
- Shared IPC functions.
- Partial message handling.
- Socket timeouts.
- Five additional IPC error tests.

### Fixed
- Incomplete message handling.
- Basic handling of failed connections.

### Limitations
- Sequential client processing.
- No application-level authentication.


## v0.0.3.1 - Build and tests

- Added Makefile.
- Added automated IPC tests.
- Added socket permission checks.


## v0.0.3 - Local IPC

### Added
- Unix domain socket communication.
- statusctl client.
- Local status request handling.

### Tested
- IPC communication in Codespaces.
- Socket creation in QEMU.
- Client/server communication inside QEMU.

### Limitations
- One command supported.
- No general permission or IPC framework yet.


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
