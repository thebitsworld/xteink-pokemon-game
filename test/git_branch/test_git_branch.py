import importlib.util
import os
import re
import unittest
from pathlib import Path
from unittest import mock


REPO_ROOT = Path(__file__).resolve().parents[2]
SCRIPT_PATH = REPO_ROOT / "scripts" / "git_branch.py"

spec = importlib.util.spec_from_file_location("crossink_git_branch", SCRIPT_PATH)
git_branch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(git_branch)


class GitBranchTest(unittest.TestCase):
    def test_branch_comes_from_repository_metadata(self):
        branch = git_branch.get_git_branch(str(REPO_ROOT))

        self.assertNotEqual(branch, "unknown")
        self.assertRegex(branch, re.compile(r"^[A-Za-z0-9._-]+$"))

    def test_short_hash_comes_from_repository_metadata(self):
        short_hash = git_branch.get_git_short_hash(str(REPO_ROOT))

        self.assertNotEqual(short_hash, "00000")
        self.assertRegex(short_hash, re.compile(r"^[0-9a-f]{5}$"))

    def test_pokemon_x3_release_receives_the_requested_version(self):
        class CaptureEnv(dict):
            def __init__(self):
                super().__init__(PIOENV="pokemon-x3", PROJECT_DIR=str(REPO_ROOT))
                self.defines = []

            def Append(self, **values):
                self.defines.extend(values.get("CPPDEFINES", []))

        environment = CaptureEnv()
        with mock.patch.dict(os.environ, {"CROSSINK_RELEASE_VERSION": "2.0.0"}, clear=False):
            git_branch.inject_version(environment)

        self.assertIn(("CROSSINK_VERSION", '\\"2.0.0\\"'), environment.defines)

    def test_pokemon_x4_pro_release_receives_the_requested_version(self):
        class CaptureEnv(dict):
            def __init__(self):
                super().__init__(PIOENV="pokemon-x4-pro", PROJECT_DIR=str(REPO_ROOT))
                self.defines = []

            def Append(self, **values):
                self.defines.extend(values.get("CPPDEFINES", []))

        environment = CaptureEnv()
        with mock.patch.dict(os.environ, {"CROSSINK_RELEASE_VERSION": "v2.0.0"}, clear=False):
            git_branch.inject_version(environment)

        # A leading "v" (as GitHub's github.ref_name provides for a v*.*.* tag)
        # must be stripped, matching the tag_name format the device's OTA
        # updater fetches from the GitHub Releases API and compares against.
        self.assertIn(("CROSSINK_VERSION", '\\"2.0.0\\"'), environment.defines)

    def test_pokemon_dev_build_falls_back_to_a_parseable_version_not_the_literal_dev_default(self):
        # Regression test: without a case for pokemon-x3/pokemon-x4-pro,
        # inject_version() previously appended no CROSSINK_VERSION define at
        # all for these two pioenv values, so AppVersion.h's "#ifndef
        # CROSSINK_VERSION -> dev" fallback shipped even in real release
        # builds. A bare "dev" fails OtaUpdater's parseVersion(), so the
        # device's Wi-Fi "Check for Update" could never detect a newer
        # release. Every code path for these two environments must always
        # define something that starts with a digit (optionally after a
        # leading "v"), never the word "dev" alone.
        class CaptureEnv(dict):
            def __init__(self, pioenv):
                super().__init__(PIOENV=pioenv, PROJECT_DIR=str(REPO_ROOT))
                self.defines = []

            def Append(self, **values):
                self.defines.extend(values.get("CPPDEFINES", []))

        for pioenv in ("pokemon-x3", "pokemon-x4-pro"):
            with self.subTest(pioenv=pioenv):
                environment = CaptureEnv(pioenv)
                with mock.patch.dict(os.environ, {}, clear=True):
                    git_branch.inject_version(environment)

                versions = [value for name, value in environment.defines if name == "CROSSINK_VERSION"]
                self.assertEqual(len(versions), 1)
                # Defines are wrapped as the C string literal \"...\" - strip
                # that before checking the actual version text.
                version_string = versions[0].strip('\\"')
                self.assertNotEqual(version_string, "dev")
                self.assertRegex(version_string, re.compile(r"^v?[0-9]"))


if __name__ == "__main__":
    unittest.main()
