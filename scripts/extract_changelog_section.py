#!/usr/bin/env python3
"""Extract one version's section from a Keep a Changelog-style CHANGELOG.md.

Used by the release workflow to turn the changelog entry the maintainer
already wrote by hand into the GitHub Release notes body, instead of
re-deriving anything from commit history.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


HEADING_PATTERN = re.compile(r"^##\s+\[(?P<version>[^\]]+)\](?P<rest>.*)$")


def extract_section(changelog_text: str, version: str) -> str:
    lines = changelog_text.splitlines()
    start = None
    end = len(lines)
    seen_versions = []

    for index, line in enumerate(lines):
        match = HEADING_PATTERN.match(line)
        if match is None:
            continue
        seen_versions.append(match.group("version"))
        if match.group("version") == version:
            start = index + 1
            continue
        if start is not None:
            end = index
            break

    if start is None:
        available = ", ".join(seen_versions) or "(none found)"
        raise ValueError(f"no CHANGELOG.md section for version {version!r} - available: {available}")

    section_lines = lines[start:end]
    while section_lines and section_lines[0].strip() == "":
        section_lines.pop(0)
    while section_lines and section_lines[-1].strip() == "":
        section_lines.pop()
    return "\n".join(section_lines) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--changelog", type=Path, default=Path("CHANGELOG.md"))
    parser.add_argument("--version", required=True, help="Version to extract, without a leading 'v' (e.g. 0.3.0)")
    args = parser.parse_args()

    changelog_text = args.changelog.read_text(encoding="utf-8")
    try:
        section = extract_section(changelog_text, args.version)
    except ValueError as error:
        print(error, file=sys.stderr)
        raise SystemExit(1)

    sys.stdout.write(section)


if __name__ == "__main__":
    main()
