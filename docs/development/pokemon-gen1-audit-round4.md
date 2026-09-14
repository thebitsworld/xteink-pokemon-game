---
title: Pokémon Module Audit — Round 4
parent: Development
nav_order: 11
---

# Pokémon Module Audit — Round 4 (missing features, bugs, performance)

A fourth full read of the Pokémon module (`lib/Pokemon/*`, `src/pokemon/*`,
`src/activities/pokemon/*`, `src/components/pokemon/*`), done at `v0.20.3`.

**Relationship to the three earlier audits.**

- [pokemon-gen1-authenticity-roadmap.md](pokemon-gen1-authenticity-roadmap.md)
  is audit #1 (battle-engine Gen 1 fidelity only). Everything on it shipped.
- [pokemon-gen1-audit-round2.md](pokemon-gen1-audit-round2.md) is audit #2: 21
  items across missing features/bugs/performance. Its bugs and performance
  items shipped; its Section 1 (8 "deferred design-call" items) did not, and
  was reconfirmed still open by round 3.
- [pokemon-gen1-audit-round3.md](pokemon-gen1-audit-round3.md) is audit #3: 7
  new missing features, 8 bugs, 7 new performance items. `CHANGELOG.md`'s
  `[0.20.2]` and `[0.20.3]` entries claim **all 8 of round 3's bugs are now
  fixed**. This round's first job was to verify that claim line-by-line rather
  than trust it, then re-check everything neither round ever touched.

**What this round found, in one sentence per section:** round 2's 8 deferred
items are still all fully untouched; round 3's 7 missing features and 7
performance items are also still all untouched (only its bugs were fixed);
all 8 of round 3's bugs really were fixed correctly, though verifying them
turned up one new small bug in the 2.4 fix (2.9, a UX gap) and one
duplicated-invariant risk worth flagging in the 2.6 fix, plus a real
performance side-effect of the 2.2 fix (3.8, not a correctness issue); and
this round adds one new missing-feature candidate (a Repel equivalent, 1.8).

Four sections follow, numbered to slot directly after round 3's own
structure: **Section 0** (status of round 2's deferred items, restated a
third time), **Section 1** (missing features — round 3's 7, all still open,
plus one new candidate), **Section 2** (bugs — verification of round 3's 8
fixes), **Section 3** (performance — round 3's 7 items, all still open except
one now half-resolved as an accidental side effect, plus one new item).

---

## Section 0 — Status of round 2's 8 deferred design-call items

Re-verified against the actual code at `v0.20.3` (not the changelog — none of
these appear in it). **All 8 are still exactly as open as round 3 found them.**
Nothing in the `v0.20.1`–`v0.20.3` bugfix run touched move ordering, Toxic,
running, Haze, status-damage fractions, Speed ties, Freeze, Counter, Substitute
damage routing, or the Sleep-wake turn cost — every one of those systems is
untouched by any commit in this range. This is the third time these are being
flagged; they are real, well-scoped, and simply haven't been picked up yet.

