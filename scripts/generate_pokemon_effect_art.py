#!/usr/bin/env python3
"""Builds the battle move-effect animations (pokemon/effects/<slug>/NN.bmp).

The frames come from the original Pokémon Red/Blue move animations, as
disassembled by pret/pokered: this script replays the game's own
subanimations (data/battle_anims/subanimations.asm), placing the 8x8 tiles of
gfx/battle/move_anim_{0,1}.png where its frame blocks and base coordinates say,
frame by frame, the way engine/battle/animations.asm does on the player's turn.

Each frame is a window of the Game Boy screen centred on the area the whole
animation covers (the target, or the user for a self-buff), scaled up 2x to a
one-bit 144x108 BMP: gray pixels
become fixed 2x2 patterns, and white stays white, which the firmware leaves
transparent (GfxRenderer only paints black pixels).

    python3 scripts/generate_pokemon_effect_art.py \
      --source pokemon-art-source/pokered --output pokemon-art-output

`--source` is a pret/pokered checkout (or a folder with the same relative
paths for the files listed in SOURCE_FILES).
"""

from __future__ import annotations

import argparse
import re
from dataclasses import dataclass
from pathlib import Path

from PIL import Image

SOURCE_FILES = (
    "gfx/battle/move_anim_0.png",
    "gfx/battle/move_anim_1.png",
    "data/battle_anims/frame_blocks.asm",
    "data/battle_anims/base_coords.asm",
    "data/battle_anims/subanimations.asm",
    "constants/move_animation_constants.asm",
)

TILE = 8
SCALE = 2
# Game Boy pixels, 4:3 like the battle sprites - a little more than a 7x7-tile
# Pokemon picture, so the effects around it fit too.
WINDOW_SIZE = (72, 54)
FRAME_SIZE = (WINDOW_SIZE[0] * SCALE, WINDOW_SIZE[1] * SCALE)
OAM_X_OFFSET = 8
OAM_Y_OFFSET = 16
XFLIP = 0x20
YFLIP = 0x40


@dataclass(frozen=True)
class Effect:
    slug: str
    # (subanimation label, tileset) played in order - the tileset the real
    # move loads (the 0/1 in a label name is only the usual one).
    subanims: tuple[tuple[str, int], ...]
    frames: int  # frames kept, spread evenly over the whole sequence
    # Only this slice of the visible frames is used (default: all of them).
    first: int = 0
    stop: int | None = None


# Move-effect slugs, matching MoveEffect in src/components/pokemon/PokemonMoveEffect.h
# (and EFFECT_FRAME_COUNTS there). Each borrows the animation of a real Gen 1
# move of that type, named in the comment.
EFFECTS = (
    Effect("hit", (("Subanim_0StarThrice", 0),), 3),  # Gust's hits, Mega Kick
    Effect("flame", (("Subanim_1Flames", 1),), 5),  # Ember
    Effect("water", (("Subanim_0WaterColumns", 0),), 5),  # Hydro Pump
    Effect("ice", (("Subanim_0IceRise", 0),), 4),  # Ice Beam
    Effect("bolt", (("Subanim_1Lightning", 1),), 5),  # Thunder
    Effect("leaf", (("Subanim_1SeedLand", 1),), 3),  # Leech Seed
    Effect("poison", (("Subanim_1BlobDripEnemy", 1),), 4),  # Sludge, Acid
    Effect("rock", (("Subanim_0RocksFallEnemy", 0),), 5),  # Rock Throw
    Effect("wind", (("Subanim_1Tornado", 1),), 5),  # Gust
    Effect("psychic", (("Subanim_0CirclesCentering", 0),), 3, stop=3),  # Psywave's circles closing in
    Effect("ghost", (("Subanim_1CircleBlackToss", 1),), 5),  # a dark ball thrown at the target
    Effect("dragon", (("Subanim_1FlameColumn1", 1), ("Subanim_1FlameColumn2", 1), ("Subanim_1FlameColumn3", 1)), 5),  # Dragon Rage
    Effect("sparkle", (("Subanim_1SwordsCircling", 1),), 4),  # Swords Dance
)

FRAME_COUNTS = {effect.slug: effect.frames for effect in EFFECTS}


def _number(token: str) -> int:
    token = token.strip()
    return int(token[1:], 16) if token.startswith("$") else int(token, 0)


