---
title: Pokémon Module Audit — Round 6
parent: Development
nav_order: 13
---

# Pokémon Module Audit — Round 6 (missing features, bugs, performance)

A sixth full read of the Pokémon module (`lib/Pokemon/*`, `src/pokemon/*`,
`src/activities/pokemon/*`), done at `v0.21.1` (HEAD `0c238d1c`), immediately
after round 5's own fix (bug 3.1: Toxic's `toxicCounter` wasn't persisted
across a switch). This round's brief: (1) re-verify round 5's own fix is
correct and complete, (2) hunt hard for new bugs in the highest-risk recently-
changed code, (3) do a fresh, broad pass over areas rounds 2-5 didn't focus on
(UI/state-machine layer, i18n, gym data), and (4) look for new missing-feature
and performance candidates.

**What this round found, in one sentence per section:** round 5's own
`toxicCounter` persistence fix is fully correct and complete, with no gaps in
any of the reset/cure/heal paths checked; this round found **7 new bugs** —
two wrong-generation status-formula constants (Confusion self-hit chance,
Paralysis Speed reduction) that appear to have never been checked against
real Gen 1 numbers by any prior round, a genuine Substitute-timing gap that
lets a status/flinch/stat-drop effect "leak through" on the exact hit that
breaks the Substitute, two multi-turn-effect scoping gaps (trapping moves and
Reflect/Light Screen/Mist not releasing/persisting correctly across a
switch), a real UI navigation defect (renaming an already-owned Pokémon
always exits to the wrong screen), and a real turn-economy bug (throwing a
Poké/Great/Ultra Ball never gives the wild Pokémon its turn, unlike every
other non-attacking action) — plus 2 lower-confidence notes (Bide/Confusion
self-hit not triggering Rage, a PP-Up-before-consume ordering risk); no new
missing-feature candidates beyond what's already tracked and deliberately
deferred; one new low-priority performance note.

---

## Section 1 — Re-verification of round 5's own fix (bug 3.1: Toxic counter persistence)

Read directly against the code at `0c238d1c`, not trusted from
`CHANGELOG.md`/`TASKS.md`.

**Format bump.** `lib/Pokemon/PokemonBattleStoreCodec.h:21-35` correctly
introduces `POKEMON_BATTLE_ENTRY_BYTES = 21` (v3) alongside the preserved
`_V1`/`_V2` constants (16/20 bytes), and `POKEMON_BATTLE_STORE_VERSION = 3`.
`BattleRecordEntry::toxicCounter` (`:88`) is the new field, with a doc comment
correctly describing it as "meaningful only while `status == Ailment::Poison`
- kept at 0 otherwise."

**Codec correctness** (`PokemonBattleStoreCodec.cpp`):
- `validateBattleRecordEntry()` (`:67`) enforces the invariant directly:
  `if (entry.status != Ailment::Poison && entry.toxicCounter != 0) return false;`
  — the same pattern already used for `statusTurns` one line above it.
- `encodeBattleRecordEntry()`/`decodeBattleRecordEntry()` (`:81`, `:96`)
  correctly read/write byte offset 20 (the 21st byte), consistent with the
  new `POKEMON_BATTLE_ENTRY_BYTES`.
- `decodeBattleRecordEntryV1()`/`decodeBattleRecordEntryV2()` (`:102-134`)
  both correctly leave `toxicCounter` at its default-constructed `0` and both
  have accurate comments explaining why (a v1/v2 file predates Toxic
  escalation, so any Toxic'd Pokémon saved under an old file resumes at flat
  1/8 - a one-time, honestly-documented downgrade on first load after an
  update, not a new bug).
- `decodeBattleStoreFile()` (`:225-272`) correctly branches entry byte size
  and decode function on the header version byte for all three versions, and
  `validateBattleStoreState()` re-validates every decoded entry (so a
  corrupt/impossible `toxicCounter` value can never silently load).

**All 5 read/write call sites checked, confirmed correct:**
- `PokemonActivity::setupBattlePlayer()` (`PokemonActivity.cpp:1048`) restores
  `battlePlayer_.toxicCounter = entry.toxicCounter;` - this function runs at
  battle start *and* on every switch, which is exactly the boundary round 5
  found broken.
