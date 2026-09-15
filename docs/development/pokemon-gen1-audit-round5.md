---
title: Pokémon Module Audit — Round 5
parent: Development
nav_order: 12
---

# Pokémon Module Audit — Round 5 (missing features, bugs, performance)

A fifth full read of the Pokémon module (`lib/Pokemon/*`, `src/pokemon/*`,
`src/activities/pokemon/*`), done at `v0.21.0` (HEAD `a175bd80`), immediately
after the largest single batch of new Gen 1 mechanics and save-layer
performance work this project has ever shipped in one round (round 4's 7
missing-feature items, bug 2.9, and all 8 performance items — see
`TASKS.md` and `CHANGELOG.md`'s `[0.21.0]`/`[0.20.4]`/`[0.20.3]`/`[0.20.2]`
entries). This round's brief: re-verify round 4's own "done" claims by reading
the code (not the changelog), answer two specific open questions left behind
about the run-away formula and move-priority's implementation shape, and hunt
hard for new bugs in the newest, highest-risk code — the battle engine's new
mechanics and the save-layer batching.

**What this round found, in one sentence per section:** every item round 4
marked done really is done correctly (including the two prior open questions,
both now answered as "correct, no changes needed"), except for one genuinely
new and fairly significant bug discovered while specifically auditing the new
`toxicCounter` field's lifecycle — it is silently dropped on every Pokémon
switch because the persisted `BattleRecordEntry` format has no field for it;
no new missing-feature candidates were found beyond what's already tracked
(1.8, deliberately deferred); and one small new performance note was found in
the Alphabetical PC Box sort path, not worth fixing on its own.

---

## Section 1 — Re-verification of round 4's "done" claims

All cross-checked directly against the code at `a175bd80`, not trusted from
`TASKS.md`/`CHANGELOG.md`.

