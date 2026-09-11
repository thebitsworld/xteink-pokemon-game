import importlib.util
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = REPO_ROOT / "scripts" / "extract_changelog_section.py"

SAMPLE_CHANGELOG = """\
## [Unreleased]

## [0.3.0] - 2026-09-10

### Added

- X4 Pro support.

### Fixed

- Row clipping.

## [0.2.0] - 2026-09-09

### Added

- Battle system.
"""


def load_module():
    spec = importlib.util.spec_from_file_location("extract_changelog_section", MODULE_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not load {MODULE_PATH}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ExtractChangelogSectionTest(unittest.TestCase):
    def test_extracts_the_requested_version_only(self) -> None:
        module = load_module()
        section = module.extract_section(SAMPLE_CHANGELOG, "0.3.0")
        self.assertEqual(
            section,
            "### Added\n\n- X4 Pro support.\n\n### Fixed\n\n- Row clipping.\n",
        )

    def test_extracts_the_oldest_version_up_to_end_of_file(self) -> None:
        module = load_module()
        section = module.extract_section(SAMPLE_CHANGELOG, "0.2.0")
        self.assertEqual(section, "### Added\n\n- Battle system.\n")

    def test_raises_with_available_versions_when_missing(self) -> None:
        module = load_module()
        with self.assertRaisesRegex(ValueError, "0\\.3\\.0, 0\\.2\\.0"):
            module.extract_section(SAMPLE_CHANGELOG, "9.9.9")

    def test_unreleased_section_with_no_body_is_empty(self) -> None:
        module = load_module()
        section = module.extract_section(SAMPLE_CHANGELOG, "Unreleased")
        self.assertEqual(section, "\n")


if __name__ == "__main__":
    unittest.main()
