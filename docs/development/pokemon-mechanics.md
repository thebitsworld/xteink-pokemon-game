---
title: Pokémon Module Mechanics
parent: Development
nav_order: 4
---

# Pokémon Module Mechanics

Technical notes on how the Pokémon module (`CROSSINK_ENABLE_POKEMON`) works inside CrossInk, based on reading the source directly and building on the `pokemon-x3` environment. This document adds implementation detail on top of [docs/pokemon-game.md](../pokemon-game.md) (the end-user-facing doc).

## 1. Layered architecture

```
EpubReaderActivity (real page turns, e-ink rendering)
        │
PokemonTracker / PokemonTurnVerifier   — measures real "active reading time"
        │
PokemonService                          — device-facing logic: RNG, calls PokemonGame, calls PokemonStore
        │
PokemonGame.cpp (lib/Pokemon)           — pure-function game rules: XP, encounters, items, evolution
        │
PokemonStore + PokemonStoreCodec        — A/B file storage on SD, CRC32, versioning
```

- `lib/Pokemon/*` — hardware-independent core logic, easy to test in isolation (its own build environment, no device required).
- `src/pokemon/*` — real-device integration layer (SD read/write via `HalStorage`, real RNG, the `devicePokemonService()` singleton).
- `src/activities/pokemon/PokemonActivity.cpp` — UI (Party, PC, Pokédex, resolving encounters/evolutions).
- `src/components/pokemon/PokemonArt*.cpp` — path resolution & loading of Pokémon images (BMP) **from the SD card**, not bundled into the firmware.

The whole module is compiled conditionally under `#if defined(CROSSINK_ENABLE_POKEMON)` — the default build environment (`default`) contains none of this module's code.

## 2. Measuring "real" reading time — anti-cheat

File: [lib/Pokemon/PokemonTracker.cpp](../../lib/Pokemon/PokemonTracker.cpp)

- `PokemonTurnVerifier`: bridges the input-handling task and the render task. `request()` fires when the user turns a page; only once the frame has **rendered successfully** on the e-ink does `renderSucceeded()` confirm the page turn as valid (avoids miscounting when a render lags or fails).
- `PokemonTracker`: only accumulates seconds into `creditedSeconds_` if at least one page turn **succeeded** within the last `ACTIVE_WINDOW_SECONDS = 300` seconds (5 minutes).
  - A book left open without turning pages → no time counted.
  - Auto-page-turn (`source == "auto"` in `EpubReaderActivity`) → filtered out, not counted.
- Every `CHECKPOINT_MINUTES = 5` minutes, `commitWholeMinutes()` locks in the whole minutes accumulated so far and calls `creditMinutes()` down into `PokemonService` to persist into state.
- `flushOnExit()` is called when leaving the reader (`EpubReaderActivity.cpp:2125`) to flush the remaining leftover fraction (<5 minutes) — exiting the app cleanly loses **no minutes**.
- `beginSession()` (called when reopening the reader, `EpubReaderActivity.cpp:1996`) restores the not-yet-committed seconds from the previous session **only if the tracker survived in RAM** — it does not survive a device reboot.

## 3. Main game loop — `applyCreditedMinutes()`

File: [lib/Pokemon/PokemonGame.cpp:315](../../lib/Pokemon/PokemonGame.cpp)

Processes **one minute at a time** (never batched) so every random event lands on the correct real timestamp.

**XP & Level**
- Every minute, the Party leader (`leader`) gains +1 XP, capped at `MAXIMUM_TOTAL_XP = 8340` (level 100).
- `xpRequired(level) = 10*n + 3*n²/4` where `n = level - 1`.
- `levelForXp()` uses binary search to look up level from total XP.
- **XP is driven purely by real reading time, independent of book length or chapter count.**

**Wild Encounter**
- Rolled every 15 minutes (`ENCOUNTER_CHECK_MINUTES`) or every hour, at **2/5** odds.
- Pity mechanic: 3 consecutive misses (`ENCOUNTER_MISSES_BEFORE_GUARANTEE = 3`) → the next roll is guaranteed to hit.
- The species is chosen by weight based on its base `captureRate`; starters (Bulbasaur/Charmander/Squirtle/Pikachu) are forced to the lowest weight.
- **Gated by `bookProgressPercent`** (% progress through the *currently open book*, not total pages/minutes read overall):
  - Middle-stage evolutions only appear at `≥50%`.
  - Final-stage evolutions only at `≥75%`.
  - The wild Pokémon's level band also scales with this same progress band (`0%→lv2-6` ... `95%→lv18-30`).
  - ⚠️ Consequence: reading many short books (easy to reach a high `%`) yields noticeably better encounters than one long book, even for the same total reading time — because `%` is a function of progress through the current book, not of total time spent.