| # | Item | Status at `v0.20.3` | Evidence |
|---|---|---|---|
| 0.1 | Move priority (Quick Attack +1, Counter −1) | **Open** | `MoveData` (`lib/Pokemon/PokemonBattleTypes.h:44-53`) still has no `priority` field; `stepBattle()` orders purely by Speed: `const bool playerFirst = playerSpeed >= opponentSpeed;` (`lib/Pokemon/PokemonBattle.cpp:1632`) |
| 0.2 | Toxic's escalating damage | **Open** | No `toxicCounter` field anywhere in `BattleCombatant`. Toxic still sets `Ailment::Poison` via the shared ailment block (`PokemonBattle.cpp:1349-1361`) and ticks the same flat `STATUS_DAMAGE_FRACTION` as any other poison |
| 0.3 | Running from a wild battle can't fail | **Open** | No `attemptRun()`/`battleRunAttempts_` anywhere. RUN is still unconditional: `resolveBattleAsPass(); // RUN - always the last option either way` (`src/activities/pokemon/PokemonActivity.cpp:1834`), same on the hardware-Back path (`:2117`) |
| 0.4 | Haze only resets stat stages | **Open** | `PokemonBattle.cpp:1107-1109`: `if (moveId == HAZE_MOVE_ID) { resetBattleStages(attacker); resetBattleStages(defender); ... }` — still no clearing of status, confusion, Reflect/Light Screen/Mist/Focus Energy |
| 0.5 | Status/Leech Seed drain 1/8 (Gen 1 uses 1/16) | **Open** | `constexpr uint8_t STATUS_DAMAGE_FRACTION = 8;` (`PokemonBattle.cpp:15`), still shared by `applyEndOfTurnStatusDamage()` (`:522-525`) and `applyLeechSeedDamage()` (`:534-537`) |
| 0.6 | Speed ties always go to the player | **Open** | `PokemonBattle.cpp:1632`: `playerSpeed >= opponentSpeed` — an exact tie still resolves to the player deterministically, no coin flip |
| 0.7 | Freeze thaws on its own (20%), Gen 1 is permanent | **Open** | `constexpr uint8_t FREEZE_THAW_CHANCE_PERCENT = 20;` (`PokemonBattle.cpp:14`), used at `:478`; still no comment documenting the divergence, as round 3 also already flagged |
| 0.8 | Counter reflects any physical move (Gen 1: only Normal/Fighting) | **Open** | No `lastPhysicalDamageType` field exists. `PokemonBattle.cpp:927-939` (`FixedDamageKind::Counter`) still reflects `attacker.lastPhysicalDamageTaken * 2` unconditionally on type |

Round 2/3's two **suspected** bugs are also still open, unchanged:

- Damage absorbed by a Substitute still feeds Counter/Bide/Rage —
  `applyDamageRespectingSubstitute()` (`PokemonBattle.cpp:407-413`) reduces
  only `substituteHp` and returns early, but both call sites still pass the
  same raw damage value on to `raiseAttackIfEnraged()` (`:945-946`, `:990-991`),
  `bideDamageStored` accumulation (`:997-999`), and `lastPhysicalDamageTaken`
  (`:948`, `:1011`) regardless of whether the hit actually reached the real
  Pokémon.
- Waking from Sleep still costs no turn — `statusPreventsAction()`
  (`PokemonBattle.cpp:466-472`) returns `false` (does not prevent the action)
  the instant `statusTurns` reaches 0, letting the same turn's move fire
  immediately after waking.

---

## Section 1 — Missing Gen 1 features

### Round 3's 7 items: all still open, unchanged

Verified against current code; no commit since round 3 touches any of these.
Full analysis and recommended fixes are in round 3's own Section 1 — not
repeated here, only the current-state citation:

| # | Item | Status | Evidence |
|---|---|---|---|
| 1.1 | Fire/Poison/Ice type-based status immunity (burn/poison/freeze) | **Open** | Ailment block (`lib/Pokemon/PokemonBattle.cpp:1339-1352`) still only checks move-type effectiveness and the generic status-free/HP/Substitute/chance gates — no `typeIsImmuneToAilment()`-shaped check exists |
| 1.2 | 3 missing multi-hit moves (Double Kick 24, Spike Cannon 131, Bonemerang 155) | **Open** | `MULTI_HIT_TABLE` (`PokemonBattle.cpp:131-139`) still lists exactly 7 entries, none of the three |
| 1.3 | No damaging move has a secondary stat-drop (Acid, Bubble Beam, Aurora Beam, Psychic, Constrict, Bubble) | **Open** | `statChangeForMove()`/`STAT_CHANGE_TABLE` still only covers the 22 status-category moves; no secondary-stat-drop table exists anywhere |
| 1.4 | Razor Wind (13) not a two-turn move | **Open** | `TWO_TURN_TABLE` (`PokemonBattle.cpp:302-308`) still lists only Fly, Dig, Solar Beam, Skull Bash, Sky Attack |
| 1.5 | Hyper Beam forces recharge even when it faints the target | **Open** | `PokemonBattle.cpp:1105`: `if (moveId == HYPER_BEAM_MOVE_ID) attacker.mustRecharge = true;` — still unconditional, same justifying comment as round 3 quoted |
| 1.6 | Jump Kick / Hi Jump Kick deal no crash damage on a miss | **Open** | No dedicated miss-handling branch for move ids 26/136 anywhere in `PokemonBattle.cpp`; both fall through the shared `MoveMissed` returns |
| 1.7 | Teleport does nothing in a wild battle | **Open** | No reference to Teleport (move id 100) anywhere in `PokemonBattle.cpp` or `PokemonActivity.cpp` — still falls through to the generic "nothing happened" catch-all |

