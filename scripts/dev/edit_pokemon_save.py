#!/usr/bin/env python3
"""Dev tool to inspect and hand-edit the Pokemon save files under a
simulator's SD-card sandbox (fs_/.crosspoint by default).

This is NOT a product feature - it exists so a developer/tester can quickly
set up scenarios (queue a wild encounter, wipe HP/PP/status back to full,
clear gym progress, hand a Pokemon some items) without waiting on real
gameplay/RNG. See docs/file-formats.md for the on-disk layout this assumes;
this tool only understands the CURRENT save format used by this branch
(main save version 6, 205-byte state; battle-store version 2, 20-byte
entries) and refuses to touch anything else rather than risk corrupting an
unfamiliar layout.

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
    python3 scripts/dev/edit_pokemon_save.py set-all-bag-items --count 5
    python3 scripts/dev/edit_pokemon_save.py set-record-xp --record-id 1 --xp 5000
    python3 scripts/dev/edit_pokemon_save.py set-moves --record-id 1 --moves "33,45,52,84"
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
STATS_CSV = ROOT / "scripts" / "data" / "pokemon-stats.csv"
GYMS_CSV = ROOT / "scripts" / "data" / "pokemon-gyms.csv"
MOVES_CSV = ROOT / "scripts" / "data" / "pokemon-moves.csv"
LEARNSETS_CSV = ROOT / "scripts" / "data" / "pokemon-learnsets.csv"

MAGIC = b"PKV2"
HEADER_BYTES = 24
OFF_HEADER_RECORD_COUNT = 16  # u32
OFF_HEADER_PAYLOAD_BYTES = 20  # u32: stateBytes + recordCount * RECORD_BYTES - see decodeSnapshotHeader()
STATE_BYTES_V6 = 199
RECORD_BYTES = 48
POKEMON_NICKNAME_BYTES = 33  # record bytes 14..46; byte 47 is reserved (must be 0)
PENDING_EVENT_BYTES = 10
PENDING_EVENT_COUNT = 3
EVOLUTION_ITEM_COUNT = 6
BAG_SLOT_COUNT = 77
PP_UP_ITEM_ID = 84  # tracked in ppUpCount, not bagCounts - see PP_UP_ITEM_ID in lib/Pokemon/PokemonBattleTypes.h
BATTLE_BOOST_ITEM_ID_FIRST = 85  # X Attack..Dire Hit (90) - tracked in battleBoostCounts, not bagCounts
BATTLE_BOOST_ITEM_ID_LAST = 90
BATTLE_BOOST_ITEM_COUNT = BATTLE_BOOST_ITEM_ID_LAST - BATTLE_BOOST_ITEM_ID_FIRST + 1

# State-relative byte offsets (version 6 - see docs/file-formats.md). Bytes
# 0-194 are unchanged from version 3; 195-197 are version-4 pity counters for
# the ball/medicine/TM-HM drop tracks (Stage 23); 198 is version 5's PP Up
# item count; 199-204 are version 6's 6 battle-boost item counts (X Attack/
# X Defense/X Speed/X Special/Guard Spec./Dire Hit).
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
OFF_PPUP_COUNT = 198  # u8 - how many PP Up items (id 84) the player holds, tracked outside bagCounts
OFF_BATTLE_BOOST_COUNTS = 199  # 6 x u8 - ids 85-90, tracked outside bagCounts, same reason as PP Up

# The battle-store side file (pokemon-battle-{a,b}.bin) - see
# lib/Pokemon/PokemonBattleStoreCodec.h. Version 2 entries add a per-slot PP
# Up counter after the version-1 layout.
BATTLE_MAGIC = b"PKBT"
BATTLE_HEADER_BYTES = 10  # magic(4) + version(1) + entryCount(1) + sequence(4)
BATTLE_ENTRY_BYTES_V1 = 16
BATTLE_ENTRY_BYTES = 20  # version 2: adds ppUp[4]
BATTLE_MOVE_SLOTS = 4
BATTLE_STORE_VERSION_V1 = 1
BATTLE_STORE_VERSION = 2

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

ORIGIN_UNKNOWN = 0
ORIGIN_CAUGHT = 1
ORIGIN_STARTER = 2

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


@dataclass(frozen=True)
class Move:
    id: int
    name: str
    pp: int


def load_moves() -> dict[int, Move]:
    moves: dict[int, Move] = {}
    with MOVES_CSV.open(encoding="utf-8") as f:
        f.readline()
        for row in csv.DictReader(f):
            move_id = int(row["id"])
            moves[move_id] = Move(move_id, row["name"], int(row["pp"]))
    return moves


def find_move(move_map: dict[int, Move], query: str) -> Move:
    if query.isdigit():
        move_id = int(query)
        if move_id not in move_map:
            raise ToolError(f"no move with id {move_id}")
        return move_map[move_id]
    needle = query.strip().lower().replace(" ", "").replace("-", "")
    for move in move_map.values():
        if move.name.lower().replace(" ", "").replace("-", "") == needle:
            return move
    raise ToolError(f"no move named {query!r} (try a numeric id, or check spelling)")


def load_learnsets() -> dict[int, list[tuple[int, int]]]:
    """species_id -> [(level, move_id), ...] in ascending level order, exactly
    as scripts/data/pokemon-learnsets.csv stores them (already ascending by
    construction - see fetch_pokemon_battle_data.py)."""
    learnsets: dict[int, list[tuple[int, int]]] = {}
    with LEARNSETS_CSV.open(encoding="utf-8") as f:
        f.readline()
        for row in csv.DictReader(f):
            learnsets.setdefault(int(row["species_id"]), []).append((int(row["level"]), int(row["move_id"])))
    return learnsets


def default_moveset_for(learnsets: dict[int, list[tuple[int, int]]], species_id: int, level: int) -> list[int]:
    """Mirrors defaultMovesetForLevel() (PokemonBattle.cpp) exactly: walk the
    species' learnset backwards (most-recently-learned-at-or-below-level
    first) taking up to BATTLE_MOVE_SLOTS moves, packed at the front."""
    entries = learnsets.get(species_id, [])
    picked: list[int] = []
    for entry_level, move_id in reversed(entries):
        if entry_level > level:
            continue
        picked.append(move_id)
        if len(picked) == BATTLE_MOVE_SLOTS:
            break
    return picked


def load_base_hp() -> dict[int, int]:
    base_hp: dict[int, int] = {}
    with STATS_CSV.open(encoding="utf-8") as f:
        f.readline()
        for row in csv.DictReader(f):
            base_hp[int(row["id"])] = int(row["hp"])
    return base_hp


def battle_max_hp(base_hp: int, level: int, iv: int = 0, ev: int = 0) -> int:
    """Mirrors battleMaxHp() (PokemonBattle.cpp) exactly - iv/ev default to 0
    since this tool doesn't touch pokemon-ivev-{a,b}.bin (a freshly-created
    record has no IV/EV entry yet either, until PokemonService::ensureIvEv()
    lazily rolls one on first real use - same starting point)."""
    return (2 * (base_hp + iv) + ev // 4) * level // 100 + level + 10


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


def xp_required(level: int) -> int:
    """Mirrors xpRequired() in lib/Pokemon/PokemonTypes.cpp exactly (integer
    division, not float) so a hand-picked level round-trips through
    levelForXp() on the device without landing one level off."""
    clamped = max(1, min(100, level))
    n = clamped - 1
    return 10 * n + (3 * n * n) // 4


def level_for_xp(total_xp: int) -> int:
    """Mirrors levelForXp() (PokemonTypes.cpp) - a plain linear scan is fine
    here (only 100 levels, this tool isn't performance-sensitive)."""
    level = 1
    for candidate in range(1, 101):
        if xp_required(candidate) <= total_xp:
            level = candidate
        else:
            break
    return level


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
    if state_size != STATE_BYTES_V6:
        raise ToolError(
            f"{path}: this tool only understands version-6 saves (205-byte state), got {state_size} bytes "
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


def set_header_record_count(save: SaveFile, new_count: int) -> None:
    """Updates BOTH header fields that depend on record count:
    recordCount itself (offset 16) and the cached total payload size
    (offset 20, stateBytes + recordCount * RECORD_BYTES). decodeSnapshotHeader()
    (PokemonStoreCodec.cpp) recomputes and compares that second field on every
    load - writing recordCount alone leaves a stale payload size and the
    device rejects the whole file as corrupt on next boot."""
    struct.pack_into("<I", save.data, OFF_HEADER_RECORD_COUNT, new_count)
    payload_bytes = STATE_BYTES_V6 + new_count * RECORD_BYTES
    struct.pack_into("<I", save.data, OFF_HEADER_PAYLOAD_BYTES, payload_bytes)


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
# Battle store (pokemon-battle-{a,b}.bin) - live moveset/HP/PP/status,
# separate from the main save. See lib/Pokemon/PokemonBattleStoreCodec.h.
# --------------------------------------------------------------------------

BATTLE_STORE_NAMES_CURRENT = ("pokemon-battle-a.bin", "pokemon-battle-b.bin")


@dataclass
class BattleEntry:
    record_id: int
    moves: list[int]  # 4, 0 = empty slot
    pp: list[int]  # 4, current PP
    current_hp: int
    status: int
    status_turns: int
    ppup: list[int]  # 4, 0-3 each


def read_battle_store(save_dir: Path) -> tuple[list[BattleEntry], int]:
    """Reads whichever of pokemon-battle-{a,b}.bin decodes with the higher
    sequence (mirroring PokemonBattleStore::load()'s own pick), tolerating a
    v1 (16-byte, no ppUp) file by defaulting ppUp to 0. Returns (entries, the
    sequence number the NEXT write should use). An empty/missing/corrupt pair
    of files is not an error here - matches the firmware's own "just starts
    empty" behavior - and returns ([], 1)."""
    best: Optional[tuple[int, list[BattleEntry]]] = None
    for name in BATTLE_STORE_NAMES_CURRENT:
        path = save_dir / name
        if not path.exists():
            continue
        data = path.read_bytes()
        if len(data) < BATTLE_HEADER_BYTES + 4 or data[0:4] != BATTLE_MAGIC:
            continue
        version = data[4]
        count = data[5]
        if version not in (BATTLE_STORE_VERSION_V1, BATTLE_STORE_VERSION):
            continue
        sequence, = struct.unpack_from("<I", data, 6)
        entry_bytes = BATTLE_ENTRY_BYTES_V1 if version == BATTLE_STORE_VERSION_V1 else BATTLE_ENTRY_BYTES
        payload_size = BATTLE_HEADER_BYTES + count * entry_bytes
        if len(data) != payload_size + 4 or sequence == 0:
            continue
        crc_expected, = struct.unpack_from("<I", data, payload_size)
        if (zlib.crc32(data[:payload_size]) & 0xFFFFFFFF) != crc_expected:
            continue
        entries = []
        offset = BATTLE_HEADER_BYTES
        for _ in range(count):
            record_id, = struct.unpack_from("<I", data, offset)
            moves = list(data[offset + 4 : offset + 8])
            pp = list(data[offset + 8 : offset + 12])
            current_hp, = struct.unpack_from("<H", data, offset + 12)
            status = data[offset + 14]
            status_turns = data[offset + 15]
            ppup = list(data[offset + 16 : offset + 20]) if version == BATTLE_STORE_VERSION else [0, 0, 0, 0]
            entries.append(BattleEntry(record_id, moves, pp, current_hp, status, status_turns, ppup))
            offset += entry_bytes
        if best is None or sequence > best[0]:
            best = (sequence, entries)
    if best is None:
        return [], 1
    sequence, entries = best
    next_sequence = 1 if sequence == 0xFFFFFFFF else sequence + 1
    return entries, next_sequence


def write_battle_store(save_dir: Path, entries: list[BattleEntry], sequence: int, backup: bool, dry_run: bool) -> None:
    entries = sorted(entries, key=lambda e: e.record_id)
    body = bytearray(BATTLE_MAGIC)
    body.append(BATTLE_STORE_VERSION)
    body.append(len(entries))
    body += struct.pack("<I", sequence)
    for e in entries:
        body += struct.pack("<I", e.record_id)
        body += bytes(e.moves)
        body += bytes(e.pp)
        body += struct.pack("<H", e.current_hp)
        body.append(e.status)
        body.append(e.status_turns)
        body += bytes(e.ppup)
    crc = zlib.crc32(bytes(body)) & 0xFFFFFFFF
    body += struct.pack("<I", crc)
    if dry_run:
        return
    save_dir.mkdir(parents=True, exist_ok=True)
    for name in BATTLE_STORE_NAMES_CURRENT:
        path = save_dir / name
        if backup and path.exists():
            bak = path.with_suffix(path.suffix + ".bak")
            if not bak.exists():
                bak.write_bytes(path.read_bytes())
        path.write_bytes(bytes(body))
        print(f"wrote {path}")


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
    records_offset = HEADER_BYTES + STATE_BYTES_V6
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
    ppup_count, = struct.unpack_from("<B", state_bytes(active, OFF_PPUP_COUNT, 1))
    if ppup_count:
        item = item_map.get(PP_UP_ITEM_ID)
        print(f"  id {PP_UP_ITEM_ID} ({item.name if item else '?'}): {ppup_count}")
    battle_boost_counts = struct.unpack_from("<6B", state_bytes(active, OFF_BATTLE_BOOST_COUNTS, BATTLE_BOOST_ITEM_COUNT))
    for i, count in enumerate(battle_boost_counts):
        if count:
            item_id = BATTLE_BOOST_ITEM_ID_FIRST + i
            item = item_map.get(item_id)
            print(f"  id {item_id} ({item.name if item else '?'}): {count}")

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

    move_map = load_moves()
    battle_entries, _ = read_battle_store(args.save_dir)
    print("\nbattle entries (moveset/HP/PP/status - only Pokemon that have ever fought have one):")
    if not battle_entries:
        print("  none yet")
    for entry in sorted(battle_entries, key=lambda e: e.record_id):
        move_descs = []
        for move_id, pp, ppup in zip(entry.moves, entry.pp, entry.ppup):
            if move_id == 0:
                move_descs.append("-")
                continue
            move = move_map.get(move_id)
            name = move.name if move else f"?{move_id}"
            suffix = f" (x{ppup})" if ppup else ""
            move_descs.append(f"{name} {pp}{suffix}")
        print(f"  #{entry.record_id}: HP {entry.current_hp}, status={entry.status}, moves = {move_descs}")


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


def set_bag_item_count(save: SaveFile, item: Item, count: int) -> int:
    """Writes `count` for one item on one already-loaded save, returning the
    previous value. Does NOT recompute the CRC or write the file - callers
    doing several items at once (cmd_set_all_bag_items) recompute once at the
    end instead of once per item."""
    if item.id <= EVOLUTION_ITEM_COUNT:
        if not (0 <= count <= 0xFFFF):
            raise ToolError("count must be 0-65535 for an evolution item")
        offset = OFF_ITEM_COUNTS + (item.id - 1) * 2
        before, = struct.unpack_from("<H", state_bytes(save, offset, 2))
        set_state_bytes(save, offset, struct.pack("<H", count))
        return before
    if not (0 <= count <= 0xFF):
        raise ToolError("count must be 0-255 for a bag item")
    if item.id == PP_UP_ITEM_ID:
        # Tracked in its own ppUpCount field, not bagCounts - PokemonRecord's
        # bagCounts array is exactly 77 bytes (ids 7-83) with nothing spare,
        # so writing PP Up's count there the same way every other bag item
        # works would silently corrupt whatever follows it (battleProgress).
        before, = struct.unpack_from("<B", state_bytes(save, OFF_PPUP_COUNT, 1))
        set_state_bytes(save, OFF_PPUP_COUNT, struct.pack("<B", count))
        return before
    if BATTLE_BOOST_ITEM_ID_FIRST <= item.id <= BATTLE_BOOST_ITEM_ID_LAST:
        # Same reasoning as PP Up above - tracked in battleBoostCounts, not
        # bagCounts.
        offset = OFF_BATTLE_BOOST_COUNTS + (item.id - BATTLE_BOOST_ITEM_ID_FIRST)
        before, = struct.unpack_from("<B", state_bytes(save, offset, 1))
        set_state_bytes(save, offset, struct.pack("<B", count))
        return before
    offset = OFF_BAG_COUNTS + (item.id - EVOLUTION_ITEM_COUNT - 1)
    before, = struct.unpack_from("<B", state_bytes(save, offset, 1))
    set_state_bytes(save, offset, struct.pack("<B", count))
    return before


def cmd_set_bag_item(args: argparse.Namespace) -> None:
    item_map = load_items()
    item = find_item(item_map, args.item)
    saves = load_saves(args.save_dir)
    for save in saves:
        before = set_bag_item_count(save, item, args.count)
        recompute_crc(save)
        print(f"{save.path.name}: {item.name} (id {item.id}) {before} -> {args.count}")
    if not args.dry_run:
        write_saves(saves, backup=not args.no_backup)


def cmd_set_all_bag_items(args: argparse.Namespace) -> None:
    if not (0 <= args.count <= 0xFF):
        raise ToolError("--count must be 0-255 (the bag-item byte range - also applies to the 6 evolution items here)")
    item_map = load_items()
    saves = load_saves(args.save_dir)
    # Every real item id except Master Ball is fair game - Master Ball is
    # deliberately skipped (id 4, category Ball) since handing out 5 of a
    # guaranteed-catch item defeats the point of ever needing one; everything
    # else (stones, ordinary balls, medicine, TM/HM, PP Up, battle-boost
    # items) is set to the same count.
    for save in saves:
        for item_id in sorted(item_map):
            item = item_map[item_id]
            if item.name == "Master Ball":
                continue
            set_bag_item_count(save, item, args.count)
        recompute_crc(save)
        print(f"{save.path.name}: set every item (except Master Ball) to {args.count}")
    if not args.dry_run:
        write_saves(saves, backup=not args.no_backup)


def cmd_set_record_xp(args: argparse.Namespace) -> None:
    saves = load_saves(args.save_dir)
    if args.xp < 0:
        raise ToolError("--xp must be >= 0")
    for save in saves:
        record_count, = struct.unpack_from("<I", save.data, 16)
        records_offset = HEADER_BYTES + STATE_BYTES_V6
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


def cmd_add_party_member(args: argparse.Namespace) -> None:
    species_map = load_species()
    species = find_species(species_map, args.species)
    if not (1 <= args.level <= 100):
        raise ToolError("--level must be 1-100")
    gender = GENDER_NAMES_REVERSE.get(args.gender) if args.gender else default_gender_for(species)
    if gender is None:
        raise ToolError(f"--gender must be one of: {', '.join(GENDER_NAMES_REVERSE)}")
    validate_gender(species, gender)

    nickname_text = (args.nickname or "").encode("utf-8")[: POKEMON_NICKNAME_BYTES - 1]
    nickname_bytes = nickname_text + b"\x00" * (POKEMON_NICKNAME_BYTES - len(nickname_text))

    saves = load_saves(args.save_dir)
    records_offset = HEADER_BYTES + STATE_BYTES_V6

    # pokemon-a.bin and pokemon-b.bin can legitimately hold different
    # generations of state (that is the whole point of the double buffer),
    # so "first empty party slot" / "next free record id" must be decided
    # ONCE from the active (most-recent-sequence) file and then applied
    # identically to every file - never recomputed per file, or the two
    # copies would diverge into two different new Pokemon.
    active = max(saves, key=lambda s: s.sequence)
    party_ids = list(struct.unpack_from("<6I", state_bytes(active, OFF_PARTY_IDS, 24)))
    empty_slot = next((i for i, rid in enumerate(party_ids) if rid == 0), None)
    if empty_slot is None:
        raise ToolError("party is already full (6/6) - withdraw a Pokemon to the PC first")

    record_count, = struct.unpack_from("<I", active.data, 16)
    existing_ids = set()
    for i in range(record_count):
        offset = records_offset + i * RECORD_BYTES
        rid, = struct.unpack_from("<I", active.data, offset)
        existing_ids.add(rid)
    new_id = max(existing_ids, default=0) + 1

    total_xp = xp_required(args.level)
    record_bytes = (
        struct.pack("<IIHBBBB", new_id, total_xp, species.id, args.level, gender, ORIGIN_CAUGHT, 0)
        + nickname_bytes
        + b"\x00"  # byte 47: reserved, must stay 0 (see decodeRecord() in PokemonTypes.cpp)
    )
    if len(record_bytes) != RECORD_BYTES:
        raise ToolError(f"internal error: built a {len(record_bytes)}-byte record, expected {RECORD_BYTES}")

    for save in saves:
        # Apply to every file at its OWN current record_count/party_ids
        # (which should match the active file's after prior commands, but we
        # read them fresh per file rather than assuming - only the new
        # id/slot decided above are shared).
        save_record_count, = struct.unpack_from("<I", save.data, OFF_HEADER_RECORD_COUNT)
        insert_at = records_offset + save_record_count * RECORD_BYTES
        save.data[insert_at:insert_at] = record_bytes  # grows the file; CRC is recomputed below
        set_header_record_count(save, save_record_count + 1)

        save_party_ids = list(struct.unpack_from("<6I", state_bytes(save, OFF_PARTY_IDS, 24)))
        save_party_ids[empty_slot] = new_id
        set_state_bytes(save, OFF_PARTY_IDS, struct.pack("<6I", *save_party_ids))

        # Keep the Pokedex consistent with the party: a Pokemon you own has
        # necessarily been seen and caught (validateState() requires
        # caught subset-of seen).
        seen = bytearray(state_bytes(save, OFF_SEEN_BITS, 19))
        caught = bytearray(state_bytes(save, OFF_CAUGHT_BITS, 19))
        zero_based = species.id - 1
        seen[zero_based // 8] |= 1 << (zero_based % 8)
        caught[zero_based // 8] |= 1 << (zero_based % 8)
        set_state_bytes(save, OFF_SEEN_BITS, bytes(seen))
        set_state_bytes(save, OFF_CAUGHT_BITS, bytes(caught))

        recompute_crc(save)
        print(
            f"{save.path.name}: added record #{new_id} {species.name} (species {species.id}) at level {args.level} "
            f"into party slot {empty_slot}"
        )
    if not args.dry_run:
        write_saves(saves, backup=not args.no_backup)


def cmd_set_moves(args: argparse.Namespace) -> None:
    move_map = load_moves()
    saves = load_saves(args.save_dir)
    active = max(saves, key=lambda s: s.sequence)
    record_count, = struct.unpack_from("<I", active.data, OFF_HEADER_RECORD_COUNT)
    records_offset = HEADER_BYTES + STATE_BYTES_V6
    species_id = None
    total_xp = None
    for i in range(record_count):
        offset = records_offset + i * RECORD_BYTES
        rid, xp, sid = struct.unpack_from("<IIH", active.data, offset)
        if rid == args.record_id:
            species_id, total_xp = sid, xp
            break
    if species_id is None:
        raise ToolError(f"no record with id {args.record_id}")
    level = level_for_xp(total_xp)

    if args.moves:
        move_ids = [find_move(move_map, token.strip()).id for token in args.moves.split(",")]
        if not (1 <= len(move_ids) <= BATTLE_MOVE_SLOTS):
            raise ToolError(f"--moves must list 1-{BATTLE_MOVE_SLOTS} moves")
    else:
        move_ids = default_moveset_for(load_learnsets(), species_id, level)
        if not move_ids:
            raise ToolError(
                f"species {species_id} has no learnset move at or below level {level} - pass --moves explicitly"
            )

    moves = move_ids + [0] * (BATTLE_MOVE_SLOTS - len(move_ids))
    pp = [move_map[m].pp if m else 0 for m in moves]

    entries, next_sequence = read_battle_store(args.save_dir)
    entry = next((e for e in entries if e.record_id == args.record_id), None)
    if entry is None:
        max_hp = battle_max_hp(load_base_hp().get(species_id, 1), level)
        entry = BattleEntry(args.record_id, [0, 0, 0, 0], [0, 0, 0, 0], max_hp, 0, 0, [0, 0, 0, 0])
        entries.append(entry)
    entry.moves = moves
    entry.pp = pp
    entry.ppup = [0, 0, 0, 0]  # a freshly (re)taught moveset starts with no PP Up on any slot

    move_names = [move_map[m].name if m else "-" for m in moves]
    print(f"record #{args.record_id} (species {species_id}, level {level}): moves = {move_names}")
    write_battle_store(args.save_dir, entries, next_sequence, backup=not args.no_backup, dry_run=args.dry_run)


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

    all_bag = subparsers.add_parser(
        "set-all-bag-items", help="set every item's count to the same value in one shot (except Master Ball)"
    )
    all_bag.add_argument("--count", type=int, required=True, help="0-255")
    all_bag.set_defaults(func=cmd_set_all_bag_items)

    xp = subparsers.add_parser("set-record-xp", help="set a Pokemon record's totalXp directly (party or PC)")
    xp.add_argument("--record-id", type=int, required=True)
    xp.add_argument("--xp", type=int, required=True)
    xp.set_defaults(func=cmd_set_record_xp)

    add_member = subparsers.add_parser(
        "add-party-member", help="create a new record at a given level and drop it into the first empty party slot"
    )
    add_member.add_argument("--species", required=True, help="species id or name, e.g. 25 or pikachu")
    add_member.add_argument("--level", type=int, required=True, help="1-100 (sets totalXp to exactly xpRequired(level))")
    add_member.add_argument("--gender", choices=sorted(GENDER_NAMES_REVERSE), help="default: auto-picked to satisfy the species")
    add_member.add_argument("--nickname", help="default: none")
    add_member.set_defaults(func=cmd_add_party_member)

    moves = subparsers.add_parser(
        "set-moves", help="set a record's 4 battle move slots (auto-picked from its real learnset if --moves is omitted)"
    )
    moves.add_argument("--record-id", type=int, required=True)
    moves.add_argument(
        "--moves", help="comma-separated move ids/names, e.g. '33,45,52,84' - default: auto-pick from the species' own learnset at its current level"
    )
    moves.set_defaults(func=cmd_set_moves)

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
