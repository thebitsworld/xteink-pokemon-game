---
title: Pokémon Module Audit — Round 3
parent: Development
nav_order: 10
---

# Pokémon Module Audit — Round 3 (missing features, bugs, performance)

A third full read of the Pokémon module (`lib/Pokemon/*`, `src/pokemon/*`,
`src/activities/pokemon/*`, `src/components/pokemon/*`, ~13,000 lines, plus the
native tests under `test/pokemon_*/`) done at `v0.20.1`.

**Relationship to the two earlier audits.**

- [pokemon-gen1-authenticity-roadmap.md](pokemon-gen1-authenticity-roadmap.md)
  is audit #1: one running file with nine internal "Round N" sections, all
  scoped to the *battle engine's* Gen 1 fidelity. Everything on it shipped.
- [pokemon-gen1-audit-round2.md](pokemon-gen1-audit-round2.md) is audit #2: a
  wider pass (missing features + bugs + performance) that produced 21 items.
  Its bug and performance sections have shipped (`v0.19.1`–`v0.20.1`); its
  **Section 1 has not** — see Section 0 below.

This is audit #3 of the module. Its numbering is unrelated to the internal
"rounds" in the first document. Everything here was checked against the actual
code at `v0.20.1`, not against the changelog. Items already shipped (see
`CHANGELOG.md` `[0.1.0]`–`[0.20.1]`) and items the earlier docs explicitly ruled
out are **not** re-raised — including everything in round 2's own "considered
and explicitly not worth doing" lists (Safari Zone, trainer classes/rematches,
in-game trades, breeding/held items/abilities/natures/the Special split, the
1/256 miss bug, Rage's hard lock-in, splitting battle XP, money/Marts, the
modern type chart, per-row SD art loading, `chooseRegularSpecies()`'s two-pass
weighted pick).

Particular attention was paid to the code that shipped *after* round 2 and has
never been audited: the Release feature (`RecordMutationKind::Remove`,
`releaseRecord()`, `PokemonStore::writeSnapshot()`'s removal path,
`PokemonService::releasePokemon()`, `PC_BOX_MAX_RECORDS`, `Screen::PcReleaseConfirm`),
batched record I/O (`BatchedRecordReader`/`BatchedRecordWriter`), the IV/EV cap
change (1024 → 512 with legacy decode clamping), the shared `PokemonCrc32`, the
batched `PokemonBattleStore::upsertEntries()`, and `PokemonActivity`'s
`focusedRecord_` cache. **Two of the three highest-priority findings below are
in that new code.**

Four sections follow: **Round 2's deferred items (status only)**, **Missing
Features**, **Bugs**, **Performance**. Each item is self-contained — a future
session with zero context should be able to pick any single one and start
implementing without re-deriving the finding.

---

## Section 0 — Status of round 2's 8 deferred design-call items

Round 2's Section 1 flagged 8 items as *deferred design calls needing the
user's input*. **All 8 are still genuinely open** — verified against the code at
`v0.20.1`, not against the changelog. None of them appear in `CHANGELOG.md`, and
none has any implementation in the engine.

Do **not** re-analyse these; round 2 already has the full analysis, the real Gen 1
rule, and a recommended implementation for each. This section exists only so
whoever picks the work up next knows exactly which are real remaining work.

| # | Item | Status at `v0.20.1` | Evidence |
|---|---|---|---|
| 1.1 | Move priority (Quick Attack +1, Counter −1) | **Open** | `MoveData` (`lib/Pokemon/PokemonBattleTypes.h:43-53`) still has no `priority` field; `stepBattle()` still orders purely by Speed (`lib/Pokemon/PokemonBattle.cpp:1613`) |
| 1.2 | Toxic's escalating damage | **Open** | No `toxicCounter` anywhere in `BattleCombatant` (`PokemonBattle.h:68-256`); Toxic still falls through the generic ailment block (`PokemonBattle.cpp:1350-1361`) |
| 1.3 | Running from a wild battle can't fail | **Open** | No `attemptRun()` in the engine and no `battleRunAttempts_` in the activity; RUN is still an unconditional `resolveBattleAsPass()` (`src/activities/pokemon/PokemonActivity.cpp:1815`, hardware-Back path `:2092`) |
| 1.4 | Haze only resets stat stages | **Open** | `PokemonBattle.cpp:1107-1110` still calls only `resetBattleStages()` on both sides. The `StatsReset` doc comment (`PokemonBattle.h:292-298`) was *updated* to admit this, which resolves round 2's **Bug 2.5** (documentation/behavior divergence) but not feature 1.4 itself |
| 1.5 | Status/Leech Seed drain 1/8, Gen 1 uses 1/16 | **Open** | `STATUS_DAMAGE_FRACTION = 8` (`PokemonBattle.cpp:15`), still shared by `applyEndOfTurnStatusDamage()` (`:525`) and `applyLeechSeedDamage()` (`:537`) |
| 1.6 | Speed ties always go to the player | **Open** | `const bool playerFirst = playerSpeed >= opponentSpeed;` (`PokemonBattle.cpp:1613`) |
| 1.7 | Freeze thaws on its own (20%) | **Open** | `FREEZE_THAW_CHANCE_PERCENT = 20` (`PokemonBattle.cpp:14`) — and round 2's fallback recommendation ("leave it, but document the divergence in the constant's comment") was *also* not done; the constant still carries no comment at all |
| 1.8 | Counter reflects any physical move | **Open** | No `lastPhysicalDamageType` field; `FixedDamageKind::Counter` still reflects `lastPhysicalDamageTaken` unconditionally (`PokemonBattle.cpp:927-939`) |

Round 2's two **suspected** bugs are also still open and unchanged:

- **2.7** — damage absorbed by a Substitute still feeds Counter
  (`PokemonBattle.cpp:947-949`, `:1010-1012`), Bide (`:997-999`) and Rage
  (`:946`, `:991`). Still needs a deliberate call, not a blanket fix.
- **2.8** — waking from Sleep costs no turn (`PokemonBattle.cpp:468-476`).

