---
title: Pokémon Module Audit — Round 2
parent: Development
nav_order: 9
---

# Pokémon Module Audit — Round 2 (missing features, bugs, performance)

A fresh, full read of the Pokémon module (`lib/Pokemon/*`, `src/pokemon/*`,
`src/activities/pokemon/*`, `src/components/pokemon/*`, ~12,400 lines, plus the
native tests under `test/pokemon_*/`) done at `v0.19.0`, after
[pokemon-gen1-authenticity-roadmap.md](pokemon-gen1-authenticity-roadmap.md)
reported "every item on this roadmap is now done."

**Relationship to the first audit.** That document is a single running file with
nine internal "Round N" sections, all of them about the *battle engine's* Gen 1
fidelity, written one after another as each batch shipped. This is a separate,
standalone pass with a wider brief: Gen 1 mechanics *and* correctness bugs *and*
runtime cost on the real hardware. Its "Round" numbering is unrelated to the
internal rounds in that file — think of this as "audit #2 of the module," not
"round 10 of the battle engine."

Everything listed here was checked against the actual code, not against the
changelog. Items already shipped (see `CHANGELOG.md` `[0.1.0]`–`[0.19.0]`) and
items the earlier docs explicitly ruled out (the modern type chart, a
non-buggy Focus Energy, badge-boost stacking, HM forgetting, trainer teams'
flat IV 15/EV 0, the simplified EV curve, the 1/8-vs-sqrt EV bonus, no
Toxic-style ball-shake animation, no switching-while-trapped enforcement) are
**not** re-raised. Where something looks like one of those but genuinely isn't,
that's called out inline.

Three sections follow: **Missing Features**, **Bugs**, **Performance**. Each
item is self-contained — a future session with zero context should be able to
pick any single one and start implementing without re-deriving the finding.

---

## Section 1 — Missing Gen 1 features

Ordered cheapest-and-highest-impact first, the same way the first roadmap's
"Suggested order" was.

### 1.1 Move priority — Quick Attack always goes second against a faster Pokémon

**Priority: high. Cost: very low (~30 lines + 2 tests). Impact: medium-high.**

`MoveData` (`lib/Pokemon/PokemonBattleTypes.h:43-53`) has no `priority` field,
and `stepBattle()` (`lib/Pokemon/PokemonBattle.cpp:1539-1564`) decides turn
order purely from staged, badge-boosted, paralysis-halved Speed. Gen 1 has
exactly two moves with non-zero priority:

| Move | Id | Gen 1 priority |
|---|---|---|
| Quick Attack | 98 | +1 |
| Counter | 68 | −1 |

Confirmed in `scripts/data/pokemon-moves.csv`: both ids exist, both carry no
priority column (PokeAPI's Gen 1 export here doesn't include one).

Right now Quick Attack is just a 40-power Normal physical move — its entire
identity is gone. Counter going *last* is also load-bearing: `Counter`'s
implementation (`PokemonBattle.cpp:900-913`) only succeeds if
`attacker.lastPhysicalDamageTaken != 0`, i.e. only if the opponent already
moved this turn, so its real −1 priority is exactly what would make it reliable
instead of a coin flip on Speed.

