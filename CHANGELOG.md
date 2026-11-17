# Changelog

## 0.2.0

Persistent storage support.

- Added ext4 storage through VirtIO.
- Added read-only and read-write storage modes.
- Added storage validation and access restrictions.
- Added statusctl storage.
- Added filesystem synchronization and unmounting.
- Added persistence and storage security tests.
- Extended CI with x86-64 QEMU tests.
- Fixed intermittent boot version matching in storage tests.


## 0.1.0

Maintenance and naming cleanup.

- Moved internal paths from Mobile OS to Selyra.
- Added SELYRA_ environment variables with legacy compatibility.
- Fixed native IPC authorization testing in Codespaces.
- Added environment compatibility tests.
- Switched future releases to three-part SemVer numbering.
- Added ARMv7 image freshness checks.
- Extended GitHub Actions testing.


## 0.0.9

- Renamed the project to Selyra OS.
- Added centralized version management.
- Added RAM, CPU and system information commands.
- Added authenticated power management IPC.
- Added controlled shutdown and kernel power transitions.
- Extended QEMU, ARMv7 and security tests.
- Documented Selyra design principles.


## 0.0.8

- Added local logging, boot diagnostics and service configuration.
- Added service status, exit tracking and health checks.
- Verified recovery and IPC behavior in QEMU.
- Updated ARMv7 builds and offline Galaxy S3 image checks.

## v0.0.7 - ARMv7 support

- Added ARMv7 cross-compilation.
- Tested IPC commands under ARM emulation.
- Booted Mobile OS in ARM QEMU.
- Added automated ARM boot checks.
- Added Galaxy S3 GT-I9300 image preparation scripts.
- Added offline boot image validation and packaging.
- Physical hardware boot remains untested.


## v0.0.6

- Added PID namespace isolation for statusd.
- Improved service restart and shutdown handling.
- Added cleanup for remaining child processes.
- Added statusctl ping, uptime and stop.
- Restricted service shutdown to root.
- Added automated QEMU tests for booting, crashes
  and service shutdown.
- Added make test-all.


## v0.0.5.5 - Process information isolation

### Added
- Restricted /proc mount using hidepid=2.
- Process visibility test.

### Tested
- Own process information accessible.
- Root process information hidden.
- Existing IPC functionality and QEMU boot.

### Limitations
- No separate PID namespace yet.


## v0.0.5.4 - Mount namespace isolation

### Added
- Separate mount namespace for statusd.
- Private 4 MB tmpfs at /tmp.
- Mount isolation test.

### Tested
- Private tmpfs creation.
- Original /tmp marker hidden.
- Write access to private /tmp.
- Normal QEMU boot.

### Limitations
- Other filesystem paths are not isolated.


## v0.0.5.3 - Process hardening

### Added
- no_new_privs for statusd.
- Runtime verification of the security flag.

### Tested
- Existing IPC tests.
- QEMU boot.
- no_new_privs inheritance after exec.

### Limitations
- Privilege escalation prevention not independently tested.
- No seccomp or namespace isolation yet.


## v0.0.5.2 - Private service directories

### Added
- Private directories for system and service data.
- Directory ownership and permission setup.
- Filesystem access test.

### Tested
- Service UID and GID.
- Allowed write access to service storage.
- Denied write access to system storage.
- Normal boot and IPC tests.

### Limitations
- No filesystem sandbox.
- No persistent storage.


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
