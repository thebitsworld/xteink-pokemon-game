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

---

## Addendum: a second audit found real move-data bugs, not just gaps

After this roadmap's own 5 items shipped, a follow-up audit of `PokemonBattle.cpp` against
`scripts/data/pokemon-moves.csv` (prompted by comparing this game's full item/move set
against real Pokémon Red rather than just its battle-formula math) found something sharper
than an authenticity gap: **12 real Gen 1 moves store `power = 0`** in the move data because
their real damage isn't power-based (OHKO moves, fixed/level-based damage, Counter, Bide,
weight-based Low Kick) - and since `computeDamage()` multiplies straight by `move.power`,
every one of them was dealing a useless ~2 flat damage regardless of the target. Not a
"less authentic" shortcut - a real bug, unnoticed because nothing exercised these specific
moves before.

**Fixed** (`v0.11.2`, branch `feat/fixed-damage-moves`): a new `FIXED_DAMAGE_TABLE`/
`FixedDamageKind` in `PokemonBattle.cpp` gives 9 of these their real Gen 1 behavior -
Guillotine/Horn Drill/Fissure (OHKO, with the real level-based accuracy rule: always misses
if the user's level is below the target's, otherwise `30 + level difference`% to connect,
and it still respects type immunity), Seismic Toss/Night Shade (damage = user's level),
Dragon Rage (40)/Sonic Boom (20) (fixed flat damage), Psywave (random 1 to 1.5x the user's
level), Super Fang (halves the target's current HP), and Counter (reflects 2x the last
physical damage taken this same turn - needed one new transient field,
`BattleCombatant::lastPhysicalDamageTaken`, reset at the top of every turn). Low Kick gets a
simplified fixed power (50) instead of its real weight-based formula, since this project has
no per-species weight data and can't fetch it from PokeAPI in this environment - a
documented simplification, not the exact mechanic. Bide is deliberately NOT included here -
it needs the same multi-turn charge state as two-turn moves like Solar Beam, tracked as a
separate, larger follow-up rather than a one-off formula fix. New
`BattleLogEvent::OneHitKo`/`MoveFailed` events surface "It's a one-hit KO!" and "But it
failed!" (Counter with nothing to reflect). 10 new tests in `PokemonBattleTest.cpp`.

This came out of a broader "what's still missing vs. real Pokémon Red" review that also
flagged flinch, in-battle stat-boost items (X Attack/Defense/Speed/Special, Guard Spec.,
Dire Hit), drain moves, and Explosion/Self-Destruct's faint+halve-Defense quirk as cheap
follow-ups, and trapping/two-turn/Substitute/Disable/Transform/self-heal/screen moves as
larger ones - not tracked in this doc since they're beyond this roadmap's original battle-
formula scope, but worth knowing this fix came from the same review.

The stat-boost items shipped separately as `v0.12.0`. The three remaining cheap follow-ups
from that same review all shipped together as `v0.12.1`: **flinch** (Stomp/Rolling Kick/
Headbutt/Bite/Bone Club/Hyper Fang - a hand-authored `FLINCH_TABLE` since PokeAPI's move
data doesn't carry a flinch-chance field at all, unlike the 6 real status ailments; a new
transient `BattleCombatant::flinched` flag, checked and cleared at the very top of
`resolveAction()` before even a status-prevention check, since flinch takes priority and
costs no PP), **drain moves** (Absorb/Mega Drain/Leech Life/Dream Eater - heal the attacker
half the damage dealt, minimum 1, capped at max HP; new `BattleActionResult::drainApplied`),
and **Explosion/Self-Destruct's two real Gen 1 quirks** (the target's Defense is halved for
that one hit - `computeDamage()` gained a `moveId` parameter just for this check; the user
faints unconditionally from using the move, even on a miss, applied right after PP is spent
and before the accuracy roll). The self-KO surfaced one real pre-existing edge case in
`stepBattle()`: its early-return branches after each side's action only ever checked the
*other* side's HP, so a self-destructing attacker that also happened to KO its target in the
same hit would have been misreported as a plain win instead of the simultaneous-KO result
`finishTurn()` already handles correctly for the equivalent end-of-turn case - fixed by
checking both sides' HP in the right order in all 4 of those branches. 6 new tests in
`PokemonBattleTest.cpp` (2 for flinch, 1 for drain caps/healing, 3 for the self-destruct
quirks including the simultaneous-KO fix).

