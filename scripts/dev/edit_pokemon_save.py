#!/usr/bin/env python3
"""Dev tool to inspect and hand-edit the Pokemon save files under a
simulator's SD-card sandbox (fs_/.crosspoint by default).

This is NOT a product feature - it exists so a developer/tester can quickly
set up scenarios (queue a wild encounter, wipe HP/PP/status back to full,
clear gym progress, hand a Pokemon some items) without waiting on real
gameplay/RNG. See docs/file-formats.md for the on-disk layout this assumes;
this tool only understands the CURRENT save format used by this branch
(main save version 4, 198-byte state) and refuses to touch anything else
rather than risk corrupting an unfamiliar layout.

Every write always patches BOTH pokemon-a.bin and pokemon-b.bin (when both
exist) to the same content, mirroring the firmware's own double-buffer
convention - so whichever one the next boot picks as "active" is correct
either way. A .bak copy of each file is kept before the first write in a
session unless --no-backup is given.

Examples:
    python3 scripts/dev/edit_pokemon_save.py dump
    python3 scripts/dev/edit_pokemon_save.py reset-battle-store
    python3 scripts/dev/edit_pokemon_save.py reset-gym-progress
    python3 scripts/dev/edit_pokemon_save.py queue-encounter --species pidgey --level 5
    python3 scripts/dev/edit_pokemon_save.py set-bag-item --item "poke-ball" --count 10
    python3 scripts/dev/edit_pokemon_save.py set-record-xp --record-id 1 --xp 5000
"""

from __future__ import annotations

import argparse
import csv
import struct
import sys
import zlib
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SAVE_DIR = ROOT / "fs_" / ".crosspoint"
KANTO_CSV = ROOT / "scripts" / "data" / "pokemon-kanto-v2.csv"
ITEMS_CSV = ROOT / "scripts" / "data" / "pokemon-items.csv"
GYMS_CSV = ROOT / "scripts" / "data" / "pokemon-gyms.csv"

MAGIC = b"PKV2"
HEADER_BYTES = 24
STATE_BYTES_V4 = 198
RECORD_BYTES = 48
PENDING_EVENT_BYTES = 10
PENDING_EVENT_COUNT = 3
EVOLUTION_ITEM_COUNT = 6
BAG_SLOT_COUNT = 77

# State-relative byte offsets (version 4 - see docs/file-formats.md). Bytes
# 0-194 are unchanged from version 3; 195-197 are new pity counters for the
# ball/medicine/TM-HM drop tracks (GĐ 23).
OFF_PARTY_IDS = 0  # 6 x u32
OFF_PENDING_EVENTS = 24  # 3 x 10 bytes
OFF_ITEM_COUNTS = 54  # 6 x u16 (evolution stones, ids 1-6)
OFF_SEEN_BITS = 66  # 19 bytes
OFF_CAUGHT_BITS = 85  # 19 bytes
OFF_LIFETIME_MINUTES = 104  # u32
OFF_SEQUENCE = 108  # u32, must equal header sequence
OFF_MINUTE_REMAINDER = 112  # u8
OFF_ENCOUNTER_MISSES = 113  # u8
OFF_ITEM_MISSES = 114  # u8
OFF_DASHBOARD_NOTICE = 115  # u8
OFF_BAG_COUNTS = 116  # 77 x u8 (ids 7-83)
OFF_BATTLE_PROGRESS = 193  # u16
OFF_BALL_MISSES = 195  # u8
OFF_MEDICINE_MISSES = 196  # u8
OFF_MACHINE_MISSES = 197  # u8

PENDING_KIND_NONE = 0
PENDING_KIND_ENCOUNTER = 1
PENDING_KIND_ITEM = 2
PENDING_KIND_EVOLUTION = 3
PENDING_KIND_MOVE_LEARN = 4
PENDING_KIND_NAMES = {
    PENDING_KIND_NONE: "None",
    PENDING_KIND_ENCOUNTER: "Encounter",
    PENDING_KIND_ITEM: "Item",
    PENDING_KIND_EVOLUTION: "Evolution",
    PENDING_KIND_MOVE_LEARN: "MoveLearn",
}

GENDER_UNKNOWN = 0
GENDER_MALE = 1
GENDER_FEMALE = 2
GENDER_GENDERLESS = 3
GENDER_NAMES = {GENDER_UNKNOWN: "-", GENDER_MALE: "Male", GENDER_FEMALE: "Female", GENDER_GENDERLESS: "Genderless"}

