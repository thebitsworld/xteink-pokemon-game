import hashlib
import importlib.util
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = REPO_ROOT / "scripts" / "build_pokemon_release.py"


def load_release_module():
    spec = importlib.util.spec_from_file_location("build_pokemon_release", MODULE_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not load {MODULE_PATH}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class BuildPokemonReleaseTest(unittest.TestCase):
    def test_build_release_uses_public_filename_and_checksum(self) -> None:
        release = load_release_module()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            firmware = root / "firmware.bin"
            firmware.write_bytes(b"x3 firmware")

            binary, sums = release.build_release(firmware, "1.0.0", root / "dist")

            self.assertEqual(binary.name, "xteink-pokemon-x3-x4-v1.0.0.bin")
            digest = hashlib.sha256(b"x3 firmware").hexdigest()
            self.assertEqual(
                sums.read_text(encoding="ascii"),
                f"{digest}  xteink-pokemon-x3-x4-v1.0.0.bin\n",
            )
            self.assertEqual(binary.read_bytes(), b"x3 firmware")

    def test_build_release_merges_checksums_for_a_second_device(self) -> None:
        release = load_release_module()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            x3_x4_firmware = root / "x3-x4-firmware.bin"
            x3_x4_firmware.write_bytes(b"x3 firmware")
            x4_pro_firmware = root / "x4-pro-firmware.bin"
            x4_pro_firmware.write_bytes(b"x4 pro firmware")

            x3_x4_binary, sums = release.build_release(x3_x4_firmware, "1.0.0", root / "dist", "x3-x4")
            x4_pro_binary, sums_again = release.build_release(x4_pro_firmware, "1.0.0", root / "dist", "x4-pro")

            self.assertEqual(sums, sums_again)
            self.assertEqual(x3_x4_binary.name, "xteink-pokemon-x3-x4-v1.0.0.bin")
            self.assertEqual(x4_pro_binary.name, "xteink-pokemon-x4-pro-v1.0.0.bin")
            x3_x4_digest = hashlib.sha256(b"x3 firmware").hexdigest()
            x4_pro_digest = hashlib.sha256(b"x4 pro firmware").hexdigest()
            self.assertEqual(
                sums.read_text(encoding="ascii"),
                f"{x3_x4_digest}  xteink-pokemon-x3-x4-v1.0.0.bin\n"
                f"{x4_pro_digest}  xteink-pokemon-x4-pro-v1.0.0.bin\n",
            )

    def test_build_release_rejects_empty_firmware(self) -> None:
        release = load_release_module()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            firmware = root / "firmware.bin"
            firmware.write_bytes(b"")

            with self.assertRaisesRegex(ValueError, "empty"):
                release.build_release(firmware, "1.0.0", root / "dist")


if __name__ == "__main__":
    unittest.main()