Everything else from round 2 (bugs 2.1–2.6, performance 3.1–3.7) is shipped;
where a fix was only partially applied, that is called out as a *new* item in
Section 3 below rather than re-raised here.

---

## Section 1 — Missing Gen 1 features

New findings only — nothing here overlaps Section 0. Ordered
cheapest-and-highest-impact first, the same way both earlier documents were.

### 1.1 Status conditions ignore the target's own type immunity (Fire can be burned, Ice can be frozen)

**Priority: high. Cost: low (~15 lines + 3 tests). Impact: medium — affects a large fraction of real battles.**

`v0.19.2` correctly added *move*-type immunity to the ailment block
(`lib/Pokemon/PokemonBattle.cpp:1339-1349`): a move whose own type scores 0%
against the defender can no longer inflict its ailment. That covers Thunder
Wave vs. Ground. It does **not** cover Gen 1's separate, unrelated rule that
some types are immune to specific *status conditions* regardless of the move's
type:

| Defender type | Immune to | Currently |
|---|---|---|
| Fire | Burn | Fire Blast/Flamethrower/Ember/Fire Punch can burn a Charizard |
| Poison | Poison | Poison Powder/Toxic/Smog/Sludge/Poison Sting can poison a Muk |
| Ice | Freeze | Ice Beam/Blizzard/Ice Punch can freeze a Lapras |

None of these is blocked by the existing check, because none of those matchups
is 0% on the type chart (Fire→Fire is 50%, Poison→Poison is 50%, Ice→Ice is
50%). Verified by reading: the ailment block at
`PokemonBattle.cpp:1350-1361` only tests `defender.status == Ailment::None`,
`currentHp > 0`, `substituteHp == 0` and the chance roll.

This is a real gameplay effect, not a footnote — Koga's entire gym roster is
Poison-type and can currently be poisoned; Blaine's is Fire and can be burned.

**How to implement.** In `resolveGenericMoveEffect()`, right before the
`defender.status = move->ailment;` line, add a helper in the anonymous
namespace:

```cpp
// Real Gen 1 type-based status immunities - independent of the move's own
// type effectiveness (which is already handled above): a Fire-type can never
// be burned, a Poison-type never poisoned, an Ice-type never frozen.
// Deliberately does NOT include the Gen 6+ additions (Electric/paralysis,
// Grass/powder moves) - those are not Gen 1 rules.
bool typeIsImmuneToAilment(const EffectiveTypes& types, Ailment ailment) {
  const auto has = [&](PokemonType t) { return types.primary == t || types.secondary == t; };
  if (ailment == Ailment::Burn) return has(PokemonType::Fire);
  if (ailment == Ailment::Poison) return has(PokemonType::Poison);
  if (ailment == Ailment::Freeze) return has(PokemonType::Ice);
  return false;
}
```

Use `effectiveTypesFor(defender, *defenderSpecies)` (already computed at
`:887-892` as `defenderTypes` — hoist it out of that `if` so the ailment block
can see it) so a Conversion-changed type is respected, matching how every other
type check in this file already works. Report
`BattleLogEvent::MoveNoEffect` for a pure Status move that is fully blocked
this way (same branch shape as `:1339-1349`); for a damaging move with a
blocked secondary effect, just skip the status silently — the damage still
happened and already has its own log line.

**Tests:** mirror the three `v0.19.2` immunity tests in
`test/pokemon_battle/PokemonBattleTest.cpp` (grep for the Thunder-Wave-vs-Ground
test added there) with Ember-vs-Charmander, Poison-Powder-vs-Grimer and
Ice-Beam-vs-Dewgong.

### 1.2 Three real Gen 1 multi-hit moves are missing from `MULTI_HIT_TABLE`

**Priority: medium-high. Cost: trivial (3 table rows + 1 test). Impact: low-medium.**

`MULTI_HIT_TABLE` (`lib/Pokemon/PokemonBattle.cpp:131-139`) lists 7 moves. Gen 1
has 10. The three missing ones all exist in `scripts/data/pokemon-moves.csv`
and are currently plain single-hit attacks:

| Move | Id | Real Gen 1 behaviour | Table entry to add |
|---|---|---|---|
| Double Kick | 24 | always exactly 2 hits | `{24, 2}` |
| Spike Cannon | 131 | rolls the 2/3/4/5 distribution | `{131, 0}` |
| Bonemerang | 155 | always exactly 2 hits | `{155, 0}` → **`{155, 2}`** |

All three are strictly weaker than intended right now (Spike Cannon is a
20-power move that should average ~2.9 hits; Double Kick a 30-power move that
should always hit twice). The `fixedHits` mechanism Twineedle already uses
(`{41, 2}`) covers Double Kick and Bonemerang with no new code at all.

Everything else this touches — recoil summing, `result.hitCount`, the
Substitute-break early exit added in `v0.19.2` — already generalises over any
table entry, so this really is three rows plus a test asserting Double Kick
lands exactly twice.

### 1.3 No damaging move has a secondary stat-drop effect

**Priority: medium. Cost: low (one small table + ~15 lines + 2 tests). Impact: medium.**

`statChangeForMove()` (`lib/Pokemon/PokemonBattle.cpp:70-93`, dispatched at
`:1111`) covers the 22 *status-category* stat-changing moves. Gen 1 also has six
**damaging** moves that carry a secondary chance to lower one of the target's
stats, and none of them is modelled — they are all currently plain attacks:

| Move | Id | Real Gen 1 secondary effect |
|---|---|---|
| Acid | 51 | 10% chance to lower Defense by 1 |
| Bubble Beam | 61 | 10% chance to lower Speed by 1 |
| Aurora Beam | 62 | 10% chance to lower Attack by 1 |
| Psychic | 94 | ~33% chance to lower Special by 1 |
| Constrict | 132 | 10% chance to lower Speed by 1 |
| Bubble | 145 | 10% chance to lower Speed by 1 |

Psychic is the notable one: its Special drop is a large part of why Psychic-types
dominated Gen 1, and it is on both Agatha's and the Champion's rosters.

**How to implement.** A new hand-authored table alongside `FLINCH_TABLE`
(`:229-236`) is the established pattern — PokeAPI's export carries no
stat-change column, which is exactly why the existing 22-move table is
hand-authored too:

