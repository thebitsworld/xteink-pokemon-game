#!/usr/bin/env python3
"""Draws the battle move-effect icons (pokemon/effects/<slug>.bmp).

Each icon is original artwork drawn here with simple shapes - no source files,
no network - as a one-bit 96x72 BMP (the 4:3 shape of the battle sprites).
White is transparent on screen (GfxRenderer only paints black pixels), so an
icon is drawn straight over the battlefield.

Slugs match MoveEffect in src/components/pokemon/PokemonMoveEffect.h.
"""

from __future__ import annotations

import argparse
import math
from pathlib import Path

from PIL import Image, ImageDraw

SIZE = (96, 72)
BLACK = 0
WHITE = 1

EFFECT_SLUGS = (
    "hit", "flame", "water", "ice", "bolt", "leaf", "poison",
    "rock", "wind", "psychic", "ghost", "dragon", "sparkle",
)


def star(draw: ImageDraw.ImageDraw, cx: float, cy: float, outer: float, inner: float, points: int) -> None:
    corners = []
    for index in range(points * 2):
        radius = outer if index % 2 == 0 else inner
        angle = math.pi * index / points - math.pi / 2
        corners.append((cx + radius * math.cos(angle), cy + radius * math.sin(angle)))
    draw.polygon(corners, fill=BLACK)


def draw_hit(draw: ImageDraw.ImageDraw) -> None:
    star(draw, 48, 36, 34, 14, 8)
    star(draw, 48, 36, 18, 8, 8)


def draw_flame(draw: ImageDraw.ImageDraw) -> None:
    for cx, height, width in ((30, 46, 22), (48, 62, 30), (68, 40, 20)):
        base = 66
        draw.polygon(
            [(cx - width / 2, base), (cx - width / 2 + 2, base - height * 0.45), (cx - 3, base - height * 0.7),
             (cx, base - height), (cx + 4, base - height * 0.65), (cx + width / 2 - 2, base - height * 0.4),
             (cx + width / 2, base)],
            fill=BLACK,
        )


def drop(draw: ImageDraw.ImageDraw, cx: float, cy: float, radius: float) -> None:
    draw.ellipse((cx - radius, cy - radius, cx + radius, cy + radius), fill=BLACK)
    draw.polygon([(cx - radius * 0.9, cy - radius * 0.4), (cx, cy - radius * 2.6), (cx + radius * 0.9, cy - radius * 0.4)],
                 fill=BLACK)
    draw.ellipse((cx - radius * 0.5, cy - radius * 0.5, cx - radius * 0.1, cy - radius * 0.1), fill=WHITE)


def draw_water(draw: ImageDraw.ImageDraw) -> None:
    drop(draw, 28, 52, 10)
    drop(draw, 52, 40, 13)
    drop(draw, 74, 56, 8)


def draw_ice(draw: ImageDraw.ImageDraw) -> None:
    cx, cy = 48, 36
    for index in range(3):
        angle = math.pi * index / 3
        dx, dy = 32 * math.cos(angle), 32 * math.sin(angle)
        draw.line((cx - dx, cy - dy, cx + dx, cy + dy), fill=BLACK, width=5)
        for sign in (-1, 1):
            tx, ty = cx + sign * dx * 0.65, cy + sign * dy * 0.65
            for branch in (-0.6, 0.6):
                bx = tx + sign * 10 * math.cos(angle + branch * 2)
                by = ty + sign * 10 * math.sin(angle + branch * 2)
                draw.line((tx, ty, bx, by), fill=BLACK, width=3)


def draw_bolt(draw: ImageDraw.ImageDraw) -> None:
    draw.polygon([(56, 2), (26, 40), (46, 40), (34, 70), (72, 26), (51, 26), (64, 2)], fill=BLACK)


def draw_leaf(draw: ImageDraw.ImageDraw) -> None:
    for cx, cy, angle in ((32, 30, -30), (62, 44, 25)):
        leaf = Image.new("1", (48, 24), WHITE)
        leaf_draw = ImageDraw.Draw(leaf)
        leaf_draw.ellipse((0, 2, 47, 21), fill=BLACK)
        leaf_draw.line((4, 12, 44, 12), fill=WHITE, width=2)
        rotated = leaf.rotate(angle, expand=True, fillcolor=WHITE)
        draw._image.paste(rotated, (int(cx - rotated.width / 2), int(cy - rotated.height / 2)),
                          rotated.point(lambda value: 255 if value == 0 else 0).convert("1"))