### New finding 1.8 — No Repel equivalent to suppress wild encounters

**Priority: medium. Cost: low-medium. Impact: medium — a real, frequently-felt Gen 1 mechanic with no analogue in this game's item system at all.**

The encounter system (`lib/Pokemon/PokemonGame.cpp`, the hourly/pity-gated
`processEncounterCheck()`/`processHourlyEncounter()` path feeding
`enqueuePendingEvent()`) is purely time- and book-progress-driven, with no
item, flag, or counter that can ever suppress it. `ItemCategory`
(`lib/Pokemon/PokemonBattleTypes.h`) has no Repel-shaped category, and
`PokemonState` (`lib/Pokemon/PokemonTypes.h`) has no field resembling a
"suppress encounters for N minutes" counter.

Gen 1's Repel/Super Repel/Max Repel is one of the most-used items in the real
games specifically because it lets the player control *when* they want a
battle. This project's own reading-time-driven encounter loop is the closest
thing this game has to Gen 1's "walking through grass," and it currently gives
the player no way to say "not right now" — e.g. right before starting a gym
run, or while trying to bank reading time toward a stone/TM drop without
fielding another fight.

**How to implement.** Add a `repelReadingMinutesRemaining`-style field to
`PokemonState`, using the same append-only pattern `ppUpCount` already
established (a new field after the current last one, bumping the state
version — see `docs/development/pokemon-gen1-authenticity-roadmap.md`'s PP Up
section for the exact precedent). Add one new bag item id/`ItemCategory`
value; using it sets the counter (e.g. 100/200/... credited minutes, mirroring
the real games' step-based durations translated into this game's
minutes-instead-of-steps currency). Gate the top of
`processEncounterCheck()`/wherever the wild-encounter roll actually happens on
`repelReadingMinutesRemaining == 0`, decrementing it alongside whatever
already tracks credited reading minutes. Drop-source integration can reuse the
existing Medicine-drop-track weighting the same way PP Up's item id was
folded in (see `v0.11.1`, referenced in `CLAUDE.md`'s memory notes).

**Considered and explicitly not worth doing (new to this round):** a
Pokédex-completion banner/reward for owning all 151 species. Checked and
**this already exists** — `mewIsReady()` (`lib/Pokemon/PokemonGame.cpp:230-235`)
already implements exactly the real Gen 1 "catch 'em all" mechanic: Mew (151)
only becomes a legendary-encounter candidate once every one of species 1-150
is marked caught. There is no gap here; an earlier draft of this finding
mistakenly flagged one before this function was found — recorded here so a
future audit doesn't rediscover the same false lead.

---

## Section 2 — Bug-fix verification (round 3's 8 bugs)

All 8 read directly from the current code, not trusted from the changelog.
**All 8 are correctly fixed.** Verifying them turned up two things worth
flagging: a genuinely new small bug in the shape of the 2.4 fix (written up
as new item 2.9 below, in the same section since it's a correctness/UX issue
rather than a performance one), and a duplicated-invariant note on the 2.6
fix (not a regression today, but a latent risk — see inline). One of the
fixes (2.2) also has a real performance side-effect that is not itself a
correctness bug — that one is written up as new performance item 3.8 in
Section 3 instead.

- **2.1 (critical — record-id reuse after Release broke catching forever).**
  Correctly fixed. `PokemonStore` gained `uint32_t highestRecordId_` (declared
  `src/pokemon/PokemonStore.h:48`) with a doc comment explaining exactly the
  bug it replaces (`:33`), exposed as `nextRecordId() { return
  highestRecordId_ + 1U; }` (`:36`), and used at the one call site that used
  to compute `recordCount() + 1` (`src/pokemon/PokemonService.cpp:301`).
  `highestRecordId_` is set from `inspectSnapshot()`'s existing single-pass
  walk (no new scan added) in both `begin()`
  (`src/pokemon/PokemonStore.cpp:328`) and `writeSnapshot()`'s post-write
  verification (`:473`), exactly as round 3 recommended. Verified the
  append-time invariant (`mutation.record.recordId > lastRecordId`,
  `:449-450`) is still satisfied by a `nextRecordId()`-issued id. No overflow
  or double-counting risk: `highestRecordId_` is recomputed from the file's
  actual contents on every `begin()`/commit, so it self-corrects even if it
  were ever wrong on a previous run.
- **2.2 (evolution-prompts toggle couldn't be turned back on; stale
  `focusedRecord_`).** Correctly fixed. `refreshSnapshot()`
  (`src/activities/pokemon/PokemonActivity.cpp:523-541`) now re-reads
  `focusedRecordId_` into `focusedRecord_` after every `loadSnapshot()`
  (`:539`), with a doc comment citing this exact round-3 bug and explaining
  the ignore-failure-on-purpose behavior. This fixes every present and future
  staleness bug of this shape at once, as round 3's "smallest and most robust"
  option recommended. See performance item 3.8 for the one real cost this
  introduces.
- **2.3 (IV/EV cap 512 vs. true live-record ceiling 518).** Correctly fixed.
  `POKEMON_IVEV_MAX_ENTRIES = 518` (`lib/Pokemon/PokemonIvEvStoreCodec.h:27`),
  with an extensive doc comment deriving the 512+6 arithmetic explicitly and
  citing round 3's bug number. The legacy decode path
  (`POKEMON_IVEV_LEGACY_MAX_ENTRIES = 1024`, `:34`) is untouched, so old
  larger files still decode correctly — verified the two constants remain
  properly separated (`POKEMON_IVEV_FILE_MAX_BYTES` uses the new 518 cap,
  `POKEMON_IVEV_LEGACY_FILE_MAX_BYTES` keeps the old 1024 cap, `:44-58`). No
  regression: raising the live cap doesn't touch the legacy clamping logic at
  all.
- **2.4 (simultaneous KO from the player's own move scored as a loss).**
  Correctly fixed, and correctly symmetric. The mid-turn resolution in
  `stepBattle()` (`lib/Pokemon/PokemonBattle.cpp:1655-1678`) now checks for a
  mutual KO immediately after *whichever side just acted* and scores it in
  that side's favor — `PlayerWon` when the player's action caused it
  (`:1655-1659`, with a doc comment explicitly citing "real Gen 1 resolves a
  mutual KO in favor of whoever's attack caused it" and this audit's bug
  number), `OpponentWon` when the opponent's action caused it (`:1674`,
  `:1687`) — while `finishTurn()`'s genuine end-of-turn tie (a shared
  status/Leech Seed tick nobody "caused") is left as `OpponentWon` unchanged
  (`:1463-1464`), matching round 3's recommendation exactly. One very minor,
  purely cosmetic gap noted while verifying: in the branch where the *second*
  actor's move causes the mutual KO, only the side whose HP the check already
  expected (`player.currentHp == 0`) gets `faintCombatant()` called on it —
  the other side's `status`/`statusTurns` aren't explicitly cleared in that
  exact sub-path (e.g. `:1672` inside the `playerFirst` branch, opponent's
  second action). This has no observable effect since the battle ends and
  both `BattleCombatant`s are discarded either way — not a real bug, noted
  only for completeness. **A second, more real gap surfaced while verifying
  this fix — see new item 2.9 below.**