The larger, multi-turn-state group mostly shipped too, as `v0.13.0`: **two-turn charge
moves** (Fly/Dig/Solar Beam/Skull Bash/Sky Attack - a new `BattleCombatant::forcedMoveId`/
`forcedTurnsRemaining` pair drives the automatic charge-then-release, shared with trapping
moves below; Fly/Dig additionally set `invulnerable`, simplified to "everything just misses"
during the charge turn rather than modeling the real games' specific bypass moves),
**trapping moves** (Wrap/Bind/Fire Spin/Clamp - it's the *attacker* that's locked into
repeating the move for 2-5 turns via the same `forcedMoveId` mechanism, not the target;
follow-up turns skip the accuracy roll entirely, a documented simplification), **Bide**
(braces for 2 turns via `bideDamageStored`/`bideTurnsRemaining`, then unleashes double back,
ignoring type effectiveness like Counter does), **Leech Seed** (drains 1/8 max HP each turn
to heal whoever planted it; Grass-type targets are immune), **Reflect/Light Screen**
(halve incoming Physical/Special damage respectively - a crit still bypasses both, matching
the real games), and **Recover/Soft-Boiled/Rest** (heal the user; Rest also cures status and
sleeps for a real fixed 2 turns, not the random 1-3 this engine already simplified normal
Sleep to). **Mist and Focus Energy were folded into the existing Guard Spec./Dire Hit
battle-boost-item fields** (`guardSpecActive`/`direHitActive`) rather than getting new state,
since both move/item pairs do the exact same thing. The player-side UI needed one small
change too - `Screen::Battle`'s FIGHT handler now detects a forced continuation
(charge/trap/Bide) and skips straight to it, the same way it already does for a forced
Struggle; `chooseOpponentMoveSlot()` got the equivalent check for the AI side.
`BIDE_MOVE_ID` moved to the public header for this reason. 10 new tests in
`PokemonBattleTest.cpp`.

**Whirlwind/Roar, Disable, and Substitute shipped too, as `v0.13.1`** - the three items
above that looked like they'd need deep UI work turned out tractable with a narrower scope
than first estimated:

- **Whirlwind/Roar**: the engine can't know whether a switch is even possible (a roster
  concern only `PokemonActivity.cpp` can answer), so `resolveAction()` just reports a new
  `BattleLogEvent::ForcedSwitch` unconditionally on use (these moves have accuracy 0 -
  "never misses" in this dataset) and leaves what actually happens to the caller.
  `resolveBattlePlayerMoveTurn()` checks for it on either side: the opponent's own use
  forces the player into `Screen::BattleSwitch` (the exact same flow a faint-forced switch
  already uses, just without the active Pokemon fainting); the player's own use against a
  wild Pokemon ends the encounter via the existing `resolveBattleAsPass()` (same outcome as
  running away); against a trainer, it calls `advanceGymOpponentOrFinish()` directly
  (skipping the XP award, since nothing was defeated) if there's a next team member,
  otherwise it has no effect - matching the real games, where Whirlwind/Roar can't be used
  to skip past a trainer's last Pokemon.