- `PokemonActivity::savePlayerBattleEntry()` (`:1190`) writes it back out,
  correctly re-zeroing it if status isn't Poison
  (`battlePlayer_.status == pokemon::Ailment::Poison ? battlePlayer_.toxicCounter : 0`)
  rather than trusting whatever stale value might be sitting in
  `battlePlayer_` from an earlier cure.
- The mid-battle item-sync path (`:1680`, the `BattleMedicine` branch of
  `Screen::ItemTarget`) pulls the freshly-cured/topped-up entry back into
  `battlePlayer_.toxicCounter`, including going back to 0 if the item cured
  the Poison outright.
- `PokemonService::useConsumable()`'s status-cure branch
  (`PokemonService.cpp:591`) and `healPartyOnRead()`'s full-heal-on-full-HP
  branch (`:865`) both explicitly zero `entry.toxicCounter`/`healed.toxicCounter`
  alongside `status`/`statusTurns`, with comments citing round 5 directly.

**Edge cases specifically re-checked, per this round's brief:**
- **`PokemonBattleStore::reset()`** (`src/pokemon/PokemonBattleStore.cpp:195-198`)
  writes a fully default-constructed `BattleStoreState{}` - every entry
  (including `toxicCounter`) is zeroed. Used by the Settings "Reset Game"
  flow (`PokemonActivity.cpp:2142` → `PokemonService::reset()` →
  `battleStore_.reset()`, `PokemonService.cpp:1011`). No gap.
- **Evolution mid-battle**: doesn't exist as a code path at all - evolution
  in this project is a reading-time event (`queueMoveLearnIfNeeded()`/
  `resolveEvolution()`), never triggered from inside a battle turn, so there
  is no "does toxicCounter survive an in-battle evolution" question to answer.
- **Full Heal/other status-cure items and the reading-time heal-on-read
  path**: both explicitly checked above (`useConsumable()`,
  `healPartyOnRead()`) - both correctly zero `toxicCounter` in lockstep with
  `status`.
- **A Safeguard-shaped move**: this project has no Safeguard (Gen 2) or any
  move that blocks status infliction outright; not applicable.
- **`faintCombatant()`** (`PokemonBattle.cpp:1649`) and Haze's
  `clearHazeState()` (`:1285`) both zero `toxicCounter` in-memory, as already
  confirmed by round 5 - re-confirmed still correct.

**Conclusion for Section 1: round 5's fix is complete and correct. No gaps
found in any reset/cure/heal/switch/reload path.**

---

## Section 2 — New bugs