**How to implement.** Hand-author the table the same way
`isHighCritRatioMove()` / `RECOIL_TABLE` / `FLINCH_TABLE` already are (the
project's established pattern for "PokeAPI doesn't carry this field"):

```cpp
// PokemonBattle.cpp, anonymous namespace
int8_t movePriority(uint8_t moveId) {
  if (moveId == 98) return 1;   // Quick Attack
  if (moveId == 68) return -1;  // Counter
  return 0;
}
```

Then in `stepBattle()`, resolve the player's and opponent's chosen move ids
*before* the Speed comparison and compare priority first, Speed second. Two
wrinkles the implementer must handle:

- The opponent's move slot is currently picked inline at the call sites
  (`PokemonBattle.cpp:1598` and `:1605`). To know its priority up front, hoist
  `chooseOpponentMoveSlot()` above the ordering decision and pass the resulting
  slot into both branches. This changes RNG call ordering, which will perturb
  any test scripting an exact `RandomSource` sequence — check `PokemonBattleTest.cpp`.
- A forced continuation (`forcedMoveId`, Bide, a trapped side, Struggle's
  `BATTLE_MOVE_SLOTS` sentinel) has no slot to read a move id from; treat all of
  those as priority 0.

`stepOpponentOnlyTurn()`/`stepPlayerOnlyTurn()` need no change — only one side
acts there.

### 1.2 Toxic is a plain poison, with no escalating damage

**Priority: high. Cost: low (one transient field + ~20 lines + 1 test). Impact: medium.**

Toxic (move id 92, `status` category, `ailment = Poison` in
`scripts/data/pokemon-moves.csv`) falls straight through
`resolveGenericMoveEffect()`'s generic ailment block
(`PokemonBattle.cpp:1299-1312`) and sets `Ailment::Poison` like any other
poison move. End-of-turn damage (`applyEndOfTurnStatusDamage()`,
`PokemonBattle.cpp:522-528`) is a flat `maxHp / STATUS_DAMAGE_FRACTION` forever.

Real Gen 1: badly-poisoned damage is `N × maxHP/16` with `N` starting at 1 and
incrementing every turn, and it resets to 1 when the Pokémon switches out (and
"downgrades" to regular poison on switch). Right now Toxic is strictly worse
than Poison Powder (same effect, worse accuracy, less PP), so it is a dead move.

**How to implement.** Add `uint8_t toxicCounter = 0;` to `BattleCombatant`
alongside the other transient fields (it is *not* persisted — see the doc
comments on `bideTurnsRemaining` etc. for the established wording). Set it to 1
when Toxic specifically inflicts the status; have
`applyEndOfTurnStatusDamage()` use `toxicCounter` as a multiplier and increment
it, capped so `clampToUint16()` can't be reached pathologically. Zero it in the
same places a fresh `BattleCombatant{}` is built (which is free) and in
`faintCombatant()` alongside `status`/`statusTurns`. Note this interacts with
item 1.5 below: fix the fraction first or the numbers compound.

### 1.3 Running from a wild battle always succeeds, for free

**Priority: medium-high. Cost: low. Impact: medium (real balance lever).**

`Screen::Battle`'s RUN option calls `resolveBattleAsPass()` unconditionally
(`src/activities/pokemon/PokemonActivity.cpp:1771`, and the hardware-Back path
at `:2032`), which immediately ends the encounter with no turn spent and no
chance of failure. The only thing that can block it is the round-4
forced-continuation guard (`:1707-1713`, `:2028-2031`).

Real Gen 1: fleeing a wild battle rolls against
`(playerSpeed * 32 / max(1, opponentSpeed/4)) + 30 * attemptCount` out of 256,
and a failed attempt costs the whole turn (the wild Pokémon attacks). Today a
losing wild fight has zero cost — the player can always walk away with a
half-dead Pokémon and nothing lost — which quietly undercuts the whole
switch/heal/item layer the last several releases added.

**How to implement.** Add `bool attemptRun(const BattleCombatant& player, const
BattleCombatant& opponent, uint8_t attemptCount, const RandomSource&)` to
`PokemonBattle.h/.cpp` (pure, testable, mirrors `attemptCatch()`'s shape
exactly). In `PokemonActivity`, add a `uint8_t battleRunAttempts_ = 0;` member
reset in `enterBattle()`, and on RUN: if it succeeds call
`resolveBattleAsPass()` as today; if it fails, increment the counter, write a
"Couldn't get away!" line and route through `finishItemUseMidBattle()`-style
handling (`stepOpponentOnlyTurn()` — the opponent gets its free turn) instead.
Gym/Elite Four RUN should stay an unconditional forfeit (it is a menu-level
"abandon the challenge," not an in-world escape) — guard on
`gymChallengeIndex_ == 0`.

### 1.4 Haze only resets stat stages

**Priority: medium. Cost: very low (~10 lines). Impact: low-medium.**

`PokemonBattle.cpp:1069-1072` handles Haze (id 114) as
`resetBattleStages(attacker); resetBattleStages(defender);`, and
`resetBattleStages()` (`:1472-1479`) zeroes exactly the six stage fields.

Real Gen 1 Haze also: cures both sides' non-volatile status *and* confusion,
and clears Reflect, Light Screen, Mist and Focus Energy from both sides. All
four of those are already modeled here as `reflectActive`, `lightScreenActive`,
`guardSpecActive` (Mist/Guard Spec.) and `direHitActive` (Focus Energy/Dire
Hit) — so this is purely "also clear the fields that already exist."

Note this is also a documentation defect: `BattleLogEvent::StatsReset`'s own
comment (`PokemonBattle.h:292`) already claims "both sides' stages (and this
engine's status slot) reset," which the code does not do. See Bug 2.5.

Judgement call for whoever picks this up: clearing the *opponent's* status with
Haze makes Haze strictly better for the player than the real games in one
respect (Gen 1's version left a well-known "lingering status" glitch). Follow
the project's existing precedent — implement the intended behavior, not the
glitch — and say so in the code comment.

### 1.5 Poison/burn/Leech Seed drain 1/8 per turn; Gen 1 uses 1/16

**Priority: medium. Cost: trivial (one constant) but touches test expectations. Impact: medium.**

`STATUS_DAMAGE_FRACTION = 8` (`PokemonBattle.cpp:15`) is used by both
`applyEndOfTurnStatusDamage()` (`:522-528`) and `applyLeechSeedDamage()`
(`:534-544`). In Gen 1 all three of poison, burn and Leech Seed tick for
`max(1, maxHP/16)` — 1/8 is the Gen 2+ value for poison/burn, and Leech Seed
has never been 1/8 in any generation.

Caveat worth stating plainly: the first roadmap's "Round 2" intro says that
pass looked at "status damage fractions" among other things and made no change
there, so this *may* already be a deliberate (just undocumented) balance
choice. Treat this item as "confirm with the user first, then either change the
constant or write the reasoning down," not as an unambiguous fix. The Leech
Seed half is the clearer case: it isn't a status condition at all, wasn't named
in that review, and at 1/8 it out-damages a same-turn Poison tick.

If changed: `PokemonGameTest.cpp` / `PokemonBattleTest.cpp` contain exact
numeric HP expectations that will move. Consider splitting into two constants
(`STATUS_DAMAGE_FRACTION`, `LEECH_SEED_FRACTION`) so the two can be tuned
independently.

### 1.6 Speed ties always go to the player

**Priority: low. Cost: trivial. Impact: low but real.**

`PokemonBattle.cpp:1564`: `const bool playerFirst = playerSpeed >= opponentSpeed;`.
Gen 1 breaks a Speed tie with a random roll. Because gym rosters are fixed, a
same-Speed matchup against a gym leader is currently a guaranteed first-strike
for the player every single turn of that fight.

**How to implement.** `playerFirst = playerSpeed > opponentSpeed || (playerSpeed
== opponentSpeed && coinFlip)`. This adds an RNG call only on the tie path, so
it's cheap, but it *does* add a call — any test using a scripted
`RandomSource` where speeds happen to tie will shift. `ZERO_RANDOM` (rolls 0
always) would make the opponent or the player win every tie depending on which
way the comparison is written; pick the direction that keeps the most existing
tests green and document it.

### 1.7 Freeze thaws on its own; in Gen 1 it never does

**Priority: low. Needs a design call before implementing.**

`statusPreventsAction()` (`PokemonBattle.cpp:477-484`) gives a frozen Pokémon a
`FREEZE_THAW_CHANCE_PERCENT = 20` chance to thaw each turn. In Gen 1, freeze is
permanent for the rest of the battle — nothing thaws it except being hit by a
Fire-type move (which the engine also doesn't model) or an Ice Heal / Full Heal.

This is the single most player-hostile mechanic in Gen 1 and the reason
Blizzard was banned in competitive play. Implementing it faithfully would mean
a frozen Pokémon is simply out of the fight until an item is used. Given the
project's stated audience (a reading companion, not a battle simulator), the
honest recommendation is: **leave the 20% thaw, and document it as a deliberate
divergence in the constant's comment** — the code currently gives no hint that
this isn't the real rule. If the user wants the authentic version, the matching
piece to add is "a Fire-type damaging move thaws the target," which is ~5 lines
in `resolveGenericMoveEffect()`'s damage block.

### 1.8 Counter reflects any physical move

**Priority: low. Cost: two lines. Impact: low.**

`FixedDamageKind::Counter` (`PokemonBattle.cpp:900-913`) reflects
`lastPhysicalDamageTaken`, which every physical hit sets
(`:920-922`, `:972-974`). Gen 1's Counter only reflects damage from
Normal- and Fighting-type moves (and, via a famous quirk, it can reflect the
damage of a *Special* move if that move happens to be Normal/Fighting-typed —
which in Gen 1 none are, so the practical rule is the type check alone).

To implement, record the type alongside the damage: add
`PokemonType lastPhysicalDamageType` next to `lastPhysicalDamageTaken`, set it
at both damage-application sites, and make the `Counter` case fail
(`BattleLogEvent::MoveFailed`) unless the stored type is Normal or Fighting.

### Considered and explicitly **not** worth doing

Stated here so a future audit doesn't silently rediscover them and so nobody
mistakes their absence for an oversight.

- **Safari Zone.** Needs a map, a step counter, bait/rock mechanics and its own
  catch algorithm — an entire second game mode with no reading-time hook. This
  project's wild-encounter loop already occupies that niche. Out of scope.
- **Trainer classes, rematches, roaming/overworld trainers.** All require a
  world map. The gym/Elite Four/Champion ladder is this project's whole
  trainer layer by design (see the battle roadmap).
- **In-game trades.** Depends on NPCs and a map; the Link Cable already exists
  purely as an evolution item, which is the useful half.
- **Breeding, held items, the Special split, abilities, natures.** Gen 2+.
  Adding them moves the game *away* from Gen 1 — the same reasoning the first
  roadmap used for the single Special stat.
- **The 1/256 accuracy miss.** A genuine bug in the original games. The project
  has consistently declined to re-introduce original bugs (Ghost-vs-Psychic,
  Focus Energy, badge-boost stacking) — this belongs in the same bucket.
- **Rage's hard lock-in.** Gen 1 really does lock the user into Rage for the
  rest of the battle. The current implementation
  (`PokemonBattle.cpp:798`, "using any other move cancels it") is the *nicer*
  reading and is already documented as a deliberate choice on
  `BattleCombatant::enraged`. Leave it.
- **Splitting battle XP across every participant.** Gen 1 divides earned XP
  among all Pokémon that took part. This project's XP model is already a
  deliberate simplification (`battleVictoryXp()` is a flat `level × 4/6`, not
  the real species-yield formula — see `PokemonBattle.h:504-511`), and only one
  Pokémon is ever "out" at a time here anyway, so there's nothing meaningful to
  split.
- **Money, Pokémon Centers, Marts, whiteout penalties.** Reading is this
  project's economy and its healing (`PokemonService::healPartyOnRead()`);
  bolting on currency would compete with the core loop rather than support it.

---

## Section 2 — Bugs

Each item says whether it is **confirmed by reading the code path** or
**suspected**. None of these are style nitpicks.

### 2.1 A forced multi-turn move whose slot has hit 0 PP hard-locks the battle (Fly/Dig: unrecoverable)

**Severity: critical. Confirmed by reading. Cost to fix: ~5 lines + 1 test.**

`resolveAction()` checks the chosen slot's PP *before* it recognises a forced
continuation:

```
PokemonBattle.cpp:672-682
  const bool forcedStruggle = moveSlot >= BATTLE_MOVE_SLOTS;
  BattleMoveSlot* slot = nullptr;
  uint8_t moveId = STRUGGLE_MOVE_ID;
  if (!forcedStruggle) {
    slot = &attacker.moves[moveSlot];
    if (slot->moveId == 0 || slot->currentPp == 0) {
      result.event = BattleLogEvent::MoveHadNoPp;
      return result;                       // <-- returns before any forced-move handling
    }
    moveId = slot->moveId;
  }
```

The two-turn / trapping / Thrash / Bide continuation logic that would clear
`forcedMoveId`, `forcedTurnsRemaining` and `invulnerable` all lives *after*
this point (`:706-753`, `:1011-1059`), and PP is deliberately not spent on a
continuation turn (`:711-713`).

**The reachable sequence** (all of it in one battle, no save/reload needed):

1. A Pokémon with Fly (id 19, 15 PP) or Dig (id 91, 10 PP) burns that slot down
   to exactly 1 PP over the course of a fight.
2. It uses Fly. `resolveAction()` decrements PP to 0, then hits the charge
   branch (`:742-748`): `forcedMoveId = 19`, `forcedTurnsRemaining = 1`,
   `invulnerable = true`.
3. Next turn the player presses FIGHT.
   `PokemonActivity.cpp:1724-1733` detects `forcedContinuationMoveId != 0`,
   finds the slot, and calls `resolveBattlePlayerMoveTurn(slot)`.
4. `resolveAction()` hits the `currentPp == 0` early return above.
   `MoveHadNoPp` → `formatBattleActionLine()` writes an empty string
   (`PokemonActivity.cpp:390-393`), so nothing is even printed.
   `forcedMoveId`/`invulnerable` are never cleared.
5. The opponent acts. Every one of its moves misses, because
   `resolveGenericMoveEffect()` returns `MoveMissed` unconditionally against an
   invulnerable defender (`PokemonBattle.cpp:808-811`).
6. BAG / BALL / SWITCH / RUN are all blocked, because
   `lockedIntoContinuation` is true (`PokemonActivity.cpp:1707-1713`), and the
   hardware Back button is blocked by the identical guard in `goBack()`
   (`:2028-2031`).

Result: neither side can ever damage the other, the player cannot leave the
battle screen, and the Pokémon activity cannot be exited at all. Only a device
reboot recovers (battle state is RAM-only, so nothing is corrupted
persistently).

The same PP-0 early return also silently breaks Wrap/Bind/Fire Spin/Clamp
(10–20 PP), Thrash/Petal Dance and Bide, and the AI side too —
`chooseOpponentMoveSlot()` returns the forced slot without checking PP
(`PokemonBattle.cpp:1332-1336`). Those cases are merely "this Pokémon does
nothing for the rest of the fight" rather than a lock, because there is no
invulnerability keeping the battle alive.

**Fix.** Recognise the continuation before the PP gate. Concretely, hoist the
"is this a continuation of `attacker.forcedMoveId` / an in-progress Bide"
determination above the slot check, and skip the `currentPp == 0` rejection
when it is true (the slot's `moveId` check should stay — a genuinely empty slot
is still a hard error):

```cpp
if (!forcedStruggle) {
  slot = &attacker.moves[moveSlot];
  const bool isContinuation =
      (attacker.forcedMoveId != 0 && slot->moveId == attacker.forcedMoveId) ||
      (attacker.bideTurnsRemaining > 0 && slot->moveId == BIDE_MOVE_ID);
  if (slot->moveId == 0 || (slot->currentPp == 0 && !isContinuation)) {
    result.event = BattleLogEvent::MoveHadNoPp;
    return result;
  }
  moveId = slot->moveId;
}
```

Defence in depth worth adding at the same time: in `PokemonActivity.cpp`, if a
forced continuation can't be resolved to a real slot, clear
`battlePlayer_.forcedMoveId`/`forcedTurnsRemaining`/`invulnerable`/
`bideTurnsRemaining` rather than leaving the player in a menu-less state.

**Test to add** (`PokemonBattleTest.cpp`): give a combatant Fly with
`currentPp = 1`, step once (expect `ChargingMove`), step again with the same
slot, and assert the release actually lands — `forcedMoveId == 0`,
`invulnerable == false`, defender HP dropped. No existing test covers a
continuation at 0 PP (`grep "currentPp = 0"` in that file only covers the
AI-falls-back-to-Struggle case at `:1224-1226`).

### 2.2 A type-immune damaging move still inflicts its secondary status

**Severity: medium. Confirmed by reading. Cost to fix: one condition.**

The ailment block at the end of `resolveGenericMoveEffect()` never looks at
type effectiveness:

```
PokemonBattle.cpp:1301-1312
  if (defender.status == Ailment::None && defender.currentHp > 0 && defender.substituteHp == 0 &&
      move->ailment != Ailment::None && rollPercentChance(random, effectiveAilmentChance)) {
    defender.status = move->ailment;
    ...
```

`effectivenessPercent` is computed locally inside the damaging branch
(`:864-868`) and is out of scope here. So Body Slam (Normal, 30% paralysis)
against a Ghost-type deals 0 damage, correctly reports
`BattleLogEvent::MoveNoEffect` — and then paralyzes it anyway, with no log line
saying so. Same for every damaging move with a secondary ailment against any
immune type combination (Thunder Punch vs Ground, Poison Sting vs Steel-free
Gen 1 aside, Ember vs a Fire-immune matchup, etc.).

**Fix.** Hoist `effectivenessPercent` (or a `bool hitWasImmune`) out of the
damaging branch and add `&& !hitWasImmune` to the condition. Status-category
moves must keep applying — they never enter that branch — so initialise the
flag to `false` and only set it in the damaging path. See 2.3, which is the
mirror-image problem and is best fixed in the same change.

### 2.3 Status moves ignore type immunity entirely (Thunder Wave paralyzes Ground-types)

**Severity: medium. Confirmed by reading. Cost to fix: ~10 lines.**

`resolveGenericMoveEffect()` only consults the type chart inside
`if (move->category != MoveCategory::Status)` (`PokemonBattle.cpp:861-868`).
A Status-category move therefore skips the effectiveness lookup completely and
goes straight to the ailment block.

Concrete, verified-from-data case: Thunder Wave is
`86, Thunder-Wave, Electric, power 0, accuracy 90, status, Paralysis`
(`scripts/data/pokemon-moves.csv`), and `typeEffectivenessPercent(Electric,
Ground, ...)` returns 0 (`PokemonTypeChart.cpp:20`, Electric row, Ground
column). In the real games Thunder Wave simply doesn't affect a Ground-type; here
it paralyzes Diglett, Sandshrew, Rhyhorn, Geodude, and every other Ground-type
in the gym rosters at a 90% clip.

**Fix.** Compute `effectivenessPercent` unconditionally (before the
category branch), and for a Status move whose type is not
`PokemonType::None`, bail out with `BattleLogEvent::MoveNoEffect` when it is 0.
Be careful not to break the many non-typed status effects that already dispatch
before the ailment block (Haze, Reflect, Recover, Substitute, Mimic,
Metronome…) — those should keep working regardless, so apply the immunity check
only on the path that reaches the ailment block, or explicitly only to moves
carrying a real `ailment`.

Cross-check before shipping: several Gen 1 status moves are Normal-type
(Sing, Lovely Kiss, Supersonic is Normal, Glare is Normal) and Ghosts are
immune to Normal. That is authentic for Gen 1 too, but it does make Gengar
noticeably harder to status — worth mentioning to the user, since it's a
visible difficulty change rather than a pure bug fix.

### 2.4 Learning a move into any empty slot except the first fails with "Save error"

**Severity: medium (user-visible, reproducible in two taps). Confirmed by reading.**

`Screen::Moveset` always lists exactly `BATTLE_MOVE_SLOTS` rows
(`PokemonActivity.cpp:578-579`), rendering empty ones as `-`
(`:2404-2415`). Selecting any of them stores it verbatim:

```
PokemonActivity.cpp:1401-1404
  case Screen::Moveset:
    movesetSlot_ = static_cast<uint8_t>(selected_);
    setScreen(Screen::MovesetPick);
```

and `PokemonService::learnMoveIntoSlot()` writes it verbatim too
(`src/pokemon/PokemonService.cpp:391-407`). But
`validateBattleRecordEntry()` requires moves to be packed at the front with no
gaps (`lib/Pokemon/PokemonBattleStoreCodec.cpp:46-57`,
`if (sawEmptyMoveSlot) return false;`), so writing into slot 3 while slot 2 is
empty makes `upsertEntry()` reject the whole entry.

Reproduction: a Pokémon that knows 2 moves → Party → Actions → Moves → pick the
**fourth** row (`-`) → pick any learnable move → "Save error" (`:1423-1426`),
and nothing is learned. Picking the *third* row works. The error message is
also misleading — nothing about storage failed.

**Fix, pick one:**

- *Smallest:* in `learnMoveIntoSlot()`, clamp the target slot down to the first
  empty index when the requested slot is empty and a lower one also is. One
  line, no UI change, matches what the player obviously meant.
- *Cleanest:* have `Screen::Moveset`'s `activate()` do the same clamping before
  storing `movesetSlot_`, so `MovesetPick`'s own header/context matches the slot
  that will actually be written.
- *Also worth doing either way:* change the failure message from
  `STR_POKEMON_SAVE_ERROR` to `STR_POKEMON_NOT_APPLICABLE` for this path.

`TmReplaceSlot`, `PpUpSlot` and the `MoveLearn` event path are **not** affected —
`TmReplaceSlot` is only reached on `TeachMoveOutcome::MovesetFull` (all four
slots occupied), `MoveLearn` events are only queued when no empty slot exists
(`PokemonService.cpp:796-814`), and `applyPpUp()` explicitly rejects an empty
slot (`:446`).

### 2.5 `BattleLogEvent::StatsReset`'s documented contract doesn't match Haze's behavior

**Severity: low (documentation/behavior divergence). Confirmed by reading.**

`PokemonBattle.h:292` documents `StatsReset` as
`// Haze - both sides' stages (and this engine's status slot) reset`, but
`resetBattleStages()` (`PokemonBattle.cpp:1472-1479`) touches only the six
stage fields and the Haze dispatch (`:1069-1072`) calls nothing else. Anyone
reading the header will assume Haze cures status and will not go looking.

Either implement the missing half (see Missing Feature 1.4, which is the same
work) or correct the comment. Do not leave both.

### 2.6 A multi-hit move keeps hitting real HP after it breaks a Substitute mid-sequence

**Severity: low. Confirmed by reading.**

The per-hit loop only stops on `defender.currentHp > 0`
(`PokemonBattle.cpp:943`), and `applyDamageRespectingSubstitute()`
(`:407-413`) silently switches from `substituteHp` to `currentHp` the moment
the substitute reaches 0. So a Double Slap that breaks a 12 HP substitute on
hit 2 of 4 puts hits 3 and 4 straight onto the real Pokémon.

Real Gen 1: once the Substitute breaks, the rest of a multi-hit move's hits do
nothing. Fix by snapshotting `defenderHadSubstitute` (that local already exists
at `:939` for the trapping check) and breaking out of the loop when the
substitute was up at the start of the action and has since reached 0.

### 2.7 (Suspected) Damage absorbed by a Substitute still feeds Counter, Bide and Rage

**Severity: low. Suspected — Gen 1's real behavior differs per mechanic and I could not verify all three from the code alone.**

All three "damage taken" hooks run on the raw damage number, with no check for
whether a Substitute ate it:

- `defender.lastPhysicalDamageTaken = clampToUint16(totalDamage)` (`PokemonBattle.cpp:972-974`, and `:920-922` for fixed-damage moves) — so a Pokémon behind a full-HP Substitute can Counter for 2× damage it never took.
- `defender.bideDamageStored += damage` (`:959-961`).
- `raiseAttackIfEnraged(defender, damage)` (`:953`, `:919`).

Gen 1 is genuinely inconsistent here (Bide is known to store substitute damage;
Counter's behavior behind a Substitute is a documented oddity), so this needs a
deliberate call rather than a blanket "route them all through
`substituteHp == 0`." Flagged so it's on the record, not as a definite defect.

### 2.8 (Suspected) Waking from Sleep costs no turn

**Severity: very low. Suspected — depends on how the engine's already-simplified sleep counter is read.**

`statusPreventsAction()`'s Sleep case (`PokemonBattle.cpp:468-476`) decrements
while `statusTurns > 0` and, on the turn it reaches 0, cures and returns
`false` — so the Pokémon wakes up *and* acts on the same turn. Gen 1 loses the
wake-up turn. Net effect: sleep here is effectively one turn shorter in impact
than the same counter value in the real games. Since the duration itself is
already simplified from 1–7 to `rollStatusDuration(random, 1, 3)` (`:1305`),
this may well be intentional compensation. Low value either way; listed for
completeness.

---

## Section 3 — Performance and efficiency

Context from `CLAUDE.md` and the mechanics doc, restated because it drives the
priorities below: flash is the tighter budget (~94% of the 6.25 MB OTA slot
used, ~375 KB free), static RAM is now 24.8% of 327,680 B, heap *fragmentation*
matters more than raw usage, and SD-card I/O is by far the most expensive
operation in the module. The `v0.18.1`–`v0.18.6` crash family came from exactly
this area.

Only items with arguable real impact are listed. Micro-optimisations that would
cost readability are deliberately omitted, and one thing that *looks* like a hot
loop but isn't is called out at the end.

### 3.1 PC Box ordering re-reads the entire record file once per distinct species

**Priority: high. Cost: medium. Impact: large on any save with a real collection.**

`PokemonStore::readPcPage()` (`src/pokemon/PokemonStore.cpp:393-496`) is O(1)
file passes for `PcOrder::CatchDate`, but for `PokedexNumber` and `Alphabetical`
it does:

1. one full pass over every record to build a `pcSpecies` bitset (`:434-444`);
2. then, for each species present, `appendSpecies()` seeks back to the start and
   re-reads *every* record looking for matches (`:446-461`, `:463-470`);
3. and for `Alphabetical`, an additional O(151) `strcmp` scan over the species
   table per output row to find the next name in order (`:472-489`).

With `N` records and `S` distinct species that's `S × N` 48-byte `readExact()`
calls plus `S × N` `decodeRecord()` invocations — and `decodeRecord()` runs
`validateRecord()`, which runs full UTF-8 validation on the 33-byte nickname
(`lib/Pokemon/PokemonTypes.cpp:17-59`, `:141-150`). For a 100-record PC box
holding 60 species that is ~6,000 record reads ≈ 288 KB of SD traffic **per
page of ten rows**, and it runs inside `buildRows()` (`PokemonActivity.cpp:2470-2480`),
i.e. on every redraw of the PC Box screen, including every scroll step.

**Fix, in order of increasing effort:**

- Cheapest real win: the `appendSpecies()` inner loop can stop early once
  `written == output.size()`, and the whole species loop can stop once the page
  is full — the `PokedexNumber` branch already checks `written < output.size()`
  in its `for` condition, but `appendSpecies()` itself keeps reading after
  returning `true`. Bound both.
- Better: do a **single** pass that builds a small `(speciesId, recordId)`
  index into a fixed-size scratch array, sort that in memory, then read only the
  ≤10 records the page actually needs. Memory cost: 6 bytes × N; at the
  1024-record cap that's 6 KB, which should be heap-allocated with
  `new (std::nothrow)` following the `PokemonIvEvStore` precedent
  (`src/pokemon/PokemonIvEvStore.cpp:58-63`), not a stack local.
- Complementary: cache the built order across page turns, invalidated on any
  `commit()`. The activity already re-reads per page, so this alone removes
  most of the cost without changing the algorithm.

### 3.2 The main save is read and written 48 bytes at a time

**Priority: high. Cost: low-medium. Impact: large, and it multiplies every other save-path cost.**

Every record-level file operation in `PokemonStore.cpp` moves exactly one
`RecordBytes` (48 B) per call:

- `inspectSnapshot()`'s verification loop (`:100-120`) — runs twice at
  `begin()` (slot A and slot B) **and once more after every single write** as
  the read-back verification (`:324-328`).
- `writeSnapshot()`'s record copy loop (`:294-307`) — one `readExact()` plus
  one `writeExact()` per record.
- `readRecord()` (`:344-358`), `loadOwnedEvolutionNeeds()` (`:375-389`),
  `readPcPage()` (all three orderings).

So a single `PokemonStore::commit()` on a 1024-record save is roughly
1024 reads + 1024 writes + 1024 verification reads ≈ 3,072 `FsFile` calls plus
~150 KB of traffic. And `commit()` is called from `consumeBagItem()` (i.e.
**every Poké Ball throw**, `PokemonService.cpp:575-602`), `markSpeciesSeen()`
(`:193-204`), `awardBattleXp()` (`:878-887`), `movePartyMember()`,
`depositPokemon()`, `withdrawPokemon()`, `resolveEncounter()`,
`acknowledgeItem()`, and every 5-minute reading checkpoint via
`creditMinutes()` (`:958-993`).

**Fix.** Batch through a fixed intermediate buffer — e.g. 2,048 bytes
(42 records, a nice multiple of 48 and of a typical SD block) — in the copy and
scan loops. The CRC is already computed incrementally over byte ranges
(`updateSnapshotCrc32(crc, data, size)`), so it composes trivially with a
larger buffer. Keep the buffer off the stack given this module's crash history:
either a file-scope `static` (single-threaded access, and these paths are
already non-reentrant) or `new (std::nothrow)` with a graceful `false` return,
matching `PokemonIvEvStore`'s established pattern and comments.

Note this is a pure I/O-shape change: the on-disk format, the record ordering
invariant, and the validate-then-commit discipline all stay exactly as they are.

### 3.3 List screens call `PokemonService::readRecord()` once per row, per redraw

**Priority: medium-high. Cost: low. Impact: medium.**

`readRecord()` opens the active save file, seeks to the records region, and
linearly decodes records until it finds the id (`PokemonStore.cpp:335-361`).
It is called:

- once per row inside `buildRows()` for `Screen::Moveset` (`PokemonActivity.cpp:2405-2409`),
  `MovesetPick` (`:2418-2420`), `TmReplaceSlot` (`:2437-2441`), `PpUpSlot`
  (`:2454-2458`) and the `MoveLearn` variant of `Screen::Event` (`:2621-2625`) —
  all five look up the *same* `focusedRecordId_` every single row;
- again inside `logicalCount()` for `Screen::MovesetPick` (`:580-587`), and
  `logicalCount()` is itself called from `loop()` (`:2090`), `onRow()`
  (`:2290`), `buildRows()` (`:2304`), `renderFocused()` (`:2913`, `:2923`),
  `battleMenuTop()` (`:3168`) and every `renderXxxGrid()` (`:3237`, `:3258`,
  `:3274`, `:3290`) — so several times per frame *and* on every key press;
- again in `renderFocused()` for `Screen::Summary` (`:2940`).

On a PC-box-sized save each of those is a file open plus a scan of up to N
records. A single redraw of the Moveset screen currently costs on the order of
5–10 full record scans for one record's data.

**Fix.** Cache the focused record on the activity: add
`pokemon::PokemonRecord focusedRecord_{}` populated whenever
`focusedRecordId_` is assigned (there are only a handful of assignment sites:
`:1330`, `:1511`, `:1558`), and have all of the above read the cache. Refresh it
alongside `refreshSnapshot()`. This is a small, readable change that removes the
per-row and per-`logicalCount()` file access entirely.

### 3.4 The IV/EV store reserves 1,024 entries (16 KB resident) for a handful of Pokémon

**Priority: medium-high. Cost: low. Impact: ~12–15 KB of permanent RAM plus a much smaller heap spike.**

`POKEMON_IVEV_MAX_ENTRIES = 1024` (`lib/Pokemon/PokemonIvEvStoreCodec.h:18`)
was chosen to match `PokemonService::resolveEncounter()`'s hard record cap
(`PokemonService.cpp:262`). `IvEvEntry` is 14 bytes on disk but `sizeof` 16 in
memory (a `uint32_t` plus two 5-byte arrays, padded to 4-byte alignment), so
`IvEvStoreState` is **16,384 bytes**. `PokemonIvEvStore` holds one as a member
(`state_`), and the store is a function-local `static`
(`PokemonIvEvStore.cpp:204-207`), so that 16 KB sits in `.bss` for the life of
the firmware — which lines up exactly with the RAM jump from 19.7% to 24.8%
recorded in `CLAUDE.md` when IV/EV shipped.

On top of that, `upsertEntry()` → `writeState()` transiently holds a
`IvEvStoreState` candidate (16 KB) + `IvEvStoreFileBytes` (14 KB) + a second
`IvEvStoreState` for verification (16 KB) — the sequencing at
`PokemonIvEvStore.cpp:151` already frees `bytes` before allocating `verified`
specifically to keep the peak down, and `v0.18.3` still had to add
`new (std::nothrow)` guards because those allocations were failing under render
pressure.

**Fix — two independent options, both worth taking:**

- **Right-size the cap.** 1,024 records is the theoretical ceiling of the main
  save, not a realistic collection; 256 entries (4 KB resident, 3.6 KB file)
  covers a complete-Pokédex-plus-duplicates playthrough with room to spare and
  cuts every buffer in this subsystem by 4×. The format already carries a
  `uint16_t` entry count, so raising it again later is a header-only change —
  but note `decodeIvEvStoreFile()` rejects `count > POKEMON_IVEV_MAX_ENTRIES`
  (`PokemonIvEvStoreCodec.cpp:170`), so *lowering* the cap makes an
  already-written larger file undecodable. Handle that (e.g. decode up to the
  new cap and drop the tail, with a log line) or confirm no shipped save can
  exceed it.
- **Stop the `verified` round-trip from needing a second full state.** The
  verification at `:152-162` decodes the whole file back into a fresh
  `IvEvStoreState` and compares with `operator==`. Comparing the re-read
  *bytes* against the buffer just written (before freeing it) would be just as
  strong, needs no `IvEvStoreState` at all, and removes 16 KB from the peak.

### 3.5 `healPartyOnRead()` performs up to six full battle-store write+verify cycles per checkpoint

**Priority: medium. Cost: low. Impact: medium (it runs every 5 minutes of reading).**

`PokemonService::healPartyOnRead()` (`PokemonService.cpp:730-775`) loops over
the party and calls `battleStore_.upsertEntry(healed)` per member (`:773`).
Each `upsertEntry()` copies the whole `BattleStoreState`, encodes the entire
file, opens/writes/`sync()`s it, then re-opens and fully re-decodes it to verify
(`src/pokemon/PokemonBattleStore.cpp:165-170`, `:126-163`). Six party members
being healed at once means six complete file rewrites and six read-back
verifications of the same 134-byte file — twelve file opens where one write and
one verify would do.

The file is small, so the byte cost is negligible; the *syscall and flash-erase*
cost is not, and it lands on the same 5-minute cadence as the main-save commit.

**Fix.** Add a batch entry point to `PokemonBattleStore` — build the candidate
`BattleStoreState` by applying every party member's heal, then a single
`writeState()`. `upsertBattleEntry()` in the codec is already a pure function on
a `BattleStoreState` (`PokemonBattleStoreCodec.cpp:133-150`), so this is a
loop-hoist, not a redesign. Keep the existing early-out at `:770`
(`if (healed == *existing) continue;`) so a party with nothing to heal still
writes nothing.

### 3.6 Every battle win writes both the main save and the IV/EV store in full

**Priority: medium. Cost: medium. Impact: medium.**

`awardBattleXp()` (`PokemonService.cpp:849-888`) does, per victory:
`readRecord()` (file scan) → `ensureIvEv()` (in-memory) →
`ivEvStore_.upsertEntry()` (full IV/EV file encode + write + verify re-read) →
`loadReadyState()` (file open + decode) → `store_.commit()` (see 3.2). In a gym
run that's once per defeated team member, up to six times per challenge, on top
of the per-turn `savePlayerBattleEntry()` (`PokemonActivity.cpp:1073-1109`,
called from every one of `resolveBattlePlayerMoveTurn()`'s branches,
`finishItemUseMidBattle()`, and both `Screen::BattleSwitch` paths).

3.2 and 3.4 between them remove most of the per-call cost, which is why they are
listed first. Beyond that, the EV accumulation and the XP commit could share a
single `commit()` — they already both happen unconditionally and in sequence —
and the IV/EV write could be deferred to the end of a gym challenge rather than
once per team member, since EVs are not read again mid-battle
(`setupBattlePlayer()` reads them once at `:956`).

### 3.7 CRC32 is computed bit-by-bit over the whole save, three times per commit

**Priority: low-medium. Cost: trivial. Impact: small but free.**

`updateSnapshotCrc32()` (`lib/Pokemon/PokemonStoreCodec.cpp:114-122`) —
and its identical twins `updateBattleStoreCrc32()`
(`PokemonBattleStoreCodec.cpp:34-42`) and `updateIvEvStoreCrc32()`
(`PokemonIvEvStoreCodec.cpp:34-42`) — run eight shift/xor iterations per byte.
On a commit the full payload is CRC'd once while writing and once more during
`inspectSnapshot()`'s verification, plus twice more at `begin()` for the two
slots. On a large save that is millions of iterations on a 160 MHz RISC-V core.

**Fix.** A nibble-wise table (16 `uint32_t` entries = 64 bytes of flash, shared
by all three copies) halves the work for essentially nothing; a 256-entry table
(1 KB flash) is ~8× faster. Given flash is the tighter budget, the 64-byte
nibble table is the right trade here. The three identical implementations should
also collapse into one shared helper while someone is in there — that is a small
flash saving on its own.

### Checked and deliberately **not** flagged

- **`chooseRegularSpecies()` walking all 151 species twice per encounter roll**
  (`lib/Pokemon/PokemonGame.cpp:84-106`), and `mewIsReady()`'s 150-species scan
  (`:231-237`). These run at most once per 15 credited reading-minutes, on a
  `constexpr` in-flash table with O(1) `speciesData()` lookups
  (`PokemonSpecies.cpp:9-12`). Not a hot path; the readable two-pass weighted
  pick is worth keeping.
- **Per-row SD art loading** (`PokemonArt.cpp:23-43` opens and parses a BMP per
  icon, up to ~7 per list redraw). This is the module's founding design decision
  — artwork lives on the SD card specifically so the firmware stays ~43 KB
  (see the mechanics doc §6) — and moving it into flash is the exact trade that
  document already rejected.
- **`std::vector<std::string>` from `renderer.wrappedText()` in the battle log**
  (`PokemonActivity.cpp:3608-3623`) and in the message screen (`:2895`). Real
  per-frame heap churn, but it's CrossInk's own renderer API; changing it means
  changing shared engine code for a small gain, and this module is not where
  that decision belongs.
- **`bagItemCount()` / `bagItemIdAt()` walking the 84-item table per row**
  (`PokemonActivity.cpp:211-243`). O(84) over a `constexpr` flash table with no
  I/O; measurable only in the abstract.

---

## Summary

| Section | Count |
|---|---|
| Missing features (actionable) | 8, plus 8 explicitly ruled out |
| Bugs | 6 confirmed, 2 suspected |
| Performance | 7, plus 4 explicitly ruled out |

Highest-priority items across all three sections:

1. **Bug 2.1** — the 0-PP forced-move lock. A reachable, unrecoverable softlock
   that requires a device reboot.
2. **Perf 3.2** — 48-byte-at-a-time save I/O. It multiplies the cost of every
   other save-path item and is a mechanical, low-risk change.
3. **Missing 1.1** — move priority. Two move ids, ~30 lines, and it restores
   Quick Attack's entire reason to exist.
