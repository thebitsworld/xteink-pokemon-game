# Pokémon save-edit tool (`scripts/dev/edit_pokemon_save.py`)

A dev-only tool for viewing/patching the Pokémon save file under the simulator's SD-card sandbox (`fs_/.crosspoint` by default), for fast testing (queuing a wild encounter, restoring full HP/PP/status, clearing gym history, granting bag items, setting XP...) without waiting on real gameplay/RNG. Not a product feature.

It only understands **this branch's current save format** (main save version 3, 195-byte state) — on an unrecognized format it refuses to run rather than risk an overwrite that could corrupt the save.

**Write safety**: every write patches both `pokemon-a.bin` and `pokemon-b.bin` identically (if both exist), matching the firmware's own double-buffer convention — so whichever file the next boot picks as "active", the result is correct either way. A `.bak` copy of each file is created automatically before the first write of a run, unless `--no-backup` is passed.

## Setup

Close the simulator before editing the save (to avoid the tool and a running simulator process overwriting each other). Run from the repo root:

```sh
python3 scripts/dev/edit_pokemon_save.py <command> [args]
```

By default the tool looks for `fs_/.crosspoint/pokemon-{a,b}.bin`. Use `--save-dir <path>` to point at a different sandbox.

## Global options (placed before the command name)

- `--save-dir SAVE_DIR` — directory containing `pokemon-{a,b}.bin` (default: `<repo>/fs_/.crosspoint`)
- `--no-backup` — skip creating a `.bak` copy
- `--dry-run` — print what would change without writing anything

## Commands

### `dump`
Prints the full party, records, gym progress (badges/gym-battle history), bag item counts, and pending events. Use it to check the current state before making a change, or to verify it afterward.

```sh
python3 scripts/dev/edit_pokemon_save.py dump
```

### `queue-encounter`
Queues a wild-Pokémon pending event so you can test the catching flow immediately, without reading until the encounter threshold is reached.

```sh
python3 scripts/dev/edit_pokemon_save.py queue-encounter --species pidgey --level 5
```

- `--species` — numeric id or species name (e.g. `16` or `pidgey`)
- `--level` — 1-100
- `--gender` (`female`/`genderless`/`male`) — defaults to auto-picking a value valid for the species (rejects a gender you specify if it doesn't match the species' `gender_rate`, e.g. Chansey can't be `male`)
- `--slot` — pending-event slot 0-2, defaults to the first empty slot (a clear error if all 3 slots are full)

### `reset-battle-store`
Deletes the auxiliary file holding battle HP/PP/moveset/status (`pokemon-battle-{a,b}.bin` plus the older single file if it still exists) — each Pokémon's next battle will rebuild full HP/PP with no status ailment.

```sh
python3 scripts/dev/edit_pokemon_save.py reset-battle-store
```

### `reset-gym-progress`
Zeroes out `battleProgress` in the state — wipes both gym-battle history **and** any badges already earned.

```sh
python3 scripts/dev/edit_pokemon_save.py reset-gym-progress
```

### `set-bag-item`
Directly sets the count of one bag item (automatically detects whether the selected item is an evolution stone in the u16 `itemCounts` array or a regular item in the u8 `bagCounts` array).

```sh
python3 scripts/dev/edit_pokemon_save.py set-bag-item --item "poke-ball" --count 10
```

- `--item` — numeric id or item name
- `--count` — the count to set

### `set-record-xp`
Directly sets `totalXp` for one Pokémon record (party or PC) by `record-id` (see ids via `dump`).

```sh
python3 scripts/dev/edit_pokemon_save.py set-record-xp --record-id 1 --xp 5000
```

### `add-party-member`
Creates a brand-new record at an exact level and drops it into the first empty party slot. Useful for quickly filling out a 6-member party to test battles, TM/HM compatibility, evolutions, etc. without waiting on real encounters.

```sh
python3 scripts/dev/edit_pokemon_save.py add-party-member --species pikachu --level 20
```

- `--species` — numeric id or species name
- `--level` — 1-100; `totalXp` is set to exactly `xpRequired(level)` so it round-trips through the device's `levelForXp()` without landing a level off
- `--gender` (`female`/`genderless`/`male`) — default: auto-picked to satisfy the species
- `--nickname` — default: none

Fails if the party already has 6 members. Also marks the species as seen and caught in the Pokédex, since a Pokémon you own must be both. HP/PP for a newly-added member come out full the moment it's used (no battle-store entry exists yet), unless you also run `reset-battle-store` to be certain a stale entry for a reused record id isn't picked up first.

## More examples via `--help`

```sh
python3 scripts/dev/edit_pokemon_save.py --help
python3 scripts/dev/edit_pokemon_save.py <command> --help
```

Every species/item name is read directly from `scripts/data/pokemon-kanto-v2.csv` and `scripts/data/pokemon-items.csv` (never hardcoded), so when either source CSV changes the tool picks it up automatically — no tool code changes needed.