Two independent passes were done: a full read of `lib/Pokemon/PokemonBattle.cpp`/
`.h` (the highest-risk file, most recently touched by rounds 4-5's mechanics),
and a full read of the UI/state-machine layer (`PokemonActivity.cpp`), gym
data, and i18n strings (deliberately under-scrutinized by rounds 2-5, which
focused on the battle engine and save layer).

### 2.1 (new, high value) — Confusion's self-hit chance uses the wrong generation's value: 33% instead of Gen 1's real 50%

**Severity: medium-high. Confidence: high.**

`lib/Pokemon/PokemonBattle.cpp:13`:
```cpp
constexpr uint8_t CONFUSION_SELF_HIT_CHANCE_PERCENT = 33;
```
used at `:623` (`statusPreventsAction()`'s `Ailment::Confusion` case) to decide
whether a confused Pokemon hurts itself instead of acting. **Real Generation I
confusion has a 50% (1-in-2) chance of self-hit** - Bulbapedia documents this
plainly, and explicitly notes the value was *lowered* to 33% starting in
Generation III onward. 33% is a real, well-known number in this exact
constant's own file - it's also the correct chance for Psychic's Gen 1
secondary stat-drop quirk (`SECONDARY_STAT_DROP_TABLE`, `PokemonBattle.cpp:262`,
correctly commented "a real Gen 1 quirk gives this one ~33%") - which suggests
this may be an accidental cross-contamination between the two constants
rather than an considered choice. No comment anywhere near
`CONFUSION_SELF_HIT_CHANCE_PERCENT` claims 33% is intentional or Gen-1-accurate,
unlike every other deliberately-kept divergence in this codebase (Toxic
fraction, Freeze thaw chance, Counter's type-agnosticism), which are always
documented as considered trade-offs. This one simply has the wrong number.

**Effect**: a confused Pokemon in this engine is meaningfully *less* punished
by its own confusion than real Gen 1 (self-hits ~1/3 of the time instead of
~1/2), understating Confusion's real danger.

**Not previously flagged.** Grepped every prior round's doc
(`docs/development/pokemon-gen1-audit-round{2,3,4,5}.md`) and `TASKS.md` for
"confus" and "CONFUSION" - no hit. This constant was never checked against
real Gen 1 numbers before.

### 2.2 (new, high value) — Paralysis halves Speed instead of Gen 1's real 1/4 reduction

**Severity: medium-high. Confidence: high.**

`lib/Pokemon/PokemonBattle.cpp:358` (`effectiveSpeed()`, shared by
`stepBattle()`'s turn-order calculation *and* `attemptRun()`'s escape-odds
formula - so this one wrong constant affects two separate mechanics at once):
```cpp
if (combatant.status == Ailment::Paralysis) speed /= 2U;
```
**Real Generation I (through Generation VI) reduces a paralyzed Pokemon's
Speed to 1/4 of its normal value**, not 1/2 - Bulbapedia documents the change
to a milder 1/2 reduction as a Generation VII balance change specifically
because the original 3/4 reduction was considered too punishing. This project
aims for Gen 1 authenticity throughout (its own comments cite Gen 1 constants
by name everywhere else - `PARALYSIS_FAIL_CHANCE_PERCENT = 25` two lines
above this one is correctly the real Gen 1 value), so `/2U` here should be
`/4U`.

**Effect**: a paralyzed Pokemon in this engine keeps twice as much Speed as it
should relative to real Gen 1, meaning it out-speeds/ties opponents far more
often than authentic, and `attemptRun()` (which reuses this exact function,
`PokemonBattle.cpp:346-348`'s own doc comment says so explicitly) gives a
paralyzed Pokemon better escape odds than Gen 1 would.

**Not previously flagged.** Round 2's own doc mentions the phrase
"paralysis-halved Speed" once (`pokemon-gen1-audit-round2.md:49`) but only in
passing, while describing what the code does for an unrelated finding (move
priority) - it was never actually checked against the real fraction. No other
round mentions it at all.

### 2.3 (new, high value) — Substitute doesn't block a secondary effect (flinch, stat-drop, or primary status infliction) on the exact hit that breaks it

**Severity: high. Confidence: high (own code paths cross-checked directly).**

The engine already has the correct pattern for this, used in exactly one
place: the trap-immobilization check at `PokemonBattle.cpp:1231` gates on
`defenderHadSubstitute` - a snapshot taken *before* the hit lands
(`:1112`, `const bool defenderHadSubstitute = defender.substituteHp > 0;`),
specifically because "a trapping move's own damage can break the defender's
Substitute in this same action, but the trap should still be judged against
whatever was true when the hit landed... not the just-broken aftermath"
(comment at `:1107-1111`).

Three other secondary-effect checks in the same function do **not** reuse
that snapshot and instead check the post-hit, already-decremented
`substituteHp` directly:

1. **Flinch** (`PokemonBattle.cpp:1184-1188`):
   ```cpp
   if (const FlinchTableEntry* flinch = flinchEntryForMove(moveId);
       flinch != nullptr && effectivenessPercent != 0 && totalDamage > 0 && defender.currentHp > 0 &&
       defender.substituteHp == 0 && rollPercentChance(random, flinch->chancePercent)) {
     defender.flinched = true;
   }
   ```
2. **Secondary stat-drop** (Acid/Bubble Beam/Aurora Beam/Psychic/Constrict/
   Bubble), `PokemonBattle.cpp:1195-1199`, same `defender.substituteHp == 0`
   guard.
3. **Primary damaging-move ailment infliction** (Thunder Shock's paralysis,
   Ember's burn, Ice Beam's freeze, Poison Sting's poison, ...),
   `PokemonBattle.cpp:1550-1552`:
   ```cpp
   } else if (defender.status == Ailment::None && defender.currentHp > 0 && defender.substituteHp == 0 &&
              move->ailment != Ailment::None && rollPercentChance(random, effectiveAilmentChance)) {
     defender.status = move->ailment;
   ```
   This third site is in a later part of `resolveGenericMoveEffect()`,
   entirely out of scope of the earlier `defenderHadSubstitute` local (it's
   declared inside the generic-damage branch above) - a real fix needs a
   fresh pre-hit snapshot taken at the top of the function, not just reusing
   the existing one.

**Why this is wrong**: real Gen 1 Substitute absorbs damage *and* blocks every
secondary consequence of the hit that broke it - the decoy, not the real
Pokemon, was the thing that got hit. This engine gets that right for
trap-immobilization but wrong for flinch, secondary stat-drop, and (most
importantly, since it's the most common case) a damaging move's own status
chance. Net effect: any single hit that both breaks a Substitute and rolls
its ailment/flinch/stat-drop chance incorrectly lands that effect on the real
Pokemon behind it - e.g. a Thunder Shock that breaks a 1-HP Substitute can
still paralyze the real Pokemon 10% of the time, which real Gen 1 would never
allow.

### 2.4 (new, medium) — A trapped target isn't released if the trapping Pokemon faints or switches out mid-trap

**Severity: medium. Confidence: high.**

`BattleCombatant::trappedTurnsRemaining` (`PokemonBattle.h:257-262`) lives
entirely on the *victim's* side and is only ever decremented, unconditionally,
by `finishTurn()` (`PokemonBattle.cpp:1656-1657`):
```cpp
if (player.trappedTurnsRemaining > 0) --player.trappedTurnsRemaining;
if (opponent.trappedTurnsRemaining > 0) --opponent.trappedTurnsRemaining;
```
Nothing anywhere checks whether the specific Pokemon that applied the trap
(Wrap/Bind/Fire Spin/Clamp) is still the one on the field. `setupBattlePlayer()`/
`setupBattleOpponent()` (`PokemonActivity.cpp:1028`, `:1072`) both fully reset
their own side's `BattleCombatant` on a switch (`= pokemon::BattleCombatant{}`),
which correctly clears the *attacker's* `forcedMoveId`/`forcedTurnsRemaining`,
but does nothing about the *victim's* `trappedTurnsRemaining`, which is a
field on the other side's struct entirely and is untouched by either side's
own reset.

**Why this is wrong**: in real Gen 1, if the Pokemon that used a trapping move
faints or is switched out, the trapped target is released immediately. Here,
if the trapper faints (recoil, poison, a counter-attack) or a gym-leader AI
voluntarily switches it out mid-trap (a real, already-shipped mechanic since
`v0.17.0`), the victim stays immobilized against a completely different,
freshly-arrived Pokemon for however many turns were left on the original
trap - with no relationship at all to what's now actually on the field.

### 2.5 (new, medium) — Reflect/Light Screen/Mist end when the user switches out, instead of persisting for the side

**Severity: medium. Confidence: high (matches real Gen 1 mechanics; the
project's own comment already documents extending the duration but not the
scoping gap).**

`reflectActive`/`lightScreenActive` (`PokemonBattle.h:179-184`) and
`guardSpecActive` (reused for Mist, `:146`) all live directly on
`BattleCombatant`, which - as established in 2.4 above - is fully reset on a
switch. The existing doc comment on these fields already flags a *deliberate*
divergence ("simplified from the real 5-turn timer... for the rest of the
battle") but never addresses that the effect is scoped to the individual
combatant rather than the side. Real Gen 1 Reflect/Light Screen/Mist protect
the whole side and persist through a switch, only ending on their timer or
when the side is defeated. Here, switching the Pokemon that set up Reflect/
Light Screen/Mist silently drops the protection immediately, rather than it
carrying over to whichever Pokemon comes in next.

(Guard Spec./Dire Hit/Focus Energy being individual-scoped and ending on
switch is *correct* - those really are per-Pokemon effects in Gen 1 - so this
finding is specifically about Reflect/Light Screen/Mist.)

### 2.6 (new, medium-high) — Renaming an already-owned Pokémon always exits to the wrong screen

**Severity: medium-high (always-reproducible on every use of this feature,
though not data-corrupting). Confidence: high.**

`PokemonActivity::openNickname()` (`PokemonActivity.cpp:780-804`) takes a
`cancelScreen` parameter, but only actually uses it on the Cancel branch:
```cpp
startActivityForResult(std::move(keyboard), [this, recordId, starter, cancelScreen](const ActivityResult& result) {
  if (result.isCancelled) {
    setScreen(cancelScreen);
    return;
  }
  ...
  } else if (service_.renamePokemon(recordId, keyboardResult->text) == pokemon::ServiceStatus::Ok) {
    if (!refreshSnapshot()) return;
    nicknamePrompt_ = {};
    setScreen(pokemon::pendingEventFront(snapshot_.state) == nullptr ? Screen::Menu : Screen::Event);
  } else {
    showMessage(tr(STR_POKEMON_SAVE_ERROR), Screen::Menu);
  }
});
```
Both the success path and the SD-write-failure path hard-code
`Screen::Menu`/`Screen::Event`, ignoring `cancelScreen` entirely.

This happens to be correct for the post-catch/starter naming flow
(`PokemonActivity.cpp:1414`, `openNickname(..., Screen::NicknameQuestion)`) -
landing on the main Menu (or the next queued Event) right after naming a
fresh catch is the right behavior. It is **wrong** for the second call site,
`Screen::Actions`'s `CollectionAction::Rename` on an already-owned Pokemon
from Party or PC Box (`PokemonActivity.cpp:1508`,
`openNickname(focusedRecordId_, false, Screen::Actions)`): every sibling
action in that same switch (`Deposit`/`Withdraw` → `actionSource_`,
`EvolutionPrompts` → `Screen::Summary`) correctly returns the player to where
they came from, but Rename unconditionally kicks the player out to the
top-level Pokemon Menu (or `Screen::Event`, if a reading-time event happens to
be queued) instead of back to `Screen::Actions`/`Screen::Party`/`Screen::Pc`.

### 2.7 (new, medium-high) — Throwing a Ball never gives the wild Pokémon its turn on a failed catch

**Severity: medium-high (systematically exploitable turn-economy bug, not
just a cosmetic gap). Confidence: high.**

`Screen::BattleBalls`'s activate handler (`PokemonActivity.cpp:2060-2093`)
calls `service_.attemptBattleCatch()` (a pure probability check with no side
effect on the opponent), consumes the ball, saves the player's battle entry,
and on a failed catch just shows "X broke free!" and returns to
`Screen::Battle` - **with no call to `service_.resolveOpponentOnlyTurn()`**.

Every other non-attacking action in this same file explicitly implements the
real Gen 1 rule that spending a turn on anything besides attacking still
costs the turn and lets the opponent act:
- `finishItemUseMidBattle()` (`PokemonActivity.cpp:1223-1248`) - its own doc
  comment states *"Using an item mid-battle costs the whole turn in Gen 1 -
  the opponent attacks the active Pokemon right away..."* and calls
  `resolveOpponentOnlyTurn()`.
- The voluntary-switch path in `Screen::BattleSwitch`
  (`PokemonActivity.cpp:1990`) does the same after a switch.

Throwing a Ball is exactly this same category of action, yet it's the one
case where the opponent is never given a turn. **Effect**: a player can throw
Balls at a wild Pokemon indefinitely with zero risk of being hit, poisoned,
put to sleep, or anything else - a real, systematically exploitable
turn-economy bug, not merely a missing authenticity nicety, since the
codebase already explicitly encodes "anything besides attacking still costs
you the turn" as a rule everywhere else and this path silently violates it.

### 2.8 (new, low confidence, noted for completeness) — Two damage sources don't trigger Rage's Attack-raise

**Severity: low-medium. Confidence: medium (the general Rage mechanic is
solidly documented; whether Bide's release and Confusion's self-hit
specifically should trigger it in this engine's already-simplified Rage
model is a closer call).**

Every other damage-application path calls `raiseAttackIfEnraged(defender,
damage)` right alongside `applyDamageRespectingSubstitute()` - the
`FIXED_DAMAGE_TABLE` branch (`PokemonBattle.cpp:1092`) and the generic
per-hit loop (`:1137`) both do. Two damage sources don't:
- **Bide's release** (`:886-892`) - `applyDamageRespectingSubstitute(defender,
  bideDamage);` with no adjacent `raiseAttackIfEnraged()` call.
- **Confusion's self-hit** (`:623-636`) - same omission; real Gen 1's
  documented Rage behavior ("Attack rises whenever the user takes damage")
  does include a confused Pokemon's own self-hit as a case that stacks Attack.

Both are edge cases (an enraged Pokemon being hit by *its own* stored Bide
release, or a Rage user that's also confused) rather than a common
interaction, which is why this is flagged at lower confidence/severity than
2.1-2.7 above.

### 2.9 (new, low, likely same class as an already-fixed bug) — PP Up is applied and persisted before the item is confirmed consumed

**Severity: low (requires an actual SD write failure to trigger).**

`Screen::PpUpSlot`'s activate handler (`PokemonActivity.cpp:1754-1776`) calls
`service_.applyPpUp()` (which durably persists the PP Up to the move slot)
*before* attempting `service_.consumeBagItem(pokemon::PP_UP_ITEM_ID)`. If
`applyPpUp` succeeds but the subsequent `consumeBagItem` fails (a rare SD
write error), the boost is already saved but the item was never removed from
the bag - a free PP Up on retry. This is the exact bug shape round 3 already
found and fixed for battle-boost items in `Screen::BattleBag` (see the
comment at `PokemonActivity.cpp:2022-2026` citing that fix directly:
*"Confirmed applicability AND consume the item before mutating battlePlayer_"*)
- the fix was never mirrored into the PP Up path (shipped later, in
`v0.11.x`).

---

## Section 3 — Missing Gen 1 features

No new candidates found. Specifically considered and ruled out:

- **EV vitamins (HP Up/Protein/Iron/Calcium/Carbos)**: real Gen 1 lets a
  player manually add EVs via these items. This project's EV model is
  already a documented, deliberate simplification (auto-accumulated on every
  battle win, not the real "Stat Experience" mechanic - see
  `docs/development/pokemon-iv-ev-plan.md`), and adding vitamins on top would
  be a second, redundant way to gain the same resource this project already
  chose to automate. Not recommended - would need its own design conversation
  about whether manual EV control is even desired here, not a natural
  "missing feature" to just add.
- **Held items, Safari Zone, trainer classes/rematches, in-game trades,
  breeding, abilities/natures**: already explicitly ruled out of scope by
  round 2, re-confirmed no reason to revisit.
- **Repel (item 1.8)**: remains open exactly as the user left it - deliberately
  not re-proposed here per their explicit request.
- **HM moves being un-forgettable outside a Move Deleter**: real Gen 1
  restricts this, but this project's Moveset screen letting a player freely
  forget an HM move is arguably *more* convenient for a game with no overworld
  puzzle-solving use for Cut/Surf/Strength outside battle - not a gap worth
  closing.

---

## Section 4 — New performance findings

No new candidates found beyond what round 5 already flagged and left
unresolved (5.1, the PC Box alphabetical sort's `O(S²)` species-ordering step -
still small, still not a priority). Specifically checked and found clean:
`PokemonArt.cpp`'s `drawPath()` re-opens/re-parses the relevant `.bmp` file on
every call rather than caching a decoded bitmap - but every call site is a
screen-redraw path on an e-ink device (rare, human-paced redraws, not a
per-frame animation loop), so this is not a meaningful cost the way the
already-fixed per-frame `Summary`/`refreshSnapshot()` issues were; not
recommended as a finding. The i18n string lookup (`I18n::get()`) is a direct
O(1) array-offset lookup, not a linear scan - no issue. The two
`Screen::Event` `readRecord()` calls (`PokemonActivity.cpp:3351`, `:3358`) are
each in a different `pending.kind` branch (Evolution vs. MoveLearn), not a
redundant double-read of the same record - no issue.

---

## Summary

| Section | Count |
|---|---|
| Round 5 fix re-verified | 1 of 1 (toxicCounter persistence) - fully correct and complete, no gaps found in any reset/cure/heal/switch/reload path |
| Bugs (new) | 7 new (2.1-2.7), plus 2 lower-confidence/lower-severity notes (2.8, 2.9) |
| Missing features (new) | 0 new candidates; 4 considered and explicitly ruled out (EV vitamins, already-ruled-out-scope items reconfirmed, Repel reconfirmed deferred, HM-forgettability reconfirmed not a gap) |
| Performance (new) | 0 new findings; 3 areas checked and confirmed clean |

**Highest-priority/highest-value finding overall: 2.3 (Substitute doesn't
block a secondary effect on the hit that breaks it)** - it's the most
mechanically significant of the seven (affects the outcome of ordinary
damaging-move turns, not just an edge case), though **2.1 and 2.2 (Confusion
self-hit chance and Paralysis Speed reduction using the wrong generation's
values) are the most surprising**, since both are simple, checkable constants
that no prior round appears to have ever verified against real Gen 1 numbers,
unlike every other status-effect constant in the same file which is already
correctly cited or explicitly flagged as a deliberate divergence. **2.7 (Ball
throws give no opponent turn) is the most exploitable** in terms of actual
player-facing gameplay impact if left unfixed.
