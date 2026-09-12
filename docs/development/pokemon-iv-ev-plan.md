---
title: IV/EV Implementation Plan
parent: Development
nav_order: 9
---

# Gen 1 IVs/EVs — Implementation Plan

The last item on [the Gen 1 authenticity roadmap](pokemon-gen1-authenticity-roadmap.md).
Written after a read-only research pass over the current save-format, stat-pipeline, and
data-fetch code — see that research folded into the decisions below rather than duplicated
as a separate document. **Nothing has been implemented yet — this is the plan to review
before branching off real code.**

## Decisions already made (with the user, before writing this plan)

- **EVs are simplified, not Gen 1-faithful.** Real Gen 1 EVs run 0-65,535 per stat with a
  `floor(sqrt(EV)/4)` bonus curve. This project caps EVs at **0-255 per stat** with a flat
  `EV/4` bonus instead — no square root. This isn't a random simplification: the max bonus
  either formula produces is **the same, 63** (`sqrt(65535)/4 → 63`, `255/4 → 63`), so the
  ceiling is identical to real Gen 1; only the climb curve and the storage cost (1 byte/stat
  instead of 2) change. Matches this project's established precedent of rejecting a real Gen
  1 formula when its native numbers don't fit this game's much slower, reading-driven pace
  (same reasoning `battleVictoryXp()` used to reject the real species-yield XP formula).
- **Gym/Elite Four/Champion trainer Pokémon get a fixed IV of 15 in every stat, EV 0.**
  Mirrors the real games' own "trainer Pokémon have high, fixed DVs" convention, and avoids
  needing any persistence at all for non-`PokemonRecord` battle combatants (gym teams are
  defined in `PokemonGymData.cpp`, never saved as records). Wild encounters get a genuinely
  random IV roll (0-15 per stat) same as a caught Pokémon would.
- **IV/EV display ships in this same phase**, added to the existing `Screen::Summary` (see
  §6 below) rather than deferred as follow-up polish.

## 1. Storage: a new side file, not a `PokemonRecord` change

`PokemonRecord` is genuinely out of room: 47 of its 48 bytes are real fields, and the 1
remaining byte (offset 47) is already spent as a corruption-detection sentinel
(`decodeRecord()` requires it to be exactly `0`) — nowhere near the ~3 bytes even a packed
IV set needs, and there is **no existing precedent for versioning `PokemonRecord` itself**
(the append-then-bump-version pattern that's been used 3 times already only applies to
`PokemonState`, a different struct with an explicit version field `PokemonRecord` doesn't
have). Growing `PokemonRecord` past 48 bytes is possible but would mean inventing that
versioned-record machinery from scratch — real, but disproportionate engineering for this.

Reusing the existing `pokemon-battle-{a,b}.bin` side file (which already stores per-Pokémon
HP/PP/status/moveset) was considered and rejected: that file only tracks the 6 **active
party members** and is explicitly designed as "reconstructible, lossy-if-corrupted, no
permanent identity lives here" (its own header comment). IVs need to be permanent and
available for **every** stored Pokémon, PC box included, which conflicts with that file's
whole reason for existing.

**Decision: a new file, `pokemon-ivev-{a,b}.bin`**, double-buffered exactly like the two
existing side files (`PKIV` magic, a version byte, `sequence`/CRC32 the same way
`pokemon-battle-{a,b}.bin` already does), keyed by `recordId`, covering **every** record —
architecturally closer to the main `pokemon-{a,b}.bin` store (arbitrary count, not a fixed
6-slot array) than to the battle side file.

**Per-entry layout (12 bytes, no padding needed):**

| Bytes | Field |
|---:|---|
| 4 | `recordId` (u32, key) |
| 3 | IVs — 5 stats × 4 bits (HP/Attack/Defense/Special/Speed), packed; 4 spare bits |
| 5 | EVs — 5 stats × 1 byte each (0-255) |

A missing entry (not yet rolled, or a pre-existing save from before this feature) means
"IV 0 / EV 0 for this record" — i.e. **exactly today's current stats**, so old saves are
unaffected until each of their Pokémon is backfilled (see §5).

## 2. Stat formula changes

`battleMaxHp`/`battleWorkingStat` (`lib/Pokemon/PokemonBattle.h/.cpp`) both gain an `iv` and
`ev` parameter:

```cpp
uint16_t battleMaxHp(uint8_t baseHp, uint8_t level, uint8_t ivHp, uint8_t evHp);
uint16_t battleWorkingStat(uint8_t baseStat, uint8_t level, uint8_t iv, uint8_t ev);
```

Formula (simplified EV curve, real Gen 1 IV term and level scaling otherwise):

```
HP    = floor((2*(baseHp+ivHp)   + evHp/4) * level / 100) + level + 10
other = floor((2*(baseStat+iv)   + ev/4)   * level / 100) + 5
```

`iv=0, ev=0` reduces exactly to today's existing formula — this is the backward-compat
invariant every existing test for the current formula must still satisfy unchanged.

**Fan-out**: 11 production call sites need the new arguments threaded through (not just the
2 in the pure engine) — `PokemonBattle.cpp` (damage calc, turn-order speed), 3 in
`PokemonService.cpp` (battle-entry synthesis, catch, reading-credit HP recompute), and 4 in
`PokemonActivity.cpp` (battle setup ×2, Summary HP display, Party/target HP peek), plus
their tests. `BattleCombatant` gains 10 new in-memory fields (5 IV + 5 EV, plain `uint8_t`
each — no packing needed here, this struct was never size-constrained, only the on-disk
format is) so the 2 in-engine call sites have everything they need without extra plumbing.

## 3. IV rolling

