#!/usr/bin/env python3
"""Validate Selyra version numbers using SemVer 2.0.0."""

import argparse
import re
from pathlib import Path

VERSION_PATTERN = re.compile(
    r"(?P<major>0|[1-9][0-9]*)\."
    r"(?P<minor>0|[1-9][0-9]*)\."
    r"(?P<patch>0|[1-9][0-9]*)"
    r"(?:-(?P<prerelease>[0-9A-Za-z-]+"
    r"(?:\.[0-9A-Za-z-]+)*))?"
    r"(?:\+(?P<build>[0-9A-Za-z-]+"
    r"(?:\.[0-9A-Za-z-]+)*))?"
)


def is_valid_semver(version):
    match = VERSION_PATTERN.fullmatch(version)

    if not match:
        return False

    prerelease = match.group("prerelease")

    if prerelease:
        for part in prerelease.split("."):
            if (
                part.isascii()
                and part.isdigit()
                and len(part) > 1
                and part[0] == "0"
            ):
                return False

    return True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--tag",
        help="Check intended Git tag, e.g. v0.1.0"
    )
    args = parser.parse_args()

    version = Path("VERSION").read_text(
        encoding="utf-8"
    ).strip()

    if not is_valid_semver(version):
        parser.exit(
            1,
            f"FAIL: Invalid SemVer: {version!r}\n"
        )

    if args.tag and args.tag != "v" + version:
        parser.exit(
            1,
            f"FAIL: Git tag does not match VERSION\n"
        )

    print(f"PASS: SemVer {version}")

    if args.tag:
        print(f"PASS: Git tag {args.tag} matches VERSION")


if __name__ == "__main__":
    main()
