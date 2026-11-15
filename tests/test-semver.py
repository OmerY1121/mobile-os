#!/usr/bin/env python3

import runpy

is_valid = runpy.run_path(
    "scripts/check-version.py"
)["is_valid_semver"]

valid_versions = [
    "0.1.0",
    "0.0.10",
    "1.0.0",
    "0.1.0-selyra.alpha.1",
    "0.1.0-selyra.beta.1",
    "0.1.0-selyra.rc.1",
    "0.1.0+build.001",
    "0.1.0-selyra.rc.1+sha.abc123",
]

invalid_versions = [
    "0.0.9.1",
    "0.1",
    "v0.1.0",
    "01.2.3",
    "0.01.0",
    "0.1.00",
    "0.1.0-",
    "0.1.0+",
    "0.1.0-alpha..1",
    "0.1.0-alpha.01",
    "0.1.0-01",
    "0.1.0-ä",
]

for version in valid_versions:
    assert is_valid(version), (
        f"Valid version rejected: {version}"
    )

for version in invalid_versions:
    assert not is_valid(version), (
        f"Invalid version accepted: {version}"
    )

print("PASS: Valid SemVer versions accepted")
print("PASS: Invalid SemVer versions rejected")
print("PASS: Selyra SemVer tests completed")
