---
title: Pokémon Red Authenticity Gap Analysis
parent: Development
nav_order: 8
---

# Pokémon Red Authenticity Gap Analysis — TODO Roadmap

An audit of this game's battle engine (`lib/Pokemon/PokemonBattle.cpp`) against the real
Pokémon Red/Blue (Gen 1) mechanics it's modeled on, done at the user's request after
shipping battle EXP (`v0.6.0`). Lists what's genuinely missing, what's already correct but
easy to mistake for missing, and a rough priority order for tackling the gaps in future
phases. Numbers below come from reading the actual code and the real move/item data this
project already fetched from PokeAPI (`scripts/data/pokemon-moves.csv`,
`pokemon-items.csv`), not estimates.

Every gap here was already flagged as an explicit **scope boundary** in
`docs/development/pokemon-battle-roadmap.md`'s Stage 2 writeup (see "Known scope
boundaries") — this doc doesn't change that call, it just re-examines it with real data now
that the game has more mileage, and turns it into a prioritized TODO instead of a one-line
disclaimer.

---

## What's already correct and authentic (no change needed)

Worth stating plainly since one of the items requested for review turns out to already be
correct, not a gap:

- **The single "Special" stat is accurate to Gen 1.** `PokemonBattle.cpp:116-117` uses
  `special` as both the attacking stat for special moves and the defending stat against
  them. This is **not a bug** — Gen 1 really did have one shared Special stat; the split
  into Special Attack / Special Defense is a **Gen 2** mechanic. Adding a split would move
  this game *away* from Gen 1 authenticity, not toward it. If this is still wanted, treat it
  as a deliberate "modernize past Gen 1" decision, not a correctness fix — flag explicitly
  before starting, since it changes the stat formulas, the stats CSV, and every damage
  calculation.
- **Stat growth from leveling already works and is already Gen 1's real formula.** This was
  double-checked directly against the code in the conversation that led to this doc:
  `battleMaxHp()`/`battleWorkingStat()` in `PokemonBattle.cpp:293-299` are the authentic
  Gen 1 no-IV/EV stat formulas, and the per-minute XP loop already caps at level 100
  correctly. Leveling up does make a Pokémon meaningfully stronger already (confirmed with
  concrete Pikachu numbers: level 5→100 is roughly a 10-13x jump in HP/Attack/Speed). If
  "cộng stat khi lên lvl" meant this, it's already done — the real gap is *individual*
  variance between two same-species same-level Pokémon (IVs), not growth over levels.
- **Type effectiveness, 6 status effects, real base stats/catch rates, authentic
  gym/Elite Four/Champion rosters** — all already sourced from real data and modeled
  correctly (see the battle roadmap for detail). Not re-litigated here.

---

## Confirmed gaps, with real impact numbers

### 1. Stat stages (Growl, Swords Dance, Reflect, etc.) — ✅ done (`v0.8.0`, branch `feat/stat-stages`)

Implemented via `BattleCombatant`'s 6 new stage fields (Attack/Defense/Special/Speed/
Accuracy/Evasion, `-6..+6`, reset every battle/switch for free by the existing
`= BattleCombatant{}` reset pattern), a hand-authored 22-entry `STAT_CHANGE_TABLE` in
`PokemonBattle.cpp` covering the self-buff and opponent-debuff status moves (Swords Dance,
Growl, Agility, Barrier, Acid Armor, Amnesia, Harden, Sharpen, Meditate, Screech, Double
Team, Minimize, Withdraw, Defense Curl, Tail Whip, Leer, Sand Attack, Smokescreen, Flash,
Kinesis, String Shot), plus Haze (id 114) as a hardcoded full-reset special case. Real Gen 1
non-linear multiplier tables (`applyStatStage`/`applyAccuracyEvasionStage`): `(2+stage)/2`
up / `2/(2-stage)` down for Attack/Defense/Special/Speed (25% floor, 400% ceiling);
`(3+stage)/3` up / `3/(3-stage)` down for Accuracy/Evasion (33% floor, 300% ceiling). Wired
into `computeDamage()`, `stepBattle()`'s turn-order Speed check, and `resolveAction()`'s
accuracy check. Also implements the real (not simplified) Gen 1 crit quirk: a critical hit
ignores whichever staged value would hurt the attacker — a negative Attack/Special stage on
the attacker, or a positive Defense/Special stage on the defender — while still applying any
stage that helps.