SAVE_NAMES = ("pokemon-a.bin", "pokemon-b.bin")
BATTLE_STORE_NAMES = ("pokemon-battle-a.bin", "pokemon-battle-b.bin", "pokemon-battle.bin")


class ToolError(RuntimeError):
    pass


# --------------------------------------------------------------------------
# Species / item data lookups (read straight from the CSVs the firmware
# itself generates from - never hardcode a name/id mapping here, since a
# typo would silently write bad data into a real save).
# --------------------------------------------------------------------------


@dataclass(frozen=True)
class Species:
    id: int
    name: str
    gender_rate: int  # 0-8 (eighths female), or 255 for genderless


def load_species() -> dict[int, Species]:
    species: dict[int, Species] = {}
    with KANTO_CSV.open(encoding="utf-8") as f:
        f.readline()  # provenance comment line
        for row in csv.DictReader(f):
            species_id = int(row["id"])
            species[species_id] = Species(species_id, row["name"], int(row["gender_rate"]))
    return species


@dataclass(frozen=True)
class Item:
    id: int
    name: str
    category: str


def load_items() -> dict[int, Item]:
    items: dict[int, Item] = {}
    with ITEMS_CSV.open(encoding="utf-8") as f:
        f.readline()
        for row in csv.DictReader(f):
            item_id = int(row["id"])
            items[item_id] = Item(item_id, row["name"], row["category"])
    return items


def find_species(species_map: dict[int, Species], query: str) -> Species:
    if query.isdigit():
        species_id = int(query)
        if species_id not in species_map:
            raise ToolError(f"no species with id {species_id}")
        return species_map[species_id]
    needle = query.strip().lower().replace(" ", "").replace("-", "")
    for species in species_map.values():
        if species.name.lower().replace(" ", "").replace("-", "") == needle:
            return species
    raise ToolError(f"no species named {query!r} (try a numeric id, or check spelling)")


def find_item(item_map: dict[int, Item], query: str) -> Item:
    if query.isdigit():
        item_id = int(query)
        if item_id not in item_map:
            raise ToolError(f"no item with id {item_id}")
        return item_map[item_id]
    needle = query.strip().lower().replace(" ", "").replace("-", "")
    for item in item_map.values():
        if item.name.lower().replace(" ", "").replace("-", "") == needle:
            return item
    raise ToolError(f"no item named {query!r} (try a numeric id, or check spelling)")


def default_gender_for(species: Species) -> int:
    """Picks a gender that genderMatchesSpecies() (PokemonTypes.cpp) accepts,
    mirroring its exact rule so a hand-picked species/gender pair is never
    rejected by validatePendingEvent() on the device."""
    if species.gender_rate == 255:
        return GENDER_GENDERLESS
    if species.gender_rate == 0:
        return GENDER_MALE
    if species.gender_rate == 8:
        return GENDER_FEMALE
    return GENDER_MALE  # either Male or Female is valid; Male is an arbitrary pick


def validate_gender(species: Species, gender: int) -> None:
    if species.gender_rate == 255:
        ok = gender == GENDER_GENDERLESS
    elif species.gender_rate == 0:
        ok = gender == GENDER_MALE
    elif species.gender_rate == 8:
        ok = gender == GENDER_FEMALE
    else:
        ok = gender in (GENDER_MALE, GENDER_FEMALE)
    if not ok:
        raise ToolError(
            f"{species.name} (gender_rate={species.gender_rate}) can't be {GENDER_NAMES[gender]} - "
            "see genderMatchesSpecies() in lib/Pokemon/PokemonTypes.cpp"
        )


# --------------------------------------------------------------------------
# Save file I/O
# --------------------------------------------------------------------------


@dataclass
class SaveFile:
    path: Path
    data: bytearray
    sequence: int


def load_save(path: Path) -> SaveFile:
    data = bytearray(path.read_bytes())
    if len(data) < HEADER_BYTES or data[0:4] != MAGIC:
        raise ToolError(f"{path}: not a PKV2 save file (bad magic)")
    version, header_size = struct.unpack_from("<HH", data, 4)
    sequence, = struct.unpack_from("<I", data, 8)
    state_size, record_size = struct.unpack_from("<HH", data, 12)
    if header_size != HEADER_BYTES:
        raise ToolError(f"{path}: unexpected header size {header_size} (expected {HEADER_BYTES})")
    if state_size != STATE_BYTES_V4:
        raise ToolError(
            f"{path}: this tool only understands version-4 saves (198-byte state), got {state_size} bytes "
            f"(version {version}) - refusing to touch an unfamiliar layout"
        )
    if record_size != RECORD_BYTES:
        raise ToolError(f"{path}: unexpected record size {record_size} (expected {RECORD_BYTES})")
    return SaveFile(path, data, sequence)


