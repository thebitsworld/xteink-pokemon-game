#!/usr/bin/env python3
"""Create a public firmware artifact and checksum entry.

Supports more than one device (e.g. x3, x4-pro) sharing a single output
directory: each call adds its own binary and merges its checksum line into
the same SHA256SUMS file, replacing any prior line for that exact binary
name rather than overwriting the whole file.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import shutil
from pathlib import Path


VERSION_PATTERN = re.compile(r"[0-9A-Za-z][0-9A-Za-z._-]*")
DEVICE_NAME_PATTERN = re.compile(r"[0-9A-Za-z][0-9A-Za-z-]*")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(65536), b""):
            digest.update(block)
    return digest.hexdigest()


def build_release(firmware: Path, version: str, output: Path, device_name: str = "x3-x4") -> tuple[Path, Path]:
    if not firmware.is_file() or firmware.stat().st_size == 0:
        raise ValueError("firmware is missing or empty")

    clean_version = version.removeprefix("v")
    if VERSION_PATTERN.fullmatch(clean_version) is None:
        raise ValueError("invalid release version")
    if DEVICE_NAME_PATTERN.fullmatch(device_name) is None:
        raise ValueError("invalid device name")

    output.mkdir(parents=True, exist_ok=True)
    binary = output / f"xteink-pokemon-{device_name}-v{clean_version}.bin"
    shutil.copyfile(firmware, binary)

    checksums = output / "SHA256SUMS"
    lines = []
    if checksums.is_file():
        lines = [
            line
            for line in checksums.read_text(encoding="ascii").splitlines()
            if line and not line.endswith(f"  {binary.name}")
        ]
    lines.append(f"{sha256(binary)}  {binary.name}")
    checksums.write_text("\n".join(lines) + "\n", encoding="ascii")
    return binary, checksums


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--firmware", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--name", default="x3-x4", help="Device label used in the artifact filename (default: x3-x4)")
    args = parser.parse_args()

    binary, checksums = build_release(args.firmware, args.version, args.output, args.name)
    print(binary)
    print(checksums)


if __name__ == "__main__":
    main()