```cpp
struct SecondaryStatDropEntry {
  uint8_t moveId;
  StatKind stat;
  uint8_t chancePercent;
};
constexpr SecondaryStatDropEntry SECONDARY_STAT_DROP_TABLE[] = {
    {51, StatKind::Defense, 10}, {61, StatKind::Speed, 10},  {62, StatKind::Attack, 10},
    {94, StatKind::Special, 33}, {132, StatKind::Speed, 10}, {145, StatKind::Speed, 10},
};
```

Apply it in the generic damaging branch, right beside the existing flinch roll
(`:1038-1042`), under the exact same guards that roll already uses
(`effectivenessPercent != 0 && totalDamage > 0 && defender.currentHp > 0 &&
defender.substituteHp == 0`) plus `!defender.guardSpecActive` — a Substitute and
Mist/Guard Spec. both block an opponent's stat drops everywhere else in this
engine (`:1117-1121`), and this must not become the one exception.

Wrinkle for the implementer: `formatBattleActionLine()`
(`src/activities/pokemon/PokemonActivity.cpp:413-422`) derives the stat name for
its "X's STAT fell!" clause from `statChangeForMove(moveId)`, which will return
`nullptr` for these moves. Either extend that lookup to also consult the new
table, or add a `StatKind` + a `statDropApplied` flag to `BattleActionResult`
(cleaner, and consistent with how `critical`/`hitCount`/`recoilApplied` are
already separate result fields rather than folded into `event`).

### 1.4 Razor Wind is not a two-turn move

**Priority: medium-low. Cost: trivial (1 table row + 1 test). Impact: low.**

`TWO_TURN_TABLE` (`lib/Pokemon/PokemonBattle.cpp:302-308`) has Fly, Dig, Solar
Beam, Skull Bash and Sky Attack. Gen 1 has a sixth: **Razor Wind (id 13)**,
which charges on turn 1 and attacks on turn 2 (no invulnerability). It is
currently an 80-power Normal special move that hits immediately — strictly
better than the real thing.

Add `{13, false}`. The whole two-turn state machine (forced continuation, the
PP-on-first-turn-only rule, the `v0.19.1` 0-PP continuation fix) already
generalises over the table.

### 1.5 Hyper Beam always forces a recharge, even when it faints the target

**Priority: medium-low. Cost: trivial (one condition). Impact: low-medium.**

`PokemonBattle.cpp:1105`: `if (moveId == HYPER_BEAM_MOVE_ID) attacker.mustRecharge = true;`
— unconditional on any non-miss, with a comment explicitly stating that even a
0-damage immune hit forces the recharge.

Real Gen 1: **the recharge is skipped if the target faints.** (It is also
skipped if the hit only broke a Substitute — the well-known second half of the
same quirk.) This matters most in a gym run, where KO'ing one team member with
Hyper Beam currently hands the trainer's next Pokémon a free turn.

Fix: `if (moveId == HYPER_BEAM_MOVE_ID && defender.currentHp > 0) attacker.mustRecharge = true;`
placed where it already is (after damage resolution, so `currentHp` is current).
Decide separately whether to include the Substitute half — this project has
consistently implemented intended behaviour rather than original bugs, but this
particular one is the *player-favourable* reading of a documented rule, so it is
a judgement call worth asking about rather than assuming.

### 1.6 Jump Kick / High Jump Kick deal no crash damage when they miss

**Priority: low. Cost: trivial (~5 lines + 1 test). Impact: low.**

Jump Kick (26) and High Jump Kick (136) are both high-power Fighting moves with
no downside at all right now — `resolveGenericMoveEffect()` returns
`BattleLogEvent::MoveMissed` at `:870-872` and nothing else happens.

Real Gen 1: a miss costs the user **1 HP** (not the 1/8-of-damage-dealt of later
generations — Gen 1's crash damage is a literal 1 HP). Cheap to add at the miss
return, and it reuses `result.recoilApplied` for the log clause so no new i18n
string is needed.

### 1.7 Teleport does nothing in a wild battle

**Priority: low. Cost: low. Impact: low — but pairs naturally with round 2's item 1.3.**

Teleport (id 100, `status` category, no ailment) has no dispatch branch, so it
falls through to the "But nothing happened!" catch-all at
`PokemonBattle.cpp:1326-1331`. In Gen 1, Teleport in a wild battle is a
**guaranteed escape** (and simply fails against a trainer).

The engine already has the right shape for this: `BattleLogEvent::ForcedSwitch`
is emitted by Whirlwind/Roar and deliberately leaves "what that actually means"
to `PokemonActivity.cpp`, which already ends a wild encounter via
`resolveBattleAsPass()` when the player's own Whirlwind connects
(`PokemonActivity.cpp:928-932`). Teleport used by the player against a wild
Pokémon can reuse that exact path; against a trainer it should report
`MoveFailed`. The engine can't tell the two apart on its own, so the cleanest
split is a new `BattleLogEvent::Teleported` that the activity interprets.

Worth doing **at the same time as round 2's item 1.3** (making RUN able to
fail), since Teleport's whole point is being the escape that never fails — it
has no identity until running can fail.

### Considered and explicitly **not** worth doing

Stated here so a future audit doesn't silently rediscover them.

- **Bite's Dark typing.** `scripts/data/pokemon-moves.csv` lists Bite (44) as
  `Dark`, which did not exist in Gen 1 (it was Normal there). But this project
  deliberately runs the *modern* type chart (`PokemonTypeChart.cpp:10-15`,
  already ruled out in round 2), under which Dark is a coherent, correctly-handled
  type — `singleTypeEffectivenessPercent()` resolves it fine. Changing it would
  make Bite inconsistent with the chart the rest of the game uses. Same reasoning
  covers Karate Chop (Fighting, was Normal) and Gust (Flying, was Normal).
- **Dizzy Punch's 20% confusion / Tri Attack's `ailment_chance` 20.** Both are
  modern-data artifacts (neither had a secondary effect in Gen 1). Tri Attack's
  chance is harmless (its `ailment` is `None`, so the block at `:1350` never
  fires). Dizzy Punch is a *more generous* divergence on a single move; not worth
  a data-file special case.
