#!/usr/bin/env python3

import importlib.util
import tempfile
import unittest
from pathlib import Path

from PIL import Image

REPO_ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "generate_pokemon_icon_art", REPO_ROOT / "scripts/generate_pokemon_icon_art.py"
)
GENERATOR = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(GENERATOR)


def write_rgba_icon(path: Path, size: tuple[int, int]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    image = Image.new("RGBA", size, (255, 255, 255, 0))
    width, height = size
    image.putpixel((width // 2, height // 2), (0, 0, 0, 255))
    image.save(path)


def write_shaded_icon(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    image = Image.new("RGBA", (40, 30), (255, 255, 255, 0))
    for y in range(5, 25):
        for x in range(8, 32):
            shade = 72 + ((x + y) % 5) * 32
            image.putpixel((x, y), (shade, shade, shade, 255))
    image.save(path)


class PokemonIconArtGeneratorTest(unittest.TestCase):
    def test_builds_species_and_item_sizes_as_one_bit_bmps(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            pokemon_source = root / "pokemon"
            item_source = root / "items"
            output = root / "output"
            write_rgba_icon(pokemon_source / "1.png", (40, 30))
            write_rgba_icon(item_source / "fire-stone.png", (32, 32))

            GENERATOR.build_icons(
                pokemon_source,
                item_source,
                output,
                species_count=1,
                items=("fire-stone",),
            )

            expected = {
                output / "sprites/001.bmp": (40, 30),
                output / "heroes/001.bmp": (120, 90),
                output / "items/fire-stone.bmp": (32, 32),
                output / "heroes/items/fire-stone.bmp": (64, 64),
            }
            for path, size in expected.items():
                with Image.open(path) as image:
                    self.assertEqual(image.mode, "1")
                    self.assertEqual(image.size, size)

            with Image.open(output / "sprites/001.bmp") as sprite:
                self.assertEqual(sprite.getpixel((0, 0)), 255)
                self.assertEqual(sprite.getpixel((20, 15)), 0)

    def test_accepts_zero_padded_species_filenames(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            write_rgba_icon(root / "pokemon/001.png", (40, 30))
            write_rgba_icon(root / "items/fire-stone.png", (32, 32))

            GENERATOR.build_icons(
                root / "pokemon",
                root / "items",
                root / "output",
                species_count=1,
                items=("fire-stone",),
            )

            self.assertTrue((root / "output/sprites/001.bmp").is_file())

    def test_renders_hero_at_final_resolution_before_dithering(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source.png"
            write_shaded_icon(source)

            GENERATOR.save_pair(
                source,
                root / "small.bmp",
                root / "hero.bmp",
                (40, 30),
                3,
            )

            with Image.open(root / "small.bmp") as small, Image.open(root / "hero.bmp") as hero:
                enlarged_small = small.resize(hero.size, Image.Resampling.NEAREST)
                self.assertNotEqual(hero.tobytes(), enlarged_small.tobytes())

    def test_builds_back_sprites_at_hero_resolution_only(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            back_source = root / "back"
            write_rgba_icon(back_source / "1.png", (96, 96))

            GENERATOR.build_back_sprites(back_source, root / "output", species_count=1)

            with Image.open(root / "output/heroes/back/001.bmp") as image:
                self.assertEqual(image.mode, "1")
                self.assertEqual(image.size, (120, 90))
            self.assertFalse((root / "output/sprites/back").exists())

    def test_bag_item_slug_maps_tm_by_move_type_and_hm_by_number(self) -> None:
        items_module = GENERATOR._load_module("generate_pokemon_items")
        moves_module = GENERATOR._load_module("generate_pokemon_moves")
        repo_root = Path(GENERATOR.SCRIPT_DIR).parent
        all_items = {item.item_id: item for item in items_module.load_items(repo_root / "scripts/data/pokemon-items.csv")}
        moves_by_id = {
            move.move_id: move for move in moves_module.load_moves(repo_root / "scripts/data/pokemon-moves.csv")
        }

        poke_ball = all_items[7]
        self.assertEqual(GENERATOR.bag_item_slug(poke_ball, moves_by_id), "poke-ball")

        rare_candy = all_items[24]
        self.assertEqual(GENERATOR.bag_item_slug(rare_candy, moves_by_id), "rare-candy")

        tm01 = all_items[29]  # teaches Mega Punch, a Normal-type move
        self.assertEqual(GENERATOR.bag_item_slug(tm01, moves_by_id), "tm-normal")

        hm01 = all_items[79]
        self.assertEqual(GENERATOR.bag_item_slug(hm01, moves_by_id), "hm01")

    def test_builds_bag_item_icons_for_the_requested_ids_only(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            item_source = root / "items"
            write_rgba_icon(item_source / "poke-ball.png", (32, 32))
            write_rgba_icon(item_source / "tm-normal.png", (32, 32))
            write_rgba_icon(item_source / "hm01.png", (32, 32))

            GENERATOR.build_bag_item_icons(item_source, root / "output", item_ids={7, 29, 79})

            for item_id in (7, 29, 79):
                with Image.open(root / f"output/items/{item_id:03}.bmp") as image:
                    self.assertEqual(image.mode, "1")
                    self.assertEqual(image.size, (32, 32))
            self.assertFalse((root / "output/items/006.bmp").exists())  # Link Cable (id 6) is out of range

    def test_builds_eight_badge_icons(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            badge_source = root / "badges"
            for gym_index in range(1, 9):
                write_rgba_icon(badge_source / f"{gym_index}.png", (32, 32))

            GENERATOR.build_badges(badge_source, root / "output")

            for gym_index in range(1, 9):
                with Image.open(root / f"output/badges/{gym_index:02}.bmp") as image:
                    self.assertEqual(image.mode, "1")
                    self.assertEqual(image.size, (32, 32))

    def test_rejects_a_missing_species_icon(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "pokemon").mkdir()
            write_rgba_icon(root / "items/fire-stone.png", (32, 32))

            with self.assertRaisesRegex(ValueError, "missing species icon 001"):
                GENERATOR.build_icons(
                    root / "pokemon",
                    root / "items",
                    root / "output",
                    species_count=1,
                    items=("fire-stone",),
                )


if __name__ == "__main__":
    unittest.main()