A new `PokemonService` helper, `rollIvSet(RandomSource&)`, returns 5 fresh `0-15` rolls.
Called once, immediately, whenever a `PokemonRecord` is first created — starter pick, wild
catch, and any gift/event Pokémon path — and written straight to the new side file. Wild
*encounters* (not yet caught) also get a rolled IV set for their `BattleCombatant` at
encounter time (so the fight itself already reflects the individual wild Pokémon's variance,
same as the real games), but that roll is only persisted if the catch succeeds.

Trainer (gym/Elite Four/Champion) `BattleCombatant`s skip rolling entirely — `setupBattleOpponent()`
hardcodes IV 15 / EV 0 for that path, per the decision above.

## 4. EV accumulation

`PokemonService::awardBattleXp()` currently takes `(recordId, opponentLevel,
isTrainerBattle)` — no species identity, which real Gen 1 EV yield needs (EVs are yielded
**per defeated species**, not per level). Both of its call sites
(`PokemonActivity.cpp:838,1496`) already have `battleOpponent_.speciesId` live in scope right
where the call happens, so this is a one-parameter signature widening
(`awardBattleXp(recordId, opponentLevel, isTrainerBattle, opponentSpeciesId)`), not new
plumbing to reach the data.

Needs a new per-species **EV yield table**, fetched alongside the existing base-stats data:
`scripts/fetch_pokemon_battle_data.py` already loops every species' PokeAPI `stats` array to
pull `base_stat`; the real per-stat EV yield is the sibling `effort` field on that same
array, already being iterated — a small addition to an existing working loop, not a new
endpoint. Lands in a new `scripts/data/pokemon-ev-yield.csv` (or an added column to
`pokemon-stats.csv`) and a generated `EvYield` struct mirroring `BaseStats`.

**Must be spot-checked once fetched**, not trusted blindly: real Gen 1 EV yields are small
per-species integers (e.g. Bulbasaur → 1 Special EV, Chansey → 3 HP EVs) and the existing
fetch script has already had to hand-correct 2 other PokeAPI-vs-Gen-1 mismatches before, so
this isn't guaranteed to be pixel-perfect on the first fetch.

Accumulation itself: on a win, look up the defeated species' EV yield, add it into the
winner's stored EV entry per stat, **saturating at 255** (not wrapping) — same
"defeating a same-species opponent repeatedly still just approaches the cap, never breaks
it" behavior the real games have, just with a lower, simplified ceiling.

## 5. Migration for existing saves

No existing Pokémon has ever had an IV or EV — every current save's Pokémon effectively
reads as `IV=0, EV=0` today (the new side file simply has no entries for them yet). Two
options:

1. **Leave them at 0 forever** (fully honest to "IVs are rolled once, at creation, and
   that's it" — these Pokémon were created before the mechanic existed, so they never got a
   roll). Simple, but creates a permanent two-tier population where every already-caught
   Pokémon is quietly weaker than anything caught after the update ships.
2. **Backfill on first load**: the first time a pre-existing record is looked up and found
   to have no IV/EV entry, roll a fresh IV set for it right then (as if it were being caught
   for the first time) and persist it immediately.

**Recommended: option 2 (backfill on first load).** It's a small addition to the same
"look up or create" path IVs already need for brand-new records, and avoids permanently
penalizing anyone who was already playing before this ships. Worth flagging to the user
explicitly when this phase actually starts, since it is a small deviation from strict "Gen 1
never re-rolls an IV" purity — existing Pokémon get exactly one implicit re-roll the first
time this code runs for them, never again after.

## 6. UI: Summary screen

`Screen::Summary` (`PokemonActivity.cpp:2414` onward) currently shows species art, name,
nickname, dex/level/gender, an HP bar, type(s), EXP progress, "Met" info, evolution state,
and moves+PP — **no numeric Attack/Defense/Special/Speed block exists at all today**, so
this isn't extending an existing stat row, it's new UI. The screen already has a repeatable
`drawField(label, value)` helper (`:2444-2448`) that a new IV/EV block can reuse directly
rather than inventing new layout primitives.

Exact wording/layout (e.g. "IV" and "EV" as compact per-stat rows, or a single summarized
line) is a detail to nail down once this stage actually starts, not before.

## 7. Suggested staging

Given the fan-out (new file + codec + fetch-script change + 11 call-site updates + new UI),
this is closer in size to the original battle-roadmap's multi-stage work than to the
single-PR shape critical hits/stat stages/recoil each shipped as. Proposed stages, each
independently testable:

1. **Data + storage**: extend the fetch script for EV yield, regenerate species data; add
   the new `pokemon-ivev-{a,b}.bin` codec/store (mirroring the existing side-file pattern)
   with round-trip unit tests. No behavior change yet — nothing reads this data.
2. **Engine**: widen `battleMaxHp`/`battleWorkingStat`, add IV/EV fields to
   `BattleCombatant`, thread IV/EV through all 11 call sites. Verify `iv=0,ev=0` reproduces
   today's exact stat numbers (the backward-compat invariant) before anything else.
3. **Rolling + accumulation**: wire IV rolling into every record-creation path, EV
   accumulation into `awardBattleXp()`, trainer-team fixed IV 15/EV 0 into
   `setupBattleOpponent()`.
4. **Migration**: backfill-on-first-load for pre-existing records.
5. **UI**: the new Summary IV/EV display block.
6. **Full verification**: native suite, a full clean `pio run -e pokemon-x3` build, and
   (per this project's standing rule for anything touching save format) real
   simulator/device confirmation before merging — this is the first feature in this whole
   roadmap effort that changes what gets *written to disk* for every single Pokémon, so it
   deserves the same care the original reading-XP fix and the v0.5.0 gender work got.

None of this needs to happen in one sitting — each stage above is a plausible standalone
branch, same as every other item on the roadmap so far.