- **Confusion self-damage hitting the user's own Substitute.** Real Gen 1 does
  route it there; `statusPreventsAction()` (`:506-508`) applies it straight to
  `currentHp`. This is one move-interaction deep, affects almost nothing, and the
  fix would need a second `applyDamageRespectingSubstitute()` call site with its
  own reasoning about who owns the substitute. Left alone deliberately.
- **Twineedle's poison rolled once instead of per hit.** The ailment block runs
  after the whole multi-hit loop (`:1350`), so Twineedle gets one 20% roll rather
  than one per hit. The practical difference is 20% vs. 36%; not worth
  restructuring the loop.

---

## Section 2 — Bugs

Each item says whether it is **confirmed by reading the code path** or
**suspected**. None of these are style nitpicks.

### 2.1 Releasing any Pokémon other than the most recently caught one permanently breaks catching

**Severity: critical. Confirmed by reading. Cost to fix: ~15 lines + 1 test.**

This is the headline finding of this audit. It is in the brand-new Release
feature and no test covers it.

New record ids are allocated as `record count + 1`:

```
src/pokemon/PokemonService.cpp:296
    mutation.requestedRecordId = store_.recordCount() + 1U;
```

That was correct while records could only ever be appended — ids were dense
`1..N`, so `N+1` was always both unused and greater than every existing id. The
Release feature broke that invariant: `RecordMutationKind::Remove` deletes a
record from the middle of the file (`src/pokemon/PokemonStore.cpp:416-420`)
without renumbering anything, so ids become sparse while `recordCount()` drops.

Meanwhile the append path still enforces strictly-increasing ids (which
`docs/file-formats.md:133` documents as a real format invariant, and which
`inspectSnapshot()` re-verifies at `PokemonStore.cpp:209`):

```
src/pokemon/PokemonStore.cpp:433-437
  if (appending) {
    writeOk = writeOk && mutation.record.recordId > lastRecordId &&
              writeExact(destination, mutationRecordBytes.data(), mutationRecordBytes.size());
```

**The reachable sequence** (no save editing, no reboot, ~2 minutes of play):

1. Starter is record 1. Catch two Pokémon → records 2 and 3. `recordCount() == 3`.
2. Deposit record 2 to the PC Box, then PC Box → Actions → Release it.
   Records are now `{1, 3}`, `recordCount() == 2`.
3. Catch anything. `requestedRecordId = 2 + 1 = 3`. The copy loop walks records
   1 and 3, leaving `lastRecordId == 3`. The append check is `3 > 3` → false →
   `writeOk` false → `commit()` returns false → `resolveEncounter()` returns
   `StorageError` → the UI shows "Save error" (`PokemonActivity.cpp:1969-1972`).
4. Nothing is written, so `recordCount()` stays 2 and the next catch computes
   the same colliding id. **Every subsequent catch fails, forever.**

The player's only escape is to release the *highest-id* record too (which drags
`lastRecordId` back below `recordCount()+1`), which is not discoverable and
costs them another Pokémon. Releasing the newest Pokémon happens to work, which
is exactly why a quick manual test would miss this.

No data is corrupted — the write fails cleanly against the inactive slot and the
active save is untouched — but the game is unwinnable from that point.

**Fix.** Allocate `highest existing record id + 1` instead of `count + 1`:

- Add a `uint32_t highestRecordId_` member to `PokemonStore`, set alongside
  `activeHeader_` in `begin()` and `writeSnapshot()`. `inspectSnapshot()`
  already walks every record tracking `previousRecordId`
  (`PokemonStore.cpp:195`, `:215`) — return it through `outputHeader`'s sibling
  or an out-param rather than adding a second pass.
- Expose `uint32_t nextRecordId() const { return highestRecordId_ + 1U; }` and
  use it at `PokemonService.cpp:296`.
- Keep the existing `recordCount() >= 1024U` guard at `:284` as-is — that is a
  *live record* cap, and it stays correct with sparse ids.

Note the deliberate consequence: released ids are never reused, so ids grow
monotonically over a long playthrough. That is fine (`uint32_t`, and the format
only requires increasing, not dense), but it interacts with item 2.3 below —
read that before implementing.

**Tests to add** (`test/pokemon_service/PokemonServiceTest.cpp`, next to the
existing `ReleasePokemonRemovesTheRecordAndFreesItsSideStoreSlots` at `:299`):
release a middle record, then `resolveEncounter(Catch, ...)` and assert
`ServiceStatus::Ok` with a `caughtRecordId` greater than every surviving id.
A second test should release the *highest* record and assert the next catch
still doesn't reuse that id.

### 2.2 Evolution prompts can be turned off but not back on (regression introduced by the `focusedRecord_` cache)

**Severity: medium (user-visible, reproducible in four taps). Confirmed by reading. Cost to fix: ~3 lines.**