**Deliberately out of scope for this pass** (kept as smaller, separate future work since
they're either self-heal/switch-forcing/move-copying mechanics or screen effects rather than
a stage change): Recover, Soft-Boiled, Rest, Substitute, Teleport, Mimic, Metronome, Mirror
Move, Transform, Conversion, Disable, Leech Seed, Whirlwind, Roar, Focus Energy, Mist,
Reflect, Light Screen.

### 2. Critical hits — ✅ done (`v0.7.0`, branch `feat/critical-hits`)

**Real Gen 1 behavior**: crit chance is `baseSpeed / 512` (or `/64` for a handful of
"high-crit" moves like Slash, Razor Leaf, Crabhammer, Karate Chop), and a crit does 2x
damage using the attacker's *unboosted* stats (a real, well-known Gen 1 quirk — negative
stat stages on the attacker are ignored on a crit).

**Implemented as designed**: `rollCriticalHit()` in `PokemonBattle.cpp` uses the simplified
`baseSpeed/512` / `baseSpeed/64` thresholds against a 512-wide roll, capped at 511 (no real
Gen 1 base Speed comes close to needing the cap). The high-crit list is the real 4 Gen 1
moves (Karate Chop id 2, Razor Leaf id 75, Crabhammer id 152, Slash id 163), hand-authored
rather than a new CSV column — same rationale as `PokemonTypeChart.cpp`. A crit doubles the
already-computed base damage in `computeDamage()`, before STAB/type/the random 85-100%
variance roll — mathematically equivalent to doubling Gen 1's level term, since level is a
pure multiplicative factor in the real formula. The "ignore negative stat stages on crit"
quirk doesn't apply yet since stat stages (item 1 below) aren't modeled.

`BattleActionResult` gained a `critical` bool (kept separate from the `event` enum, since a
hit can be both critical and super/not-very effective at once); the UI shows "A critical
hit!" (`STR_POKEMON_CRITICAL_HIT`) as its own clause. One pre-existing test needed a fix:
`ZERO_RANDOM` (always rolls 0) now also means "always crit" for any Pokemon with positive
Speed, which changed an old test's assumptions about who acts first and survives.

### 3. IVs/EVs — ✅ done (`v0.10.0`, branch `feat/iv-ev-system`)

Implemented per [the IV/EV implementation plan](pokemon-iv-ev-plan.md) (read that doc for
the full design/staging reasoning) - summary of what shipped:

- **Storage**: a new side file, `pokemon-ivev-{a,b}.bin` (`PokemonIvEvStoreCodec.h/.cpp`,
  `PokemonIvEvStore.h/.cpp`), double-buffered exactly like the existing
  `pokemon-battle-{a,b}.bin`/`pokemon-{a,b}.bin` stores, keyed by `recordId`, covering every
  record (party and PC box both) up to 1024 entries - the same hard cap
  `PokemonService::resolveEncounter()` already enforces on total records. `PokemonRecord`
  itself was untouched (it only had 1 spare byte, nowhere near enough).
- **IVs**: 0-15 per stat, rolled once via `rollIvSet()` the first time a record is looked up
  with no existing entry (`PokemonService::ensureIvEv()`) - this covers both a brand-new
  catch/starter and backfilling a record created before this feature existed, with no
  separate migration step needed.
- **EVs**: a deliberate simplification of real Gen 1 (which ran 0-65,535 per stat with a
  `floor(sqrt(EV)/4)` bonus curve): capped at 0-255 with a flat `EV/4` bonus instead. Both
  formulas reach the same maximum bonus (63), so the ceiling is authentic even though the
  climb curve and the on-disk cost (1 byte/stat instead of 2) are simpler. EV yield per
  species comes from PokeAPI's own `effort` field (a modern-games mechanic, not literally
  Gen 1's own "Stat Experience" formula - see `BaseStats`'s doc comment in
  `PokemonBattleTypes.h`), fetched into `scripts/data/pokemon-stats.csv` alongside base
  stats, and accumulated in `PokemonService::awardBattleXp()` on every battle win,
  independent of the XP/level-100 cap.
- **Trainer teams** (Gym/Elite Four/Champion, defined in `PokemonGymData.cpp`, never
  persisted as records): fixed IV 15 in every stat, EV 0 - mirrors the real games' own
  "trainer Pokemon have high, fixed DVs" convention, and needs no persistence.
- **Display**: the Summary screen gained two new compact rows (one line each for IV/EV,
  order spelled out in the value itself - `HP12 A3 D15 S0 Sp8`) rather than a wholly new
  screen, reusing the existing `drawField()` helper.

**Backward compatibility**: `battleMaxHp()`/`battleWorkingStat()` both default `iv`/`ev` to
`0`, reproducing the exact pre-this-feature formula for any caller that hasn't been taught a
Pokemon's real IV/EV yet.