def parse_frame_blocks(text: str) -> list[list[tuple[int, int, int, int]]]:
    """Frame blocks in FrameBlockPointers order -> [(y, x, tile, flags), ...]
    relative to the base coordinate."""
    by_label: dict[str, list[tuple[int, int, int, int]]] = {}
    order: list[str] = []
    current: list[tuple[int, int, int, int]] | None = None
    for line in text.splitlines():
        line = line.split(";")[0].strip()
        pointer = re.fullmatch(r"dw\s+(FrameBlock\w+)", line)
        if pointer:
            order.append(pointer.group(1))
            continue
        label = re.fullmatch(r"(FrameBlock\w+):", line)
        if label:
            current = by_label.setdefault(label.group(1), [])
            continue
        sprite = re.fullmatch(r"dbsprite\s+(.+)", line)
        if sprite and current is not None:
            fields = [field.strip() for field in sprite.group(1).split(",")]
            x_tile, y_tile, x_pixel, y_pixel, tile = (_number(value) for value in fields[:5])
            flags = 0
            if "OAM_XFLIP" in fields[5]:
                flags |= XFLIP
            if "OAM_YFLIP" in fields[5]:
                flags |= YFLIP
            current.append(((y_tile * TILE + y_pixel) % 256, (x_tile * TILE + x_pixel) % 256, tile, flags))
    return [by_label[label] for label in order]


def parse_constants(text: str, prefix: str) -> dict[str, int]:
    """`const <prefix>NAME` lines -> their value (counting from 0 within the list)."""
    names = re.findall(rf"const\s+({prefix}\w+)", text)
    return {name: index for index, name in enumerate(names)}


def parse_base_coords(text: str) -> list[tuple[int, int]]:
    """BASECOORD_NN -> (y, x) in OAM coordinates."""
    return [(_number(y), _number(x)) for y, x in re.findall(r"db\s+(\$?\w+),\s*(\$?\w+)\s*;\s*BASECOORD_", text)]


def parse_subanimations(
    text: str, blocks: dict[str, int], coords: dict[str, int]
) -> tuple[dict[str, list[tuple[int, int, int]]], dict[str, str]]:
    """Subanim label -> [(frame block, base coordinate, mode), ...], and its SUBANIMTYPE."""
    subanims: dict[str, list[tuple[int, int, int]]] = {}
    types: dict[str, str] = {}
    name = ""
    current: list[tuple[int, int, int]] | None = None
    for line in text.splitlines():
        line = line.split(";")[0].strip()
        label = re.fullmatch(r"(Subanim_\w+):", line)
        if label:
            current = subanims.setdefault(label.group(1), [])
            name = label.group(1)
            continue
        header = re.fullmatch(r"subanim\s+SUBANIMTYPE_(\w+),.*", line)
        if header and current is not None:
            types[name] = header.group(1)
            continue
        entry = re.fullmatch(r"db\s+(FRAMEBLOCK_\w+),\s*(BASECOORD_\w+),\s*FRAMEBLOCKMODE_(\w+)", line)
        if entry and current is not None:
            current.append((blocks[entry.group(1)], coords[entry.group(2)], int(entry.group(3), 16)))
    return subanims, types


@dataclass
class Sources:
    tilesets: tuple[Image.Image, Image.Image]
    frame_blocks: list[list[tuple[int, int, int, int]]]
    base_coords: list[tuple[int, int]]
    subanims: dict[str, list[tuple[int, int, int]]]
    subanim_types: dict[str, str]


def load_sources(root: Path) -> Sources:
    missing = [name for name in SOURCE_FILES if not (root / name).is_file()]
    if missing:
        raise ValueError(f"missing pret/pokered file(s): {', '.join(missing)}")
    constants = (root / SOURCE_FILES[5]).read_text(encoding="utf-8")
    base_coords_text = (root / SOURCE_FILES[3]).read_text(encoding="utf-8")
    subanims, subanim_types = parse_subanimations(
        (root / SOURCE_FILES[4]).read_text(encoding="utf-8"),
        parse_constants(constants, "FRAMEBLOCK_"),
        {name: index for index, name in enumerate(re.findall(r";\s*(BASECOORD_\w+)", base_coords_text))},
    )
    return Sources(
        tilesets=(Image.open(root / SOURCE_FILES[0]).convert("L"), Image.open(root / SOURCE_FILES[1]).convert("L")),
        frame_blocks=parse_frame_blocks((root / SOURCE_FILES[2]).read_text(encoding="utf-8")),
        base_coords=parse_base_coords(base_coords_text),
        subanims=subanims,
        subanim_types=subanim_types,
    )


Sprite = tuple[int, int, int, int]  # OAM y, OAM x, tile, flags


def play(sources: Sources, label: str) -> list[list[Sprite]]:
    """The shadow-OAM contents each time the subanimation waits a frame.

    FRAMEBLOCKMODE_00/01 draw, show and clear; _02 draws without showing
    (the next block adds to it); _03 shows and keeps; _04 shows, keeps, and
    lets the next block draw over the same sprites.
    """
    if label not in sources.subanims:
        raise ValueError(f"unknown subanimation {label}")
    # On the player's turn every type plays untransformed except
    # SUBANIMTYPE_ENEMY (GetSubanimationTransform1/2).
    enemy_type = sources.subanim_types.get(label) == "ENEMY"
    shown: list[list[Sprite]] = []
    oam: list[Sprite] = []
    for block, coord, mode in sources.subanims[label]:
        base_y, base_x = sources.base_coords[coord]
        if enemy_type:
            # SUBANIMTYPE_ENEMY on the player's turn: SUBANIMTYPE_HFLIP.
            drawn = [((base_y + y + 40) % 256, (168 - base_x - x) % 256, tile, flags ^ XFLIP)
                     for y, x, tile, flags in sources.frame_blocks[block]]
        else:
            drawn = [((base_y + y) % 256, (base_x + x) % 256, tile, flags)
                     for y, x, tile, flags in sources.frame_blocks[block]]
        start = len(oam)
        oam.extend(drawn)
        if mode == 2:
            continue
        shown.append(list(oam))
        if mode == 3:
            continue
        if mode == 4:
            del oam[start:]
            continue
        oam = []
    return shown


