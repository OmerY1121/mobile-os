# Selyra OS — Design Principles

Selyra is an independent, Linux-based operating
system project. It is not an Android fork or ROM.

Its central principle is simple:

Your device. Your freedom.

## 1. User ownership

Users should control their own devices, software,
updates and system configuration.

Manufacturer restrictions should not unnecessarily
prevent legitimate system modification.

## 2. Version freedom

Selyra aims to support user-controlled upgrades
and downgrades.

Future Selyra hardware should not implement
manufacturer-enforced binary revision locks or
irreversible anti-rollback counters.

Software authenticity and integrity verification
should remain available.

Older releases may contain security vulnerabilities.
Downgrades must include clear warnings and account
for data compatibility and recovery.

This policy cannot override restrictions imposed
by third-party hardware.

## 3. No punitive modification flags

Future Selyra devices should not use permanent
warranty-void indicators as a punishment for
user-authorized system modification.

Actual warranty rights remain subject to applicable
law and the circumstances of a repair.

## 4. Privacy by design

No unnecessary telemetry, tracking or mandatory
personal data collection.

Users should have understandable control over
permissions and data sharing.

## 5. Minimal software

No unnecessary preinstalled applications,
advertising services or forced vendor software.

Core services should have clear purposes.

## 6. Google independence

Selyra must not require Google services or an
Android framework to operate.

Optional compatibility solutions may be explored
for users who want Google applications.

## 7. Security without unnecessary lock-in

Maintain privilege separation, process isolation,
authenticated IPC and software integrity checks.

Security mechanisms should protect users without
unnecessarily taking control away from them.

## Current implementation

Selyra currently provides experimental Linux
userspace components, including init, service
management, logging, IPC and system diagnostics.

Full mobile hardware support, application
compatibility, persistent user data management,
and user-controlled downgrade functionality
are future goals, not completed features.
