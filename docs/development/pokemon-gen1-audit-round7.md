---
title: Pokémon Module Audit — Round 7
parent: Development
nav_order: 14
---

# Pokémon Module Audit — Round 7 (major bugs + genuinely useful features)

Done at `v0.21.1` on `main` (HEAD includes `ad99d85d`, "docs: add Pokemon module
audit round 6"). This round's brief is deliberately narrower than rounds 2-6:
only major, real, player-visible bugs, and genuinely useful new features for
*this* game specifically (reading-time-driven pacing), not more Gen 1
completism for its own sake.

Read in full for this round, beyond what rounds 2-6 already covered in depth:
`lib/Pokemon/PokemonGame.cpp` (the reading-time/save-mutation core loop -
previously the least-scrutinized file relative to the battle engine),
`src/pokemon/PokemonService.cpp`, `docs/development/pokemon-mechanics.md`, and
targeted re-reads of `PokemonBattle.cpp`/`PokemonActivity.cpp`.

---

## 1. Major bugs

### 1.1 (new) — Using an evolution stone/Link Cable is blocked by *any* unrelated pending event, with a misleading "no effect" message

**Severity: medium-high (real, reproducible, confusing false-negative on a
core-loop action). Confidence: high.**

`pokemon::useEvolutionItem()` (`lib/Pokemon/PokemonGame.cpp:777-780`):
```cpp
bool useEvolutionItem(PokemonState& state, PokemonRecord& record, const EvolutionItem item, RecordMutation& mutation) {
  if (!validateState(state) || !validateRecord(record) || pendingEventFront(state) != nullptr ||
      item < EvolutionItem::MoonStone || item > EvolutionItem::LinkCable || mutation.kind != RecordMutationKind::None) {
    return false;
  }
  ...
```
This rejects the evolution attempt whenever `state.pendingEvents` is
non-empty **at all** - not just when the front event concerns the Pokémon
being evolved, or the item in question. A player who has, say, a completely
unrelated "You found an Ultra Ball!" or "A wild Pidgey appeared!" notification
sitting in the 3-slot pending queue (queued from ordinary reading, unrelated
to the evolution attempt) will have a perfectly valid stone-on-matching-
species evolution attempt fail outright.

The UI (`PokemonActivity.cpp:1725-1734`) can't tell the two failure reasons
apart either - `ServiceStatus::NotApplicable` from a blocked-by-queue failure
renders the exact same generic `STR_POKEMON_NOT_APPLICABLE` ("This won't have
any effect") as a genuinely wrong item/species combination:
```cpp
const auto status = service_.useEvolutionItem(recordId, selectedItem_);
if (status == pokemon::ServiceStatus::NotApplicable)
  showMessage(tr(STR_POKEMON_NOT_APPLICABLE), bagScreen);
```
So the player sees "no effect" on a Fire Stone against their Eevee - which
should always work - with zero indication that the real blocker is an
unrelated item/encounter notification waiting elsewhere, and no hint to go
clear the Pokémon menu's `!` first.

**Why this looks like an oversight, not a deliberate design call:**
- No comment anywhere near this check explains the reasoning (unlike every
  other deliberate divergence in this codebase - Toxic's fraction, Freeze's
  thaw chance, Counter's type-agnosticism - which are always documented as a
  considered trade-off, per this project's own established convention).
- The sibling code path for level-based evolution, `resolveEvolution()`
  (`PokemonGame.cpp:748-775`), does the analogous "backfill moves after
  evolving" work (`queueMoveLearnIfNeeded()`) without requiring the whole
  queue to be empty - it only requires that *this specific* Evolution event is
  the one at the front (`front->recordId == record.recordId`), which is the
  narrow, correct scope. `useEvolutionItem()` requires the *entire* queue
  empty for no apparent structural reason - `queueMoveLearnIfNeeded()`
  (`PokemonService.cpp:879-919`) already tolerates a full queue gracefully
  (`enqueuePendingEvent(state, event); // best-effort: a full queue just skips
  this one`), and Rare Candy's use path (`useConsumable()`,
  `PokemonService.cpp:546`) calls the very same helper with no such
  precondition at all.
- No test in `test/pokemon_game/PokemonGameTest.cpp` or
  `test/pokemon_service/PokemonServiceTest.cpp` exercises `useEvolutionItem`
  with a non-empty, unrelated pending queue - this interaction was never
  actually verified either way, just accidentally encoded once and left
  untouched.

**Player-visible effect**: an active reader who is regularly earning items/
encounters (i.e., the target audience of this whole game) is *exactly* the
player most likely to have a queued notification sitting around at any given
moment - meaning stone evolution silently fails unpredictably, in a way that
looks like a real bug in the stone/species matching rather than an unrelated
scheduling conflict. This is the kind of thing a real player would file a bug
report about, not shrug off as an authenticity nitpick.

**Suggested fix shape** (not implemented, per this round's brief): narrow the
guard to only reject when the front pending event actually targets this
`record.recordId`, mirroring `resolveEvolution()`'s own scoping - or drop the
guard's queue-emptiness requirement entirely and rely on
`queueMoveLearnIfNeeded()`'s already-safe full-queue behavior, exactly as
Rare Candy already does.

### 1.2 (re-affirmed from round 6, not re-litigated in depth) — Ball throws still give the wild Pokémon no turn on a failed catch

Re-read `PokemonActivity.cpp:2060-2093` directly: bug 2.7 is still present,
unchanged, still the most exploitable of round 6's open items (see Section 3).
No new angle to add on top of round 6's own writeup - flagged here only to
confirm this round's own major-bugs pass agrees it's real and still the
single most exploitable open issue in the whole backlog, ahead of this
round's own 1.1.

---

## 2. Genuinely useful new features

The brief here was to find things that make the *reading-driven* loop more
transparent/rewarding, not generic Gen-1-completism. Two candidates stood out
on a full read of `PokemonGame.cpp`/`PokemonService.cpp`/`pokemon-mechanics.md`
against what the UI (`PokemonActivity.cpp`) actually exposes - both are real
gaps between what the engine already tracks internally and what the player can
ever see:

### 2.1 (most compelling) — The player has zero visibility into their own odds/progress toward the next encounter, item, or ball

`PokemonState` already tracks five independent pity counters -
`encounterMisses`, `itemMisses` (evolution stones, hourly), `ballMisses`,
`medicineMisses`, `machineMisses` (`PokemonGame.cpp:19,28,31` for the
constants; fields declared in `lib/Pokemon/PokemonTypes.h`) - each counting
down to a guaranteed hit after 3 (or 19, for the hourly stone roll) misses.
Confirmed via grep: **none of these five fields, nor `lifetimeMinutes`, nor
`bookProgressPercent`'s current tier, ever appear anywhere in
`src/activities/pokemon/PokemonActivity.cpp`.** The entire "am I close to a
guaranteed drop" question - the exact mechanism that makes the reading-time
loop feel paced rather than purely random - is completely invisible to the
player today. `docs/pokemon-game.md` itself only tells the player "every
15-60 minutes," with no per-player state ever surfaced in the app.

This is a real, specific gap for *this* game's identity (a reading companion
whose core hook is "your reading is quietly being rewarded") rather than a
generic Gen-1 feature request: a player currently has no way to know "I'm 2
checks away from a guaranteed wild Pokémon" or "book progress just crossed
50%, evolved-form encounters just unlocked" - both of which are already
computed by the engine every single credit cycle and simply never surfaced.
A compact readout (e.g., on the Pokémon main menu or a new small panel:
"Next guaranteed encounter within N checks", current book-progress tier, and
lifetime minutes read) would cost no new save-format work at all - every
field already exists and is already persisted - and would make the existing
pacing mechanism legible instead of a black box.

### 2.2 — No lifetime/collection summary screen

Similarly, `state.lifetimeMinutes` (persisted, incremented every credited
minute since the game began, `PokemonGame.cpp:586`) and the Pokédex
seen/caught bitsets are both durable, long-lived counters with no dedicated
"trainer stats" surface anywhere in the UI - the closest existing thing is
the Pokédex list itself (per-species, not aggregate). A simple summary (total
minutes credited, Pokédex completion count, badges earned, items collected)
would give the player a sense of overall progress across their entire
reading history, which is a natural complement to 2.1's more moment-to-moment
"what's coming up" framing. Lower priority than 2.1 since it's a "nice
retrospective" rather than something that changes how the player plays going
forward, but genuinely free to add given all the underlying data already
exists and is already persisted.

**No feature candidate found in this round that beats or meaningfully adds to
the already-deferred Repel discussion** - it remains exactly where the user
left it (Section 3 of round 6, `TASKS.md` item 1.8), not re-litigated here.

---

## 3. Status check — round 6's 7 bugs (2.1-2.7)

Re-verified directly against current `main` (not trusted from the round 6
doc). All seven are confirmed still present, unchanged, no regressions or
partial fixes found:

- **2.1** (Confusion self-hit 33% vs. real Gen 1 50%) - still `33` at
  `PokemonBattle.cpp:13`. Still open.
- **2.2** (Paralysis halves Speed instead of quartering it) - still `speed /=
  2U;` at `PokemonBattle.cpp:358`. Still open.
- **2.3** (Substitute doesn't block flinch/secondary-stat-drop/status on the
  hit that breaks it) - all three sites (`:1184-1188`, `:1195-1199`,
  `:1550-1552`) still check post-hit `defender.substituteHp == 0` rather than
  a pre-hit snapshot. Still open.
- **2.4** (trapped victim not released when the trapper faints/switches) -
  `trappedTurnsRemaining` still lives only on the victim's side with no reset
  hook on the trapper leaving the field. Still open.
- **2.5** (Reflect/Light Screen/Mist end on switch instead of persisting for
  the side) - still combatant-scoped fields, still fully reset by
  `= BattleCombatant{}` on switch. Still open.
- **2.6** (Rename always exits to `Screen::Menu`/`Screen::Event`, ignoring
  `cancelScreen`) - `openNickname()`'s success/failure branches
  (`PokemonActivity.cpp:780-804`) still hard-code the destination screen.
  Still open.
- **2.7** (Ball throw never gives the wild Pokémon a turn on a failed catch) -
  `Screen::BattleBalls`'s handler (`PokemonActivity.cpp:2060-2093`) still has
  no `resolveOpponentOnlyTurn()` call on the miss path. Still open.

---

## Summary

| Section | Count |
|---|---|
| Major bugs (new) | 1 new (1.1, evolution-item blocked by unrelated pending event) + 1 re-affirmed from round 6 (1.2 / 2.7, still the most exploitable open issue) |
| New features | 2 candidates (2.1 pity/progress visibility, most compelling; 2.2 lifetime/collection summary, secondary) |
| Round 6's 7 bugs | all 7 confirmed still open, no regressions, no partial fixes |

**Most important major bug this round: 1.1** - a real, reproducible, and
confusing false-negative on a core action (using an evolution stone) that
this project's own conventions (documented deliberate divergences,
consistent sibling-code scoping) suggest was never actually an intentional
design call, just an unexamined precondition that happens to fire constantly
for the exact player profile this game is built for (someone actively
accumulating items/encounters from reading). **Most compelling new feature:
2.1** - surfacing the five pity counters/book-progress tier/lifetime minutes
that the engine already computes and persists every cycle, turning the
core reading-reward loop from a black box into something the player can
actually see themselves progressing toward, at effectively zero
save-format/engine cost.