- **Disable**: picks a random one of the target's moves that still has PP and isn't already
  disabled (`BattleCombatant::disabledMoveSlot`/`disableTurnsRemaining`), for a simplified
  1-3 turns (same range as this engine's own Sleep, down from the real 1-7).
  `chooseOpponentMoveSlot()` skips it for the AI; the player's own `Screen::BattleMoves`
  handler and `battlePlayerHasAnyUsablePp()` reject selecting it (falling back to Struggle
  if it was the only move left) - no visual greying-out in the move list itself, a smaller,
  disclosed simplification.
- **Substitute**: creates a decoy for 1/4 of the user's own max HP (minimum 1, fails without
  enough HP to spare or if one's already up). A new `applyDamageRespectingSubstitute()`
  helper redirects every damage-application site (the generic hit loop, every
  `FIXED_DAMAGE_TABLE` kind, and Bide's release) through `BattleCombatant::substituteHp`
  instead of `currentHp` while one is up - a hit that would deal more than the remaining
  substitute HP just breaks it outright, no overflow onto the real Pokemon, matching the
  real games. Status infliction, opponent-debuff stat changes, flinch, and Leech Seed are
  all blocked while a substitute holds (added as extra guard conditions on their existing
  checks) - no new UI-visible HP-bar state was actually needed, since the existing HP bar
  already just reflects `currentHp`, which correctly stays untouched while the substitute
  absorbs hits.

7 new tests in `PokemonBattleTest.cpp`.

**`v0.14.0`, the final group - Transform/Mimic/Metronome/Mirror Move/Conversion
(move-copying/self-modifying mechanics)**: `resolveAction()` was split into itself (slot
lookup, PP spend, flinch/status checks, Bide/two-turn-charge/trap-continuation state) and a
new `resolveGenericMoveEffect(attacker, defender, moveId, move, random, allowMultiTurnLock,
moveSlotOfMimicUser, result)` covering everything from the invulnerability/accuracy check
through damage/ailment resolution, parameterized directly on a move id instead of a slot
index - this is the "execute an arbitrary move id" infrastructure Metronome/Mirror Move
needed, and both invoke it recursively for whatever move they end up executing.
`allowMultiTurnLock=false` on such a redirected call stops a picked/mirrored trapping move
from locking the attacker into repeating it next turn (there's no real slot to keep
"choosing" it from) - a documented simplification; a redirected two-turn charge move
likewise just resolves as one instant hit rather than starting its usual charge, since that
logic lives in `resolveAction()`'s own preamble, which a redirect bypasses entirely.
- **Metronome**: `pickRandomMetronomeMove()` uniformly rolls a real move id (1..MOVE_COUNT),
  rejecting Struggle and the other 5 special/move-copying moves (`isMoveCopyingOrSpecialMove()`).
- **Mirror Move**: a new `BattleCombatant::lastMoveUsedAgainstMe` field is set at the top of
  `resolveGenericMoveEffect()` (so a redirect naturally records the real underlying move, not
  the wrapper's own id) - Mirror Move replays it, failing ("But it failed!") if nothing
  qualifies yet or it isn't a plain replayable move.
- **Conversion**: a new `conversionType1`/`conversionType2` override on `BattleCombatant`
  (`PokemonType::None` = no override) copies the defender's current effective type for the
  rest of the battle; a new `effectiveTypesFor()` helper is now the single place STAB,
  type-effectiveness, and Leech Seed's Grass-immunity check look up a combatant's type,
  consulting the override when present.
- **Mimic**: copies one of the defender's known moves into the slot Mimic itself was used
  from, at 5 PP (or the copied move's own base PP if lower - the real Gen 1 rule). A real
  persistence risk was caught before writing any code: `PokemonActivity::
  savePlayerBattleEntry()` serializes every move slot's live `moveId`/`currentPp`
  unconditionally, which would have permanently saved the borrowed move over Mimic itself.
  Fixed with new `mimicActive`/`mimicSlot`/`mimicOriginalPp` fields - the save function now
  restores that one slot back to `MIMIC_MOVE_ID` (exposed publicly, like `BIDE_MOVE_ID`) with
  its remembered pre-Mimic PP instead of persisting the copy.
- **Transform**: copies the defender's species (so its type and, combined with the attacker's
  own unchanged level, its Attack/Defense/Special/Speed via the normal stat formula), current
  stat stages, and moveset (each copied move also capped at 5 PP) - HP, level, and status are
  deliberately left untouched, matching the real games. A new `transformed` flag makes a
  second use fail ("But it failed!"). Same persistence risk as Mimic, bigger blast radius
  (all 4 slots, plus the species id): `savePlayerBattleEntry()` now skips the moves/PP/PP-Up
  loop entirely when transformed, instead persisting whatever `peekBattleMoves()` already has
  on record - `speciesId` itself was never part of `BattleRecordEntry` to begin with, so no
  extra handling was needed there. Fixed a real, unrelated latent bug noticed while
  restructuring this function: the per-turn save path never wrote `entry.ppUp` at all
  (defaulting to all-zero every save, since `saveBattleEntry()` is a plain overwrite, not a
  merge) - now carried through from `battlePlayer_.ppUp` alongside everything else.

9 new tests in `PokemonBattleTest.cpp`. This closes out the Gen 1 mechanics gaps audit - see
the memory note for the final status.

## Round 2: a fresh audit after the roadmap above was fully done (`v0.15.0`)

With every item above shipped, a second audit was done specifically to look *beyond* battle
move mechanics this time (catching, status damage fractions, EXP, the type chart, trainer AI)
as well as re-checking move data for anything that still doesn't fit any existing hand-
authored table. Three items were explicitly raised with the user for a design call before
touching anything, since they're either already-deliberate design choices or make a move
strictly worse for the player:

- **The type chart stays the modern (non-buggy) chart** - the user chose not to reintroduce
  the real Gen 1 Ghost-vs-Psychic bug (Ghost moves doing nothing to Psychic, rather than the
  intended-but-never-real super effective). No change made.
- **Focus Energy stays a real buff** - the user chose not to replicate Gen 1's actual bug
  (which lowers crit chance instead of raising it). No change made.
- **Wrap/Bind/Fire Spin/Clamp: the user chose to add the missing target-side immobilization**
  (see below) - real Gen 1 traps BOTH sides at once, not just locking the attacker in as this
  project already did.

Everything else found was a clear, uncontroversial gap - fixed directly:

- **Burn halves Attack**: `computeDamage()` now halves the attacker's raw (pre-stage)
  Attack stat for a physical move while burned - the same treatment `stepBattle()` already
  gives Paralysis's halved Speed. Applied before staging, so unlike a negative stat stage, a
  critical hit does NOT bypass it (matches the real games; a burn's Attack penalty isn't a
  stage at all).
- **Dream Eater requires a sleeping target**: fails outright ("But it failed!", no accuracy
  roll or damage) unless `defender.status == Ailment::Sleep` - previously it happened to
  work (and drain HP) against any target, awake or not.
- **Hyper Beam's recharge turn**: a new `BattleCombatant::mustRecharge` flag, set whenever
  Hyper Beam lands a hit (a miss doesn't set it), checked first in `resolveAction()` - ahead
  of even flinch/status - forcing a completely skipped turn next time, then clearing itself.
- **Rage**: a new `BattleCombatant::enraged` flag, toggled true/false at the top of
  `resolveGenericMoveEffect()` based on whether Rage is the move actually being used this
  action (so choosing any other move cancels it, matching Gen 1 - no hard lock-in the way
  Thrash below has). A new `raiseAttackIfEnraged()` helper, called from both damage-
  application sites (`FIXED_DAMAGE_TABLE`'s branch and the generic per-hit loop), raises the
  enraged combatant's own Attack stage by 1 every time it takes nonzero damage.
- **Thrash/Petal Dance**: reuse the same `forcedMoveId`/`forcedTurnsRemaining` machinery the
  two-turn-charge/trapping moves already have (`isThrashMove()`), locking the attacker into
  repeating the move for 1-2 more turns (2-3 total) - unlike a trapping move, each repeat
  still rolls accuracy normally, and the user becomes confused (Gen 1's real 2-4 turn range)
  the instant the lock ends.
- **Wrap/Bind/Fire Spin/Clamp now trap the target too**: a new
  `BattleCombatant::trappedTurnsRemaining` field is set on the defender (`attacker
  .forcedTurnsRemaining + 1`, the same off-by-one trick Disable's own duration already uses)
  the moment the attacker's own lock starts; `resolveAction()` checks it first (right after
  `mustRecharge`, ahead of flinch/status) and immobilizes that side entirely for the turn,
  reporting a new `Trapped` event. Counted down once per turn in `finishTurn()`, alongside
  `disableTurnsRemaining`. A documented simplification: the real games also block switching
  while trapped, which this project's UI still allows (no PokemonActivity.cpp change was
  needed for the move-selection side, since the engine-level check already overrides
  whatever the player picks, the same way sleep/paralysis already work today).

11 new tests in `PokemonBattleTest.cpp`. Full native suite 496/496, clean
`pio run -e pokemon-x3`/`pokemon-simulator-X3` builds.