def state_bytes(save: SaveFile, offset: int, size: int) -> bytes:
    base = HEADER_BYTES + offset
    return bytes(save.data[base : base + size])


def set_state_bytes(save: SaveFile, offset: int, value: bytes) -> None:
    base = HEADER_BYTES + offset
    save.data[base : base + len(value)] = value


def recompute_crc(save: SaveFile) -> None:
    payload = bytes(save.data[:-4])
    crc = zlib.crc32(payload) & 0xFFFFFFFF
    struct.pack_into("<I", save.data, len(save.data) - 4, crc)


def load_saves(save_dir: Path) -> list[SaveFile]:
    saves = []
    for name in SAVE_NAMES:
        path = save_dir / name
        if path.exists():
            saves.append(load_save(path))
    if not saves:
        raise ToolError(f"no {'/'.join(SAVE_NAMES)} found in {save_dir}")
    return saves


def write_saves(saves: list[SaveFile], backup: bool) -> None:
    for save in saves:
        if backup:
            bak = save.path.with_suffix(save.path.suffix + ".bak")
            if not bak.exists():
                bak.write_bytes(save.path.read_bytes())
        save.path.write_bytes(save.data)
        print(f"wrote {save.path}")


# --------------------------------------------------------------------------
# Pending events
# --------------------------------------------------------------------------


def read_pending_event(save: SaveFile, slot: int) -> tuple[int, int, int, int, int, int]:
    offset = OFF_PENDING_EVENTS + slot * PENDING_EVENT_BYTES
    return struct.unpack_from("<IHBBBB", state_bytes(save, offset, PENDING_EVENT_BYTES))


def write_pending_event(
    save: SaveFile, slot: int, record_id: int, species_id: int, level: int, gender: int, item: int, kind: int
) -> None:
    offset = OFF_PENDING_EVENTS + slot * PENDING_EVENT_BYTES
    packed = struct.pack("<IHBBBB", record_id, species_id, level, gender, item, kind)
    set_state_bytes(save, offset, packed)


def first_empty_pending_slot(save: SaveFile) -> Optional[int]:
    for slot in range(PENDING_EVENT_COUNT):
        _, _, _, _, _, kind = read_pending_event(save, slot)
        if kind == PENDING_KIND_NONE:
            return slot
    return None


# --------------------------------------------------------------------------
# Subcommands
# --------------------------------------------------------------------------