| # | Item | Status | Evidence |
|---|---|---|---|
| 1.1 | Type-based status immunity (Fire/Burn, Poison/Poison+Toxic, Ice/Freeze) | **Confirmed correct** | `typeIsImmuneToAilment()` (`lib/Pokemon/PokemonBattle.cpp:509-515`), gated correctly for both a damaging move's secondary ailment and a pure Status-category move (`:1540-1549`) |
| 1.2 | 3 new multi-hit moves (Double Kick 24, Spike Cannon 131, Bonemerang 155) | **Confirmed correct** | `MULTI_HIT_TABLE` (`PokemonBattle.cpp:131-142`) now has 10 entries; Double Kick/Bonemerang correctly fixed at 2 hits, Spike Cannon correctly rolls via `rollMultiHitCount()` |
| 1.3 | Secondary stat-drop table (Acid/Bubble Beam/Aurora Beam/Psychic/Constrict/Bubble) | **Confirmed correct** | `SECONDARY_STAT_DROP_TABLE` (`:258-265`), applied at `:1195-1205` with the correct guards (immune/fainted/Substitute/Guard Spec. all block it, matching the flinch roll's own guards) |
| 1.4 | Razor Wind as a real 2-turn move | **Confirmed correct** | `TWO_TURN_TABLE` (`:398-405`) lists Razor Wind (`13, false`) alongside Fly/Dig/Solar Beam/Skull Bash/Sky Attack |
| 1.5 | Hyper Beam skips recharge on a KO | **Confirmed correct** | `PokemonBattle.cpp:1270`: `if (moveId == HYPER_BEAM_MOVE_ID && defender.currentHp > 0) attacker.mustRecharge = true;` |
| 1.6 | Jump Kick/Hi Jump Kick crash damage on miss | **Confirmed correct** | `applyCrashDamageIfMissed()` (`:311-316`), called from both the invulnerable-defender miss path (`:967`) and the real accuracy-roll miss path (`:1012`) |
| 1.7 | Teleport (guaranteed wild escape, trainer failure) | **Confirmed correct** | `BattleLogEvent::Teleported` set at `PokemonBattle.cpp:1300`; `PokemonActivity.cpp:932-934`/`:945-947` rewrite it to `MoveFailed` for any gym/E4/Champion battle before display, `:984-988` treats it as a guaranteed pass for a wild battle |
| 0.1 (Move priority) | Quick Attack +1 / Counter −1 | **Confirmed correct** | See Section 2, question 2, below |
| 0.2 (Toxic escalation) | `n × maxHP/16` per turn | **Confirmed correct in isolation** | `applyEndOfTurnStatusDamage()` (`:647-665`) — but see new bug 3.1 below for a real persistence gap in its supporting field |
| 0.3 (Run can fail) | Bulbapedia formula, both UI entry points | **Confirmed correct** | See Section 2, question 1, below |
| 0.4 (Haze full reset) | Status/confusion/Reflect/Light Screen/Mist/Focus Energy | **Confirmed correct** | `clearHazeState()` lambda (`PokemonBattle.cpp:1282-1290`) clears all 6, correctly including `toxicCounter` itself (`:1285`) |
| 0.6 (Speed tie) | Coin flip | **Confirmed correct** | `PokemonBattle.cpp:1829-1837`, correctly only reached when `playerPriority == opponentPriority` |
| 2.9 (mutual-KO UX gap) | Player now told their own Pokémon fainted too | **Confirmed correct** | `finishBattleAfterWildFainted()` (`PokemonActivity.cpp:1241-1279`) captures `playerAlsoFainted` before any state changes (`:1256`) and both the wild (`:1268-1277`) and gym (`advanceGymOpponentOrFinish(playerAlsoFainted)`, `:1258`, `:1318-1329`) tails correctly show the fainted message and force a switch (or end the run) |
| 3.1 (`readRecords()` batching) | `loadSnapshot()` no longer scans once per party member | **Confirmed correct** | `PokemonStore::readRecords()` (`src/pokemon/PokemonStore.cpp:522-549`) is a genuine single pass; `loadSnapshot()` (`src/pokemon/PokemonService.cpp:54-66`) correctly skips the call entirely when `partySlotCount == 0` (empty party) |
| 3.2 (`healPartyOnRead()` read batching) | Same, for the 5-minute heal tick | **Confirmed correct** | `PokemonService.cpp:820-822`; correctly tolerates a slot `readRecords()` couldn't decode via the `record.recordId != recordId` guard at `:833` |
| 3.3 (PC Box single-pass sort) | No more O(species×N) re-scan | **Confirmed correct** | `readPcPage()` (`src/pokemon/PokemonStore.cpp:582-717`) now does exactly 2 full passes regardless of species count (a species/count-gathering pass, then a placement pass using precomputed `speciesStartIndex`) — see new performance note 4.1 below for the one remaining, smaller cost this doesn't address |
| 3.4 (IV/EV write buffer resized) | Sized for 518, not the legacy 1024 | **Confirmed correct** | `IvEvStoreWriteFileBytes` (`lib/Pokemon/PokemonIvEvStoreCodec.h:75`) is `POKEMON_IVEV_FILE_MAX_BYTES`-sized (518-entry) and is what `PokemonIvEvStore.cpp:134` actually allocates for a write |
| 3.5 (IV/EV write-verify via memcmp) | No second full state decode | **Confirmed correct** | `PokemonIvEvStore.cpp:173-176`: reads back into a second `IvEvStoreWriteFileBytes` and compares with `std::memcmp` |
| 3.6 (`BatchedRecordReader` custom chunk size) | Single-record lookups use a 1-record chunk | **Confirmed correct** | `PokemonStore::readRecord()` (`src/pokemon/PokemonStore.cpp:490-520`) constructs `BatchedRecordReader(file, activeHeader_.recordCount, 1)` |
| 3.7 (Summary reads `focusedRecord_`, not SD) | No more per-frame `readRecord()` | **Confirmed correct** | `PokemonActivity.cpp:3135-3139`: `const pokemon::PokemonRecord& record = focusedRecord_;` with a doc comment citing this exact item |
| 3.8 (`refreshSnapshot()` in-memory party search) | No redundant scan on every mutation | **Confirmed correct** | `PokemonActivity.cpp:560-569` linear-searches `snapshot_.party[]` (≤6 comparisons) before falling back to `service_.readRecord()` |

**Edge cases specifically checked for the batching work**, per this round's
brief:

- **Empty party**: `loadSnapshot()` (`PokemonService.cpp:48-49,54`) computes
  `partySlotCount == 0` and skips the `readRecords()` call entirely (an empty
  span would already have been handled correctly by `readRecords()` itself —
  `PokemonStore.cpp:525`, `if (recordIds.empty()) return true;` — but the
  caller doesn't even reach that path). `healPartyOnRead()` always requests
  all `PARTY_SIZE` slots including zero `recordId` entries; `readRecords()`'s
  inner-loop guard `if (recordIds[i] != 0 && ...)` (`PokemonStore.cpp:544`)
  correctly ignores those pseudo-requests without matching them to any real
  record (`recordId == 0` never appears as a real, valid record's id).
- **PC Box with 0 or 1 species present**: `readPcPage()`'s `orderedCount == 0`
  case correctly produces `totalQualifying == 0` and thus `written == 0`
  (`PokemonStore.cpp:714-715`) with no out-of-bounds access; a single species
  present degenerates the alphabetical selection-sort loop
  (`:660-673`) to exactly one iteration, and `speciesStartIndex`/
  `perSpeciesSeen` (both sized `KANTO_SPECIES_COUNT + 1`) are always
  large enough regardless of how many distinct species actually appear.
- **A requested id that doesn't exist**: `readRecords()` never fails outright
  for this — a slot whose `recordId` is never matched during the scan is left
  as `PokemonRecord{}` (recordId 0) forever, and both callers check for that
  explicitly (`loadSnapshot()`'s `allFound` loop, `PokemonService.cpp:58-59`;
  `healPartyOnRead()`'s `record.recordId != recordId` guard, `:833`) rather
  than assuming a hit. `readRecord()`'s own single-id lookup is unchanged in
  this respect and already returns `false` cleanly (`PokemonStore.cpp:519`).

**Conclusion for Section 1: everything round 4 marked "done" is genuinely,
correctly done.** No regressions or incorrect fixes found.

---

## Section 2 — The two open questions from round 4, answered

### Question 1: Is the run-away `+30 × failed-attempt-count` bonus real Gen 1 behavior?

**Yes — confirmed correct, this is Bulbapedia's fully documented Generation I
escape formula, not a simplified stand-in.**

`attemptRun()` (`lib/Pokemon/PokemonBattle.cpp:2014-2031`):

```cpp
const uint32_t f = (static_cast<uint32_t>(playerSpeed) * 32U) / divisor + static_cast<uint32_t>(attemptCount) * 30U;
if (f > 255U) return true;
uint32_t roll = 0;
if (!rollBelow(random, 256U, roll)) return false;
return roll < f;
```

This is exactly Gen 1's real formula: `F = (playerSpeed × 32) / (opponentSpeed
/ 4, floored, min 1) + 30 × (number of prior failed escape attempts this
battle)`; `F > 255` is a guaranteed escape, otherwise a roll in `[0, 256)` must
land below `F`. The `+30` per-failed-attempt term is not an embellishment or
misreading — it is the real games' own mechanic for making repeated escape
attempts progressively more likely to succeed, and this implementation's
`attemptCount` parameter (incremented once per failed attempt at both
`PokemonActivity.cpp:1935` and `:2229`, reset to 0 once per new wild battle at
`:1119`) matches its intended lifetime exactly. **No change needed.**

### Question 2: Is a hand-authored `movePriority(moveId)` function (rather than a `MoveData` field) a reasonable choice?

**Yes — this is the correct, lowest-risk choice, and matches an already
well-established convention in this exact file. No other part of the codebase
assumes a move's full data lives in one place.**

`MoveData` (`lib/Pokemon/PokemonBattleTypes.h:43-53`) is a small, CSV-generated
struct (`moveId`, `name`, `type`, `power`, `accuracy`, `pp`, `category`,
`ailment`, `ailmentChance`) with no room reserved for the handful of Gen-1-only
mechanics PokeAPI's own data doesn't carry a column for. This project's
established pattern for exactly that situation — precedented well before this
round — is a small hand-authored `moveId → X` lookup table or function living
alongside `MoveData` rather than widening the generated struct itself:
`STAT_CHANGE_TABLE` (`PokemonBattle.cpp:70-93`), `RECOIL_TABLE` (`:108-113`),
`MULTI_HIT_TABLE` (`:131-142`), `FIXED_DAMAGE_TABLE` (`:196-207`),
`FLINCH_TABLE` (`:232-239`), `SECONDARY_STAT_DROP_TABLE` (`:258-265`), and
`TWO_TURN_TABLE` (`:398-405`) are all exactly this shape already, each with a
doc comment citing the same rationale ("PokeAPI's own move data doesn't carry
a column for this"). `movePriority()` (`:330-334`) is a one-line version of
the same pattern — arguably simpler than most of its neighbors, since
priority needs no accompanying data (just `-1`/`0`/`+1`).

Checked specifically for anything that would break from this choice: no code
anywhere in `lib/Pokemon/` or `src/pokemon/` iterates `MoveData` fields
generically (e.g. for serialization, a debug dump, or a data-integrity self-
check) in a way that would need to also see priority — every read of
`MoveData` is a named-field access at a specific call site. The scripts that
generate `MoveData` from CSV (`scripts/generate_pokemon_moves.py` and
similar) are consumers only at build time and have no runtime dependency on
priority being present in that struct. **No problem found; no change
recommended.**

---

## Section 3 — New bugs found in the newest code

### 3.1 (new, high value) — Toxic's escalating damage counter is silently lost on every Pokémon switch

**Severity: medium-high (a real, easily-triggered gameplay regression of a
mechanic that shipped this very round). Confirmed by reading, not yet
exercised through the simulator. Cost to fix: low-medium (one new persisted
field, following the exact append-only precedent this project already uses
repeatedly).**

`BattleCombatant::toxicCounter` (`lib/Pokemon/PokemonBattle.h:89`) is the new
field this round's Toxic-escalation feature (round 4 item 0.2, `TASKS.md`
design-call item) depends on entirely — `applyEndOfTurnStatusDamage()`
(`PokemonBattle.cpp:647-665`) only computes the escalating `n × maxHP/16`
damage `if (combatant.status == Ailment::Poison && combatant.toxicCounter >
0)`; otherwise it silently falls back to the flat `STATUS_DAMAGE_FRACTION`
(1/8) any ordinary Poison already used before this round.

But **`BattleRecordEntry`** — the struct this project persists a party
member's live battle state into between turns and across a switch/exit/reload
(`lib/Pokemon/PokemonBattleStoreCodec.h:56-77`, the `pokemon-battle-{a,b}.bin`
file) — has no `toxicCounter` field at all. It carries `status`/`statusTurns`
(both correctly round-tripped) but nothing for the new counter. This is not
an oversight limited to the encode/decode layer; it's a genuine hole in the
struct itself, confirmed by reading every site that moves data between a live
`BattleCombatant` and a persisted `BattleRecordEntry`:

- `PokemonActivity::savePlayerBattleEntry()` (`PokemonActivity.cpp:1150-1186`)
  copies `entry.status = battlePlayer_.status;` and
  `entry.statusTurns = battlePlayer_.statusTurns;` (`:1180-1181`) but has
  nothing to copy `toxicCounter` into, because there is no `entry.toxicCounter`
  to copy it into.
- `PokemonActivity::setupBattlePlayer()` (`:1021-1063`), called both at battle
  start and — critically — **on every single voluntary or forced Switch**
  (`Screen::BattleSwitch`'s handler at `:1960-1961`: `savePlayerBattleEntry();
  if (!setupBattlePlayer(slot)) ...`), does `battlePlayer_ =
  pokemon::BattleCombatant{};` (`:1028`, a full fresh-default reset, so
  `toxicCounter` starts at its default 0) and then repopulates fields from the
  freshly-loaded `entry` — including `battlePlayer_.status = entry.status;`
  (`:1042`) — but again, there is no `entry.toxicCounter` to restore
  `battlePlayer_.toxicCounter` from.

The practical effect: switch a Toxic-poisoned Pokémon out and back in (a
completely ordinary, encouraged action — trainers routinely switch mid-fight
to answer a bad matchup), and its `status` correctly survives as `Poison`, but
`toxicCounter` silently resets to 0. The very next end-of-turn tick then takes
the `else` branch in `applyEndOfTurnStatusDamage()` and applies the flat 1/8
rate instead of continuing the escalation — permanently and silently
downgrading a Toxic'd Pokémon to ordinary Poison for the rest of the battle,
with no way to re-escalate it (Toxic itself can't be reapplied to an already-
Poisoned target — `defender.status == Ailment::None` gate at
`PokemonBattle.cpp:1550`). This defeats the entire point of round 4's Toxic
work for any fight that involves even one switch, which is most fights longer
than a couple of turns.

Also checked and NOT a problem: `faintCombatant()` (`PokemonBattle.cpp:1646-
1651`) and the Haze handler's `clearHazeState()` (`:1282-1290`) both correctly
zero `toxicCounter` in-memory — those are the two in-battle lifecycle events
this round's own author clearly had in mind. The gap is specifically the
save/reload boundary this same round's performance-adjacent switch/save code
already exercises every time, which nothing here treats as a
`toxicCounter`-resetting event on purpose — it's an unintentional side effect
of the field simply not existing in the persisted format.

**Fix.** Add `std::array<uint8_t, 1>` or a plain `uint8_t toxicCounter = 0;`
field to `BattleRecordEntry`, following the exact precedent
`BattleRecordEntry::ppUp` already set for this same struct (see
`PokemonBattleStoreCodec.h:63-74`'s doc comment) and the wider precedent this
project uses everywhere a persisted format needs one more field without
breaking old saves: bump `POKEMON_BATTLE_STORE_VERSION` to 3, add the new
field at the end of the 20-byte v2 layout (making v3 21 bytes), and branch
`decodeBattleStoreFile()` on the header version to default `toxicCounter` to 0
for any v1/v2 file — precisely the way `ppUp` was introduced in v2 over v1.
Then wire it into the 3 read/write sites identified above
(`savePlayerBattleEntry()`, `setupBattlePlayer()`, and the Full
Heal/Antidote-style mid-battle item sync at `PokemonActivity.cpp:1662-1667`,
which also never touches it but is harmless today only because it only
matters when status is being *cured*, not preserved).

### 3.2 — Move-priority/forced-move interactions: no bugs found

Specifically checked, per this round's brief, whether the new
`movePriorityForSlot()` (`PokemonBattle.cpp:340-343`) interacts badly with
Struggle, two-turn moves, trapping, or Disable:

- **Struggle**: `moveSlot >= BATTLE_MOVE_SLOTS` (the sentinel both a real
  forced-Struggle call and `chooseOpponentMoveSlot()`'s "no usable move"
  return use) is checked first in `movePriorityForSlot()` and always yields
  priority 0 — correct, real Gen 1 Struggle has no special priority.
- **Two-turn moves (Fly/Dig/Solar Beam/Razor Wind/Skull Bash/Sky Attack)**:
  both the charge turn and the release turn pass the same real move slot
  through to `stepBattle()`, so `movePriorityForSlot()` reads the correct
  `moveId` (none of the 6 have nonzero priority in `movePriority()` anyway —
  correctly 0 both turns).
- **Trapping moves (Wrap/Bind/Fire Spin/Clamp) and Thrash/Petal Dance**: same
  reasoning — the forced continuation always resolves through the same real
  slot index, so priority is recomputed correctly turn over turn; none of
  these carry nonzero priority in Gen 1 regardless.
- **Disable**: a disabled slot simply can't be chosen at all (checked before
  reaching `stepBattle()`, both for the player at `PokemonActivity.cpp:1945-
  1947` and for the AI inside `chooseOpponentMoveSlot()` at
  `PokemonBattle.cpp:1598`), so there's no path where a disabled Quick
  Attack/Counter's priority could leak into turn-order math for a move that
  was never actually selectable.
- **Hyper Beam's forced recharge**: `resolveAction()` checks
  `attacker.mustRecharge` before anything else and unconditionally returns
  `MustRecharge` (`PokemonBattle.cpp:790-794`) — but this happens *after*
  `stepBattle()` has already decided turn order using that side's
  `movePriorityForSlot()`. Checked whether this could let a recharging
  Pokémon's stale/irrelevant slot selection wrongly grant or deny priority:
  it can't matter either way, since the recharging side does nothing this
  turn regardless of order — the only question priority actually affects
  (who acts first) is moot for a side that isn't really acting. No bug.

**No bugs found in this area — the new priority system composes correctly
with every existing forced-move mechanic.**

### 3.3 — RUN wiring (menu and hardware Back): no bugs found

Both entry points (`Screen::Battle`'s on-screen RUN option,
`PokemonActivity.cpp:1931-1936`, and the hardware Back handler,
`:2225-2230`) call the identical `service_.attemptRunFromBattle(battlePlayer_,
battleOpponent_, battleRunAttempts_)`, share the same `battleRunAttempts_`
counter (incremented identically on failure at both sites), and both correctly
gate the same way on `gymChallengeIndex_ != 0` (an unconditional forfeit,
never routed through the real escape-odds roll) before ever reaching the RUN
logic. A failed attempt at either entry point correctly costs the turn — both
route to `finishItemUseMidBattle(tr(STR_POKEMON_COULDNT_ESCAPE))` rather than
just re-showing the menu, matching the real games' "the wild Pokémon attacks!"
consequence of a failed escape. **No inconsistency between the two entry
points found.**

---

## Section 4 — Missing Gen 1 features

No new candidates found beyond what's already tracked. Specifically
considered and ruled out as not worth adding, given this project's own scope:

- **Sleep Talk-shaped edge cases**: Sleep Talk itself (the move) isn't part of
  this project's move set at all (not in `scripts/data/pokemon-moves.csv`'s
  learnsets for any Gen 1 species that would need it modeled specially), so
  there's no "Sleep Talk while frozen" or similar edge case to find — nothing
  to do here.
- **Other 2-turn/invulnerability move edge cases**: Fly/Dig's semi-invulnerable
  turn is already a documented, deliberate simplification ("everything just
  misses" rather than modeling Swift/Earthquake's real exceptions — see
  `PokemonBattle.cpp:962-969`'s own comment). No new gap found beyond what's
  already flagged as a known simplification.
- **Item-use-in-battle edge cases**: checked the mid-battle item sync path
  (`PokemonActivity.cpp:1643-1675`) specifically for the toxicCounter question
  above; no other new gap found there (PP-restore items, Revive/Max Revive,
  and stat-boost items were already covered by round 2/3's own audits and
  remain correct).
- **Evolution-cancel-via-B-button**: already present, just implemented as a
  persistent per-Pokémon toggle rather than a one-shot B-button prompt — the
  `EvolutionPromptsDisabled` record flag (`PokemonActivity.cpp:1502-1504`)
  gives the player a standing way to say "never evolve this one," which is a
  reasonable and arguably more convenient equivalent for a game with no
  simultaneous button-press input model to hook a "hold B during the
  animation" gesture onto. Not a gap.
- **Trade-evolution species (Kadabra/Machoke/Graveler/Haunter)**: already
  modeled correctly, and already noted as settled in round 2 — these evolve
  via the in-game "Link Cable" item (`scripts/data/pokemon-kanto-v2.csv`
  rows for species 64/67/75/93, `EvolutionTrigger::Item` with
  `EvolutionItem::LinkCable`), a real, ordinarily-acquirable bag item in this
  project (deliberately rendered with no icon — see
  `docs/artwork-setup.md`/`docs/third-party-assets.md`). Not a "these can
  never evolve" gap; this is this project's own considered substitute for a
  trade this single-player game has no other way to offer.
- **Held items**: has no existing partial implementation to extend (no item
  slot exists on `BattleCombatant`/`PokemonRecord` at all) and would be a
  large, net-new feature on the scale of a whole new roadmap item, not a small
  gap — flagged here only so a future audit doesn't have to re-derive that
  this is a genuine "not started" area rather than an oversight, but not
  recommended as this round's pickup given how large a change it would be
  relative to this game's own reading-companion scope.

**Item 1.8 (a Repel equivalent) remains open exactly as round 4 left it,
per the user's explicit 2026-09-15 request not to re-propose it without new
reasoning — not re-litigated here.**

---

## Section 5 — New performance findings

### 5.1 (new, minor) — Alphabetical PC Box sort still pays an `O(S²)` species-ordering cost on every page, independent of round 4's `O(N)` fix

**Priority: low. Cost: low. Impact: small — bounded by `S ≤ 151` regardless of
how large the Box itself grows, and already explicitly called out as an
accepted, unchanged cost in round 4's own item 3.3 writeup.**

Round 4's fix for item 3.3 correctly eliminated the `O(species × N)` full
file re-scan (confirmed in Section 1 above). But the species-ordering step
that builds `orderedSpecies[]` for `PcOrder::Alphabetical`
(`PokemonStore.cpp:659-673`) is still an `O(S²)` selection sort over up to
`KANTO_SPECIES_COUNT` (151) species — for each of up to 151 output slots, it
rescans all 151 candidate species doing a `std::strcmp` against the
previous pick. This is small in absolute terms (at most ~151×151 ≈ 22,800
string compares of very short strings, no I/O involved) and round 4's own
writeup already flagged it as "the exact same cost this already had before
this fix, just no longer paired with a per-species file re-scan" — i.e. a
known, accepted, unchanged cost, not a regression.

Confirmed this round: nothing about it got worse, and it's small enough
relative to real SD I/O costs elsewhere in this same function (the two
`O(N)` file passes just before/after it) that it's very unlikely to be
measurable on a real device. **Not recommended as a priority fix** — noted
only because the task brief asked specifically for new performance
opportunities in the code that changed most recently, and this is the only
one found that wasn't already fully addressed. If it's ever worth revisiting,
the fix would be to build the alphabetically-sorted species list once (it
only depends on `speciesData()`'s static names, not on save contents) and
cache it as a `constexpr`/static table at compile time or first use, rather
than recomputing the sort on every single `Alphabetical`-order page request.

### 5.2 — No new inefficiency found in the batching primitives themselves

`BatchedRecordReader`/`BatchedRecordWriter` (`PokemonStore.cpp:55-148`) were
read in full looking for a case where the new custom-chunk-size constructor
(item 3.6) could accidentally end up allocating a chunk larger than actually
needed, or where a caller might construct a fresh reader per iteration of an
outer loop (which would silently reintroduce the exact per-record heap-churn
problem 3.6 fixed). No such call site found: every multi-record scan
(`inspectSnapshot()`, `writeSnapshot()`'s copy-forward, `readRecords()`,
`readPcPage()`'s two passes) constructs exactly one `BatchedRecordReader` for
the whole scan, and `readRecord()`'s single-record lookup is the only caller
passing a reduced chunk size, exactly as intended.

---

## Summary

| Section | Count |
|---|---|
| Round 4 items re-verified | 18 of 18 confirmed correctly done (7 missing-feature items, 5 of round 2-4's design-call items directly touched this round, bug 2.9, all 8 performance items) — 0 regressions or incorrect fixes found |
| Open questions answered | 2 of 2 — both confirmed correct as implemented, no changes recommended |
| Bugs (new) | 1 new (3.1, medium-high severity — Toxic's escalation counter lost on switch), plus 2 specifically-hunted-for areas (move-priority/forced-move interactions, RUN wiring) checked and confirmed bug-free |
| Missing features (new) | 0 new candidates found; 6 considered and explicitly ruled out or confirmed already covered (Sleep Talk, other invulnerability-move edge cases, item-use edge cases, evolution-cancel, trade evolutions, held items) |
| Performance (new) | 1 minor new note (5.1, PC Box alphabetical sort's `O(S²)` species-ordering step, not recommended as a priority fix); the batching primitives themselves checked clean |

**Highest-priority/highest-value finding overall: new bug 3.1.** Every one of
round 4's 7 new battle mechanics is individually implemented correctly, and
the extensive save-layer performance batching work preserves exact prior
behavior in every edge case checked (empty party, 0/1-species PC Box, missing
record ids). But the Toxic-escalation feature — this same round's own
headline new mechanic — has a real persistence gap: its supporting
`toxicCounter` field was added to the live `BattleCombatant` struct but never
added to the persisted `BattleRecordEntry` format those live values are
saved into and reloaded from on every switch. The result is that Toxic
silently degrades to ordinary flat-rate Poison the moment a player does the
single most common mid-battle action there is — switching. This is worth
fixing before the next release; the fix is small and has a well-established
precedent in this exact file (`ppUp`'s own v1→v2 format bump).

**Build/test verification for this round**: full native suite run clean,
**24/24 Pokémon tests passing** (`ctest -R Pokemon --output-on-failure`,
confirmed 2026-09-15 against the current `test/build`); no code was changed
by this audit (research/doc-only round, per the task's own instructions), so
no firmware build was necessary or attempted.