`v0.20.0` added `focusedRecord_` to avoid re-reading the focused Pokémon's record
on every row/frame (round 2's performance item 3.3). The cache is populated at
exactly three sites — `PokemonActivity.cpp:1350-1351` (Party/PC select),
`:1539-1540` (PP Up target) and `:1587-1588` (TM replace-slot) — and is **never
refreshed after a mutation**, including inside `refreshSnapshot()`.

The Evolution-prompts toggle was converted to read the cache in the same commit:

```
src/activities/pokemon/PokemonActivity.cpp:1402-1411
  case pokemon::CollectionAction::EvolutionPrompts: {
    if (focusedRecord_.recordId == 0) return;
    const bool enabled =
        (focusedRecord_.flags & pokemon::recordFlag(pokemon::RecordFlag::EvolutionPromptsDisabled)) == 0;
    if (service_.setEvolutionPrompts(focusedRecordId_, !enabled) != pokemon::ServiceStatus::Ok) {
```

(it previously did `service_.readRecord(focusedRecordId_, record)` and read the
fresh flags — confirmed by `git show 469be7a7 -- src/activities/pokemon/PokemonActivity.cpp`).

**Reproduction:** Party → pick a Pokémon → Actions → "Evolutions" (prompts go
Off, screen moves to Summary, which correctly shows "Off" because Summary reads
from disk) → Back (returns to Actions) → "Evolutions" again. `focusedRecord_`
still holds the *pre-toggle* flags, so `enabled` is computed as `true` again and
the service is asked to set the identical value. `setEvolutionPrompts()` succeeds
(it is effectively idempotent), so there is no error — the setting simply never
turns back on. The only way out is to back out to the Party/PC list and re-select
the same Pokémon.

The same staleness affects one cosmetic path: renaming a Pokémon
(`openNickname()` at `:1399`) updates the save but not `focusedRecord_`, so the
"Release NAME?" confirmation prompt (`:1394-1396`) shows the *old* nickname if
you rename and then release without leaving the Actions screen.

**Fix, pick one:**

- *Smallest and most robust:* re-populate the cache inside `refreshSnapshot()` —
  after `loadSnapshot()` succeeds, if `focusedRecordId_ != 0`, re-read it
  (`service_.readRecord(focusedRecordId_, focusedRecord_)`, ignoring failure).
  That costs one record scan per mutation, which is negligible next to the
  `commit()` that just happened, and it fixes every present and future staleness
  bug of this shape at once.
- *Narrowest:* in the `EvolutionPrompts` branch only, flip the cached flag bit
  locally after a successful call. Leaves the rename case broken.

### 2.3 `PC_BOX_MAX_RECORDS` (512) + party (6) exceeds `POKEMON_IVEV_MAX_ENTRIES` (512) by exactly the party size

**Severity: low. Confirmed by reading (logic/documentation mismatch). Cost to fix: one constant or one comment.**

`PokemonService.h:15-21` states the intent explicitly:

> matched to `POKEMON_IVEV_MAX_ENTRIES` (`PokemonIvEvStoreCodec.h`) so every
> record that can exist once this cap is enforced - up to `PARTY_SIZE` in the
> party, up to this many in the Box - always has room for a persisted IV/EV entry.

The arithmetic doesn't hold: the capacity gate at `PokemonService.cpp:289-295`
only blocks a catch when the party is full **and** `boxCount >= 512`, so the
maximum live record count is `512 + 6 = 518`, against an IV/EV store of exactly
512 entries (`PokemonIvEvStoreCodec.h:23`).

At that ceiling, `upsertIvEvEntry()` returns false for the last 6 records
(`PokemonIvEvStoreCodec.cpp:92`), `ensureIvEv()` falls back to its in-RAM
`pendingIvEvRolls_` cache (`PokemonService.cpp:760-766`), and those Pokémon get
a *fresh* IV roll after every reboot — their displayed max HP changes between
sessions. It degrades gracefully (that fallback exists precisely for this), it
is unreachable for any realistic save, and nothing crashes — hence low severity.
But the comment asserts a guarantee the code does not provide.

**Fix:** either raise `POKEMON_IVEV_MAX_ENTRIES` to `518` (costs 96 bytes of
`.bss`; note `decodeIvEvStoreFile()` accepts up to the legacy 1024 already, so
raising is a header-only change with no migration concern), or lower
`PC_BOX_MAX_RECORDS` to `506`, or correct the comment to say the last handful of
records degrade to unpersisted IVs. Raising is cleanest. Also note this
interacts with **2.1**: once ids stop being dense, a save can accumulate more
*distinct historical* ids than live records — but the IV/EV store is keyed by
live record id and `releasePokemon()` already calls `ivEvStore_.removeEntry()`
(`PokemonService.cpp:244`), so entry count still tracks live records, not id
range. That half is correct.

### 2.4 A simultaneous KO reports a loss, then forces a switch against an already-fainted opponent

**Severity: low-medium. Confirmed by reading; the exact on-screen feel would need a runtime check.**

Both simultaneous-faint paths resolve to `OpponentWon`:

```
lib/Pokemon/PokemonBattle.cpp:1463-1464   (end-of-turn, finishTurn)
  if (player.currentHp == 0 && opponent.currentHp == 0) {
    result.outcome = BattleOutcome::OpponentWon;  // simultaneous KO: wild Pokemon is still standing in spirit
lib/Pokemon/PokemonBattle.cpp:1631-1636   (mid-turn, after the player acts)
```

That convention is defensible in isolation, but the UI's handling of
`OpponentWon` doesn't account for the opponent also being at 0 HP:

```
src/activities/pokemon/PokemonActivity.cpp:902-910
  } else if (result.outcome == pokemon::BattleOutcome::OpponentWon) {
    if (usablePartySlotCount() > 0) {
      forcedBattleSwitch_ = true;
      setScreen(Screen::BattleSwitch);
```

So when the player KOs the opponent with Explosion/Self-Destruct, or faints to
Take Down/Double-Edge/Struggle recoil on the same hit that KOs the target:

- **With party members left:** the player is forced (Back is blocked,
  `:2099-2103`) to send out a replacement against a 0-HP opponent. The next time
  they act, `stepBattle()`'s own entry guard (`PokemonBattle.cpp:1583-1586`)
  immediately returns `PlayerWon` and the battle resolves as a win — so XP *is*
  awarded, just a turn late and after a nonsensical forced switch.
- **With no party members left:** `finishBattleAfterPlayerFainted()` runs — the
  fight counts as a loss, **no XP is awarded**, and for a gym challenge
  `finishGymChallenge(false)` discards the whole run even though the trainer's
  Pokémon also fainted.

The outcome of the same in-game event therefore depends on how many spare
Pokémon the player happens to have. Real Gen 1 resolves a simultaneous KO in the
player's favour when it was the player's attack that caused it.

**Fix.** Treat "both at 0" as `PlayerWon` when the *player's own action* caused
it (the `stepBattle()` mid-turn site at `:1631`), and leave the end-of-turn tie
(`finishTurn()`, where neither side "won") as-is with an updated comment. If
instead the current convention is deliberate, the UI side still needs fixing:
`resolveBattlePlayerMoveTurn()` should check `battleOpponent_.currentHp == 0`
before forcing a switch and route to `finishBattleAfterWildFainted()` instead.
Either way, do not leave the party-size-dependent split.

### 2.5 A mid-battle boost item is applied before its consumption is confirmed

**Severity: low. Confirmed by reading. Cost to fix: reorder two calls.**

```
src/activities/pokemon/PokemonActivity.cpp:1885-1894
  if (itemId == 0 || !pokemon::applyBattleBoostItem(battlePlayer_, itemId)) { ...NotApplicable... }
  if (service_.consumeBagItem(itemId) != pokemon::ServiceStatus::Ok) {
    showMessage(tr(STR_POKEMON_SAVE_ERROR), Screen::BattleBag);
    return;
  }
```

`applyBattleBoostItem()` mutates the live `battlePlayer_` (a stat stage, or the
Guard Spec./Dire Hit flag) *before* the bag count is decremented. If the
`consumeBagItem()` write fails, the buff has already landed and the item is still
in the bag — the player can retry and stack the effect for free, repeatedly, for
as many times as the SD write keeps failing.

Every neighbouring flow gets this ordering right (`Screen::ItemTarget`'s
Medicine path at `:1600-1606` consumes before syncing the combatant). Fix by
decrementing first and only applying the effect on success; note that
`applyBattleBoostItem()` doubles as the "is this even applicable" test (it
returns false at +6 / already-active), so the check and the mutation need
splitting, or the item needs re-crediting on failure.

### 2.6 A Poké Ball is spent and battle XP awarded before the Box-full check

**Severity: low. Confirmed by reading.**

In `Screen::BattleBalls` (`src/activities/pokemon/PokemonActivity.cpp:1940-1972`)
the order is: roll the catch → `consumeBagItem()` → `savePlayerBattleEntry()` →
`refreshSnapshot()` → on success `awardBattleXp()` → **then**
`resolveEncounter(Catch, ...)`, which is the first thing that can return
`ServiceStatus::BoxFull`.

So a successful throw into a full Box costs the ball, banks the XP, shows
"Box full", and drops the player back to the Menu with the encounter still
pending (`resolveEncounter()` never popped it). The Pokémon can be re-fought
after freeing space, so nothing is permanently lost — but the ball is, and the
XP is double-awarded on the retry.

`v0.20.0` added the `BoxFull` status specifically to stop catching "silently
failing", and this is the one path where it still costs the player something.
Fix by hoisting the capacity check to before the throw: the same
`partyFull && boxCount >= PC_BOX_MAX_RECORDS` test is already computable from
`snapshot_` in the activity (`ownedCount`/`partyCount` are right there — the
PC Box header already does this arithmetic at `:3981`), so gate the BALL menu
option on it the same way the ball-count check at `:1791` already gates it.

### 2.7 `upsertEntries()` discards the whole batch if any single entry is invalid

**Severity: very low. Confirmed by reading.**

```
src/pokemon/PokemonBattleStore.cpp:172-179
  for (const BattleRecordEntry& entry : entries) {
    if (!pokemon::upsertBattleEntry(candidate, entry)) return false;
  }
  return writeState(candidate);
```

Before `v0.20.1`, `healPartyOnRead()` called `upsertEntry()` once per party
member, so one bad entry only lost that member's heal. Now a single validation
failure (e.g. a corrupt persisted entry that `validateBattleRecordEntry()`
rejects) silently discards the heal for the entire party, on every checkpoint,
indefinitely.

The call is already best-effort (`PokemonService.cpp:834-838`), so the blast
radius is "reading stops healing anyone" rather than data loss. Fix by
continuing past a failed entry instead of returning, and only returning false if
nothing at all could be applied.

### 2.8 (Latent, not currently reachable) `CollectionActionSet` is exactly full

**Severity: none today; flagged because the next feature to touch it overflows silently.**

`CollectionActionSet::items` is `std::array<CollectionAction, 6>`
(`lib/Pokemon/PokemonGame.h:58-61`) and `collectionActions()` appends without any
bounds check (`lib/Pokemon/PokemonGame.cpp:801`). The Box branch with a
non-full party now emits exactly 6 (Summary, Moveset, Withdraw, Release, Rename,
EvolutionPrompts) — `Release` used the last free slot. A seventh action writes
`items[6]` out of bounds.

One line in `append`: `if (actions.count >= actions.items.size()) return;`.

---

## Section 3 — Performance and efficiency

Same constraints as round 2, restated because they still drive the priorities:
flash is the tighter budget, static RAM is meaningful but not critical, heap
*fragmentation* matters more than raw usage, and SD-card I/O is by far the most
expensive operation in the module (the `v0.18.1`–`v0.18.6` crash family came
from exactly this area).

Nothing round 2 already fixed is re-flagged. Where a round-2 item was only
*partially* addressed, that remainder is stated explicitly.

### 3.1 `loadSnapshot()` opens and linearly scans the whole save once per party member

**Priority: high. Cost: low-medium. Impact: large — this is the single hottest save-path call in the module.**

```
src/pokemon/PokemonService.cpp:48-55
  for (size_t slot = 0; slot < PARTY_SIZE && output.state.partyRecordIds[slot] != 0; ++slot) {
    if (!store_.readRecord(output.state.partyRecordIds[slot], output.party[slot])) {
```

`PokemonStore::readRecord()` (`src/pokemon/PokemonStore.cpp:460-487`) opens the
active file, seeks to the records region, and decodes forward until it finds the
id. With a full party that is **6 independent file opens and 6 independent
linear scans**, each decoding (and full-UTF-8-validating, via
`validateRecord()`) up to every record in the save.

`loadSnapshot()` is called by `refreshSnapshot()`, which runs after *every*
mutation — every catch, ball throw, item use, deposit, withdraw, release,
evolution, rename, TM teach, PP Up, gym victory — plus `loadInitialScreen()`.
On a 512-record Box with a level-appropriate party, one refresh is ~3,000
`decodeRecord()` calls and ~150 KB of SD traffic *on top of* the commit that
just happened.

`v0.19.3`'s batching reduced the syscall count (one read per 42 records) but not
the decode count, and it did nothing about the 6× repetition.

**Fix.** Add a single-pass multi-lookup to `PokemonStore`:

```cpp
// Reads up to PARTY_SIZE records in ONE pass instead of one pass per id.
// `ids` and `output` are parallel; a missing id leaves its slot default and
// is reported through `foundMask`.
bool readRecords(std::span<const uint32_t> ids, std::span<PokemonRecord> output, uint8_t& foundMask) const;
```

Implement it with the existing `BatchedRecordReader`, matching each decoded
record against the (tiny, ≤6) id list as it goes, and early-exiting once every
id is found — party ids are usually low, so the early exit alone will typically
stop within the first batch. Then have `loadSnapshot()` call it once. Same
one-pass treatment applies to `loadDashboardSnapshot()` (`:248-262`, one id) —
that one is already minimal.

### 3.2 `healPartyOnRead()` repeats the same per-member record scan every 5 minutes

**Priority: medium-high. Cost: low (falls out of 3.1). Impact: medium.**

```
src/pokemon/PokemonService.cpp:794-805
  for (const uint32_t recordId : state.partyRecordIds) {
    ...
    if (!store_.readRecord(recordId, record)) continue;
    ...
    const IvEvEntry ivEv = ensureIvEv(recordId);
```

Same shape as 3.1: up to 6 full-file scans, on the reading checkpoint cadence
(every 5 credited minutes), immediately after `creditMinutes()` has already done
a full `commit()` + read-back verification. `v0.20.1` fixed the *write* side here
(one `upsertEntries()` instead of six `upsertEntry()` calls) but left the read
side untouched.

Fix: reuse 3.1's `readRecords()`. Better still, `creditMinutes()`
(`:1022-1057`) already loaded the state and the leader — pass the already-read
records down rather than re-reading them.

Secondary note: `ensureIvEv()` is called per member per checkpoint. It is an
in-RAM lookup in the common case, but when an entry is genuinely missing it
triggers a full IV/EV file encode + write + read-back verify *inside* the loop,
once per such member. With 3.5/3.6 below that is ~30 KB of transient heap per
member. Consider batching the IV/EV store the same way the battle store was.

### 3.3 PC Box ordering still re-decodes every record once per distinct species

**Priority: high. Cost: medium. Impact: large, and Release makes a big Box the advertised norm.**

Round 2's item 3.1 was only half-addressed. The cheap half shipped —
`appendSpecies()` now stops once the page is full
(`src/pokemon/PokemonStore.cpp:590`) and both outer loops bound on
`written < output.size()` (`:596`, `:605`) — and `v0.19.3`'s batching cut the
syscall count. The algorithm is unchanged:

```
src/pokemon/PokemonStore.cpp:562-621
  // one full pass to build the pcSpecies bitset  (:564-575)
  // then, per species present, a full re-scan from the start (:577-593)
```

For `PcOrder::PokedexNumber`/`Alphabetical` that is still `S × N`
`decodeRecord()` calls (each running `validateRecord()`'s full UTF-8 nickname
validation, `lib/Pokemon/PokemonTypes.cpp:82-87`, `:17-59`) per page of ten
rows — and `buildRows()` calls `readPcPage()` on every redraw of the PC Box
screen, including every scroll step
(`src/activities/pokemon/PokemonActivity.cpp:2528-2537`). At the newly-advertised
512-record Box holding ~100 species that is ~51,000 record decodes per keypress.

`Alphabetical` additionally does an O(151) two-`strcmp` scan per species to find
the next name in order (`:605-612`) — cheap by comparison, not the problem.

**Fix (unchanged from round 2, restated with current line numbers).** One pass
building a `(speciesId, recordId)` index into a heap-allocated scratch array
(6 bytes × N; 3 KB at the 512-record Box cap), sorted in memory, then read only
the ≤10 records the page needs. Allocate with `new (std::nothrow)` following the
`PokemonIvEvStore` precedent (`src/pokemon/PokemonIvEvStore.cpp:63`,
`:96-101`) — never a stack local, given this module's crash history — and fall
back to today's algorithm if the allocation fails. Complementary and much
cheaper: cache the built order on `PokemonActivity` across page turns,
invalidated on any mutation.

### 3.4 The IV/EV write buffer is still sized for the retired 1024-entry format

**Priority: medium. Cost: trivial (one type). Impact: ~7 KB off the peak transient heap of every IV/EV write.**

`IvEvStoreFileBytes` is defined at the *legacy* bound:

```
lib/Pokemon/PokemonIvEvStoreCodec.h:49-54
  constexpr size_t POKEMON_IVEV_LEGACY_FILE_MAX_BYTES = ... 1024 entries ...;   // 14,351 B
  using IvEvStoreFileBytes = std::array<uint8_t, POKEMON_IVEV_LEGACY_FILE_MAX_BYTES>;
```

That is correct and necessary on the **read** side (`inspectSlot()` must be able
to read an oversized file written by an older build so
`decodeIvEvStoreFile()` can clamp it — `src/pokemon/PokemonIvEvStore.cpp:46-63`).
But the same type is reused on the **write** side
(`PokemonIvEvStore.cpp:131-137`), where this build can never emit more than
`POKEMON_IVEV_FILE_MAX_BYTES` (7,179 B). Every single IV/EV persist therefore
heap-allocates 14,351 bytes to use half of it.

Fix: introduce `using IvEvStoreWriteBytes = std::array<uint8_t, POKEMON_IVEV_FILE_MAX_BYTES>;`
and have `encodeIvEvStoreFile()`/`writeState()` take that; leave the legacy-sized
type on the decode path only. Halves the peak allocation on a path that already
has three documented real-device OOM crash fixes behind it.

### 3.5 The IV/EV write-verify still needs a second full 8 KB state

**Priority: medium. Cost: low. Impact: ~8 KB off the same peak.**

Round 2's item 3.4 had two independent bullets. The first (right-size the cap)
shipped in `v0.20.0` — 1024 → 512, halving `IvEvStoreState` from 16 KB to 8 KB.
The second did not:

```
src/pokemon/PokemonIvEvStore.cpp:156-167
  bytes.reset();
  std::unique_ptr<IvEvStoreState> verified(new (std::nothrow) IvEvStoreState());
  ...
  if (!inspectSlot(destinationPath, *verified, verifiedSequence) || ... || !(*verified == state))
```

The read-back verification decodes the whole file into a fresh 8 KB
`IvEvStoreState` and compares with `operator==`. Comparing the re-read *bytes*
against the buffer just written (before freeing it) is exactly as strong a
check, needs no `IvEvStoreState` at all, and removes 8 KB from the peak — plus
the 14 KB read buffer `inspectSlot()` allocates internally, which would also go
away if the comparison is done against a plain re-read.

Combined with 3.4, the peak transient heap of one IV/EV persist drops from
roughly `8 (candidate) + 14 (encode) → 8 (candidate) + 8 (verify) + 14 (inspect)`
to roughly `8 + 7`.

### 3.6 `BatchedRecordReader` heap-allocates 2 KB per call, including for single-record lookups

**Priority: medium-low. Cost: low. Impact: heap-fragmentation hygiene rather than raw throughput.**

`v0.19.3` was a clear net win for full-file passes. But the reader is also used
for point lookups that stop early:

```
src/pokemon/PokemonStore.cpp:469-484   (readRecord: finds one id, then breaks)
src/pokemon/PokemonStore.cpp:501-516   (loadOwnedEvolutionNeeds)
src/pokemon/PokemonStore.cpp:537-553, :564-575, :579-591  (readPcPage, incl. one reader per species)
```

Each constructs a reader whose first `refill()` does
`new (std::nothrow) uint8_t[2016]` (`:74-86`), then frees it on scope exit. For
`readRecord(1)` — the single most frequent call in the module (see 3.1) — that
allocates 2 KB and reads 2,016 bytes from SD to find a record in the first 48.
For the `Alphabetical` PC page it is one 2 KB alloc/free **per species**, inside
a loop, on every redraw.

None of this is wrong, and the buffers are correctly kept off the stack. But it
is a new malloc/free churn pattern on a heap the module has already crashed
against three times, and it partly defeats the point for near-record lookups.

**Options, cheapest first:**

- Give `BatchedRecordReader` a caller-supplied capacity, and pass a small one
  (say 4 records) from `readRecord()` while full-file scanners keep 42.
- Let `readPcPage()` construct **one** reader and `seek()`+reset it per species
  instead of constructing a new one each time.
- A single file-scope `static` buffer shared by all readers — these paths are
  non-reentrant and single-threaded, and the existing comment at `:47-53`
  explicitly considered and rejected this for stack-safety reasons that do not
  apply to a `static`. Cheapest of all, at the cost of 2 KB permanent `.bss`.

### 3.7 The Summary screen still re-reads its record from SD on every redraw

**Priority: low-medium. Cost: trivial (now that `focusedRecord_` exists). Impact: small but free.**

Round 2's item 3.3 listed three call sites to fix; `v0.20.0` fixed the per-row
and per-`logicalCount()` ones but left this one:

```
src/activities/pokemon/PokemonActivity.cpp:2999
  if (service_.readRecord(focusedRecordId_, record) != pokemon::ServiceStatus::Ok) {
```

`renderFocused()`'s Summary branch runs on every frame of that screen and does a
full record scan for data `focusedRecord_` already holds.

This is only safe to switch once **Bug 2.2** is fixed — Summary is currently the
screen that *masks* the stale cache (it is the only reason the evolution-prompts
row displays the correct value after a toggle). Do 2.2's `refreshSnapshot()`
re-population first, then swap this to `focusedRecord_` and the last per-frame
`readRecord()` in the activity is gone.

### Checked and deliberately **not** flagged

- **CRC-32.** Round 2's item 3.7 shipped correctly: one shared nibble-table
  implementation (`lib/Pokemon/PokemonCrc32.cpp:16-40`, 64 bytes of flash) behind
  three thin wrappers. Verified the table is generated from the standard
  `0xEDB88320` polynomial and that two 4-bit steps per byte are mathematically
  identical to the old eight 1-bit steps. Nothing left to do here.
- **`PokemonIvEvStore::state_` at 8 KB resident.** `IvEvEntry` is 14 bytes on
  disk but `sizeof` 16 in memory; the padding is forced by `uint32_t recordId`'s
  alignment and cannot be removed without packing the struct, which is not worth
  it. 512 × 16 = 8 KB is the honest cost of the feature, already halved once.
- **`peekBattleMoves()` / `ensureIvEv()` called per row per frame** (the Party,
  BattleSwitch and battle-HUD dot rows). Both resolve to in-RAM lookups against
  the already-loaded store state; no SD I/O. Not a hot path in the expensive
  sense.
- **`extraItemIdAt()` / `bagItemIdAt()` walking the 90-item table per row.**
  O(90) over a `constexpr` flash table with no I/O, same as round 2's finding for
  the 84-item version. Measurable only in the abstract.
- **`writeSnapshot()`'s post-write `inspectSnapshot()` re-read.** Doubles the I/O
  of every commit, but it is the validate-then-commit discipline this whole
  storage layer is built on and the reason power-loss mid-write is survivable.
  Not a defect.

---

## Summary

| Section | Count |
|---|---|
| Round 2's deferred items still open | 8 of 8 (plus its 2 suspected bugs) |
| Missing features (new, actionable) | 7, plus 4 explicitly ruled out |
| Bugs | 7 confirmed + 1 latent |
| Performance | 7, plus 5 explicitly not flagged |

Highest-priority items across all sections:

1. **Bug 2.1** — releasing any Pokémon other than the newest permanently breaks
   catching, via record-id allocation that still assumes ids are dense. Reachable
   in about two minutes of normal play in the freshly-shipped Release feature,
   with no recovery path a player could find, and no test covering it.
2. **Perf 3.1** — `loadSnapshot()`'s six independent full-file scans per refresh.
   It is the most frequently executed save-path code in the module, it runs after
   every single mutation, and the fix is one new single-pass store method that
   3.2 then reuses for free.
3. **Missing 1.1** — type-based status immunities. ~15 lines for a rule that
   currently lets the player burn Blaine's entire roster and poison Koga's, and
   the one remaining large hole in the status system after `v0.19.2` closed the
   move-type half of it.
