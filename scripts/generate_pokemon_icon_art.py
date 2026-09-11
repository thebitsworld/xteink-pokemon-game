#!/usr/bin/env python3
"""Convert user-supplied Pokémon and item PNGs into the X3 SD artwork layout."""

from __future__ import annotations

import argparse
import importlib.util
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError as error:  # pragma: no cover - environment-specific message
    raise SystemExit("Pillow is required: python -m pip install Pillow") from error


SCRIPT_DIR = Path(__file__).resolve().parent

ITEMS = ("moon-stone", "fire-stone", "thunder-stone", "water-stone", "leaf-stone")

# The 8 Gym Leaders + 4 Elite Four members + the Champion, in the same
# challenge order as scripts/data/pokemon-gyms.csv - the slug pokemondb.net
# uses for each trainer's portrait file, e.g. https://pokemondb.net/red-blue/
# gymleaders-elitefour (img.pokemondb.net/sprites/trainers/red-blue/<slug>.png).
TRAINER_SLUGS = (
    "brock", "misty", "lt-surge", "erika", "koga", "sabrina", "blaine", "giovanni",
    "lorelei", "bruno", "agatha", "lance", "blue",
)

# Ids 7-83: every non-evolution bag item (Ball/Medicine/StatusCure/Candy/
# PPRestore/Machine) - see scripts/data/pokemon-items.csv. Evolution items
# (ids 1-6) keep using the separate --item-source/ITEMS pipeline above,
# sourced from PokéSprite rather than PokeAPI.
BAG_ITEM_ID_START = 7


def _load_module(name: str):
    spec = importlib.util.spec_from_file_location(name, SCRIPT_DIR / f"{name}.py")
    if spec is None or spec.loader is None:
        raise SystemExit(f"could not load {name}.py")
    module = importlib.util.module_from_spec(spec)
    # dataclasses needs the module registered in sys.modules before
    # exec_module() runs (it looks itself up by __module__ name).
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def bag_item_slug(item, moves_by_id: dict) -> str | None:
    """Map a bag item (id 7-83) to its PokeAPI sprites/items/<slug>.png name.

    TMs have no per-number icon in PokeAPI Sprites - only one icon per move
    type (matching the real games, where every TM of a given type shares an
    icon). HMs are numbered directly. Everything else maps to its own name.
    """
    if item.name.startswith("TM"):
        move = moves_by_id.get(item.teaches_move_id)
        if move is None:
            return None
        return f"tm-{move.type_.lower()}"
    if item.name.startswith("HM"):
        return item.name.lower()  # "HM01" -> "hm01"
    return item.name.lower().replace(" ", "-")


def species_source(root: Path, species_id: int) -> Path:
    for name in (f"{species_id}.png", f"{species_id:03}.png"):
        candidate = root / name
        if candidate.is_file():
            return candidate
    raise ValueError(f"missing species icon {species_id:03}")


def species_back_source(root: Path, species_id: int) -> Path:
    for name in (f"{species_id}.png", f"{species_id:03}.png"):
        candidate = root / name
        if candidate.is_file():
            return candidate
    raise ValueError(f"missing back sprite {species_id:03}")


def item_source(root: Path, item: str) -> Path:
    candidate = root / f"{item}.png"
    if not candidate.is_file():
        raise ValueError(f"missing item icon {item}")
    return candidate


def badge_source(root: Path, gym_index: int) -> Path:
    candidate = root / f"{gym_index}.png"
    if not candidate.is_file():
        raise ValueError(f"missing badge icon {gym_index}")
    return candidate


def trainer_source(root: Path, gym_index: int) -> Path:
    slug = TRAINER_SLUGS[gym_index - 1]
    candidate = root / f"{slug}.png"
    if not candidate.is_file():
        raise ValueError(f"missing trainer portrait {gym_index} ({slug}.png)")
    return candidate