- **2.5 (mid-battle boost item applied before consumption confirmed).**
  Correctly fixed by exactly the split round 3 suggested. A new read-only
  `battleBoostItemWouldApply()` (`lib/Pokemon/PokemonBattle.h:380`,
  implemented `PokemonBattle.cpp:1541-1558`) checks applicability without
  mutating state; the call site
  (`src/activities/pokemon/PokemonActivity.cpp:1911-1919`) now checks
  applicability, then calls `consumeBagItem()`, and only calls the mutating
  `applyBattleBoostItem()` after that succeeds — with a comment citing this
  exact bug. No new "item consumed but effect not applied" gap was
  introduced: `battleBoostItemWouldApply()` and `applyBattleBoostItem()`'s
  switch statements were checked side-by-side and cover the identical set of
  item ids with identical gating conditions (stage `< 6` / `!...Active`), so
  a pass on the read-only check guarantees the later mutating call also
  succeeds.
- **2.6 (Ball throw + XP awarded before the Box-full check).** Correctly
  fixed, but via an earlier duplicate gate rather than by reordering the
  original site — worth knowing before assuming the underlying ordering flaw
  itself was restructured. `src/activities/pokemon/PokemonActivity.cpp:1806-1811`
  now gates entry into `Screen::BattleBalls` itself on `partyCount ==
  PARTY_SIZE && ownedCount - partyCount >= PC_BOX_MAX_RECORDS`, with a comment
  citing this bug and noting it mirrors `resolveEncounter()`'s own gate. That
  correctly stops the originally-reported case. But the internal
  `Screen::BattleBalls` handler itself (`:1965-1993`) still runs
  ball-consume → `awardBattleXp()` (`:1983-1986`) → `resolveEncounter(Catch)`
  → the `ServiceStatus::BoxFull` check (`:1990-1993`) in the exact same order
  as before this fix — the underlying flaw was pre-empted by an
  independently-computed second gate, not removed. The two gates read
  different data (`snapshot_.ownedCount`/`partyCount`, cached at menu-select
  time, vs. `resolveEncounter()`'s own live `store_.recordCount()`/state), so
  if a future change ever alters one formula without the other, or
  `snapshot_` goes stale between the two checks, the original "ball spent +
  XP awarded, then Box full" sequence reappears with nothing currently
  testing the internal ordering directly. Not a regression today, but worth
  flagging so a future refactor of either gate doesn't quietly reopen this.
- **2.7 (`upsertEntries()` discarded the whole batch on one invalid entry).**
  Correctly fixed. `PokemonBattleStore::upsertEntries()`
  (`src/pokemon/PokemonBattleStore.cpp:172-186`) now loops every entry,
  tracks `anyApplied`, and only returns `false` if nothing at all could be
  applied — with a comment citing this bug and explicitly naming
  `healPartyOnRead()` as the regression it fixes. This is exactly the
  skip-and-continue semantics round 3 recommended; it does not mask
  systematic corruption silently, since `anyApplied == false` (i.e. every
  single entry was invalid) still correctly reports failure.
- **2.8 (latent — `CollectionActionSet` unchecked 6-slot append).**
  Correctly fixed. The local `append` lambda in `collectionActions()`
  (`lib/Pokemon/PokemonGame.cpp:807-810`) now checks `if (actions.count >=
  actions.items.size()) return;` before writing, with a comment citing this
  bug and explaining it's defense-in-depth for the next action added to this
  function. Correct and minimal, exactly the one-line fix round 3 proposed.

### New finding 2.9 — A self-caused mutual-KO "win" never tells the player their own Pokémon fainted too

**Severity: low-medium (UX gap, no data loss or exploit). Confirmed by reading. Cost to fix: low.**

Bug 2.4's fix (verified above) correctly awards `PlayerWon` when the player's
own Explosion/Self-Destruct/recoil move faints both Pokémon simultaneously.
But `PokemonActivity::resolveBattlePlayerMoveTurn()` routes every `PlayerWon`
outcome straight into `finishBattleAfterWildFainted()` unconditionally, and
that function never checks whether `battlePlayer_.currentHp` is also 0 in
this particular case. The result: the player is shown the normal victory flow
(XP award, catch/gym-progress handling) with no acknowledgement that their
own active Pokémon also just fainted — no "Pokémon fainted!" message, and no
forced-switch prompt the way an ordinary loss (`Screen::BattleSwitch`) would
show.

Nothing is corrupted or exploitable: `savePlayerBattleEntry()` persists the
0 HP correctly before the outcome branch runs, and the next battle's
usable-party-slot logic already correctly skips a Pokémon left at 0 HP from
this path. But the player has no in-the-moment signal that the Pokémon they
just won with is now fainted and needs healing/switching before it can be
sent out again — easy to discover by surprise on the very next battle.

**Fix.** In the `PlayerWon` branch that can be reached via a mutual KO
(`resolveBattlePlayerMoveTurn()`'s handling of `stepBattle()`'s result),
check `battlePlayer_.currentHp == 0` alongside the win and surface the same
"fainted" messaging an ordinary loss already shows, before proceeding to the
usual victory handling — the win outcome itself doesn't need to change, only
the player-facing acknowledgement that their own Pokémon didn't survive the
exchange.

---

## Section 3 — Performance and efficiency

### Round 3's 7 items: all still open

None of `v0.20.1`-`v0.20.3`'s bug fixes touch any of these — confirmed by
reading the current code. Full analysis and recommended fixes remain in round
3's own Section 3; only the current-state citation is restated here.

| # | Item | Status | Evidence |
|---|---|---|---|
| 3.1 | `loadSnapshot()` opens/scans the whole save once per party member (6 independent linear scans) | **Open** | `src/pokemon/PokemonService.cpp:48-55` still loops `PARTY_SIZE` calling `store_.readRecord()` per slot; `PokemonStore::readRecord()` (`src/pokemon/PokemonStore.cpp:478-500`) still opens the file fresh and scans forward per call. No `readRecords()` multi-lookup exists |
| 3.2 | `healPartyOnRead()` repeats the same per-member scan every 5 minutes | **Open (read side); write side already fixed as part of 2.7)** | `src/pokemon/PokemonService.cpp:799-807` still calls `store_.readRecord(recordId, record)` once per party member. The *write* side of this same function was correctly batched into one `upsertEntries()` call (verified under bug 2.7 above) — only the read-side scan itself remains unaddressed |
| 3.3 | PC Box ordering re-decodes every record once per distinct species | **Open** | `src/pokemon/PokemonStore.cpp`'s `readPcPage()`/`appendSpecies()` unchanged: one pass to build the species bitset, then a full re-scan from the start per species present. Still `O(S×N)` decodes per page redraw |
| 3.4 | IV/EV write buffer still sized for the retired 1024-entry format | **Open** | `IvEvStoreFileBytes` (`lib/Pokemon/PokemonIvEvStoreCodec.h:54`) is still `POKEMON_IVEV_LEGACY_FILE_MAX_BYTES`-sized and is still the type used on the write path (`src/pokemon/PokemonIvEvStore.cpp:131`) — the entry-cap change from 2.3 (512→518) didn't touch this waste at all, it's a fully separate axis |
| 3.5 | IV/EV write-verify still needs a second full 8 KB `IvEvStoreState` | **Open** | `src/pokemon/PokemonIvEvStore.cpp`'s write-verify path still heap-allocates a fresh `IvEvStoreState` and compares via `operator==` rather than a plain re-read byte comparison |
| 3.6 | `BatchedRecordReader` heap-allocates ~2 KB per call, including single-record lookups | **Open** | `BatchedRecordReader` (`src/pokemon/PokemonStore.cpp:54-90`) still hard-codes a fixed buffer capacity with no caller-supplied size; still constructed fresh per `readRecord()` call and per species inside `readPcPage()`'s loop |
| 3.7 | Summary screen re-reads its record from SD on every redraw | **Partially fixed — blocker resolved, fix itself not applied** | The bug-2.2 fix (`refreshSnapshot()` now keeping `focusedRecord_` fresh, see Section 2 above) removes the exact staleness concern round 3 cited as the reason this couldn't safely be done yet. But the Summary render path itself (`src/activities/pokemon/PokemonActivity.cpp:~3022-3028`) still independently calls `service_.readRecord(focusedRecordId_, record)` on every frame instead of reading the now-reliable `focusedRecord_`. This is now a one-line swap away from being fixed — flagged so whoever picks it up next doesn't have to re-derive that the blocker is gone |