- Legendaries (144-146 the bird trio, 150 Mewtwo): require both accumulated `lifetimeMinutes` across all time **and** the current book's `bookProgressPercent` to clear their thresholds.
- Mew (151): only appears once all other 150 species have been caught.
- Gender is randomized according to the species' real `genderRate`.

**Rare items**
- Rolled every hour at 1/20 odds, with pity after 19 misses.
- Prioritizes whichever item type the Party currently needs to evolve.

**Evolution**
- On level-up, `evolutionsFor(speciesId)` (static data in `PokemonSpecies.cpp`) is checked; if `minimumLevel` is reached, an event is queued for confirmation (unless `RecordFlag::EvolutionPromptsDisabled`).
- Item-triggered evolution (`useEvolutionItem`) is resolved immediately, outside the per-minute loop.

## 4. Pending event queue

`PokemonState.pendingEvents`: a FIFO capped at `PENDING_EVENT_CAPACITY = 3`. Once full, new encounters/items are dropped, but the pity counters (`encounterMisses`/`itemMisses`) still keep incrementing. `DashboardNotice` shows a `!` icon while an event is waiting.

## 5. Storage — double-buffer + CRC

File: [src/pokemon/PokemonStore.cpp](../../src/pokemon/PokemonStore.cpp)

- Two alternating files, `/.crosspoint/pokemon-a.bin` / `pokemon-b.bin`. Each `commit()` writes to the **inactive** file, then swaps — a power loss mid-write still leaves the previous good copy intact.
- Snapshot = header (version, sequence, recordCount) + state + N records, all covered by one CRC32.
- `inspectSnapshot()` verifies at boot: CRC, `sequence` match, `recordId` values increasing contiguously, every record referenced by Party/pending-evolution actually existing.
- `validateRecord()`/`validateState()` (PokemonTypes.cpp) are invariants checked at **every** write site (candidate → validate → commit) — a defense against corruption from power loss mid-write.
- Older `pokemon-v2-a/b.bin` files migrate automatically to the current format.

## 6. Measured Flash/RAM budget (X3 = ESP32-C3, no PSRAM)

Measured via `pio run -e pokemon-x3` / `pio run -e default` (16MB flash, dual-OTA `partitions.csv`: each app slot is 6.25MB = 6,553,600 bytes):

| Build | Flash used | % | Free in OTA slot |
|---|---|---|---|
| `default` (no Pokémon) | 6,259,237 B | 95.5% | 280,208 B |
| `pokemon-x3` (with Pokémon) | 6,303,483 B | 96.2% | 235,968 B |

- **The Pokémon module currently only costs ~43KB of flash** — because artwork lives on the SD card (`Storage.openFileForRead`, path `/pokemon/sprites/%03u.bmp`...) rather than being bundled into the firmware image.
- Static RAM (`.data+.bss`) uses only **17.7%** (`57,852 / 327,680 bytes`) — not a bottleneck.
- **The real bottleneck is flash**: the base CrossInk build (without Pokémon) already occupies 95.5% of the OTA slot. Only about 230KB of headroom remains for new features.
- `scripts/check_firmware_size.py` (a post-build script in `platformio.ini`) automatically fails the build if `firmware.bin` exceeds the smallest "app" partition in `partitions.csv` — this is the official flash-overflow check, run on every `pio run`.
- Growing the available headroom would require changing the partition scheme (giving up the safe dual-OTA rollback) — a significant tradeoff, not to be done lightly.

## 7. Notes for extending the mechanics further (e.g. a battle system)

- A full graphical/animated system in the style of the original Pokémon Red games: high risk, easily exceeds the remaining ~230KB once move data + type chart + animation engine + new battle UI are added.
- A **text-based** approach (FIGHT/ITEM/PKMN/RUN menus + text log) is far more feasible: it reuses the existing `EpdFont`/`GfxRenderer`/`ButtonNavigator`, with no new animation engine required. Estimated additional cost is ~20-40KB of flash — well within the existing headroom.
- One thing to watch: multi-language (i18n) strings for move names/battle log lines can balloon quickly if multiplied across many languages from the start.