def render_window(sources: Sources, tileset: int, sprites: list[Sprite], centre: tuple[int, int]) -> Image.Image:
    """The Game Boy pixels of a WINDOW_SIZE window around `centre` (white = empty)."""
    left = centre[0] - WINDOW_SIZE[0] // 2
    top = centre[1] - WINDOW_SIZE[1] // 2
    window = Image.new("L", WINDOW_SIZE, 255)
    sheet = sources.tilesets[tileset]
    columns = sheet.width // TILE
    for oam_y, oam_x, tile, flags in sprites:
        tile_x, tile_y = (tile % columns) * TILE, (tile // columns) * TILE
        if tile_y + TILE > sheet.height:
            continue
        image = sheet.crop((tile_x, tile_y, tile_x + TILE, tile_y + TILE))
        if flags & XFLIP:
            image = image.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
        if flags & YFLIP:
            image = image.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
        x = oam_x - OAM_X_OFFSET - left
        y = oam_y - OAM_Y_OFFSET - top
        # Colour 0 (white) of an object is transparent.
        window.paste(image, (x, y), image.point(lambda value: 0 if value >= 250 else 255).convert("1"))
    return window


# Each Game Boy pixel becomes a 2x2 block: black fully, dark gray 3 of 4,
# light gray 2 of 4 (a diagonal), white none.
PATTERNS = {0: (1, 1, 1, 1), 1: (1, 1, 1, 0), 2: (1, 0, 0, 1), 3: (0, 0, 0, 0)}


def to_one_bit(window: Image.Image) -> Image.Image:
    frame = Image.new("1", FRAME_SIZE, 1)
    pixels = window.load()
    out = frame.load()
    for y in range(window.height):
        for x in range(window.width):
            shade = min(3, round(pixels[x, y] / 85))
            for index, ink in enumerate(PATTERNS[shade]):
                if ink:
                    out[x * SCALE + index % 2, y * SCALE + index // 2] = 0
    return frame


def pick(frames: list[Image.Image], count: int) -> list[Image.Image]:
    """`count` frames spread evenly from first to last (all of them if fewer)."""
    if len(frames) <= count:
        return frames
    return [frames[round(index * (len(frames) - 1) / (count - 1))] for index in range(count)]


def build_effect_frames(sources: Sources, effect: Effect) -> list[Image.Image]:
    played = [(tileset, sprites) for label, tileset in effect.subanims for sprites in play(sources, label)]
    xs = [x - OAM_X_OFFSET for _, sprites in played for _, x, _, _ in sprites]
    ys = [y - OAM_Y_OFFSET for _, sprites in played for y, _, _, _ in sprites]
    if not xs:
        raise ValueError(f"{effect.slug}: the animation draws nothing")
    centre = ((min(xs) + max(xs) + TILE) // 2, (min(ys) + max(ys) + TILE) // 2)
    frames = []
    for tileset, sprites in played:
        window = render_window(sources, tileset, sprites, centre)
        if window.getextrema()[0] < 255:  # skip frames with nothing inside the window
            frames.append(to_one_bit(window))
    chosen = pick(frames[effect.first:effect.stop], effect.frames)
    if len(chosen) != effect.frames:
        raise ValueError(f"{effect.slug}: only {len(chosen)} visible frame(s), expected {effect.frames}")
    return chosen


def build_effects(source: Path, output: Path) -> None:
    sources = load_sources(source)
    for effect in EFFECTS:
        folder = output / "effects" / effect.slug
        folder.mkdir(parents=True, exist_ok=True)
        for stale in folder.glob("*.bmp"):
            stale.unlink()
        for index, frame in enumerate(build_effect_frames(sources, effect), start=1):
            frame.save(folder / f"{index:02}.bmp", format="BMP")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--source", type=Path, required=True, help="pret/pokered checkout (see SOURCE_FILES)")
    parser.add_argument("--output", type=Path, required=True, help="Local Pokémon art-pack root")
    args = parser.parse_args()
    build_effects(args.source.resolve(), args.output.resolve())
    print(f"Generated {len(EFFECTS)} move-effect animations in {args.output / 'effects'}")


if __name__ == "__main__":
    main()