### New finding 3.8 — The bug-2.2 fix adds a redundant full-file scan on almost every mutation

**Priority: medium. Cost: low (a ≤6-entry in-memory search before falling back to I/O). Impact: real but modest — one extra `readRecord()` call added to a call path that already does 6, on ~30 call sites.**

`refreshSnapshot()`'s fix for bug 2.2 (`src/activities/pokemon/
PokemonActivity.cpp:523-541`) is correct and necessary — see Section 2 above —
but it has a real performance cost that wasn't there before this round's bug
fixes: it does exactly one more independent `readRecord()` linear scan
(`:539`) immediately after `service_.loadSnapshot(snapshot_)` (`:526`) has
*already* read every party member's full record into `snapshot_.party[]`
(`src/pokemon/PokemonService.cpp:48-55`, this is round 3's still-open item
3.1). Whenever the currently-focused Pokémon is a party member — the dominant
case, since Moveset/TM-teach/PP-Up/Evolution-toggle/Rename are all reached via
Party → select → Actions — this second call redundantly re-decodes a record
`loadSnapshot()` already produced and is sitting unused in `snapshot_.party[]`
a few lines later.

`refreshSnapshot()` is called from roughly 30 sites across
`PokemonActivity.cpp` (every catch, ball throw, item use, deposit/withdraw,
release, move learn/forget, PP Up, evolution, gym win, rename, ...), so this
is one additional full linear file scan on very close to every mutation in
the module — compounding round 3's still-open item 3.1 (which already
described `loadSnapshot()` itself as "the single hottest save-path call in the
module") rather than being a free fix.

**Fix.** In `refreshSnapshot()`, before falling back to `service_.readRecord()`
for `focusedRecordId_`, linear-search `snapshot_.party[0..snapshot_.partyCount)`
(≤6 in-memory struct comparisons, no I/O) for a record whose `recordId ==
focusedRecordId_`, and only call `service_.readRecord()` if it isn't found
there — i.e. only when the focused Pokémon is actually in the PC Box, not the
party. This keeps the bug-2.2 fix fully intact (Box-focused records still get
a real, fresh read) while eliminating the SD scan for the common
party-focused case. This is also exactly the kind of fix that gets easier,
not harder, once round 3's item 3.1 (`readRecords()`, a single-pass multi-id
lookup) ships — the two fixes are complementary, and whoever eventually
implements 3.1 should make sure `refreshSnapshot()`'s `focusedRecord_` refresh
is updated to use it (or the in-memory `snapshot_.party[]` search suggested
here) rather than left calling the old single-id `readRecord()`.

---

## Summary

| Section | Count |
|---|---|
| Round 2's deferred items still open | 8 of 8 (plus its 2 suspected bugs), unchanged for the third audit in a row |
| Missing features (round 3's, still open) | 7 of 7, plus 1 new candidate (Repel equivalent), plus 1 candidate considered and ruled out (Pokédex-completion reward — already implemented via `mewIsReady()`) |
| Bugs (round 3's 8, verified) | 8 of 8 correctly fixed; plus 1 new bug found while verifying (2.9, a UX gap in the 2.4 fix) and 1 note on a duplicated-invariant risk in the 2.6 fix |
| Performance (round 3's 7, still open) | 6 of 7 fully open, 1 (item 3.7) half-resolved as an incidental side effect of the bug-2.2 fix, plus 1 new item (3.8, itself a side effect of the same bug-2.2 fix) |

Highest-priority items across all sections:

1. **All 8 of round 3's bugs are genuinely, correctly fixed**, with careful
   attention to not reintroducing any of the original failure modes (id-reuse
   edge cases, cache staleness, cap arithmetic, mutual-KO symmetry, item
   consumption ordering, box-full gating, batch-write partial failure, and
   bounds-checking) — this is the single most important confirmation this
   round makes, since it's what the previous two rounds' entire bug sections
   were building toward.
2. **New finding 3.8** is the most actionable new item: the very fix that
   closed bug 2.2 quietly added one more full linear SD scan to the module's
   single most frequently executed mutation-completion path, on top of round
   3's still-unfixed item 3.1. The fix is small (an in-memory party-array
   search before falling back to I/O) and directly complements 3.1's own
   proposed `readRecords()` — whoever eventually implements 3.1 should land
   3.8's fix in the same change.
3. **New finding 1.8** (a Repel equivalent) is the most substantive genuinely
   new missing-feature candidate found by this round — three consecutive
   audits have now covered the battle engine and the save/UI layer in real
   depth, and this is the first item found that isn't a battle-mechanic
   nuance but a real gap in the *item system's* Gen 1 fidelity: the player
   currently has no way to say "not right now" to a wild encounter, unlike
   in the real games.
