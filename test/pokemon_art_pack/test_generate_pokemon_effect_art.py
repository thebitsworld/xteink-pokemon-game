#!/usr/bin/env python3

import importlib.util
import re
import sys
import tempfile
import unittest
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]


def load(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


GENERATOR = load("generate_pokemon_effect_art", ROOT / "scripts/generate_pokemon_effect_art.py")
PACKAGE = load("package_pokemon_v2_release", ROOT / "scripts/package_pokemon_v2_release.py")

FRAME_BLOCKS = """FrameBlockPointers:
\tdw FrameBlock00
\tdw FrameBlock01
\tdw FrameBlock01

FrameBlock01:
\tdb 2 ; #
\tdbsprite  0,  0,  0,  0, $00, 0
\tdbsprite  1,  0,  0,  0, $00, OAM_XFLIP

FrameBlock00:
\tdb 0 ; #
"""

BASE_COORDS = """FrameBlockBaseCoords:
\tdb $20, $70 ; BASECOORD_00
\tdb $28, $78 ; BASECOORD_01
"""

SUBANIMATIONS = """Subanim_0Test:
\tsubanim SUBANIMTYPE_HFLIP, 4
\tdb FRAMEBLOCK_01, BASECOORD_00, FRAMEBLOCKMODE_02
\tdb FRAMEBLOCK_01, BASECOORD_01, FRAMEBLOCKMODE_00
\tdb FRAMEBLOCK_01, BASECOORD_00, FRAMEBLOCKMODE_03
\tdb FRAMEBLOCK_SPECIAL, BASECOORD_01, FRAMEBLOCKMODE_00

Subanim_0Enemy:
\tsubanim SUBANIMTYPE_ENEMY, 1
\tdb FRAMEBLOCK_01, BASECOORD_00, FRAMEBLOCKMODE_00
"""

CONSTANTS = """\tconst_def
\tconst FRAMEBLOCK_00
\tconst FRAMEBLOCK_01
\tconst FRAMEBLOCK_SPECIAL
"""


def write_sources(root: Path) -> None:
    (root / "gfx/battle").mkdir(parents=True)
    (root / "data/battle_anims").mkdir(parents=True)
    (root / "constants").mkdir(parents=True)
    tiles = Image.new("L", (128, 40), 255)
    for x in range(8):
        for y in range(8):
            tiles.putpixel((x, y), 0 if x < 4 else 85)
    tiles.save(root / "gfx/battle/move_anim_0.png")
    tiles.save(root / "gfx/battle/move_anim_1.png")
    (root / "data/battle_anims/frame_blocks.asm").write_text(FRAME_BLOCKS, encoding="utf-8")
    (root / "data/battle_anims/base_coords.asm").write_text(BASE_COORDS, encoding="utf-8")
    (root / "data/battle_anims/subanimations.asm").write_text(SUBANIMATIONS, encoding="utf-8")
    (root / "constants/move_animation_constants.asm").write_text(CONSTANTS, encoding="utf-8")


class GeneratePokemonEffectArtTest(unittest.TestCase):
    def test_parses_frame_blocks_coordinates_and_named_constants(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            write_sources(root)
            sources = GENERATOR.load_sources(root)
            self.assertEqual(sources.frame_blocks[1], [(0, 0, 0, 0), (0, 8, 0, GENERATOR.XFLIP)])
            self.assertEqual(sources.base_coords, [(0x20, 0x70), (0x28, 0x78)])
            # FRAMEBLOCK_SPECIAL is the third constant, so the third pointer.
            self.assertEqual(sources.subanims["Subanim_0Test"][3], (2, 1, 0))
            self.assertEqual(sources.subanim_types["Subanim_0Enemy"], "ENEMY")

    def test_frame_block_modes_decide_what_each_shown_frame_holds(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            write_sources(root)
            shown = GENERATOR.play(GENERATOR.load_sources(root), "Subanim_0Test")
            # _02 adds without showing, _00 shows both blocks then clears,
            # _03 shows and keeps, so the last block is shown on top of it.
            self.assertEqual([len(frame) for frame in shown], [4, 2, 4])

    def test_enemy_subanimations_are_flipped_on_the_players_turn(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            write_sources(root)
            (frame,) = GENERATOR.play(GENERATOR.load_sources(root), "Subanim_0Enemy")
            self.assertEqual(frame[0], (0x20 + 40, 168 - 0x70, 0, GENERATOR.XFLIP))

    def test_frames_are_one_bit_at_the_packaged_size(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            write_sources(root)
            effect = GENERATOR.Effect("test", (("Subanim_0Test", 0),), 2)
            frames = GENERATOR.build_effect_frames(GENERATOR.load_sources(root), effect)
            self.assertEqual(len(frames), 2)
            for frame in frames:
                self.assertEqual(frame.mode, "1")
                self.assertEqual(frame.size, (144, 108))
                self.assertIsNotNone(frame.point(lambda value: 255 - value).getbbox())

    def test_frame_counts_match_the_packager_and_the_firmware(self) -> None:
        self.assertEqual(GENERATOR.FRAME_COUNTS, PACKAGE.EFFECT_FRAMES)
        source = (ROOT / "src/components/pokemon/PokemonMoveEffect.cpp").read_text(encoding="utf-8")
        body = source[source.index("uint8_t moveEffectFrameCount"):]
        firmware: dict[str, int] = {}
        for cases, count in re.findall(r"((?:case MoveEffect::\w+:\s*)+)return (\d+);", body):
            for name in re.findall(r"MoveEffect::(\w+)", cases):
                firmware[name.lower()] = int(count)
        slugs = {"hit": "hit", "flame": "flame", "water": "water", "ice": "ice", "bolt": "bolt", "leaf": "leaf",
                 "poison": "poison", "rock": "rock", "wind": "wind", "psychic": "psychic", "ghost": "ghost",
                 "dragon": "dragon", "sparkle": "sparkle"}
        self.assertEqual({slugs[name]: count for name, count in firmware.items()}, GENERATOR.FRAME_COUNTS)


if __name__ == "__main__":
    unittest.main()
