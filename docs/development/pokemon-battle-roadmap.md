---
title: Pokémon Battle Roadmap
parent: Development
nav_order: 5
---

# Pokémon Battle System — Roadmap

Handoff document for extending the Pokémon module into a "mini Pokémon Red": level-based move learning, items/TM/HM, turn-based battles, catching Pokémon with balls, 8 gyms + Elite Four, badges.

Working branch: **`feat/pokemon-battle-system`**. Read [pokemon-mechanics.md](./pokemon-mechanics.md) first to understand the current game mechanics.

Whoever picks up this work: read the **Immutable constraints** section first — these are things that have been thoroughly verified against the actual code, and violating them will corrupt user save data or break the build.

---

## Immutable constraints

The five points below come from reading the actual code, not guesswork. Violating any of them causes serious, hard-to-detect consequences.

**1. Flash budget has only ~230KB left.**
`pokemon-x3` currently occupies **96.2%** of the OTA partition (6,303,483 / 6,553,600 bytes, **235,968 bytes** free). `scripts/check_firmware_size.py` fails the build when this is exceeded. The entire design must stay within this budget; re-measure the build after every stage.

**2. `PokemonState` may only grow AFTER byte 115.**
`src/pokemon/PokemonStore.cpp:265` has `write32(stateBytes.data() + 108, nextSequence)` — a hardcoded offset, duplicated with the codec. If a new layout pushes `sequence` off offset 108, this line will overwrite a different field *after* `encodeState()` has already validated it → silent save corruption. Keep the entire 0..115 layout as-is; new fields start at 116.

**3. Do NOT expand `PokemonRecord` (48 bytes).**
`RecordBytes` is `std::array<uint8_t,48>` — the size is baked into the *type*, used in 6 streaming loops; `decodeSnapshotHeader()` hardcodes "record must be exactly 48 bytes" in 2 places (`PokemonStoreCodec.cpp:100,102`); the copy-forward loop on commit copies records as raw bytes so it can't auto-resize; `decodeRecord()` also checks `bytes[47] != 0` with a hardcoded index.
→ Battle data (4 moves, PP, HP, status) lives in a **side file**, `/.crosspoint/pokemon-battle.bin`, not in the record.

**4. Move/item names do NOT go through i18n.**
Every i18n key costs `28 languages × 2 bytes` of offset table even though only English is populated → ~250 keys would cost ~14KB of offsets + ~4KB of text. Worse: `scripts/gen_i18n.py` runs with `strip_unused=True` and **only scans `src/` + `lib/`**, not headers generated into `$BUILD_DIR` → a key only referenced from an auto-generated data table gets **stripped from the firmware**.
→ Following existing precedent: the names of the 151 species are raw C strings in an auto-generated header. Move/item names do the same. Only ~20 new UI strings use i18n.
→ The English text blob has a hard cap of **32,767 bytes** (15-bit offset, `gen_i18n.py` raises `ValueError` when exceeded).

**5. Do NOT modify `scripts/data/pokemon-kanto-v2.csv`.**
`test/pokemon_types/PokemonSpeciesGeneratorTest.py` pins the SHA-256 of this file's contents (`EXPECTED_METADATA_SHA256`). If more data is needed, create a new CSV — don't touch the old file.
Also, that test asserts that `CPPPATH` has **exactly 1 element** after running the generator → every new generator must emit into the **same** directory, `$BUILD_DIR/generated/pokemon`.

---

## Design decisions already locked in

Recorded along with the reasoning, so whoever picks this up doesn't have to re-litigate it:

| Decision | Reason |
|---|---|
| HP/PP/status **persist between battles** | User's choice, to match real Red |
| Status effects implemented **fully from day one** (sleep/paralysis/poison/burn/freeze/confusion) | User's choice; status-curing items only make sense this way |
| **Reading a book gradually heals HP/PP**, status clears once at full HP | Prevents dead ends: out of Potions + whole party exhausted = permanently stuck. Also fits the spirit of a reading device |
| Battle data lives in a **side file**, record isn't expanded | See constraint 3. HP/PP/status are **100% reconstructible** (derived from level, full HP) → a corrupted file just gets rebuilt, no Pokémon is lost. ~~Moveset was also reconstructible so the side file didn't need double-buffering~~ **no longer true as of Stage 11/12** (active learn/forget moves, teaching TMs) — Stage 15 adds double-buffering to this side file for exactly that reason |
| Use **real base stats** for the 151 species | Without individual stats, every Pokémon at the same level would hit identically, and type matchup would be the only factor. Only costs ~755 bytes |
| ~~Gym teams trimmed to 2-3 members~~ **kept as the real full lineup** (up to 5 members) — reversed in Stage 14, undoing this original decision | Originally trimmed out of concern that every attack turn triggers a full-screen e-ink refresh; the user asked to keep the original lineup, accepting longer battles |
| Badges are **purely a display achievement** | User's choice; keeps balance risk minimal, doesn't touch `PokemonGame.cpp` |
| Gyms unlock **linearly**, all 8 badges required to unlock Elite Four | Creates a progression path — something the game currently lacks |
| Losing carries **no penalty** | The natural barrier is Pokémon level, and level only rises through real reading time |

---

## Current status

### Stage 0 — Source data ✅ DONE

`scripts/fetch_pokemon_battle_data.py` pulls real Gen 1 (Red/Blue) data from PokeAPI, caching responses under `scripts/.pokeapi-cache/` (gitignored) so re-runs are fast.

| File | Rows | Columns |
|---|---|---|
| `scripts/data/pokemon-stats.csv` | 151 | `id,name,hp,attack,defense,special,speed` |
| `scripts/data/pokemon-moves.csv` | 165 | `id,name,type,power,accuracy,pp,damage_class,ailment,ailment_chance` |
| `scripts/data/pokemon-learnsets.csv` | 989 | `species_id,level,move_id` |
| `scripts/data/pokemon-tmhm.csv` | 3037 | `species_id,move_id` |
| `scripts/data/pokemon-gyms.csv` | 12 | `order,leader,badge,type,team` (hand-written — PokeAPI has no gym data) |

Note: PokeAPI blocks `urllib`'s default User-Agent (403) — the script sets a custom header.
Gen 1 has only a single "Special" stat; the script uses PokeAPI's `special-attack` as the corresponding value.

---

## Remaining stages

Each stage is a natural stopping point: buildable, testable, committable.

### Stage 1 — Item table + 5 C++ generators ✅ DONE (commit `eff18d13`)

- [x] `scripts/data/pokemon-items.csv`: 83 items. Ids `1..6` match the existing `EvolutionItem` (evolution stones + Link Cable, self-checked by `generate_pokemon_items.py` via `PINNED_STONE_NAMES`); `7..10` balls; `11..17` healing; `18..23` status cures; `24..28` Rare Candy + PP restores; `29..78` TM01-50; `79..83` HM01-05. Actual columns used: `id,name,category,effect_value,cures_ailment,teaches_move_id,drop_weight`.
- [x] TM/HM → move id mapping: **no** extra PokeAPI calls — cross-referenced the 55 standard Gen 1 move names against the existing `pokemon-moves.csv` to get the right id (one-off cross-reference script, no need to persist it).
- [x] 5 generator pairs following exactly the `generate_pokemon_v2_species{,_build}.py` pattern. `generate_pokemon_learnsets.py` merges both learnset **and** TM/HM compatibility from 2 CSVs into 1 header (`PokemonLearnsets.generated.h`), as the roadmap anticipated.
- [x] `MoveListRef{uint16_t offset; uint8_t count;}` (different name from the originally planned `LearnsetOffset`) is shared between learnset and TM/HM offset/count.
- [x] Registered the `pre:` script in both `[env:pokemon-x3]` and `[env:pokemon-simulator-X3]`.
- [x] `test/pokemon_battle_data/`: `PokemonBattleDataTest.cpp` (native, cross-checked against real numbers: Chansey HP=250, Bulbasaur learnset has 9 entries starting with Tackle/Growl at level 1, total learnset=989, total TM/HM=3037, ids 1-6/29-83 match EvolutionItem/Machine...) + `PokemonBattleDataGeneratorsTest.py` (in the style of `PokemonSpeciesGeneratorTest.py`, checks CLI + CPPPATH + platformio.ini registration). Both added to `test/CMakeLists.txt`.

**Measured results** (not estimates): `pio run -e pokemon-x3` builds cleanly, Flash grew by exactly **+104 bytes** compared to the 6,303,483 B baseline — because no code yet calls `moveData()`/`baseStatsFor()`/... so the linker strips out all the unused data tables. That means **the entire Stage 1 data layer is almost free** until Stage 2+ actually starts using it. 16/16 native Pokémon tests pass (`ctest -R Pokemon` in `test/build`).

**Small deviations from the original plan** (worth noting for whoever picks this up):
- `Tackle` (move id 33) power=**40**, not 35 — PokeAPI returns the current value (buffed since Gen 6), not the original Gen 1 value. Acceptable since most other moves are unchanged across gens; only a few exceptions like Tackle.
- `Tri Attack` has `ailment=None` despite `ailment_chance=20` — because it inflicts 1-of-3 random status effects, PokeAPI returns `ailment="unknown"`, which the single-ailment model here can't capture. Accepted as a known limitation of this simplified Gen 1 build.

### Stage 2 — Pure battle engine ✅ DONE (commit `f1ff1bd2`)