### 4. Recoil, multi-hit moves, Struggle — ✅ done (`v0.9.0`, branch `feat/recoil-multihit-struggle`)

Take Down/Double-Edge/Submission now recoil 1/4 of the damage they deal (Struggle recoils a
full 1/2 — both real Gen 1 fractions), via a hand-authored `RECOIL_TABLE` in
`PokemonBattle.cpp`. Comet Punch/Fury Attack/Double Slap/Pin Missile/Barrage/Fury Swipes now
roll the real Gen 1 2/3/4/5-hit distribution (3/8, 3/8, 1/8, 1/8 via `rollMultiHitCount()`,
not a flat spread); Twineedle always hits exactly twice rather than rolling. Each hit gets
its own independent crit roll and damage-variance roll, and the sequence stops early if the
defender faints partway through (matching Gen 1 — a multi-hit move never "overhits" a fainted
target).

Struggle (move id 165, already present in this game's own move data) is now a real forced
action rather than a silent skipped turn: passing `pokemon::BATTLE_MOVE_SLOTS` itself as a
move slot to `stepBattle()`/`stepOpponentOnlyTurn()` forces it — the exact same sentinel
`chooseOpponentMoveSlot()` already returned for "the AI has no usable move," so the opponent
side got real Struggle for free just by making `resolveAction()` honor that sentinel instead
of treating it as `MoveHadNoPp`. The player side needed a small `PokemonActivity.cpp` change:
once every learned move is out of PP, selecting FIGHT no longer opens the move-picker menu at
all — it resolves a forced Struggle turn immediately, matching how the real games never offer
a choice once you're fully out of PP.

`BattleActionResult` gained `hitCount` (0 for any non-multi-hit move) and `recoilApplied`,
both surfaced as independent message clauses in `formatBattleActionLine()` alongside the
existing `critical` clause (a hit can be multi-hit, critical, and recoiling all in the same
turn). New i18n strings for the hit-count and recoil messages.

### 5. Minor real-Gen-1 items not yet in the item set — ✅ done (`v0.11.0`)

**PP Up** (raises a move's max PP by 20% per use, up to 3 uses, real Gen 1 item) now exists
(item id 84, `PP_UP_ITEM_ID`). `maxPpFor(basePp, ppUpCount)` in `PokemonBattle.h/.cpp` is the
real Gen 1 formula (`floor(basePp/5)` per use - a 40-PP move goes 40→48→56→64).
`BattleRecordEntry` gained a per-slot `ppUp[4]` counter (bumping the battle-store file to a
new version, v2, handled the same append-and-branch-on-version way `PokemonState`'s own
ladder already does); `PokemonState` itself gained a new `ppUpCount` field (v5) rather than
growing `bagCounts` (which would have shifted every byte after it in every already-shipped
save). **Now wired into the automatic reading-time item-drop pool** (`v0.11.1`) - PP Up
joined the shared "Medicine" drop track (`isMedicineCategory()` in `PokemonGame.cpp`), the
same way every other medicine item drops from reading; `itemCountIsFull()`/
`incrementItemCount()` already routed it to `state.ppUpCount` instead of `bagCounts`, so no
special-casing was needed in the drop logic itself. This did perturb
`PokemonGameTest.cpp`'s fragile, hand-scripted hour-by-hour drop sequence as expected, fixed
by capping `ppUpCount` alongside `bagCounts` in the one test that relies on "every medicine
item is sold out." In the Bag UI, PP Up moved from its own dedicated category to a trailing
row inside Bag → Medicine (its count lives in `ppUpCount`, not `bagCounts`, so it's appended
as a synthetic row rather than joining the generic per-category item list).

---

## Suggested order (cheapest + highest-impact first)

1. ✅ **Critical hits** — done (`v0.7.0`).
2. ✅ **Stat stages** — done (`v0.8.0`). Biggest move-roster impact (revives ~25% of the moveset), moderate
   cost, no save-format changes (battle-only state).
3. ✅ **Recoil / multi-hit moves / a real player-facing Struggle** — done (`v0.9.0`).
4. ✅ **PP Up** — done (`v0.11.0`).
5. ✅ **IVs/EVs** — done (`v0.10.0`). See
   [the IV/EV implementation plan](pokemon-iv-ev-plan.md) for the full design (a new side
   file, not a `PokemonRecord` change - `PokemonRecord` only had 1 spare byte, nowhere near
   enough).

**Every item on this roadmap is now done, including PP Up's acquisition path** (`v0.11.1`) -
no open threads left.