def cmd_dump(args: argparse.Namespace) -> None:
    species_map = load_species()
    item_map = load_items()
    saves = load_saves(args.save_dir)
    active = max(saves, key=lambda s: s.sequence)
    print(f"active file: {active.path.name} (sequence {active.sequence})")
    for save in saves:
        marker = " (active)" if save is active else ""
        print(f"  {save.path.name}: sequence {save.sequence}{marker}")

    party_ids = struct.unpack_from("<6I", state_bytes(active, OFF_PARTY_IDS, 24))
    print("\nparty record ids:", [rid for rid in party_ids if rid != 0])

    header_record_count, = struct.unpack_from("<I", active.data, 16)
    records_offset = HEADER_BYTES + STATE_BYTES_V4
    print(f"\nrecords ({header_record_count}):")
    for i in range(header_record_count):
        offset = records_offset + i * RECORD_BYTES
        record_id, total_xp, species_id, caught_level, gender, origin, flags = struct.unpack_from(
            "<IIHBBBB", active.data, offset
        )
        nickname_raw = bytes(active.data[offset + 14 : offset + 47])
        nickname = nickname_raw.split(b"\x00", 1)[0].decode("utf-8", errors="replace")
        species = species_map.get(species_id)
        species_name = species.name if species else f"?{species_id}"
        in_party = " [party]" if record_id in party_ids else ""
        print(
            f"  #{record_id}: {species_name} (species {species_id}), totalXp={total_xp}, "
            f"caughtLevel={caught_level}, gender={GENDER_NAMES.get(gender, gender)}, "
            f"nickname={nickname!r}{in_party}"
        )

    battle_progress, = struct.unpack_from("<H", state_bytes(active, OFF_BATTLE_PROGRESS, 2))
    defeated_gyms = [n + 1 for n in range(8) if battle_progress & (1 << n)]
    defeated_elite = [n + 1 for n in range(4) if battle_progress & (1 << (8 + n))]
    print(f"\nbattleProgress: {battle_progress:#06x} - gyms defeated {defeated_gyms}, Elite Four defeated {defeated_elite}")

    item_counts = struct.unpack_from("<6H", state_bytes(active, OFF_ITEM_COUNTS, 12))
    print("\nevolution items (non-zero):")
    for i, count in enumerate(item_counts):
        if count:
            item = item_map.get(i + 1)
            print(f"  id {i + 1} ({item.name if item else '?'}): {count}")

    bag_counts = struct.unpack_from(f"<{BAG_SLOT_COUNT}B", state_bytes(active, OFF_BAG_COUNTS, BAG_SLOT_COUNT))
    print("bag items (non-zero):")
    for i, count in enumerate(bag_counts):
        if count:
            item = item_map.get(i + EVOLUTION_ITEM_COUNT + 1)
            print(f"  id {i + EVOLUTION_ITEM_COUNT + 1} ({item.name if item else '?'}): {count}")

    ball_misses, medicine_misses, machine_misses = struct.unpack_from("<BBB", state_bytes(active, OFF_BALL_MISSES, 3))
    print(
        f"\ndrop-track pity counters (miss streak toward each track's guaranteed hit): "
        f"ball={ball_misses}/3, medicine={medicine_misses}/3, TM-HM={machine_misses}/3"
    )

    print("\npending events:")
    for slot in range(PENDING_EVENT_COUNT):
        record_id, species_id, level, gender, item, kind = read_pending_event(active, slot)
        if kind == PENDING_KIND_NONE:
            print(f"  slot {slot}: empty")
            continue
        species = species_map.get(species_id)
        species_name = species.name if species else f"?{species_id}"
        print(
            f"  slot {slot}: {PENDING_KIND_NAMES.get(kind, kind)} - recordId={record_id}, species={species_name}, "
            f"level={level}, gender={GENDER_NAMES.get(gender, gender)}, item={item}"
        )


def cmd_reset_battle_store(args: argparse.Namespace) -> None:
    removed = 0
    for name in BATTLE_STORE_NAMES:
        path = args.save_dir / name
        if path.exists():
            if not args.dry_run:
                path.unlink()
            print(f"removed {path}")
            removed += 1
    if removed == 0:
        print("nothing to remove - no battle-store files present")
    else:
        print("HP/PP/status will be freshly rebuilt (full HP/PP, no status) the next time each Pokemon battles.")


def cmd_reset_gym_progress(args: argparse.Namespace) -> None:
    saves = load_saves(args.save_dir)
    for save in saves:
        before, = struct.unpack_from("<H", state_bytes(save, OFF_BATTLE_PROGRESS, 2))
        set_state_bytes(save, OFF_BATTLE_PROGRESS, struct.pack("<H", 0))
        recompute_crc(save)
        print(f"{save.path.name}: battleProgress {before:#06x} -> 0x0000")
    if not args.dry_run:
        write_saves(saves, backup=not args.no_backup)


def cmd_queue_encounter(args: argparse.Namespace) -> None:
    species_map = load_species()
    species = find_species(species_map, args.species)
    if not (1 <= args.level <= 100):
        raise ToolError("--level must be 1-100")
    gender = GENDER_NAMES_REVERSE.get(args.gender) if args.gender else default_gender_for(species)
    if gender is None:
        raise ToolError(f"--gender must be one of: {', '.join(GENDER_NAMES_REVERSE)}")
    validate_gender(species, gender)

    saves = load_saves(args.save_dir)
    active = max(saves, key=lambda s: s.sequence)
    slot = args.slot if args.slot is not None else first_empty_pending_slot(active)
    if slot is None:
        raise ToolError(
            "all 3 pending-event slots are already in use - pass --slot 0/1/2 to overwrite one explicitly"
        )
    if not (0 <= slot < PENDING_EVENT_COUNT):
        raise ToolError(f"--slot must be 0-{PENDING_EVENT_COUNT - 1}")

    for save in saves:
        write_pending_event(save, slot, 0, species.id, args.level, gender, 0, PENDING_KIND_ENCOUNTER)
        recompute_crc(save)
    print(
        f"queued slot {slot}: wild {species.name} (species {species.id}), level {args.level}, "
        f"gender {GENDER_NAMES[gender]}"
    )
    if not args.dry_run:
        write_saves(saves, backup=not args.no_backup)