def bubble(draw: ImageDraw.ImageDraw, cx: float, cy: float, radius: float) -> None:
    draw.ellipse((cx - radius, cy - radius, cx + radius, cy + radius), outline=BLACK, width=4)
    draw.ellipse((cx - radius * 0.5, cy - radius * 0.6, cx - radius * 0.15, cy - radius * 0.25), fill=BLACK)


def draw_poison(draw: ImageDraw.ImageDraw) -> None:
    bubble(draw, 32, 44, 16)
    bubble(draw, 62, 28, 12)
    bubble(draw, 70, 58, 8)
    bubble(draw, 18, 16, 6)


def draw_rock(draw: ImageDraw.ImageDraw) -> None:
    for points in (
        [(10, 66), (16, 46), (32, 40), (42, 54), (38, 68)],
        [(44, 68), (48, 34), (66, 26), (82, 40), (84, 68)],
        [(30, 24), (38, 10), (52, 12), (50, 26)],
    ):
        draw.polygon(points, fill=BLACK)
    draw.line((56, 40, 70, 52), fill=WHITE, width=2)
    draw.line((22, 52, 30, 60), fill=WHITE, width=2)


def draw_wind(draw: ImageDraw.ImageDraw) -> None:
    for y, length in ((20, 64), (36, 76), (52, 56)):
        draw.line((8, y, 8 + length, y), fill=BLACK, width=5)
        draw.arc((8 + length - 12, y - 12, 8 + length + 4, y + 2), start=270, end=110, fill=BLACK, width=5)


def draw_psychic(draw: ImageDraw.ImageDraw) -> None:
    for radius in (32, 22, 12):
        draw.ellipse((48 - radius * 1.3, 36 - radius, 48 + radius * 1.3, 36 + radius), outline=BLACK, width=4)
    draw.ellipse((44, 32, 52, 40), fill=BLACK)


def draw_ghost(draw: ImageDraw.ImageDraw) -> None:
    draw.pieslice((22, 6, 74, 58), start=180, end=360, fill=BLACK)
    draw.rectangle((22, 32, 74, 58), fill=BLACK)
    for index in range(4):
        x = 22 + index * 13
        draw.polygon([(x, 58), (x + 6.5, 68), (x + 13, 58)], fill=BLACK)
    draw.ellipse((34, 22, 44, 34), fill=WHITE)
    draw.ellipse((52, 22, 62, 34), fill=WHITE)


def draw_dragon(draw: ImageDraw.ImageDraw) -> None:
    for offset in (0, 16, 32):
        draw.polygon([(20 + offset, 6), (34 + offset, 6), (44 + offset, 66), (36 + offset, 66)], fill=BLACK)


def draw_sparkle(draw: ImageDraw.ImageDraw) -> None:
    for cx, cy, size in ((14, 14, 12), (82, 18, 10), (10, 58, 8), (86, 60, 12), (48, 6, 7)):
        star(draw, cx, cy, size, size / 4, 4)


DRAWERS = {
    "hit": draw_hit, "flame": draw_flame, "water": draw_water, "ice": draw_ice, "bolt": draw_bolt,
    "leaf": draw_leaf, "poison": draw_poison, "rock": draw_rock, "wind": draw_wind,
    "psychic": draw_psychic, "ghost": draw_ghost, "dragon": draw_dragon, "sparkle": draw_sparkle,
}


def render_effect(slug: str) -> Image.Image:
    image = Image.new("1", SIZE, WHITE)
    DRAWERS[slug](ImageDraw.Draw(image))
    return image


def build_effects(output: Path) -> None:
    folder = output / "effects"
    folder.mkdir(parents=True, exist_ok=True)
    for slug in EFFECT_SLUGS:
        render_effect(slug).save(folder / f"{slug}.bmp", format="BMP")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--output", type=Path, required=True, help="Local Pokémon art-pack root")
    args = parser.parse_args()
    build_effects(args.output.resolve())
    print(f"Generated {len(EFFECT_SLUGS)} move-effect icons in {args.output / 'effects'}")


if __name__ == "__main__":
    main()