def one_bit_canvas(source: Path, size: tuple[int, int], allow_upscale: bool = False) -> Image.Image:
    with Image.open(source) as opened:
        icon = opened.convert("RGBA")
    if icon.width > size[0] or icon.height > size[1] or allow_upscale:
        scale = min(size[0] / icon.width, size[1] / icon.height)
        scaled_size = (max(1, round(icon.width * scale)), max(1, round(icon.height * scale)))
        icon = icon.resize(scaled_size, Image.Resampling.NEAREST)
    canvas = Image.new("RGBA", size, (255, 255, 255, 255))
    canvas.alpha_composite(icon, ((size[0] - icon.width) // 2, (size[1] - icon.height) // 2))
    return canvas.convert("L").convert("1", dither=Image.Dither.FLOYDSTEINBERG)


def save_bmp(source: Path, destination: Path, size: tuple[int, int], allow_upscale: bool = False) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    one_bit_canvas(source, size, allow_upscale=allow_upscale).save(destination, format="BMP")


def save_pair(source: Path, small: Path, large: Path, small_size: tuple[int, int], scale: int) -> None:
    save_bmp(source, small, small_size)
    save_bmp(source, large, (small_size[0] * scale, small_size[1] * scale), allow_upscale=True)


def build_icons(
    pokemon_source: Path,
    item_source_root: Path,
    output: Path,
    species_count: int = 151,
    items: tuple[str, ...] = ITEMS,
) -> None:
    for species_id in range(1, species_count + 1):
        save_pair(
            species_source(pokemon_source, species_id),
            output / "sprites" / f"{species_id:03}.bmp",
            output / "heroes" / f"{species_id:03}.bmp",
            (40, 30),
            3,
        )
    for item in items:
        save_pair(
            item_source(item_source_root, item),
            output / "items" / f"{item}.bmp",
            output / "heroes/items" / f"{item}.bmp",
            (32, 32),
            2,
        )


def build_back_sprites(pokemon_back_source: Path, output: Path, species_count: int = 151) -> None:
    for species_id in range(1, species_count + 1):
        save_bmp(
            species_back_source(pokemon_back_source, species_id),
            output / "heroes/back" / f"{species_id:03}.bmp",
            (120, 90),
            allow_upscale=True,
        )


def build_bag_item_icons(bag_item_source: Path, output: Path, item_ids: "set[int] | None" = None) -> None:
    items_module = _load_module("generate_pokemon_items")
    moves_module = _load_module("generate_pokemon_moves")
    root = SCRIPT_DIR.parent
    all_items = items_module.load_items(root / "scripts/data/pokemon-items.csv")
    moves_by_id = {move.move_id: move for move in moves_module.load_moves(root / "scripts/data/pokemon-moves.csv")}
    for item in all_items:
        if item.item_id < BAG_ITEM_ID_START:
            continue  # evolution items (ids 1-6) use build_icons()'s ITEMS pipeline instead
        if item_ids is not None and item.item_id not in item_ids:
            continue
        slug = bag_item_slug(item, moves_by_id)
        if slug is None:
            raise ValueError(f"no PokeAPI icon mapping for item {item.item_id} ({item.name})")
        save_bmp(item_source(bag_item_source, slug), output / "items" / f"{item.item_id:03}.bmp", (32, 32))


def build_badges(badge_source_root: Path, output: Path, gym_count: int = 8) -> None:
    for gym_index in range(1, gym_count + 1):
        save_bmp(
            badge_source(badge_source_root, gym_index),
            output / "badges" / f"{gym_index:02}.bmp",
            (32, 32),
        )


def build_trainers(trainer_source_root: Path, output: Path, trainer_count: int = len(TRAINER_SLUGS)) -> None:
    for gym_index in range(1, trainer_count + 1):
        save_bmp(
            trainer_source(trainer_source_root, gym_index),
            output / "trainers" / f"{gym_index:02}.bmp",
            (32, 32),
        )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--pokemon-source",
        type=Path,
        required=True,
        help="Local folder containing 1.png through 151.png (zero padding is also accepted)",
    )
    parser.add_argument(
        "--item-source",
        type=Path,
        required=True,
        help="Local PokéSprite evo-item folder containing the five stone PNGs",
    )
    parser.add_argument("--output", type=Path, required=True, help="Local Pokémon art-pack root")
    parser.add_argument(
        "--pokemon-back-source",
        type=Path,
        help="Local folder containing PokeAPI Sprites' sprites/pokemon/back/ PNGs (1.png-151.png) - "
        "optional, skips battle back-sprite generation if omitted",
    )
    parser.add_argument(
        "--bag-item-source",
        type=Path,
        help="Local folder containing PokeAPI Sprites' sprites/items/ PNGs (poke-ball.png, potion.png, "
        "tm-<type>.png, hm0N.png, etc.) - optional, skips bag item icon generation if omitted",
    )
    parser.add_argument(
        "--badge-source",
        type=Path,
        help="Local folder containing PokeAPI Sprites' sprites/badges/ PNGs, numbered 1.png-8.png for "
        "the 8 Kanto gyms in order - optional, skips badge icon generation if omitted",
    )
    parser.add_argument(
        "--trainer-source",
        type=Path,
        help="Local folder containing the 13 Gym Battle trainer portraits (see TRAINER_SLUGS for the "
        "expected <slug>.png filenames, matching pokemondb.net's Red/Blue trainer page) - optional, "
        "skips trainer portrait generation if omitted",
    )
    args = parser.parse_args()
    output = args.output.resolve()
    build_icons(args.pokemon_source.resolve(), args.item_source.resolve(), output)
    if args.pokemon_back_source is not None:
        build_back_sprites(args.pokemon_back_source.resolve(), output)
    if args.bag_item_source is not None:
        build_bag_item_icons(args.bag_item_source.resolve(), output)
    if args.badge_source is not None:
        build_badges(args.badge_source.resolve(), output)
    if args.trainer_source is not None:
        build_trainers(args.trainer_source.resolve(), output)
    print(f"Generated species and item artwork in {output}")


if __name__ == "__main__":
    main()