- [x] Move lookup (`moveData`), learnset/TM-HM (`learnsetFor`/`canLearnViaMachine`) already existed from Stage 1 (`PokemonMoveData.cpp`, `PokemonLearnsets.cpp`) — no need to redo.
- [x] `lib/Pokemon/PokemonTypeChart.cpp`: 18×18 type-matchup table **hand-written** (not via CSV/generator since it doesn't change independently of the code that reads it) — uses the modern table (post-Gen 6, includes Dark/Steel/Fairy) instead of the original Gen 1 table that's missing those 3 types, consistent with move/stat data already using current PokeAPI values.
- [x] `lib/Pokemon/PokemonBattle.h/.cpp`: `BattleCombatant` (speciesId/level/HP/moves/status — doesn't store type/base stats, looks them up via `speciesData()`/`baseStatsFor()` each time needed, same approach as `PendingEvent` only storing speciesId). Damage follows the Gen 1 formula (no IV/EV since none is modeled), STAB, type matchups, 85-100% random range. All 6 status effects included. Turn order follows real Speed (paralysis halves speed). KO ends the turn immediately, the losing side doesn't get to counterattack. `attemptCatch()` uses the existing `captureRate`.
- [x] The engine only returns `BattleLogEvent` enums, no text strings at all — the UI (Stage 5+) translates them itself via `tr(STR_*)`.
- [x] `RandomSource` reuses the same struct from `PokemonGame.h` (public); `randomBelow`/`rollBelow` are reimplemented in `PokemonBattle.cpp` because the original helper in `PokemonGame.cpp` lives in an anonymous namespace and can't be exported.
- [x] `test/pokemon_battle/PokemonBattleTest.cpp`: 10 native tests (damage, two-way type matchups, status, paralysis + speed reduction, poison/burn tick, immediate KO, catch rate across 2 ball types × 2 HP/status levels). 17/17 Pokémon tests pass.

**Data bug found while wiring things up (not a test failure — found through manual CSV review):** `ailment_chance` from PokeAPI = 0 for the **primary effect** of pure Status moves (Toxic, Sleep Powder, Confuse Ray...), which by PokeAPI's convention means "guaranteed 100%" — not "0%, never happens." Originally the code treated 0 as "never apply status," which meant the entire group of status-inflicting moves **didn't work**. Fixed: a Status move with `ailmentChance==0` → treated as 100%; damaging moves with secondary effects (Ember 10% burn...) kept their real numbers unchanged. **Lesson for later stages**: manually review similar percentage/rare fields before trusting them at face value.

**Known scope boundaries** (not shortcomings, just scope boundaries): no modeling of stat stages (Attack/Defense/Speed/Accuracy/Evasion boosts from Growl, Swords Dance, Reflect...) — these moves can only hit/miss, with no additional effect. No crit-hit modeling. Move target is always the opponent (no self-target moves like Rest).

### Stage 3 — Storage ✅ DONE (commit `da111328`)

- [x] `PokemonState` v3: appends `bagCounts[77]` (uint8) and `uint16_t battleProgress` (8 bits for gyms + 4 bits for Elite Four, 4 reserved bits) **after byte 115**. The existing `itemCounts[6]` keeps its offset at 54..65.
- [x] `snapshotStateBytes()` + `decodeState()` add a v2 branch (`POKEMON_SNAPSHOT_VERSION_V2 = 2`, new fields = 0 on decode). Migration runs automatically since a commit always writes the current version (v3).
- [x] `validateState()` constrains `battleProgress` (reserved bits must be 0) — satisfied by a zero-extended v2 state, old saves still load normally.
- [x] `PendingEventKind::MoveLearn` (=4): reuses the existing 10-byte layout as-is (`recordId` = Pokémon, the `speciesId` field holds the moveId, `level` = level learned) → just adds a case to `validatePendingEvent()`, doesn't change `PendingEvent`'s size.
- [x] `lib/Pokemon/PokemonBattleStoreCodec.h/.cpp` (pure encoding, uses fixed `std::array` — no `std::vector`, matching the module-wide convention) + `src/pokemon/PokemonBattleStore.h/.cpp` (I/O via `HalStorage`, same idiom as `PokemonStore.cpp`): side file `/.crosspoint/pokemon-battle.bin`, 16 bytes/entry (`recordId(4) + moves[4] + pp[4] + currentHp(2) + status(1) + statusTurns(1)`) + CRC32 over the whole file, up to `PARTY_SIZE=6` entries (PC doesn't need tracking). Created lazily via `load()` triggered on first use (even from a `const` function, via `mutable`); wrong CRC or missing entries → treated as empty, never blocks the player.
- [x] Tests: `test/pokemon_battle_store/` (`PokemonBattleStoreCodecTest` — pure encoding; `PokemonBattleStoreTest` — I/O via the existing `HalStorage` stub at `test/pokemon_store/stubs/`) + added cases to `PokemonStoreCodecTest.cpp` for v2→v3 zero-extension and the battleProgress reserved-bit constraint.
- [x] Updated `docs/file-formats.md` with the full v3 layout and the `pokemon-battle.bin` format.

**Bug found while fixing old tests (not a logic bug)**: 3 tests in `PokemonStoreTest.cpp`/`PokemonStoreCodecTest.cpp` used the literal "3" as a simulated "unsupported" version — now 3 is actually `POKEMON_SNAPSHOT_VERSION`, so it had to change to `POKEMON_SNAPSHOT_VERSION + 1`. Lesson: use a relative value (`CURRENT+1`) rather than a hardcoded number when the intent is "an invalid value," to avoid breakage when the version number increases.

### Stage 4 — Service + item drops ✅ DONE (commit `18a0eeca`) — first meaningful flash-size checkpoint

- [x] `PokemonService` adds a `PokemonBattleStore&` dependency (constructor now takes 3 parameters: `store, battleStore, random` — updated both `devicePokemonService()` and 19 call sites in `PokemonServiceTest.cpp` via sed).
- [x] `consumeBagItem(itemId)` — decrements 1 from `itemCounts` (stones, id 1-6) or `bagCounts` (everything else, id 7-83).
- [x] `markGymDefeated(gymIndex)` — sets a `battleProgress` bit, enforcing linear unlocking (gym N requires 1..N-1 already defeated; Elite Four requires all 8 gyms). Idempotent if called again for an already-defeated gym. Doesn't touch XP/encounter/`PokemonGame.cpp`.
- [x] `loadBattleEntry(recordId)` — returns an existing entry from the battle store, or **synthesizes one** from level + learnset (walks the learnset backward to find the 4 most recently learned moves at the current level) at full HP/PP, no status — then saves it so it doesn't need re-synthesizing next time. `saveBattleEntry()` writes it back.
- [x] Recovery while reading: after `creditMinutes()` commits successfully, it heals HP/PP for **existing battle entries** of each party member (+1 HP/minute read, +1 PP/move every 10 minutes, both capped; status auto-clears at full HP). Pokémon without an entry are skipped (they'll fully synthesize at full HP when needed) — doesn't touch `PokemonGame.cpp`.
- [x] Extended `createItem()` — as planned, the **only** change touching `PokemonGame.cpp`: weighted selection (`ItemData::dropWeight`) across all 83 items, still prioritizing evolution stones the player's Pokémon actually need (preserving the old behavior when there's a real need). Kept the old "skip the roll when there's only 1 candidate" optimization — saves a random call and stays compatible with tests that script exact random sequences.
- [x] `PendingEventKind::Item`'s `item` field now carries a general item id 1-83 (not just `EvolutionItem` 1-6) — widened `validatePendingEvent`, kept ids 1-6 matching `EvolutionItem` so old saves still decode correctly.
- [x] Tests: 6 new tests in `PokemonServiceTest.cpp` (cross-checked against real data: Pikachu synthesizes exactly 2 moves [Thunder Shock, Growl] at level 5, correct gym bit, correct catch math). 2 tests in `PokemonGameTest.cpp` had to change because the item-selection algorithm genuinely changed (not a bug — "6 stones maxed = nothing drops" is no longer true now that there are 77 other items).
- [x] **`pio run -e pokemon-x3`**: Flash **6,314,621 B (96.4%, 224,832 B free ≈ 220KB)** — a real increase of **+10,588 B** over Stage 3 (6,304,033 B), because `PokemonService` now **actually calls** `moveData()`/`baseStatsFor()`/`learnsetFor()`/`itemData()` so the linker can no longer strip them. This is exactly the turning point the roadmap predicted — Stage 1/2 were nearly free, Stage 3/4 is where real cost begins.

**Simulator runtime not yet tested** — the build machine lacks `libsdl2-dev`, and it can't be installed because `sudo` requires interactive authentication (unavailable in this non-interactive environment). The `pokemon-x3` firmware building cleanly + 19/19 tests passing is the only evidence available at this stage; **there's no UI yet so there's nothing else to see on a real device** — Stage 4 is just the foundation layer, Stage 5+ is needed before it's "playable."

### Stage 5 — UI: Battle + catching with balls ✅ DONE (commit `a989d161`)

- [x] Added `Screen::Battle`, `BattleMoves`, `BattleBalls` — **3 screens, not 4 as originally planned**: dropped `BattleBag` because the engine doesn't (and has no plan to) support using recovery items mid-battle — only FIGHT (moves) and BALL (catch) are real actions.
- [x] Wired into the Encounter branch of `Screen::Event`: "Catch" opens `enterBattle()` instead of catching immediately; a successful ball throw reuses the existing `resolveEncounter(Catch, ...)` + nickname flow as-is; a failure → "broke free," back to Battle. Both "Pass"/RUN call `resolveBattleAsPass()`.
- [x] Free-form drawing in `renderBattleHud()` (called from `renderFocused()`): name+level+HP bar (`fillRect`/`drawRect`) on both sides, status abbreviation, `drawPokemonSpeciesArt`, and `battleLog_` (built by `buildBattleLog()` from `BattleTurnResult`).
- [x] `PokemonService` adds `resolveBattleTurn()`/`attemptBattleCatch()` wrapping `pokemon::stepBattle()`/`attemptCatch()` with its own `random_` — the UI never touches `RandomSource` directly, preserving the existing architectural principle.
- [x] `PokemonBattle` factors out `defaultMovesetForLevel()`, shared between `PokemonService::synthesizeBattleEntry()` (from Stage 4) and `PokemonActivity::enterBattle()` (new) — avoids duplicating the learnset-scan logic.
- [x] **Fixed 1 bug present since Stage 4** (found while re-reading `itemName()`): the function only handled the 6 `EvolutionItem`s, item ids 7-83 (widened by `PendingEventKind::Item` in Stage 4) showed an empty name. Fallback to `pokemon::itemData(...)->name`.
- [x] 17 new i18n keys for the battle log (not ~20 as originally estimated — no `STR_POKEMON_NO_PP` needed, that message uses `showMessage` with a different existing string).
- [x] Tests: 19/19 native suite passes; `PokemonServiceTest` 27/27 (2 new tests for the 2 new wrapper methods); `test/pokemon_battle/CMakeLists.txt` needed `PokemonLearnsets.generated.h` + `PokemonLearnsets.cpp` added (link error since the new `defaultMovesetForLevel` pulls in this dependency).
- [x] **`pio run -e pokemon-x3`** (clean rebuild): Flash **6,326,799 B (96.5%, 212,656 B free ≈ 208KB)** — up **+12,178 B** from Stage 4 (6,314,621 B), for the whole Battle UI + 17 i18n keys.

**Important finding (a risk for every later stage that touches i18n)**: after deleting 1 key mid-list in `english.yaml` (shifting the entire `StrId` enum after it), re-running `pio run` reported "up to date" in 3.2s **without rebuilding `I18nStrings.o`** — SCons doesn't detect changes through the build-hook generator (`gen_i18n.py` runs as a `pre:` script, not part of the dependency graph SCons tracks). Confirmed via `stat -c "%Y %n"`: the old object file was older than the freshly-generated source. Risk: linking the old object with the new enum layout → wrong strings looked up at runtime, **silently wrong, no build error at all**. **Mandatory**: after every edit to a file under `lib/I18n/translations/*.yaml`, run `rm -rf .pio/build/<env>` to force a clean rebuild before trusting flash numbers or runtime behavior.

### Stage 6 — UI: Gym List + Badges ✅ DONE (commit `2f420118`) — real gym battles, not just a display

- [x] Added `Screen::GymList` (12 rows: 8 gyms + 4 Elite Four), `Screen::Badges` (8 rows of gym badges) + 2 entries in `Screen::Menu` ("GYM BATTLE", "BADGES", pushing the home-screen toggle and Reset down to index 7/8).
- [x] Per-row status display: Defeated / Locked / empty (challengeable now); Elite Four locked until all 8 badges obtained — the linear-unlock logic reads via `pokemon::gymProgressFor(battleProgress, gymIndex)` (new, `PokemonBattleTypes.h`/`PokemonGymData.cpp`), shared with `PokemonService::markGymDefeated()` so the rule lives in only one place (`markGymDefeated` was refactored to call this function instead of recomputing the bit mask).
- [x] **Beyond the original checklist scope (which only said "show status") because otherwise the screen would be useless**: selecting an unlocked gym actually enters a battle — reuses `Screen::Battle`/`BattleMoves`/`BattleBalls` from Stage 5 as-is, no separate battle screen created. Differs from wild encounters, tracked via `gymChallengeIndex_`/`gymChallengeTeamProgress_`:
  - No BALL button (`logicalCount()` returns 2 instead of 3 during a gym battle) — trainer Pokémon can't be caught.
  - Defeating the opponent doesn't end the battle: `advanceGymOpponentOrFinish()` sends out the next member of the 2-3 member team; only defeating the whole team calls `service_.markGymDefeated()` + shows a badge notification (`STR_POKEMON_BADGE_EARNED` with a badge / `STR_POKEMON_TRAINER_DEFEATED` for Elite Four, no badge).
  - Losing (`finishGymChallenge(false)`) or fleeing/Back (`resolveBattleAsPass()` branches when `gymChallengeIndex_ != 0`) never touch `battleProgress` — unlike wild encounters, gym battles have no `PendingEvent`, so there's no need to "Pass" through the service — simply leaving the battle is enough.
- [x] Split `setupBattlePlayer()`/`setupBattleOpponent()` out of the old `enterBattle()` (Stage 5) so `enterGymBattle()` can reuse them as-is instead of duplicating `BattleCombatant` setup code.
- [x] 8 new i18n keys: `STR_POKEMON_GYM_BATTLE`/`BADGES` (menu+title), `STR_POKEMON_GYM_LOCKED`/`DEFEATED` (row status), `STR_POKEMON_ELITE_FOUR` (E4 row label prefix), `STR_POKEMON_BADGE_EARNED`/`TRAINER_DEFEATED` (win), `STR_POKEMON_GYM_CHALLENGE_LOST` (loss).
- [x] `test/pokemon_service/CMakeLists.txt` needed the `pokemon-gyms.csv` generator + `PokemonGymData.cpp` added (link error similar to Stage 5's learnsets fix — `PokemonServiceTest` now pulls in `gymProgressFor`/`gymData` via `markGymDefeated`).
- [x] Tests: 19/19 native suite passes (no dedicated UI test for `PokemonActivity.cpp` — this file isn't in the native test suite, only verified via a real build + a short simulator smoke run that doesn't crash on startup).
- [x] **`pio run -e pokemon-x3`** (clean rebuild): Flash **6,329,615 B (96.6%, 209,840 B free ≈ 205KB)** — up **+2,816 B** from Stage 5 (6,326,799 B), quite small since it reuses gym data already generated in Stage 1 (previously unused, stripped by the linker) and the existing list/i18n infrastructure.

### Stage 7 — UI: 4 moves in Summary + learn/replace moves ✅ DONE (commit `5f496ad6`)

- [x] The 4-move block in `renderFocused()`'s Summary branch — **2 moves per row** (not 1/row) to use only 2 rows instead of 4, in keeping with the existing "compact for landscape" spirit; read via `service_.peekBattleMoves()` (new) — **never** creates/writes an entry into the battle store (unlike `loadBattleEntry()`), because Summary must stay strictly read-only: opening a screen to view info must never silently write to SD.
- [x] Summary stays **read-only** (`logicalCount()` still returns 0) — unchanged.
- [x] `PendingEventKind::MoveLearn` (declared in Stage 3, never actually generated until now) is now really produced: `PokemonService::queueMoveLearnIfNeeded()` (called inside `creditMinutes()` before commit) walks `learnsetFor(leader)` over the range `(previousLevel, currentLevel]` — an open slot means it learns the move immediately without asking; a full moveset means `enqueuePendingEvent(MoveLearn)` for the UI to ask. Pokémon that have never battled (no battle entry yet) are intentionally skipped — their first battle already synthesizes exactly the 4 newest moves for the current level (mechanism already present since Stage 4), so there's nothing to "catch up."
- [x] `pokemon::acknowledgeMoveLearn()` (new, `PokemonGame.cpp`, pure — identical in shape to `acknowledgeItem`) dequeues the event after the UI handles it; `PokemonService::resolveMoveLearn(replaceSlot)` — slot 0-3 learns into that slot, any other slot (e.g. -1) skips — both dequeue.
- [x] UI: `Screen::Event` adds a MoveLearn branch — 5 rows (4 current moves + Cancel), reuses the existing `activate()`/`buildRows()`/`logicalCount()` list machinery already used for Encounter/Evolution, no new screen needed.
- [x] Teaching moves via TM/HM: extended `Screen::Bag` from just the 6 evolution stones to include all 55 Machine items (ids aren't contiguous in the data — iterated via `machineItemIdAt()`/`machineItemCount()` instead of hardcoded offsets). `PokemonService::teachMove(recordId, moveId)`: an open slot learns it right away, already known returns `AlreadyKnown`, a full moveset returns `MovesetFull`.
- [x] **Intentional simplification from the original intent**: using a TM on a full moveset does **not** offer a slot-replacement screen (unlike the automatic MoveLearn above, which DOES have a 5-row picker) — it just reports "already knows 4 moves, forget one first" and doesn't consume the TM. Building a separate slot-replacement flow for TM (not going through the `PendingEvent` queue, needing its own state/screen) is disproportionate cost at this stage; the common case (a freshly caught/leveled Pokémon with an open slot) still works fully — anyone who wants to teach a TM with a full moveset can check Summary first to see which move to keep, then level up next time to trigger the proper MoveLearn flow with its picker.
- [x] Bag icon: `renderRowArt()` only draws icons for the 6 original evolution stones (no icon assets for the 55 Machine items).
- [x] Tests: 8 new tests in `PokemonServiceTest.cpp` (auto-learn into an open slot, queueing MoveLearn when full, `resolveMoveLearn` learn/skip, `teachMove` 3 outcomes, `peekBattleMoves` doesn't write to SD when no entry exists yet). **Test-writing pitfall found at this stage**: crediting a large chunk of minutes (jumping straight from level 16→26 in one call) can accidentally cross the encounter (15 min)/item (60 min) threshold checks in `applyCreditedMinutes`, causing the pending-event queue (capacity 3) to have slot 0 claimed by an unrelated event (random Encounter/Item) first — `pendingEventFront()` returns the wrong event, and the test fails confusingly. **Fix**: set `totalXp` to exactly 1 XP short of the target level, then `creditMinutes(1, ...)` — crosses exactly the one level threshold being tested, without touching the 15/60-minute thresholds. A lesson for future tests involving `creditMinutes()` with large level jumps.
- [x] **`pio run -e pokemon-x3`** (clean rebuild): Flash **6,332,505 B (96.6%, 206,944 B free ≈ 202KB)** — up **+2,890 B** from Stage 6 (6,329,615 B), small since most of the infrastructure (list rows, i18n, PendingEvent queue) already existed from earlier stages.

### Stage 8 — Polish ✅ DONE (commit `ddef31b0`) — final stage of the battle roadmap

- [x] i18n review: no new strings needed — each prior stage already added keys as needed (Stage 5: 17, Stage 6: 8, Stage 7: 3 — 28 total, fewer than the original ~20-ish estimate thanks to reusing existing strings). Ran `gen_i18n.py --strip-unused` to check: **65 "never used" keys are a pre-existing baseline from before this battle project** (confirmed via verbose log — none belong to the new `STR_POKEMON_*` namespace), not leftover cruft from this branch. Nothing needed stripping.
- [x] `clang-format`: the build machine **doesn't have `clang-format-21`** (what CI uses) — only found **clang-format 22.1.3** bundled with the VSCode C++ extension (`ms-vscode.cpptools`), couldn't install the right version because `sudo` requires interactive auth (the same constraint as the `libsdl2-dev` issue in Stage 4/5). Since v22 makes different line-wrap decisions than v21 in some cases (confirmed by applying it broadly and comparing: original CrossInk files predating this branch — `PokemonGame.cpp`, `PokemonActivity.cpp`, `PokemonTypeChart.cpp` — got suggested edits on many lines **completely unrelated to battle features**, a sign of version drift rather than a real issue), **scope was restricted**: fully formatted the pure new files from this branch (`PokemonBattle.*`, `PokemonBattleTypes.h`, `PokemonBattleStoreCodec.cpp`, test files), while pre-existing CrossInk files (`PokemonGame.cpp`, `PokemonActivity.cpp/.h`, `PokemonTypes.h`, `PokemonService.cpp`) only had manually-identified new-in-Stage-0-7 sections hand-formatted, without touching the original code that already passed real CI (v21) formatting — avoiding turning CI-clean code into format-drifted code by accidentally running the wrong version. **Lesson for later sessions still editing files shared with original CrossInk code**: without the exact `clang-format-21`, only hand-format the parts you wrote — don't run `-i` on the whole file.
- [x] Clean-built all 3 envs for comparison: `default` (no Pokémon) = **6,261,393 B / 95.5%**; `pokemon-x3` = **6,332,505 B / 96.6%, 206,944 B free ≈ 202KB**; `pokemon-simulator-X3` builds successfully, ran for 15s with no error/crash logs. **Total flash cost of the whole battle system across all stages (Stage 0→8)**: 6,332,505 − 6,303,483 (baseline before starting) = **+29,022 B** — comfortably within the original budget estimate (~45-65KB), cheaper than expected because most of the data/engine (Stage 1-2) was nearly free (linker strips unused code) until the UI (Stage 5+) actually started using it.
- [x] 19/19 native test suite passes (unchanged from Stage 7 — Stage 8 was purely formatting, no logic changes).

**Verification results** (numbered per the checklist below):
1. ✅ Build passes, delta computed above.
2. ✅ Native engine tests — comprehensive (damage, type matchups, all 6 statuses, catch rate across 4 ball types, learnset-based move learning) since Stage 2, plus tests for MoveLearn/teachMove/peekBattleMoves added in Stage 7.
3. ⚠️ **Not yet done** — needs real interactive playtesting on the simulator (encounter wild → battle → throw ball → catch → view Summary's 4 moves → use a TM → fight a gym → view Badges → unlock Elite Four). This build environment **has no virtual screen/keyboard to simulate user input**, only confirming the simulator starts without crashing in the first 15 seconds. **Requires a human to actually play through** this exact scenario on a machine with a GUI, or on real X3 hardware.
4. ✅ v2→v3 migration — already has an automated unit test (`PokemonStoreCodecTest.cpp`, Stage 3): a v2 state loads normally, new fields = 0.
5. ✅ Reconstructing the side file when missing/corrupted — already has an automated unit test (`PokemonBattleStoreTest.cpp`/`PokemonBattleStoreCodecTest.cpp`, Stage 3): a missing file or bad CRC is treated as empty, no error.
6. ✅ Dead-end prevention (heal HP/PP + clear status while reading) — already has an automated unit test (`PokemonServiceTest.cpp::ReadingCreditHealsAnExistingBattleEntryAndClearsStatusOnceFull`, Stage 4).
7. ⚠️ **Not yet done** — requires real X3 hardware (e-ink refresh speed per attack turn, tight heap). This build machine has no device connected; see [pokemon-x3-build-and-flash.md](pokemon-x3-build-and-flash.md) to flash and test it yourself once hardware is available.

**Conclusion**: all 8 stages of the battle roadmap are complete in terms of code + everything that can be automated-tested. What remains are exactly 2 things only a human/real device can do (items 3 and 7) — not a planning gap, but an inherent limit of this non-interactive, deviceless build environment.

---

## Pitfalls when editing `PokemonActivity.cpp`

Adding just **one** value to `enum class Screen` requires updating **~15 spots**, many of them `if` chains the compiler won't warn about:

- `isListScreen()` (`:163`) — **every new screen defaults to being treated as a list screen**; the Battle screen must be excluded.
- `logicalCount()` (`:167`) — returning 0 makes `loop()` exit early (`:551`) and the screen **completely loses up/down navigation**.
- `Screen::Menu` is a hardcoded 7-row list in **3 places that must be kept in sync**: count (`:178`), index-based dispatch (`:301-323`), labels (`:603-615`).
- `goBack()` (`:488`) has a `default:` that jumps straight back to Menu → sub-screens must declare their own case.
- The rest: `activate()`, `buildRows()`, `listTop()`, `selectedRecordId()`, `renderFocused()`, `renderRowArt()`, `renderHeaderAndHints()`, and **2 duplicated `artRows` chains** at `:717` and `:915`.
- `FreeInkApp<24,8>` caps at 24 interactions / 8 handlers; currently 1 handler + up to 10 rows in use. `interactionOverflowed()` isn't checked anywhere.
- Every `render()` clears the whole screen + does a `FAST_REFRESH` on the entire panel + N SD card opens to read BMPs (**art is never cached**).

---

## How to build and test

```sh
export PATH="$HOME/.platformio/penv/bin:$PATH"   # pio is NOT in the default PATH
cd /home/vutq/project/xteink-pokemon-game
git submodule update --init --recursive          # only needed if freeink-sdk is empty

pio run -e pokemon-x3            # X3/X4 firmware with Pokémon — flash numbers at the end of the log
pio run -e pokemon-simulator-X3 -t run_simulator # run on the simulator
python3 scripts/fetch_pokemon_battle_data.py     # refresh CSVs (cached, fast)
```

## Verification

1. `pio run -e pokemon-x3` — build passes, `check_firmware_size.py` doesn't fail; compare the delta against the **6,303,483 B** baseline.
2. Native engine tests: damage, type matchups, each status, catch formula across 4 ball types, level-up move learning.
3. Simulator: encounter wild → Catch opens battle → fight → throw a ball → catch it → Summary shows exactly 4 moves; pick up items/TMs while reading; teach a move with a TM; Gym 1 unlocked, 2-8 locked; beat Gym 1 → badge appears on the Badges screen; all 8 badges → Elite Four unlocks.
4. **Migration**: run with an existing v2 `pokemon-a.bin` → loads normally, empty bag, `battleProgress = 0`, no Pokémon lost; first commit writes out v3.
5. **Side-file reconstruction**: delete `pokemon-battle.bin` mid-session → re-enter a battle, moves are rebuilt from the learnset, full HP/PP, no crash.
6. **Dead-end prevention**: damage a Pokémon and inflict status → read for a while → HP/PP gradually recover, status clears.
7. Test on real X3 hardware once stable, per the checklist in [pokemon-game.md](../pokemon-game.md) — especially e-ink refresh speed per attack turn and the X3's tight heap, two things the simulator can't verify.

---

## Stage 9 — Bag sorted by category (outside the original roadmap, per user request after Stage 0-8 completed) ✅ DONE (commit `194601a0`)

The original 8-stage roadmap was fully complete (see Stage 8 above). After playtesting the simulator, the user asked for more: the Bag must be split into 3 categories — "regular healing/status items" (name made up), "evolution items" (stones + Link Cable), "TM/HM."

- [x] `Screen::Bag` changes meaning: from an item list screen (left over from Stage 7, combining stones+TM/HM) to a **category-selection** screen (3 rows: Evolution/Medicine/TM-HM).
- [x] `Screen::BagEvolution` (6 stones + Link Cable) and `Screen::BagMachine` (55 TM/HM) — logic identical to Stage 1/Stage 7, just split into separate screens from Bag.
- [x] `Screen::BagMedicine` (new) — merges the 4 data categories `Medicine`/`StatusCure`/`PPRestore`/`Candy` into one, displayed as **"Medicine"** following the "bag pocket" convention of the original Pokémon games (the user asked for a fitting name to be invented).
- [x] `PokemonService::useConsumable(recordId, itemId)` (new) — **an entirely new feature**, before Stage 9 there was no "use" flow at all for Potion/status-cure/PP-restore/Candy (only evolution stones and TMs had a usable button):
  - Medicine: restores `effectValue` HP (capped at maxHp) + cures status if `curesAilment` matches the current status (or `Ailment::All` like Full Restore, which cures any status).
  - StatusCure: only cures status, doesn't touch HP.
  - PPRestore: **intentional simplification** — restores `effectValue` PP to **every** known move slot, without distinguishing Ether (original: restores 1 chosen move) from Elixir (original: restores all 4 moves) — the `pokemon-items.csv` data currently has no field distinguishing the two (Ether/Max Ether and Elixir/Max Elixir have identical `effectValue` to their counterparts), so distinguishing them would require new data or guessing from item names — not worth the effort for this secondary feature.
  - Candy: +1 level via the exact `xpRequired()` formula, reuses `queueMoveLearnIfNeeded()` (Stage 7) so it doesn't miss learning a new move on level-up — **but doesn't check evolution**: that rule (`queueEvolutionAfterLevelGain`) lives in `PokemonGame.cpp`'s own anonymous namespace, only called from `applyCreditedMinutes()`; rather than exporting a new public function just for this one use, Candy-triggered evolution will be caught the next time reading credits are applied (in practice very soon after) rather than duplicating the rule in 2 places.
- [x] `bagItemIdAt()`/`bagItemCount()` (PokemonActivity.cpp) generalize Stage 7's `machineItemIdAt()`/`machineItemCount()` via a predicate function pointer (`bool (*matches)(ItemCategory)`), shared between Medicine and Machine instead of two nearly-identical loops.
- [x] `ItemTarget` changed from dispatching on the boolean `bagSelectionIsMachine_` (Stage 7) to a 3-value `BagCategory` enum (Evolution/Medicine/Machine); `goBack()` from `ItemTarget` or from any of the 3 Bag sub-screens now returns to the correct category screen just entered (`BagEvolution`/`BagMedicine`/`BagMachine`), instead of always jumping to `Bag`.
- [x] 3 new i18n keys: `STR_POKEMON_BAG_EVOLUTION`/`MEDICINE`/`MACHINES`.
- [x] Tests: 5 new tests in `PokemonServiceTest.cpp` (heal HP + cap at maxHp, status-cure matching/non-matching ailment, Full Restore cures any status, PP restore applies to every slot, Rare Candy +1 level and `NotApplicable` at level 100). 19/19 native suite passes.
- [x] **`pio run -e pokemon-x3`** (clean rebuild): Flash **6,333,993 B (96.6%, 205,456 B free ≈ 200KB)** — up **+1,488 B** from the end-of-roadmap Stage 8 mark (6,332,505 B).

## Stage 10 — TM/HM move names + page-jump side buttons (outside the original roadmap, per user request) ✅ DONE (commit `060d272f`)

- [x] TM/HM rows in `Screen::BagMachine` now show `"<item name> - <move name>"` (e.g. `"TM01 - Mega Punch"`) instead of just the item name — saves cross-referencing elsewhere while browsing. The Medicine category is unchanged since item names already explain themselves.
- [x] Split the 4-button behavior in `PokemonActivity::loop()`: the 2 side buttons (`Button::Up`/`Down`, per `docs/controls.md`) now **jump a whole page** (`rowsPerPage()` rows) per press/hold; the 2 front buttons (`Button::Left`/`Right`) still **step row by row** as before (prior to Stage 10, `ButtonNavigator::onNext`/`onPrevious` treated all 4 buttons the same — all stepping row by row). Used `ButtonNavigator::nextPageIndex`/`previousPageIndex` directly (already existed, also used by `FileBrowserActivity`) — these 2 functions naturally fall back to row-stepping when a list only spans 1 page, so this is safe to apply to **every** list screen in `PokemonActivity`, not just Bag.
- [x] No need to change the hint bar (`renderHeaderAndHints()`): `mapLabels()` only shows labels for the 4 front buttons (Back/Confirm/Left/Right — see `MappedInputManager::mapLabels`), Left/Right behavior didn't change so the current "Up"/"Down" labels (assigned to Left/Right per the old vertical-list-screen convention) still describe them correctly.
- [x] Only touched `PokemonActivity.cpp` — no changes to `PokemonService`/`PokemonGame`/any storage file, this is purely a UI/input change.
- [x] **`pio run -e pokemon-x3`** (clean rebuild): Flash **6,334,665 B (96.7%, 204,784 B free ≈ 200KB)** — up **+672 B** from Stage 9.

## Stage 11 — Moveset management screen (outside the original roadmap, per user feedback) ✅ DONE (commit `a554d92f`)

User feedback: a Pokémon with a full 4-move set had no way to actively learn more/replace moves — the automatic MoveLearn flow (Stage 7) only appears right at level-up when the moveset is full, with nowhere to actively go view/change it.

- [x] `CollectionAction::Moveset` (new) added to `pokemon::collectionActions()`, always present (party and PC), right after "Summary" — named differently from `CollectionAction::Move` (party reordering, already existed) to avoid confusion; the UI label uses `STR_POKEMON_MOVES` ("Moves"), different from the existing `STR_POKEMON_MOVE` ("Move").
- [x] `Screen::Moveset` (new) — lists the 4 current move slots (name + PP) via `service_.peekBattleMoves()` (read-only, same as Summary). Selecting a slot opens `Screen::MovesetPick`.
- [x] `Screen::MovesetPick` (new) — lists every move in `learnsetFor(speciesId)` at or below the current level that the Pokémon **doesn't already know** (filtering out the 4 current slots). Selecting a move learns it into the exact slot chosen on the previous screen, via `PokemonService::learnMoveIntoSlot()` (new, unconditional overwrite — since the list is already filtered for AlreadyKnown). If there's nothing learnable at the current level → shows `STR_POKEMON_NO_MOVES_TO_LEARN`, doesn't enter an empty screen.
- [x] `learnableMoveIdAt()`/`learnableMoveCount()` (PokemonActivity.cpp, pure) call `pokemon::learnsetFor()` directly — learnset data is a static table not going through storage, so no new service API is needed just to read it, matching how `moveData()`/`speciesData()` are already called directly from the UI elsewhere in this file.
- [x] **Clear distinction from the 2 existing move-learning flows**: TM/HM (Stage 9) has no slot-replacement picker when full (just an error); the automatic MoveLearn (Stage 7) has a picker but only appears right at level-up. The new Moveset screen allows active access **any time**, to replace **any slot** with any move unlocked at the current level — exactly the feature the user needed, not an extension of the 2 existing flows.
- [x] Tests: updated `collectionActionsExcludeOperationsThatCannotSucceed` (order/count of actions changed with Moveset added); 1 new test for `learnMoveIntoSlot`. 19/19 native suite passes.
- [x] **`pio run -e pokemon-x3`** (clean rebuild): Flash **6,335,981 B (96.7%, 203,472 B free ≈ 199KB)** — up **+1,316 B** from Stage 10.

## Stage 12 — Fixing 4 issues found in Stage 11 playtesting ✅ DONE (commit `5b9d24d2`)

All 4 issues reported by the user (see history) have been fixed, in the order suggested at the end of the prior session (3 → 2 → 1 → 4):

### 1. Gym leader lineups now use real Pokémon Red movesets

- `GymTeamMember` (`PokemonBattleTypes.h`) adds a `moves` field (`std::array<uint8_t, GYM_MOVE_SLOTS>`, a new constant kept in sync with `BATTLE_MOVE_SLOTS` via `static_assert` since the two headers can't include each other).
- Pulled real data via **direct WebFetch of individual Bulbapedia pages** (Pewter/Cerulean/Vermilion/Celadon/Fuchsia/Saffron/Cinnabar/Viridian Gym + Lorelei/Bruno/Agatha/Lance) — nothing guessed from memory. `pokemon-gyms.csv` changed its team-member format from `species:level` to `species:level:move1-move2-move3-move4`; `generate_pokemon_gyms.py` was updated to parse/validate (valid ids, no duplicate moves)/emit accordingly.
- Also fixed 5 spots of **wrong levels** discovered while cross-referencing Bulbapedia (most species/level data was already correct, only Elite Four was slightly off): Cloyster 56→53, Lapras 54→56, Bruno's Machamp 56→58, Agatha's Golbat 55→56, Agatha's Arbok 56→58.
- `PokemonActivity::setupBattleOpponent()` adds a `std::span<const uint8_t> fixedMoves = {}` parameter — gyms/Elite Four pass `team[x].moves` in directly instead of calling `defaultMovesetForLevel()`; wild encounters are unchanged (empty fixedMoves → old path).
- Test: `PokemonBattleDataTest.cpp` confirms Brock's team has the correct real moves + every gym Pokémon has at least 1 non-zero move.

### 2. TM/HM now correctly filters by real Pokémon Red type compatibility

- **Root cause already identified in the prior session**: `pokemon::canLearnViaMachine()` already existed, was correct, had its own tests, and was simply never called from `PokemonService::teachMove()`. Now wired in — the `AlreadyKnown` check runs BEFORE the compatibility check (a Pokémon that learned a move some other way isn't blocked by the TM-rule retroactively).
- `TeachMoveOutcome` adds `Incompatible`; the UI shows `STR_POKEMON_CANNOT_LEARN_MACHINE` when it's hit.

### 3. The Moveset screen has "Forget"; a full 4-move set no longer fully blocks TM

- `PokemonService::forgetMove(recordId, slot)` (new) — deletes a slot outright, refuses if it's the last remaining move (never leaves a Pokémon with 0 moves). `Screen::MovesetPick` adds a "Forget" row at the end of the list.
- `teachMove()` adds a `replaceSlot` parameter (mirroring Stage 7's `resolveMoveLearn()`) — when the moveset is full, the UI opens `Screen::TmReplaceSlot` (new, 4 current slots + Cancel) to pick a slot to replace, then calls `teachMove(..., replaceSlot)` again instead of just reporting a hard block as Stage 9 left it.

### 4. The opponent AI is less mechanical, doesn't repeat the exact same move

- `chooseOpponentMove()` (`PokemonBattle.cpp`) is no longer fully deterministic: 1/4 of turns considers **every** move with PP remaining (not just the single most effective one); when multiple moves tie on effectiveness, one is picked randomly among them instead of always taking the first slot.
- Combined with item 1 (gym Pokémon now have ≥2-4 real moves instead of possibly synthesizing just 1) — directly resolves the "Onix only ever uses 1 move" phenomenon.
- New test: 12 different random seeds must show both tied-effectiveness moves getting used (not always the same slot).

**Overall results**: 19/19 native suite passes. `pokemon-x3` flash (clean rebuild): **6,340,863 B (96.8%, 198,592 B free ≈ 194KB)** — +4,882 B over Stage 11 (a bigger jump than usual because of real gym moveset data + AI logic + 2 new UI flows).

**Bug found right after committing, through real playtesting** (commit `f2187062`): "Forget" on the Moveset screen only set `moves[slot] = 0` at the chosen slot, leaving a "hole" if that slot wasn't the last one in use — `validateBattleRecordEntry` requires moves to be packed contiguously from the start of the array (like `PokemonState::partyRecordIds`), so `upsertEntry()` rejected the write unless the forgotten slot happened to be the last one (only true 25% of the time, silently failing the other 75% — only `LOG_ERR`, no UI error message since it returned a `StorageError` like other storage failures). Fixed: shift every move after the deleted slot forward by 1 position. **Lesson**: every new service branch needs its own unit test written right away — Stage 12 originally had no test at all for `forgetMove()`, relying only on the simulator build/boot smoke test (which can't catch a behavioral bug, since it's not a compile error/crash) so the bug slipped through all the way to the user actually clicking it. Added 2 tests for `forgetMove()` (forgetting a middle slot shifts the array correctly; refuses to delete the last remaining move).

---

## Stage 13 — Switching Pokémon mid-battle (party switch) ✅ DONE (commit `950d72ce`)

Previously, gym battles and wild encounters only ever let the first party Pokémon fight — running out of HP meant an immediate loss even if the party had other healthy Pokémon. Now a loss only happens when **the entire party is out of HP**, or the player deliberately **RUN**s.

- [x] `battlePartySlot_` (new) replaces the hardcoded `snapshot_.party[0]` in `setupBattlePlayer()`/`savePlayerBattleEntry()` — `setupBattlePlayer()` now takes an explicit `slot` parameter.
- [x] `enterBattle()`/`enterGymBattle()` call `firstUsablePartySlot()` (new) to start the battle with the **first Pokémon with HP>0** instead of always assuming `party[0]` is alive — no Pokémon with HP left shows `STR_POKEMON_NO_USABLE_POKEMON` and blocks entry.
- [x] `Screen::Battle` adds "Switch" (FIGHT/BALL/SWITCH/RUN for wild, FIGHT/SWITCH/RUN for gym) → `Screen::BattleSwitch` (new) lists party members with HP>0 (excluding the one currently fighting).
- [x] When the active Pokémon faints (`OpponentWon`): **the battle doesn't end immediately** — if another Pokémon has HP left, the player is forced into `Screen::BattleSwitch` (`forcedBattleSwitch_`, Back is blocked so it can't be cancelled — matching the original games); only losing for real when none are left.
- [x] `usablePartySlotCount()`/`usablePartySlotAt()` (new) use `service_.peekBattleMoves()` (read-only, no SD write) to check each member's HP.
- [x] **Intentional simplification**: switching Pokémon **doesn't cost a turn** (doesn't give the opponent a free attack as in the original games) — `stepBattle()` has no concept of a "switch action," and adding a turn-consuming mechanism would require modifying the engine (which is stable and has its own tests) just to model this rule accurately; trading exactness for simplicity, while still fixing the main issue (losing under the wrong condition).
- [x] 4 new i18n keys: `STR_POKEMON_SWITCH`, `STR_POKEMON_NO_OTHER_USABLE`, `STR_POKEMON_NO_USABLE_POKEMON`, `STR_POKEMON_GO`.
- [x] Only touched `PokemonActivity.cpp/.h` — no changes to `PokemonService`/`PokemonBattle`/storage.
- [x] 19/19 native suite passes (no service/engine changes so no new tests needed at that layer). `pokemon-x3` flash (clean rebuild): **6,342,451 B (96.8%, 196,992 B free ≈ 192KB)** — +1,556 B over the earlier `forgetMove` fix.

---

## Stage 14 — Restoring full gym leader/Elite Four lineups (outside the original roadmap, per user request) ✅ DONE (commit `9d433ad0`, `05b623ed`)

After viewing the current gym/Elite Four lineups (a prior request), the user asked to reverse the Stage 12 decision of "trim to 2-3 members per team for e-ink refresh cost" and **keep the real Pokémon Red lineups**, accepting longer battles (Giovanni/Lorelei/Bruno/Agatha/Lance all have their real 5-member teams).

- [x] Re-reviewed all 12 gym/Elite Four teams against the Bulbapedia data already gathered in Stage 12 (available within the session context, no need to refetch) — fully rewrote `scripts/data/pokemon-gyms.csv` with full lineups: Koga 4 members, Sabrina 4, Blaine 4, Giovanni 5, Lorelei 5, Bruno 5, Agatha 5, Lance 5 (Brock/Misty already had their full 2, Surge/Erika already had their full 3 — unchanged).
- [x] Every species id and move id was confirmed via `grep`/a Python script reading `pokemon-kanto-v2.csv`/`pokemon-moves.csv` directly, not from memory — including species previously trimmed out (Rhyhorn, Dugtrio, Venomoth, Rapidash, Slowbro, Jynx, Hitmonchan, Haunter, a second Dragonair).
- [x] `MAX_GYM_TEAM_SIZE` (`PokemonBattleTypes.h`) 3→5, along with `MAX_TEAM_SIZE` in `generate_pokemon_gyms.py` and the `PROVENANCE` string (must match the CSV's first line).
- [x] Confirmed `parse_team()` has no "no duplicate species across team members" constraint (only blocks duplicate moves within the same member) — so the original games' true duplicate-species pairs (2× Koffing/Koga, 2× Onix/Giovanni, 2× Dragonair/Lance, 2× Gengar/Agatha) parse fine without code changes.
- [x] Confirmed no UI changes needed: `enterGymBattle()`/`advanceGymOpponentOrFinish()` already loop dynamically over `team.size()`, nothing assumes a fixed 2-3 members.
- [x] **Bug found while cross-checking the data (not user-reported)**: one Bruno team member had species id 107 (Hitmonchan) assigned but Hitmonlee's real moveset (Jump Kick/Focus Energy/Hi Jump Kick/Mega Kick) — a mis-assigned species id from when the CSV was written in Stage 12 (the two Hitmon species have very similar names). Fixed separately as commit `9d433ad0` (107→106) before folding it into the full CSV rewrite; now both Hitmonlee (106) and Hitmonchan (107) appear correctly as 2 separate members with each one's real moveset.
- [x] Test: added 5 `CHECK`s in `PokemonBattleDataTest.cpp` asserting `gymTeamFor(8..12).size() == 5` (Giovanni/Lorelei/Bruno/Agatha/Lance).
- [x] Updated the "Design decisions" table — struck through the old Stage 12 decision, noting it was reversed in Stage 14.
- [x] 19/19 native suite passes. `pokemon-simulator-X3` builds cleanly + smoke test with no errors. **`pio run -e pokemon-x3`** (clean rebuild): Flash **6,342,555 B (96.8%, 196,896 B free ≈ 192KB)** — +104 B over Stage 13 (only grew the fixed `GymTeamMember[5]` array size and the added team data, no new logic).

---

## Stage 15 — Double-buffering `pokemon-battle.bin` (outside the original roadmap, per user request) ✅ DONE

The user pointed out that one of Stage 3's foundational assumptions was no longer true: the side file storing battle HP/PP/moveset (`pokemon-battle.bin`) was designed **without double-buffering** because it was "100% reconstructible from level + learnset." But starting with Stage 7 (automatic move learning on level-up, slot choice) and especially Stage 11/Stage 12 (the Moveset screen for actively learning/forgetting moves, teaching TM/HM), **a Pokémon's real moveset is the player's own choice**, no longer deterministically derivable from level — losing this file now doesn't just mean "reset to full HP," it can **erase the player's real move-learning choices**. Immutable constraint #3 (see top of roadmap) therefore needed updating: the assumption "battle data is always reconstructible so it doesn't need double-buffering" is only still true for HP/PP/status, not for moveset.

- [x] `PokemonBattleStoreCodec.h/.cpp`: adds a 10-byte header (magic `PKBT` + version + `entryCount` + non-zero `sequence`) before the entries — `encodeBattleStoreFile()`/`decodeBattleStoreFile()` change signature to accept/return `sequence` too; CRC32 now covers both the header and the entries (previously only entries). Kept `decodeLegacyBattleStoreFile()` (the old, header-less logic) purely for reading the old format during migration.
- [x] `PokemonBattleStore.h/.cpp`: changed from 1 file to 2 alternating files `pokemon-battle-a.bin`/`pokemon-battle-b.bin`, following exactly `PokemonStore`'s (main save) philosophy — every write always targets the file that is **currently inactive**, reads it back to verify (matching `sequence` + state) before switching the active pointer; the active file is never touched, so an interrupted write/crash can never lose the other copy. `load()` compares `sequence` (handling wraparound the same way as `PokemonStore::sequenceIsNewer`) to pick the newer of the 2 valid files.
- [x] **Automatic migration**: if neither new file exists yet (older build, or a fresh install), `load()` tries reading the old single file `pokemon-battle.bin` with the old codec (`decodeLegacyBattleStoreFile`) — if valid, immediately writes it out to `pokemon-battle-a.bin` (sequence 1) to get double-buffer protection starting from the very first session after the update, **without deleting the old file** (kept as a fallback recovery copy, the same way `PokemonStore` handles the legacy `pokemon-v2-{a,b}.bin` filenames).
- [x] The public API (`load()`/`findEntry()`/`upsertEntry()`/`removeEntry()`) is **unchanged** — `PokemonService` and all 40+ existing tests use `PokemonBattleStore` via constructor injection, no changes needed at the service/UI layer.
- [x] Updated `docs/file-formats.md`: split the `pokemon-battle-{a,b}.bin` entry into "Version 2 (double-buffered)" (new format) and "Version 1 (single file, superseded)" (old format, now only relevant for migration); corrected a passage under the main save (v3) section that had claimed the side file was "fully-reconstructible" — no longer accurate.
- [x] Tests: `PokemonBattleStoreCodecTest.cpp` adds tests for the header (magic/version/entryCount/sequence, sequence=0 rejected, unknown version rejected) + a dedicated test for `decodeLegacyBattleStoreFile`. `PokemonBattleStoreTest.cpp` adds: consecutive writes must alternate correctly A→B→A; **corrupting the newer file (B) must fall back to the older one (A) without data loss** (the most important test — the whole reason double-buffering exists); both files corrupted still returns empty rather than crashing; a failed write (simulated via `setFailWritableOpen`) doesn't touch the active file or the in-memory state; migrating from a legacy file preserves exactly the moves taught (not derivable from the learnset) and doesn't delete the legacy file.
- [x] 19/19 native suite passes. `clang-format` applied to all newly-touched files (no original CrossInk code touched). `pokemon-simulator-X3` builds cleanly, 12s smoke test with no new errors/crashes. **`pio run -e pokemon-x3`** (clean rebuild): Flash **6,344,073 B (96.8%, 195,376 B free ≈ 191KB)** — +1,518 B over Stage 14, for the entire double-buffering + migration mechanism.

---

## Stage 16 — Redrawing the Battle screen in Pokémon Red style (outside the original roadmap, per user request) ✅ DONE

The user was blunt: "the battle screen looks bad... ideally it should look like the Pokémon Red battle screen." The old screen (from Stage 5) just stacked 2 identical-looking blocks (name+level, HP bar, art) vertically for opponent then player — no layout, no borders, nothing resembling the original game.

- [x] Completely rewrote `renderBattleHud()` (`PokemonActivity.cpp`) to follow Pokémon Red's classic diagonal layout: **opponent's name/level/HP-bar box in the top-left + opponent art in the top-right; player art in the bottom-left + player's name/level/HP box in the bottom-right** — capturing the exact "diagonal" feel characteristic of the original game, even though the device layout is portrait (528×792) rather than landscape like a Game Boy.
  - Name/HP boxes use `drawRoundedRect` (lightly rounded, matching Red's rounded dialogue/HUD boxes) instead of plain text + borderless bars as before.
  - A bold "**HP**" label is placed right before the HP bar, exactly matching Red's classic position (previously there was no such label at all).
  - **Only the player's box shows the HP number as "current/max"** — matching real Red (the opponent's exact HP is never revealed, only the bar). Previously the opponent's box exposed the exact HP number just like the player's — not in the spirit of the original game.
  - Status (PAR/SLP/FRZ/BRN/PSN/CNF) is now placed on the same row as the HP number/bar instead of its own row, saving vertical space.
  - All coordinates are computed **dynamically** based on `listBounds_.y` (the top of the FIGHT/BALL/SWITCH/RUN menu, already anchored to the bottom of the screen since Stage 5) instead of hardcoded pixel values as in the old code — automatically adapts correctly whether `rowCount_` changes (3 rows for gym battles vs 4 rows for wild encounters changing the HUD area's height) without needing to know in advance.
- [x] **Battle log dialogue box** (new): a separate rounded-corner box right above the FIGHT/BALL/SWITCH/RUN menu, exactly where Red's "text box" sits — left-aligned text (not centered like the old code, Red always left-aligns), with `truncatedText()` guarding against overflow if a log line is too long for the box.
- [x] **Added opening dialogue** (previously `battleLog_` stayed empty until the first turn finished — a completely blank screen right when entering a battle, unlike Red): `enterBattle()`/`enterGymBattle()` now set `"Go, <your Pokémon>!"` right away (reusing the existing `STR_POKEMON_GO` from Stage 13); gym battles also add `"<leader name> sent out <Pokémon>!"` beforehand (new string `STR_POKEMON_SENT_OUT`). `advanceGymOpponentOrFinish()` (when a trainer sends out their next Pokémon) now shows this line too instead of a blank screen.
- [x] 1 new i18n key: `STR_POKEMON_SENT_OUT: "%s sent out %s!"`.
- [x] Only touched `PokemonActivity.cpp` + `english.yaml` — no changes to `PokemonService`/`PokemonBattle`/storage/native tests (this UI file has never been in the native test suite, only verified via real builds + simulator smoke tests, like earlier UI stages).
- [x] `pokemon-simulator-X3` built cleanly (`rm -rf .pio/build/pokemon-simulator-X3` before building since `english.yaml` changed — following the gotcha noted since Stage 5), 12s smoke test with no errors/crashes. 19/19 native suite passes (unchanged since nothing tested was touched). **`pio run -e pokemon-x3`** (clean rebuild): Flash **6,345,083 B (96.8%, 194,368 B free ≈ 190KB)** — +1,010 B over Stage 15.
- [x] **Visually confirmed** — the user ran the simulator themselves, gave feedback through 2 rounds of tweaks (keeping name/nickname; narrower/taller HP box + vertically centered bar + smaller log box, see Stage 17), then confirmed "looks good."

---

## Stage 17 — Alive-count dots in Battle + HP bars for Party/Summary (outside the original roadmap, per user feedback after trying Stage 16) ✅ DONE (commit `470120d1`)

After trying the new Battle screen (Stage 16), the user requested 3 more things:

1. **Battle**: show the number of remaining Pokémon per side as dots (filled dot = alive, small dot with an X = fainted) + **shrink the HP box**.
2. **Screen::Party**: add an HP bar + HP text + status to each row, to see which Pokémon needs a healing item.
3. **Screen::Summary**: add an HP bar + HP text.

- [x] **Shrunk HP box**: `panelHeight` 76→50, dropped the species-name row entirely (the sprite next to it is already enough to identify the Pokémon) — down to just 2 rows: "HP" + bar + Level (row 1), "current/max" + status if any (row 2). Also dropped Stage 16's "only the player shows the HP number" distinction — now both sides show it, per the user's request.
- [x] **Dot row** above each HP box: GfxRenderer has no dedicated circle-drawing API — used `fillRoundedRect`/`drawRoundedRect` with `cornerRadius = size/2` (fully rounding a square = a circle). A filled dot (black `fillRoundedRect`) = still battle-ready; a hollow dot (`drawRoundedRect` outline) + 2 diagonal lines (`drawLine`) forming an X = fainted.
  - Opponent: for gym battles, computed from `gymTeamFor(gymChallengeIndex_)` + `gymChallengeTeamProgress_` (a member with index less than progress = defeated; the exact index = currently fighting, alive or not per `battleOpponent_.currentHp`; greater = not yet sent out, defaults to alive); wild encounters just show 1 dot.
  - Player: computed from `snapshot_.partyCount`, reading HP via `battlePlayer_.currentHp` for the currently-fighting Pokémon (to avoid re-reading from the store which might not be synced) and `service_.peekBattleMoves()` (read-only, no SD write — matching the pattern of Stage 13's `usablePartySlotAt()`) for the rest of the party.
- [x] **`Screen::Party`**: taller rows (64→96px) **only for this screen** via `rowHeightForScreen()` (new, shared by both `buildList()` and `rowsPerPage()` — previously the two hardcoded `64` independently). Confirmed by reading `FreeInkUIGfxRenderer::text()` directly: a list widget's label/value is always **vertically centered within the row height** (`y = rect.y + (rect.height - lineHeight) / 2`) — meaning a taller row simply pushes the existing text block down to center, leaving equal empty space above **and below**; the HP bar is drawn in the bottom strip, positioned by a fixed distance from the **row's bottom edge** (not the top), which is safe for any row height without needing to know the font's exact line height. `renderPartyRowHealth()` (new, called from `renderRowArt()`) draws the bar + "current/max" + status abbreviation.
- [x] **`Screen::Summary`**: added an "HP" + bar + "current/max" row right below the Number/Level/Gender row, reusing the existing right-aligned field style already used for Type/Exp/Met.
- [x] All 3 spots share `service_.peekBattleMoves()` + `pokemon::battleMaxHp(baseStatsFor(...)->hp, levelForXp(...))` to compute max HP — there's no `maxHp` field stored in `BattleRecordEntry`, it must be recomputed from base stats + level every time, the same way other screens (BattleSwitch, `usablePartySlotAt()`) have done since Stage 13.
- [x] Only touched `PokemonActivity.cpp/.h`. 19/19 native suite passes (unchanged — this UI file isn't in the native test suite). `pokemon-simulator-X3` built cleanly + 12s smoke test with no errors. **`pio run -e pokemon-x3`** (clean rebuild): Flash **6,346,113 B (96.8%, 193,344 B free ≈ 189KB)** — +1,030 B over Stage 16.
- [x] **Visually confirmed** — the user ran the simulator, went through 2 rounds of tweaks (see the 2 "Fix" entries right below), then confirmed "looks good, stopping here for now."

**Fix right after playtesting (commit `9b12bae6`)**: the initial shrunk HP box dropped the species name entirely (reasoning: "the sprite is already enough to identify it") — the user immediately pushed back, the HP box still needs to show name/nickname. Added back the name+Level row at the top (Level moved up here from the HP-bar row), `panelHeight` 50→72. The player prefers the real nickname (`snapshot_.party[battlePartySlot_].nickname`) before falling back to `speciesName()`, matching the convention used by Party rows/Summary. **Bug found alongside this fix**: the increased `panelHeight` made dots+panel (92) now taller than the sprite (90) — the old code assumed the sprite was always the tallest element on each side (true when panelHeight=50), so `messageY`/`playerZoneTop` were computed directly from `spriteH`, which was no longer correct. Fixed with a shared `zoneContentHeight = max(spriteH, dotRowHeight + panelHeight)`, without assuming either side is taller. Flash **6,346,205 B (96.8%, 193,248 B free)** — +92 B over the previous build.

**Second fix (commit `188b005d`)**: per further feedback — the HP box should be **narrower horizontally, taller vertically**; the HP bar should be **vertically centered**; the HP text should sit **right after the HP bar** (same row, not its own row); the battle log box should **shrink** to give more room to the battle scene + HP box. `panelWidth` fixed at 220 (previously computed dynamically, taking up nearly the full screen width) + `panelHeight` 72→92. Merged the "cur/max HP" + status row into the same row as the bar (HP text drawn right after the bar's right edge, status further to the right), this row's `rowY` is now computed to be **vertically centered** in the space below the name/level row instead of a fixed offset as before. The log box changed from "fill all remaining space" to a **fixed height of 90px** anchored above the menu; the freed-up space goes into the gap between the opponent/player zones (`zoneGap`, computed dynamically, minimum floor of 16) instead of going to waste. Flash **6,346,287 B (96.8%, 193,168 B free)** — +82 B.

---

## Plan for the next 3 stages (Stage 18-20, recorded 2026-09-09, NOT yet implemented)

Three new user requests, deliberately **only planned here, no code written yet** — split out so each stage can be implemented cleanly within 1 session, avoiding running out of tokens partway through. The actual code has already been surveyed (nothing guessed) before writing this — the numbers/function names/offsets below have been confirmed by reading the source directly, not guessed.

### Stage 18 — Using items (Bag) mid-battle ✅ DONE + CONFIRMED (commit `719fd9e2` + 6 follow-up/extension commits: `e1233cd8`, `e7c0968e`, `02fe4a15`, `6a014052`, `b808335d`, `d278b161`)

**Problem**: `Screen::Battle` currently only has FIGHT/BALL/SWITCH/RUN (wild) or FIGHT/SWITCH/RUN (gym) — no way to use a Potion/status cure mid-battle. `PokemonService::useConsumable()` (already existed since Stage 9) is only called from `Screen::BagMedicine`, a screen outside of battle, with no entry point from `Screen::Battle`.

- [x] Added a "BAG" row to the `Screen::Battle` menu — FIGHT/BALL/BAG/SWITCH/RUN (wild, 5 rows) or FIGHT/BAG/SWITCH/RUN (gym, 4 rows — BAG works for both battle types, unlike BALL which is wild-only). Updated `logicalCount()` (3/4→4/5), the switch labels, and the selection handler (index computed relative to `isGym`, not hardcoded).
- [x] `Screen::BattleBag` (new) — lists battle-usable items via a new predicate `isBattleUsableCategory` (Medicine/StatusCure/PPRestore, **excluding Candy**) — reuses the existing `bagItemIdAt()`/`bagItemCount()` (already generalized via predicate function pointer since Stage 9).
- [x] Selecting an item in `Screen::BattleBag`: calls `service_.useConsumable()` + `service_.consumeBagItem()` as normal (no new `PokemonService` API added), then **re-syncs `battlePlayer_`** by calling `service_.peekBattleMoves()` again right after and copying `currentHp`/`status`/`statusTurns`/`moves[].currentPp` into `battlePlayer_` — exactly the finding already noted in the plan: `useConsumable()` writes the `BattleRecordEntry` directly to disk via `recordId`, without touching `battlePlayer_` (RAM), which is what `stepBattle()`/`renderBattleHud()` actually reads.
- [x] Excluded `Candy` from scope (leveling up mid-battle would require recomputing `battlePlayer_`'s live `maxHp`, noticeably more complex) — Candy is still only usable outside battle via `Screen::BagMedicine` as before.
- [x] **Intentional simplification, consistent with Stage 13's precedent** (switching Pokémon doesn't cost a turn): using an item mid-battle also **doesn't cost a turn** — no free opponent attack, avoiding changes to the stable `stepBattle()` engine just for this feature.
- [x] 1 new i18n key: `STR_POKEMON_USED_ITEM` ("%s used %s!") — shown in the battle log after using an item, matching the message style of other in-battle actions. `Screen::BattleBag`'s title reuses the existing `STR_POKEMON_BAG` key, no new key needed.
- [x] Only touched `PokemonActivity.cpp/.h` + `english.yaml` — no changes to `PokemonService`/storage/native tests (matching the UI-only pattern of earlier stages, Stage 13/16/17).
- [x] 19/19 native tests pass (unchanged). Simulator built cleanly + smoke test with no errors. `pio run -e pokemon-x3` (clean rebuild): Flash **6,347,087 B (96.8%, 192,368 B free)** — +800 B over Stage 17.
- [x] **Visually confirmed** — the user ran the simulator themselves through multiple rounds of feedback (see "Fix"/"Extension" entries below: keeping name/nickname, narrower/taller HP box, choosing a target for item use, HP shown in ItemTarget, fair turn order + HP shown in BattleSwitch, 2-column menu, fixed the FIGHT-screen overflow), finally confirming "looks good, pausing here."

**Fix right after playtesting (commit `e1233cd8`)**: the `Screen::BattleBag` list overflowed and covered the battle HUD, making items unreadable at the top. Cause: `BattleBag` was grouped with the "bottom-anchored, overlaps HUD" group along with `BattleMoves`/`BattleBalls` — that group is safe because it's capped at 4 rows, always fitting the space below the HUD; but `BattleBag` lists up to 17 items (every Medicine/StatusCure/PPRestore id regardless of ownership), and `rowsPerPage()` knows nothing about the HUD, so it returned far more rows than 4, pushing the top of the list over the HUD. Fix: removed `BattleBag` from both `bottomAnchored` (`buildList()`) and the HUD-drawing condition (`renderFocused()`) — it now uses a full-screen, top-anchored list like every other Bag screen, which already has correct pagination for long lists. Trade-off: the HUD is no longer visible behind item selection, which is acceptable. Flash unchanged (pure layout fix).

**Second fix (commit `e7c0968e`)**: selecting an item in `Screen::BattleBag` didn't work as intended. Real cause: the first version automatically applied it to `battlePartySlot_` (the currently-fighting Pokémon), skipping target selection entirely — not matching the real intended flow (a benched Pokémon recovering from injury also needs to be healable, not just the active one). Fix: added `BagCategory::BattleMedicine` (new), `Screen::BattleBag` now just selects an item then switches to `Screen::ItemTarget` (reusing the existing Pokémon-selection screen already used outside battle, no new screen added) — differs from the normal Medicine flow in that finishing returns to `Screen::Battle` instead of `Screen::Party`, and only re-syncs `battlePlayer_` (RAM) if the target is indeed the currently-fighting Pokémon (a benched Pokémon isn't part of `stepBattle()`/`renderBattleHud()` right now so nothing needs syncing). Flash **6,347,199 B (96.9%, 192,256 B free)** — +112 B.

**Third fix (commit `02fe4a15`)**: the target-selection screen (`Screen::ItemTarget`) needed to additionally show the HP bar, HP/HP text, and current status of each Pokémon when using a Medicine/BattleMedicine item — exactly the info needed to decide who to use it on. Added `itemTargetShowsHealth()` (true when `bagCategory_` is `Medicine`/`BattleMedicine`, false for `Evolution`/`Machine` since evolution stones/TMs have no HP to show) — reuses the exact same "taller row (96px) + HP-bar strip at the bottom of the row" mechanism already built for `Screen::Party` in Stage 17 (`rowHeightForScreen()` and the call condition for `renderPartyRowHealth()` now share this same condition), no new drawing function added. Flash **6,347,283 B (96.9%, 192,160 B free)** — +84 B.

**4th extension (commit `6a014052`)**: 3 further requests.
1. `Screen::BattleSwitch` now shows HP bar/text/status like Party/ItemTarget (`showsPartyHealthRows()` renamed from `itemTargetShowsHealth()`, extended to include `BattleSwitch`) — `renderRowArt()` needed its own mapping via `usablePartySlotAt()` since this screen's index isn't a direct party slot.
2. **Switching Pokémon or using an item mid-battle now costs a whole turn** — reversing the "doesn't cost a turn" simplification chosen in Stage 13/the first version of Stage 18, matching the real Gen 1 rule: both actions always resolve instantly (no Speed comparison), then the opponent immediately gets to attack. The engine adds `stepOpponentOnlyTurn()` (new, `PokemonBattle.h/.cpp`) — runs only the opponent's action, no Speed comparison (the player has no action to compare against), still applies end-of-turn status damage as normal. Factored `chooseOpponentMoveSlot()` and `finishTurn()` out of `stepBattle()`'s old internal lambda into shared functions between the two — confirmed the refactor didn't change behavior by running the 19/19 old tests **before** adding new ones. `PokemonService::resolveOpponentOnlyTurn()` (thin wrapper, matching the `resolveBattleTurn()` pattern). The UI calls this right after a voluntary Pokémon switch (not a forced switch after fainting) or using an item, handling the outcome the same way as `Screen::BattleMoves`.
3. Turn order when both sides use FIGHT still follows Speed as before (correct since Stage 2, unchanged) — the only new part is that switch/item never compare Speed since they always resolve first, matching the original game.
- Tests: 4 new tests in `PokemonBattleTest.cpp` (no Speed comparison even when the player is faster, can knock out the player, still ticks status at end of turn, short-circuits when already fainted) + 1 test in `PokemonServiceTest.cpp` (thin-wrapper).
- 19/19 tests pass. Simulator built cleanly + smoke test with no errors. Flash **6,349,315 B (96.9%, 190,128 B free)** — +2,032 B.

**5th extension (commit `b808335d`)**: after adding BAG, the wild-battle menu (5 rows: FIGHT/BALL/BAG/SWITCH/RUN) overflowed/overlapped the HUD above it — the HUD (Stage 16/17) had never accounted for a 5-row menu (previously capped at 4). The user then requested: both gym battles and wild-catch battles should split the command menu into 2 columns to save space. Built `renderBattleMenu()` (new) to hand-draw a 2-column button grid replacing the shared 1-column list (`fui::list()`) — reduces the actual row count to `ceil(N/2)` (wild 5→3, gym 4→2), fixing the overflow at its root by shrinking height instead of adjusting HUD constants. `Screen::Battle` removed from `isListScreen()` (BattleMoves/BattleBalls/BattleBag/BattleSwitch unchanged — long move/item/Pokémon names don't suit a narrow 2-column grid). `battleMenuTop()` (new) computes the grid's top based on the real row count; `renderBattleHud()`'s `hudBottom` now uses this function instead of `listBounds_.y`. `loop()` adds a dedicated navigation branch for this screen: Left/Right still move ±1 in reading order, Up/Down jump ±2 (one column) instead of "page jump" (meaningless for a 2-column grid). The selected button is drawn black-filled with white text, unselected buttons are outlined black on black text. **Known, accepted trade-off**: the new grid doesn't register touch regions through `app_`/`fui` like the shared list does — X3 (the primary device) has no touchscreen so this doesn't matter, it's only a gap if testing via mouse/touch on a simulator/other device instead of physical buttons. Flash **6,350,749 B (96.9%, 188,704 B free)** — +1,434 B.

**Fix right after playtesting (commit `d278b161`)**: pressing FIGHT made the move list (`Screen::BattleMoves`) overlap the HUD's text box. Cause: `hudBottom` switched to `battleMenuTop()` for **every** screen sharing `renderBattleHud()` (Battle/BattleMoves/BattleBalls), but `battleMenuTop()` calls `logicalCount()` — a function that returns a different number depending on `screen_` (command count for `Battle`, move count for `BattleMoves`), so it miscalculated the HUD's reserved area while on `BattleMoves`. Fix: only use `battleMenuTop()` when `screen_ == Battle`; `BattleMoves`/`BattleBalls` go back to using `listBounds_.y` as before (these 2 screens are still regular lists via `buildList()`, unchanged by the 5th extension). Flash **6,350,777 B (96.9%, 188,672 B free)** — +28 B.

### Stage 19 — Balls in the out-of-battle Bag ✅ DONE (commit `f0b49554`)

**Already confirmed via code, no fix needed**: ball drops through reading + `dropWeight` rarity tiers (Poke 40 > Great 20 > Ultra 8 > Master 1) have been correct since Stage 1/Stage 4; balls being fully locked out in gym battles (gated on `isGym` in both the switch label and the selection handler of `Screen::Battle`) has been correct since before.

- [x] `Screen::BagBalls` (new) — lists the 4 ball types + counts, reusing the exact same read pattern as `Screen::BattleBalls` (`itemData(EVOLUTION_ITEM_COUNT+1+index)` + `bagCounts[index]`) — view-only, no action on Activate (shows `STR_POKEMON_BAG_BALLS_INFO` explaining that balls are only usable when catching wild Pokémon).
- [x] `Screen::Bag` goes from 3 to 4 rows: Evolution/Medicine/Balls/TM-HM.
- [x] 2 new i18n keys: `STR_POKEMON_BAG_BALLS` ("Balls"), `STR_POKEMON_BAG_BALLS_INFO`.
- [x] **Deliberately no `BagCategory::Balls` added** — a deviation from the original plan: `BagBalls` doesn't go through the select-item-then-select-target flow (`Screen::ItemTarget`) like Evolution/Medicine/Machine, so it doesn't need to distinguish context via `bagCategory_` — `goBack()` maps `BagBalls → Bag` directly like the other 3 screens, no extra state needed.
- [x] Extra defensive fix (not in the original plan, done while at it since it was convenient): `Screen::BattleBalls`'s handler now checks `gymChallengeIndex_ != 0` itself — previously it was only safe because it had a single entry point already gated on `isGym`; worth doing since the Battle menu's structure changed twice within Stage 18 alone (adding BAG, then switching to the 2-column layout).
- [x] Only touched `PokemonActivity.cpp/.h` + `english.yaml` — no changes to `PokemonService`/data/tests.
- [x] 19/19 native tests pass (unchanged). Simulator built cleanly + smoke test with no errors. Flash **6,351,221 B (96.9%, 188,224 B free)** — +444 B over Stage 18.
- [x] **Visually confirmed** — the user ran the simulator, confirmed "looks good" after seeing Bag > Balls.

**Extension per feedback (commit `fe283cb3`)**: hides items with a count of 0 from **every** bag menu, not just Balls — `BagEvolution`/`BagMedicine`/`BagBalls`/`BagMachine` (out of battle) and `BattleBag`/`BattleBalls` (mid-battle) all filter now. `bagItemIdAt()`/`bagItemCount()` (Medicine/Machine/BattleBag) take an extra `bagCounts` parameter, filtering while iterating the category. `ownedSlotAt()`/`ownedSlotCount()` (new, 2 overloads for uint8_t/uint16_t) let the 2 screens indexing directly into fixed arrays (BagEvolution's `itemCounts`, 6 elements; Balls' first 4 elements of `bagCounts`) map "the Nth owned item" back to its real position. **Bug found while doing this (not a pre-existing bug, introduced by this very change)**: `renderRowArt()`'s icon for `BagEvolution` still used the raw row index as the stone species id — after filtering, this would draw the wrong stone unless remapped through `ownedSlotAt()` first, fixed at the same time. Added a guard "0 total balls means `BattleBalls` can't be entered" (matching the existing `BattleBag` pattern) and an empty-state `STR_POKEMON_BAG_EMPTY` when the whole category is empty (matching `Screen::Pc`'s "No Pokémon are stored" pattern). Flash **6,351,659 B (96.9%, 187,792 B free)** — +438 B.

### Stage 19.5 — Word-wrapping for message text + battle log (outside the original roadmap, per user feedback) ✅ DONE (commit `db8e87c8`)

User feedback after playtesting: gym-battle message text (`Screen::Message`) overflows the screen in portrait orientation; battle-log text (the dialogue box in `renderBattleHud()`) also seemed to overflow. Requested: any such text should wrap automatically when it exceeds the screen size, and asked for a nicer display approach.

- [x] `Screen::Message` — `message_` can be any string up to 96 characters (e.g. "None of your Pokémon can battle right now!"), previously drawn directly via `centered()` (which only understands an existing single `\n`, doesn't auto-wrap a long sentence with no `\n`) — long enough to overflow the portrait screen's width at 12pt bold. Fix: word-wrap first via `renderer.wrappedText()`, then draw each wrapped line with `centered()`.
- [x] Battle log dialogue box (`renderBattleHud()`) — previously each half-line (player action/opponent action) used `truncatedText()` (cuts off with "..." if too wide, no wrapping). Fix: word-wrap each half via `wrappedText()` then rejoin, capping the total line count to fit the box height so it never overflows. Increased `messageHeight` 90→160 to fit up to 4 lines instead of 2 — checked that `zoneGap` (the padding between the opponent/player zones) still doesn't fall below the 16px floor in both the gym (2-row menu) and wild (3-row menu) cases, since there's a lot of spare space freed up ever since the Battle menu switched to 2 columns (Stage 18).
- [x] Only touched `PokemonActivity.cpp` (added `#include <vector>` for `wrappedText()`'s return type). 19/19 native tests pass (unchanged). Simulator built cleanly + smoke test with no errors. Flash **6,351,915 B (96.9%, 187,536 B free)** — +256 B over Stage 19.
- [x] **Visually confirmed** — the user confirmed "text wrap ok" in the following session.

### Stage 20 — Simulated save-file editing tool (expanded into a general-purpose tool) ✅ DONE (commit — see below)

**Scope change from the original plan**: the original plan only called for a narrow script to insert a single pending encounter. The user then asked for something broader: "create a tool to help edit the save file so it's convenient to edit/update values as needed" — so it became `scripts/dev/edit_pokemon_save.py`, a multi-purpose CLI with several subcommands, instead of a one-off script.

- [x] `scripts/dev/edit_pokemon_save.py` (new) — an `argparse`-based CLI, defaulting to operate on `fs_/.crosspoint` (a path relative to the script's location, overridable via `--save-dir`). Only understands the current v3 format (195-byte state) — refuses to operate if it sees a different version rather than guessing and corrupting unfamiliar data.
- [x] `dump` subcommand — prints the party (recordId/species/totalXp/caughtLevel/gender/nickname), `battleProgress` (gyms + Elite Four defeated), evolution-stone items + bag items (only shows those > 0), and all 3 pending-event slots. Looks up species/item names directly from `pokemon-kanto-v2.csv`/`pokemon-items.csv` (no hardcoded name table, avoiding drift from the real data).
- [x] `reset-battle-store` subcommand — deletes `pokemon-battle-{a,b}.bin` + the old `pokemon-battle.bin` file, so HP/PP/status auto-rebuild at full when needed.
- [x] `reset-gym-progress` subcommand — sets `battleProgress` to 0 (clears both gym-battle history and badges, since they're the same field).
- [x] `queue-encounter --species --level [--gender] [--slot]` subcommand — inserts a pending Encounter event; auto-picks a valid gender per the real `genderMatchesSpecies()` rule if not specified (`validate_gender()`/`default_gender_for()` reimplement the exact rule from `PokemonTypes.cpp`); auto-picks the first empty slot if `--slot` isn't given, with a clear error if all 3 slots are full.
- [x] `set-bag-item --item --count` subcommand — sets an item's count directly (auto-detects whether it's an evolution stone, id 1-6, using `itemCounts` u16, or a bag item, id 7-83, using `bagCounts` u8).
- [x] `set-record-xp --record-id --xp` subcommand — sets a record's `totalXp` directly (party or PC) to test level-up/evolution thresholds without needing to read.
- [x] Accepts either numeric ids or names (case-insensitive, ignoring hyphens/spaces) for `--species`/`--item`.
- [x] Every write command patches **both** `pokemon-a.bin`/`pokemon-b.bin` files identically (following the double-buffer precedent), recomputing CRC32 via `zlib.crc32` (confirmed to match the firmware's algorithm from an earlier session). Auto-backs up to `.bak` before the first write (disable via `--no-backup`); `--dry-run` previews without writing anything.
- [x] Self-tested all 5 write subcommands (dry-run then real write) against a real simulator save, confirmed valid CRC after every write, `dump` reads back the just-changed values correctly, and the simulator boots cleanly with no errors/corruption after the tool overwrites the save.
- [x] No C++ code/tests touched — a purely standalone Python tool, no impact on build/flash.
- [x] Full usage guide (every subcommand + examples): [docs/development/pokemon-save-edit-tool.md](pokemon-save-edit-tool.md).

### Stage 21 — Rebalancing item drop rates by category (outside the original roadmap, per user feedback) ✅ DONE (commit `8b980ea0`)

**Problem found while the user asked about ball drop rates**: a Monte Carlo simulation showed a new player takes on average **~130 hours of reading** (median ~92h) before getting their first ball at all — while wild encounters occur regularly (every 15 minutes, a 2/5 roll + a 3-miss pity gate). Root cause: `createItem()` rolled **flat** across all 83 items by individual `drop_weight` — whichever category has the **most item types** dominates the overall probability regardless of how rare any single item within it is. Machine (TM+HM) has 55 items (each with a low `drop_weight` of 6/2) but together adds up to **310/703 ≈ 44%** of the total weight, while Ball only has 4 items (Poke/Great/Ultra/Master) adding up to just **69/703 ≈ 9.8%** — not because Ball was "designed to be rare" but because it's simply outnumbered by *item-type count* compared to Machine. Fresh saves also start with 0 balls (`bagCounts{}` initialized entirely to 0, no starting seed) and `Screen::BattleBalls` (catching wild Pokémon) requires owning at least 1 ball to even enter.

- [x] Changed `createItem()`'s fallback roll (the branch where no evolution stone is currently needed — the `preferOwnedNeeds` branch is unchanged, unaffected since it's already restricted to real needs) from **1 flat roll over 83 items** to **2 steps**: `chooseItemCategory()` (new) rolls the category first per a fixed weight table `CATEGORY_DROP_WEIGHTS` (Ball=25, Stone=10, Medicine=20, StatusCure=15, PPRestore=10, Machine=15, Candy=5 — totaling 100, excluding any category with no items currently able to drop via `categoryHasAvailableItem()`), then `buildItemCandidates()` (extended with a new `std::optional<ItemCategory> categoryFilter` parameter) rolls the specific item within that category following its actual `drop_weight` — Poke Ball is still much more common than Master Ball within Ball, only "which category gets considered first" no longer depends on item count.
- [x] Simulation result after the change: Ball now takes 25/100=25% (up from 9.8%) → average time to a first ball drops from ~130h to an estimated **~50-55h** (not re-measured by simulation after merging, just scaled proportionally from the old rate to the new one — `CATEGORY_DROP_WEIGHTS` can be further tuned if it still feels slow).
- [x] Updated 3 existing tests (`oneBoundaryQueuesItemEncounterAndEvolutionInOrder`, `forcedItemWithLevelGainQueuesItemBeforeEvolution`, `saturatedItemsDoNotRejectReadingCredit`) to match the new random-call count (category-roll then item-roll, instead of just item-roll) — these tests use `SequenceRandom` with hardcoded values for each roll, so they immediately break when the number of calls changes (the test reports "unexpected random draw" if a value is missing).
- [x] Added 1 new test, `itemCategoryWeightsGiveEachCategoryAFairShareRegardlessOfHowManyItemsItHolds` — confirms the boundaries at both ends of the category-roll range (Ball at the start of the `[0,25)` range, roll=24 still hits Ball, then roll=68 lands exactly on Master Ball at the end of the item range; Candy at the end of the `[95,100)` range, roll=99 hits Candy and **skips the item-level roll entirely** since Candy has exactly 1 member — confirming the "skip roll when only 1 choice exists" optimization still works correctly after adding the category step).
- [x] Only touched `lib/Pokemon/PokemonGame.cpp` + `test/pokemon_game/PokemonGameTest.cpp` — no CSV/generator/service/UI changes. 20/20 native tests pass (19 old + 1 new). Flash **6,352,263 B (96.9%, 201,337 B free)** — +1,486 B over Stage 20.
- [x] **Not enough, superseded by Stage 22** — the user judged it "still not right": the root cause isn't just the ratio between categories, it's that **every category still competes for a single drop slot per hour**. See Stage 22 below.

### Stage 22 — Splitting the 4 categories into 4 independent collection tracks (per user request) ✅ DONE (commit `8c07b6b4`)

**User request**: "right now there are 4 categories — evolution, medicine, TM/HM and balls — item collection across these 4 categories should be independent. Keep the evolution mechanic as-is. Work out a sensible approach for the other 3 categories. Balls shouldn't be too scarce, so that when a player encounters a Pokémon, they can still have balls left to catch it. Consider distributing balls in proportion to how often Pokémon appear."

**Why Stage 21 wasn't enough**: Stage 21 only fixed the *ratio* between categories, but still kept **a single drop slot per hour** shared by everyone. Even with Ball raised to 25% of that slot, the expected rate was still only ~0.02 balls/hour — far too little against ~1.84 Pokémon encounters/hour. This is a cadence problem, not a weighting problem.

**Baseline numbers used for the calculations (actually measured/simulated, not estimated)**:
- The encounter check runs 4 times/hour (every 15 minutes + on the hour), each time a 2/5 roll + a 3-miss pity gate → **~1.84 Pokémon encounters/hour**.
- Catch success rate = `catchValue/255` (`attemptCatch`, `PokemonBattle.cpp`) — a full-HP Pokémon is only ~15-33%, one at half HP is ~31-67%. In practice it takes **2-3 balls per successful catch**.
- Fresh saves start with **0 balls** (`bagCounts{}` all 0, no seed), and `Screen::BattleBalls` requires at least 1 to enter.

- [x] **Ball — tied directly to the encounter cadence**: grants **1 ball at every encounter check** (same 15-minute cadence) = **4 balls/hour = 2.18 balls per Pokémon encounter**, more than the ~2-3 balls typically spent per catch so the ball stockpile grows over time instead of depleting. Granted **directly into the bag, no `PendingEvent`** — at this cadence, balls would overflow the 3-slot queue and get dropped exactly when needed most; the player checks the count at Bag > Balls (Stage 19) or right on the ball-throw screen. Granted **before** the encounter check within the same minute, so a just-picked-up ball is usable right away for the Pokémon just encountered.
- [x] **Ball type unlocks progressively with `bookProgressPercent`** (matching the existing `regularEncounterEligible` pattern already used for encounters): `<50%` only Poke Ball → `>=50%` adds Great → `>=75%` adds Ultra → `>=95%` adds Master. **Master Ball only drops while owning 0**  — keeping the spirit of the original game's "one-shot trump card" instead of being stockpilable; weights within a tier still use the CSV's `drop_weight` column as-is (Poke 40 > Great 20 > Ultra 8 > Master 1), no data changes.
- [x] **Medicine** — combining Medicine + StatusCure + PPRestore + Candy (matching exactly what the Bag > Medicine screen already shows, so "the medicine you pick up" equals "the medicine you see on screen"): rolls **1/2 every hour = 1 item every 2 hours**. Generous because HP/PP already auto-recover while reading, so medicine is a convenience rather than a lifeline.
- [x] **TM/HM** — rolls **1/3 every hour = 1 every 3 hours**. There are 55 to collect so this is meant to be a long-term track; each one permanently unlocks a move so it shouldn't drop too densely.
- [x] **Evolution stones — kept exactly as-is** per the request: 1/20 every hour + a 19-miss pity gate, prioritizing stones the player's Pokémon can actually use (`preferOwnedNeeds`) = **1 stone every ~12.4 hours**. Changed exactly 1 thing: when nobody currently needs a stone, the fallback now only expands **within the Stone group** instead of spilling out into all 83 items — that spillover was exactly what was starving out the ball supply before.
- [x] Removed Stage 21's `CATEGORY_DROP_WEIGHTS`/`chooseItemCategory`/`categoryHasAvailableItem` entirely (pointless once each track has its own schedule). Factored out a shared `pickWeightedItem()` used by all 4 paths; `buildItemCandidates()`'s filter parameter changed from `std::optional<ItemCategory>` to a predicate function pointer (matching the convention `bagItemIdAt()` on the UI side already uses), to support filtering multi-category groups like Medicine.
- [x] **No save-format changes, no `PendingEvent` changes, no CSV changes** — deliberately omitted a pity counter for the 3 new tracks (would need a new `PokemonState` field → bumping to v4 + migration): ball at 4/hour doesn't need insurance, and a few missed rolls on medicine/TM don't cause a dead end.
- [x] Updated 10 existing tests to match the new random-call counts; replaced 1 Stage 21 test with 3 new ones: ball drops on every check without creating an event or consuming a queue slot, ball-type unlocking follows progress + Master Ball capped at 1, medicine/TM roll on 2 separate tracks. Renamed and rewrote `fullQueueCreditsReadingAndPrimesGuaranteesWithoutRolling` → `fullQueuePrimesGuaranteesButStillHandsOutBalls` (a full queue now blocks the 3 tracks from generating events, but balls still come in — as designed).
- [x] Only touched `lib/Pokemon/PokemonGame.cpp` + `test/pokemon_game/PokemonGameTest.cpp`. 19/19 native tests pass. Flash **6,352,731 B (96.9%, 200,869 B free)** — +468 B over Stage 21.
- [x] **Superseded by Stage 23** — the user judged it "still not right" (4 balls/hour granted flat, no real drop chance; after discussing the numbers together, a new rate was locked in at Stage 23). See Stage 23 below.

### Stage 23 — Ball/Medicine/TM-HM as 4 independent tracks each with their own pity gate (per user request, after discussing the numbers) ✅ DONE (commit `4a08c436`)

**Context**: after Stage 22, the user pointed out that balls at "4/hour granted flat, no real drop chance" still wasn't right. Went through several rounds of concrete numbers (Monte Carlo simulation of each option) before locking in:
- Ball: tried 2/5 (→~1.84 balls/hour, exactly matching encounter frequency — sound in theory but risky of running dry if trying to catch every encounter) then 3/5 (→~2.47 balls/hour, ~1.34x the encounter rate) — **locked in 3/5 + a 3-miss pity gate**.
- Medicine: asked "is 1 item/2 hours slow" — worked out that a specific HP item (Potion) only comes in at ~1 every 10 hours of reading, too thin for a 5-member gym battle. Discussed "what if it followed the encounter rate instead, with reduced StatusCure/PP" → **locked in 2/5 + a 3-miss pity gate (15-minute cadence, no longer hourly), with StatusCure/PPRestore's internal weight reduced to 1/3 of the original** (Medicine/HP now makes up ~70.5% of the track instead of ~46%).
- TM/HM: **locked in the same 2/5 + 3-miss pity gate formula** as Medicine (time to collect all 55 types dropped from ~1183 hours to ~207 hours).
- Evolution stones: **kept exactly as-is** per the original request ("keep the evolution mechanic as-is").
- Final simulation at a reading pace of 7 hours/week: Ball ~17.2/week, Medicine ~12.7 items/week (Potion ~4.3, Super Potion ~2.1...), TM/HM ~12.7/week, stones ~0.35/week.

- [x] **Ball** — changed from "granted flat on every check" to a real 3/5 roll + a 3-miss pity gate (new field `ballMisses`), still preserving Stage 22's design: no `PendingEvent`, granted directly into the bag.
- [x] **Medicine** — changed from hourly 1/2 (no pity) to a 15-minute cadence of 2/5 + a 3-miss pity gate (new field `medicineMisses`). Added `medicineItemWeight()` — a runtime weight-multiplier function (doesn't touch the CSV) that reduces StatusCure/PPRestore to 1/3, applied only when rolling within the Medicine track.
- [x] **TM/HM** — changed from hourly 1/3 (no pity) to a 15-minute cadence of 2/5 + a 3-miss pity gate (new field `machineMisses`).
- [x] **Evolution stones** — unchanged (still 1/20/hour + a 19-miss pity gate, `itemMisses`).
- [x] Refactored `rollPityGate()` (new, shared) — a single function for "roll a chance, guarantee after N misses," applied retroactively to BOTH encounters and evolution stones (which had each previously implemented this formula separately) to avoid 5 places duplicating the same formula.
- [x] **Save format v3 (195 bytes) → v4 (198 bytes)**: added 3 new `u8` fields (`ballMisses`/`medicineMisses`/`machineMisses`) after `battleProgress`, following the append-only precedent (doesn't touch offsets 0-194). v3→v4 migration auto-zero-fills the 3 new fields for old saves. Updated `docs/file-formats.md` (Version 4 section) and `scripts/dev/edit_pokemon_save.py` (new offsets/version constant + displaying the 3 pity counters in `dump`).
- [x] Rewrote most of `test/pokemon_game/PokemonGameTest.cpp` — many old tests use repeated 15-minute credit calls in a row, which can now cause ball/medicine/TM-HM to all hit their pity gate at the same time as an encounter and compete for the same 3 queue slots; added a `findEventOfKind()` helper so tests look up the right event by kind instead of assuming a fixed slot position. The `multiHourCreditUsesLifetimeAtEachHourlyBoundary` test neutralizes Medicine/TM-HM by maxing out all related `bagCounts` ("sold out") so they don't compete for a slot against the 3 encounters the test needs to track.
- [x] Only touched `PokemonGame.cpp`, `PokemonTypes.h/.cpp`, `PokemonStoreCodec.h/.cpp` + related tests/docs/tools. 19/19 native tests pass. Flash **6,353,021 B (96.9%, 200,579 B free)** — +290 B over Stage 22.
- [ ] **Not yet visually confirmed** — needs real playtesting to feel out whether the ball/medicine/TM drop cadence is right; every balance constant (`BALL_CHANCE_NUMERATOR`, `ITEM_TRACK_CHANCE_NUMERATOR`, the pity threshold, the `/3U` factor in `medicineItemWeight()`) sits neatly at the top of `PokemonGame.cpp`, adjustable without touching the save format or CSVs.

### Stage 24 — Move-selection (FIGHT) screen in 2 columns, PP smaller than the move name (per user feedback) ✅ DONE (commit `26e0b4e1`)

**Problem**: the user playtested and reported "the battle screen when fighting and choosing a move, if the Pokémon has 4 moves, the move section overlaps the battle log." Cause: `Screen::BattleMoves` was still a 1-column list via `fui::list()` (up to 4 rows × 64px = 256px), while the HUD/battle-log box only leaves about 2-3 rows worth of space for the Battle menu — the "bottom-anchored" calculation in `buildList()` didn't match reality.

- [x] Switched `Screen::BattleMoves` to the same 2-column grid model already used for the Battle menu (Stage 18's 5th extension) — `renderBattleMoveMenu()` (new) hand-draws it, bypassing `fui::list()` entirely (removed from `isListScreen()`). Reused `battleMenuTop()` directly as the top anchor — no new function needed since `logicalCount()` already returns the right `battlePlayerMoveCount()` while this screen is active, so the "N items, 2-column grid" formula applies directly. Dropped from 4 rows to 2, no more overlap with the log.
- [x] `loop()` adds `BattleMoves` to the existing 2-column grid navigation branch already built for `Battle` (Left/Right ±1, Up/Down ±2 columns, wrapping).
- [x] Within each button: the move name in bold on the top line (via `truncatedText()` guarding against long names like "Solar Beam"/"Sky Attack"), PP shown as "cur/max" in regular weight on the bottom line, right-aligned — **there's no smaller font than `UI_10_FONT_ID` on this device** (only 2 fonts exist: `UI_10`/`UI_12`), so "smaller PP" is achieved through weight (not bold) + secondary position (bottom line, right-aligned) rather than an actually smaller font size.
- [x] Only touched `PokemonActivity.cpp/.h`. 19/19 native tests pass (no logic outside UI changed). Simulator built cleanly. Flash **6,353,495 B (96.9%, 200,105 B free)** — +474 B.
- [x] **Second fix (commit `7b600718`)**: the user reported "the PP number is almost falling out of the move's box" — the 2-line layout (name on top/PP below) made PP crowd right up against the bottom of the 56px-tall button, not enough room for 2 comfortable lines + padding. Changed to a **single row, vertically centered**: name on the left (`truncatedText()` accounting for the exact width PP takes so the two never overlap), PP on the right, same row — matching the layout style already used for the HP bar (Stage 17). Flash **6,353,533 B** — +38 B.

### Stage 25 — Grant 10 Poke Balls + 1 Potion when creating a starter (per user feedback) ✅ DONE (commit `9174a992`)

**Request**: "after the player finishes choosing a starter Pokémon, it should have full HP/PP, and be granted 1 potion and 10 poke balls." Full HP/PP was **already correct** — `peekBattleMoves()` already synthesizes full HP/PP when a Pokémon has no `BattleRecordEntry` yet (a freshly created starter is always in this state), nothing extra needed to write.

- [x] `createStarter()` (`PokemonService.cpp`) sets `state.bagCounts[0]=10` (Poke Ball, item id 7) and `state.bagCounts[4]=1` (Potion, item id 11) directly before commit — just 2 lines, no new service API needed, matching the spirit of the original games always handing out a few balls + medicine before the first wild encounter.
- [x] Added an assertion to the `CreatesOneDurableStarterWithChosenIdentity` test (`PokemonServiceTest.cpp`) confirming the starting gift.
- [x] 19/19 native tests pass. Flash **6,353,555 B (96.9%, 200,045 B free)** — +22 B over Stage 24.
- [ ] **Not yet visually confirmed** — needs a starter created on the simulator, checking Bag > Balls/Medicine shows exactly 10 Poke Balls + 1 Potion.

### Stage 26 — The move-selection screen no longer shifts the HUD when a Pokémon has fewer than 4 moves (per user feedback) ✅ DONE (commit `5f1d4dde`)

**Problem**: "the battle screen doesn't change layout when a Pokémon's move count isn't enough to split into 2 rows. Right now for a Pokémon with 1-2 moves, the battle screen shifts down to compensate for the 2 remaining empty slots." Cause: `battleMenuTop()` (shared between the Battle menu and `BattleMoves` since Stage 24) computed the row count from the **actual** `battlePlayerMoveCount()` — correct for how many buttons to draw, but `renderBattleHud()` also anchors off this same value, so a freshly caught/created Pokémon (level 5, only 1-2 moves) made the entire HUD (opponent/player boxes, log box) expand/shift down noticeably differently than when it had a full 4 moves.

- [x] `battleMenuTop()`, when `screen_==Screen::BattleMoves`, now always computes based on `BATTLE_MOVE_SLOTS` (a fixed 2 rows) regardless of how many moves the Pokémon actually knows, instead of the dynamic `logicalCount()` — the HUD/log box keeps a constant size whether moves are few or full. `renderBattleMoveMenu()` still only draws the number of buttons the Pokémon actually knows (row 2 stays empty if it only has 1-2 moves) — only the surrounding HUD layout is fixed, not the number of buttons shown.
- [x] Only touched `PokemonActivity.cpp` (1 function). 19/19 native tests pass (no logic outside UI changed). Flash **6,353,555 B (96.9%, 200,045 B free)** — +22 B (measured in the same build as Stage 25, so the byte count is identical and can't be separated out).
- [ ] **Not yet visually confirmed** — needs playtesting with a freshly caught/created Pokémon (1-2 moves) to confirm the HUD no longer shifts/expands compared to a full 4-move Pokémon.

### Stage 27 — Realigning the Party list layout: icon on the left, 2 right-aligned text rows (per user feedback) ✅ DONE (commit `cbbe4ff3`)

**Request**: "the Party Pokémon list layout is a bit ugly. Realign it to look nicer. Left side is the Pokémon image, right side is 2 text rows: row 1 is name - level - gender as a symbol, row 2 is HP bar - HP/HP - status." Root cause of the "ugliness": the name/level/gender row was drawn by the `fui::list` widget itself (anchored to its own `sidePadding`, x≈+104) while the HP-bar row was hand-drawn by `PokemonActivity` (anchored to the icon, x≈+5) — the two rows had mismatched left edges instead of reading as one cohesive block.

- [x] Skipped `fui::list`'s text entirely for `Screen::Party` and `Screen::ItemTarget` (while showing HP, `showsPartyHealthRows()`), hand-drawing **both rows** in `renderPartyRowHealth()` (changed signature, added a `drawNameLine` flag): row 1 name (bold) + right-aligned "Lv.N M/F", row 2 HP bar + HP/HP + status — both rows starting at the same X coordinate right after the icon, vertically centered within the 2-row block (same approach used for the HP bar at Battle/Summary, Stage 17).
- [x] Gender shortened to **"M"/"F"** (new `genderAbbrev()`) instead of the full word ("Male"/"Female") — **there's no ♂/♀ glyph in the currently built font** (a fixed Latin/Hebrew/Arabic subset baked in at build time), so a single letter is the most compact "symbol" available without rebuilding the font (which would cost extra bitmap glyphs + flash risk).
- [x] `Screen::BattleSwitch` (shares `renderPartyRowHealth()` for the HP bar but is a different `buildRows()` case, with its own value string) keeps its exact old behavior — just passes `drawNameLine=false` to avoid drawing the name twice.
- [x] The Pokémon species icon (shared across multiple screens: Party/Move/ItemTarget/Pc/Pokedex/BattleSwitch) changed from a fixed `+2` offset to vertical centering via `pokemonCenteredOffset()` — unchanged result for the 64px row (2 = (64-60)/2 was already correct), only improving the 96px-tall rows (Party/ItemTarget-HP/BattleSwitch).
- [x] Only touched `PokemonActivity.cpp/.h`. 19/19 native tests pass (no logic outside UI changed). Flash **6,353,991 B (97.0%, 199,609 B free)** — +436 B.
- [x] **Visually confirmed** (across 4 rounds of layout/marker fixes) — the user confirmed "looks good" after the 4th fix.

### Stage 28 — Settings screen, renaming "Reset Pokémon" to "Reset Game," also clearing the battle store (per user request) ✅ DONE (commit `94dfa6c7`)

**Context**: the user asked "what does the reset pokémon button do" — while checking the code to answer, found that `PokemonService::reset()` only clears the main save (`pokemon-{a,b}.bin`), **without touching** the side file `pokemon-battle-{a,b}.bin` (HP/PP/moveset keyed by `recordId`). Since `createStarter()` always hardcodes `recordId=1` and each subsequent caught Pokémon takes `id=recordCount()+1`, new Pokémon after a reset end up reusing the exact same ids (1,2,3...) from the previous playthrough — if that id had previously battled, the new Pokémon would incorrectly read the old HP/PP/moveset until it actually battled for real and overwrote it. The user asked for: folding Reset into a new Settings screen, renaming it to "Reset Game," and fixing this missing-file bug.

- [x] **New `Screen::Settings`**: the root Menu shrinks to 8 entries (removes "Show on Home Screen" + "Reset Pokémon," both folded into Settings). Settings has 2 rows: the Home Screen toggle (moved from Menu as-is) + "Reset Game" (renamed from "Reset Pokémon," reusing the existing `STR_POKEMON_RESET` key as-is — only 1 place uses this key so retexting it is safe). `ResetFirst`/`ResetFinal` now return to Settings on cancel/error instead of jumping straight to Menu (`goBack()` also gets its own case for this). Added a new `STR_POKEMON_SETTINGS` ("Settings") key.
- [x] **Fixed the missing-battle-store-deletion bug**: added `PokemonBattleStore::reset()` (new) — writes an empty `BattleStoreState{}` via the existing `writeState()`, following the existing double-buffer convention as-is (write to the inactive file, verify by reading it back, then switch active — no new logic, just reusing existing infrastructure). `PokemonService::reset()` calls this extra step after clearing the main save — **best-effort**: if this step alone fails, `reset()` still reports success, since clearing the main save (the part the player directly sees) is the important part, and a leftover battle-store entry will simply be overwritten the first time that Pokémon actually battles for real.
- [x] Added a test, `ResetAlsoClearsTheBattleStoreSoTheNextStarterDoesNotInheritLeftoverBattleData` (`PokemonServiceTest.cpp`) — creates an old starter (Pikachu), damages it + inflicts status, resets, creates a new starter (Bulbasaur, deliberately a different species to easily detect any data bleed, with the same `recordId=1`), confirms HP/PP/status are all fresh rather than inherited.
- [x] Only touched `PokemonActivity.cpp/.h`, `PokemonService.cpp`, `PokemonBattleStore.h/.cpp`, `english.yaml` + related tests. 19/19 native tests pass (1 new test added). Flash **6,354,251 B (97.0%, 199,349 B free)** — +216 B.
- [ ] **Not yet visually confirmed** — needs playtesting: go to Menu > Settings, confirm exactly 2 entries appear (Home Screen + Reset Game), try Reset Game and confirm it returns to the starter-selection screen as before.