def cmd_set_bag_item(args: argparse.Namespace) -> None:
    item_map = load_items()
    item = find_item(item_map, args.item)
    saves = load_saves(args.save_dir)
    for save in saves:
        if item.id <= EVOLUTION_ITEM_COUNT:
            if not (0 <= args.count <= 0xFFFF):
                raise ToolError("--count must be 0-65535 for an evolution item")
            offset = OFF_ITEM_COUNTS + (item.id - 1) * 2
            before, = struct.unpack_from("<H", state_bytes(save, offset, 2))
            set_state_bytes(save, offset, struct.pack("<H", args.count))
        else:
            if not (0 <= args.count <= 0xFF):
                raise ToolError("--count must be 0-255 for a bag item")
            offset = OFF_BAG_COUNTS + (item.id - EVOLUTION_ITEM_COUNT - 1)
            before, = struct.unpack_from("<B", state_bytes(save, offset, 1))
            set_state_bytes(save, offset, struct.pack("<B", args.count))
        recompute_crc(save)
        print(f"{save.path.name}: {item.name} (id {item.id}) {before} -> {args.count}")
    if not args.dry_run:
        write_saves(saves, backup=not args.no_backup)


def cmd_set_record_xp(args: argparse.Namespace) -> None:
    saves = load_saves(args.save_dir)
    if args.xp < 0:
        raise ToolError("--xp must be >= 0")
    for save in saves:
        record_count, = struct.unpack_from("<I", save.data, 16)
        records_offset = HEADER_BYTES + STATE_BYTES_V4
        found = False
        for i in range(record_count):
            offset = records_offset + i * RECORD_BYTES
            record_id, = struct.unpack_from("<I", save.data, offset)
            if record_id != args.record_id:
                continue
            before, = struct.unpack_from("<I", save.data, offset + 4)
            struct.pack_into("<I", save.data, offset + 4, args.xp)
            print(f"{save.path.name}: record #{args.record_id} totalXp {before} -> {args.xp}")
            found = True
            break
        if not found:
            raise ToolError(f"{save.path.name}: no record with id {args.record_id}")
        recompute_crc(save)
    if not args.dry_run:
        write_saves(saves, backup=not args.no_backup)


GENDER_NAMES_REVERSE = {"male": GENDER_MALE, "female": GENDER_FEMALE, "genderless": GENDER_GENDERLESS}


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument(
        "--save-dir", type=Path, default=DEFAULT_SAVE_DIR, help=f"directory holding pokemon-{{a,b}}.bin (default: {DEFAULT_SAVE_DIR})"
    )
    parser.add_argument("--no-backup", action="store_true", help="skip writing .bak copies before the first edit")
    parser.add_argument("--dry-run", action="store_true", help="print what would change without writing anything")
    subparsers = parser.add_subparsers(dest="command", required=True)

    subparsers.add_parser("dump", help="print party, gym progress, bag contents, and pending events").set_defaults(
        func=cmd_dump
    )

    subparsers.add_parser(
        "reset-battle-store", help="delete the battle-entry files so HP/PP/status rebuild full on next use"
    ).set_defaults(func=cmd_reset_battle_store)

    subparsers.add_parser("reset-gym-progress", help="zero out battleProgress (clears gym history AND badges)").set_defaults(
        func=cmd_reset_gym_progress
    )

    queue = subparsers.add_parser("queue-encounter", help="queue a wild-Pokemon pending event to test the catch flow")
    queue.add_argument("--species", required=True, help="species id or name, e.g. 16 or pidgey")
    queue.add_argument("--level", type=int, required=True, help="1-100")
    queue.add_argument("--gender", choices=sorted(GENDER_NAMES_REVERSE), help="default: auto-picked to satisfy the species")
    queue.add_argument("--slot", type=int, help="pending-event slot 0-2 (default: first empty slot)")
    queue.set_defaults(func=cmd_queue_encounter)

    bag = subparsers.add_parser("set-bag-item", help="set an item's bag count directly (evolution stone or bag item)")
    bag.add_argument("--item", required=True, help="item id or name, e.g. 7 or poke-ball")
    bag.add_argument("--count", type=int, required=True)
    bag.set_defaults(func=cmd_set_bag_item)

    xp = subparsers.add_parser("set-record-xp", help="set a Pokemon record's totalXp directly (party or PC)")
    xp.add_argument("--record-id", type=int, required=True)
    xp.add_argument("--xp", type=int, required=True)
    xp.set_defaults(func=cmd_set_record_xp)

    return parser


def main(argv: list[str]) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        args.func(args)
    except ToolError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
