#pragma once

#include <array>
#include <cstdint>

#include "PokemonBattleTypes.h"
#include "PokemonGame.h"

namespace pokemon {

constexpr uint8_t BATTLE_MOVE_SLOTS = 4;
static_assert(BATTLE_MOVE_SLOTS == GYM_MOVE_SLOTS, "GymTeamMember::moves must match BATTLE_MOVE_SLOTS");

// Gen 1's real "badge boost": owning the Boulder/Thunder/Soul/Volcano badge
// gives a flat +12.5% to the corresponding stat (Attack/Defense/Speed/
// Special) for the PLAYER's own Pokemon only - wild Pokemon and trainers
// never have badges, and this project has no link battles for the other
// real exception to apply to. Deliberately does NOT replicate the real
// games' badge-boost STACKING glitch (re-applying the multiplier every
// time any stat stage changes, compounding without limit until a stat
// hits 999) - just the flat, one-time 12.5% baseline. See
// BattleCombatant::badgeBoostMask's doc comment for how these bits get
// set (PokemonActivity.cpp, from the player's own earned-badges state -
// this engine has no idea about badges beyond this bitmask).
constexpr uint8_t BADGE_BOOST_ATTACK = 1U << 0;
constexpr uint8_t BADGE_BOOST_DEFENSE = 1U << 1;
constexpr uint8_t BADGE_BOOST_SPEED = 1U << 2;
constexpr uint8_t BADGE_BOOST_SPECIAL = 1U << 3;

// The real Gen 1 move id for Struggle (already present in this game's own
// move data, scripts/data/pokemon-moves.csv - a typeless-in-spirit,
// fixed-power physical move with accuracy 0, meaning "never misses" per this
// dataset's convention). Passing this as `playerMoveSlot`/an AI move slot to
// stepBattle()/stepOpponentOnlyTurn() (any value >= BATTLE_MOVE_SLOTS) forces
// a Struggle turn instead of indexing into the combatant's own moveset - see
// resolveAction() in PokemonBattle.cpp. This is the same sentinel
// chooseOpponentMoveSlot() already returned for "no usable move" before
// Struggle existed as a real forced action.
constexpr uint8_t STRUGGLE_MOVE_ID = 165;

// Bide's real Gen 1 move id - exposed publicly (unlike most of the hand-
// authored move-id tables in PokemonBattle.cpp) because PokemonActivity.cpp
// needs it too, to detect "the player is mid-Bide, FIGHT must skip straight
// to repeating it" the same way it already does for a forced Struggle.
constexpr uint8_t BIDE_MOVE_ID = 117;

// Mimic's real Gen 1 move id - exposed publicly (unlike most of the hand-
// authored move-id constants in PokemonBattle.cpp) because
// PokemonActivity::savePlayerBattleEntry() needs it too, to restore a
// Mimic-overwritten move slot back to Mimic itself before persisting (see
// BattleCombatant::mimicActive's doc comment).
constexpr uint8_t MIMIC_MOVE_ID = 102;

struct BattleMoveSlot {
  uint8_t moveId = 0;  // 0 = empty slot
  uint8_t currentPp = 0;
};

// One side of a fight. HP/PP/status persist across battles (loaded from the
// side-file store, Stage 3) but the engine itself never touches storage - it is
// a pure function of whatever state the caller hands it.
//
// Simplification: unlike the real games, Confusion shares the single
// `status` slot with the five non-volatile ailments (Paralysis/Sleep/
// Freeze/Burn/Poison) instead of stacking independently. A Pokemon can be
// confused OR poisoned, never both. This keeps the persisted entry to a
// single status byte.
struct BattleCombatant {
  uint16_t speciesId = 0;
  uint8_t level = 1;
  uint16_t currentHp = 0;
  uint16_t maxHp = 0;
  Ailment status = Ailment::None;
  uint8_t statusTurns = 0;  // sleep/confusion countdown, engine-managed
  std::array<BattleMoveSlot, BATTLE_MOVE_SLOTS> moves{};
  // How many times each move slot has had a PP Up used on it (0-3) - copied
  // from BattleRecordEntry::ppUp at battle setup so the in-battle move menu
  // can show the real, boosted max PP (see maxPpFor()). Always all-zero for
  // a gym/Elite Four/Champion trainer's fixed roster or a wild encounter -
  // neither ever accumulates PP Up.
  std::array<uint8_t, BATTLE_MOVE_SLOTS> ppUp{};
  // Display-only - never read by the engine (damage/type/catch math is all
  // gender-independent in these games). The player's side copies it straight
  // from the party PokemonRecord; a wild opponent copies it from the
  // PendingEvent's already-rolled gender; a gym/Elite Four/Champion
  // opponent (whose fixed roster carries no gender of its own) gets one
  // rolled fresh via chooseGenderForSpecies() at battle setup.
  Gender gender = Gender::Unknown;
  // Gen 1 stat stages, -6..+6, reset to 0 whenever a fresh BattleCombatant is
  // built (a new battle, or either side switching) - matching how the real
  // games never carry stat stages across a switch/battle boundary. Never
  // persisted to the side-file store, unlike HP/status/moveset above, since
  // there's nothing to reconstruct: a battle always starts every stage at 0.
  int8_t attackStage = 0;
  int8_t defenseStage = 0;
  int8_t specialStage = 0;
  int8_t speedStage = 0;
  int8_t accuracyStage = 0;
  int8_t evasionStage = 0;
  // Permanent individual variance (IVs, 0-15, rolled once at creation and
  // never changed) and accumulated EVs (0-255, simplified from real Gen 1's
  // 0-65,535 - see docs/development/pokemon-iv-ev-plan.md), one pair per
  // stat in BaseStats/STAT_COUNT order (HP/Attack/Defense/Special/Speed).
  // Unlike the stat stages above, these are NOT reset per battle - a
  // party member's values come from PokemonService::ensureIvEv() (backed by
  // the pokemon-ivev-{a,b}.bin side file), a wild encounter gets a fresh
  // unpersisted roll for the fight (persisted only if the catch succeeds),
  // and a gym/Elite Four/Champion trainer's fixed roster hardcodes IV 15/EV
  // 0 at battle setup (no record exists for them to persist against).
  std::array<uint8_t, STAT_COUNT> iv{};
  std::array<uint8_t, STAT_COUNT> ev{};
  // How much damage this combatant took from a physical hit so far *this
  // turn* - reset to 0 at the start of every stepBattle()/stepOpponentOnlyTurn()
  // call, before either side acts (see PokemonBattle.cpp). Counter (a fixed-
  // damage move, see FIXED_DAMAGE_TABLE) reads its own copy of this to decide
  // whether it has anything to reflect - it only succeeds if the opponent
  // already moved first this same turn and hit with a physical move. Like
  // the stat stages above, this is purely transient battle state, never
  // persisted to the side-file store.
  uint16_t lastPhysicalDamageTaken = 0;
  // Guard Spec./Dire Hit (battle-boost items, ItemCategory::BattleBoost) -
  // like the stat stages above, reset to false whenever a fresh
  // BattleCombatant is built (a new battle or either side switching), never
  // persisted. guardSpecActive blocks the OPPONENT from lowering this
  // combatant's stats for the rest of the battle (simplified from the real
  // Gen 1 5-turn timer - this project already treats several other
  // temporary effects as battle-duration rather than turn-precise).
  // direHitActive raises this combatant's own critical-hit ratio to the
  // same "high-crit" tier a move like Slash gets, for the rest of the
  // battle - see rollCriticalHit()'s call site in resolveAction().
  bool guardSpecActive = false;
  bool direHitActive = false;
  // True if this combatant was just hit by a move with a flinch effect
  // (Stomp, Bite, ...) and hasn't acted since - reset to false at the start
  // of every stepBattle()/stepOpponentOnlyTurn() call, same as
  // lastPhysicalDamageTaken, and consumed (cleared) the moment it prevents
  // one action, whether or not that action ever runs.
  bool flinched = false;
  // Two-turn charge moves (Fly/Dig/Solar Beam/Skull Bash/Sky Attack) and
  // trapping moves (Wrap/Bind/Fire Spin/Clamp) share this: once started, the
  // SAME move keeps resolving automatically on this combatant's following
  // turn(s) without a fresh move being chosen - see resolveAction()'s
  // handling and chooseOpponentMoveSlot()'s early check. 0 = no multi-turn
  // move in progress. Never persisted - a fresh BattleCombatant always
  // starts clear of this, matching every other transient field above.
  uint8_t forcedMoveId = 0;
  uint8_t forcedTurnsRemaining = 0;
  // True only during a two-turn move's charge turn that grants it (Fly/Dig -
  // Solar Beam/Skull Bash/Sky Attack don't) - simplified from the real
  // semi-invulnerable turn to "every incoming move just misses" rather than
  // modeling the specific moves (Swift, Earthquake while the target Dig's,
  // ...) that are meant to bypass it.
  bool invulnerable = false;
  // Bide (move 117): stores the damage taken over the 2 turns it's braced
  // for below, then releases double that back on the turn it runs out.
  // bideTurnsRemaining == 0 means not bracing.
  uint16_t bideDamageStored = 0;
  uint8_t bideTurnsRemaining = 0;
  // Leech Seed: this combatant is seeded and loses 1/8 of its max HP at the
  // end of every turn, healing whichever opponent seeded it that same
  // amount (see finishTurn()) - persists until switched out or the battle
  // ends, same battle-duration simplification as guardSpecActive below.
  bool seeded = false;
  // Reflect/Light Screen (moves) - halve incoming Physical/Special damage
  // respectively for the rest of the battle (simplified from the real 5-turn
  // timer, same rationale as guardSpecActive). A critical hit still bypasses
  // both, matching the real games.
  bool reflectActive = false;
  bool lightScreenActive = false;
  // Disable: prevents choosing the move at this slot for a few turns.
  // disableTurnsRemaining == 0 means nothing is disabled - disabledMoveSlot
  // is only meaningful while it's positive. Counted down once per turn in
  // finishTurn(), alongside the poison/burn/Leech Seed ticks.
  uint8_t disabledMoveSlot = 0;
  uint8_t disableTurnsRemaining = 0;
  // Substitute: a decoy holding this much HP, created for 1/4 of the user's
  // own max HP (minimum 1). While it's up (> 0), incoming damage comes out
  // of this instead of currentHp (a hit that would deal more than the
  // remaining amount just breaks it outright, no overflow onto the real
  // Pokemon), and status/stat-lowering/Leech Seed/flinch from the opponent
  // are blocked entirely - see resolveAction()'s various substituteHp
  // checks. 0 means no substitute is up.
  uint16_t substituteHp = 0;
  // Mirror Move: the most recent real move id used against this combatant,
  // regardless of whether it actually hit (0 = nothing recorded yet) - see
  // resolveAction()'s tracking line and the Mirror Move dispatch. Never
  // persisted, same battle-duration-only rationale as the fields above.
  // Simplified from the real games, which also require the move to have
  // actually made contact before it can be mirrored.
  uint8_t lastMoveUsedAgainstMe = 0;
  // Mimic: true while one of this combatant's move slots has been
  // temporarily overwritten with a move copied from the opponent (see
  // resolveAction()). mimicSlot names which slot; mimicOriginalPp remembers
  // Mimic's own remaining PP from right before this use, so
  // PokemonActivity::savePlayerBattleEntry() can restore that slot back to
  // Mimic itself (id MIMIC_MOVE_ID) rather than persisting the borrowed
  // move - Mimic's copy only ever lasts for this one battle. Reset to false
  // whenever a fresh BattleCombatant is built, same as every other
  // transient field above.
  bool mimicActive = false;
  uint8_t mimicSlot = 0;
  uint8_t mimicOriginalPp = 0;
  // Transform: true once this combatant has copied an opponent's form this
  // battle - Transform fails ("But it failed!") if used again, matching
  // Gen 1. Copying itself (speciesId/stat stages/moveset, see
  // resolveAction()) doesn't need its own storage beyond this flag; HP,
  // level, and status are deliberately left untouched, matching the real
  // games. speciesId/moveset changes from Transform are never persisted
  // (see PokemonActivity::savePlayerBattleEntry()) - they only ever last
  // until this combatant switches out or the battle ends, same as every
  // other transient field above.
  bool transformed = false;
  // Conversion: overrides this combatant's own type (for STAB and
  // incoming/outgoing type-effectiveness purposes only - display, the
  // Pokedex, and every other type reference are unaffected) with whatever
  // it copied from the opponent at the moment Conversion was used, for the
  // rest of the battle. PokemonType::None means "no override - use the
  // species' real type", which conversionType1 can never legitimately hold
  // otherwise (SpeciesData::primaryType is never None for a real species).
  PokemonType conversionType1 = PokemonType::None;
  PokemonType conversionType2 = PokemonType::None;
  // Rage (move 99): while true, this combatant's Attack stage rises by 1
  // every time it's hit by an opposing move (checked wherever damage is
  // applied to a combatant). Ends the instant this combatant uses any move
  // other than Rage (including a forced Struggle) - the real Gen 1 rule is
  // that picking a different move cancels it, unlike Thrash/Petal Dance's
  // hard multi-turn lock below, so no separate "turns remaining" field is
  // needed here.
  bool enraged = false;
  // Hyper Beam (move 63): true for exactly one turn after Hyper Beam lands
  // a hit (not set on a miss) - the next turn's action is skipped entirely
  // ("must recharge"), matching the real games. Checked first, before even
  // flinch/status, since a recharge turn always takes priority over
  // everything else.
  bool mustRecharge = false;
  // Wrap/Bind/Fire Spin/Clamp (partial-trapping moves): real Gen 1 traps
  // BOTH sides at once - the attacker auto-repeats the move (the existing
  // forcedMoveId/forcedTurnsRemaining above already models that half) AND
  // the TARGET is immobilized (can't select or execute any move) for
  // roughly the same duration - unless a Substitute is up, which absorbs
  // the hit and blocks this the same way it already blocks status/stat-
  // lowering/flinch/Leech Seed. trappedTurnsRemaining > 0 means this
  // combatant can't act at all this turn; counted down once per turn in
  // finishTurn(), alongside disableTurnsRemaining. A documented
  // simplification: the real games also block switching while trapped,
  // which this project doesn't enforce in the UI.
  uint8_t trappedTurnsRemaining = 0;
  // Badge boost bitmask (BADGE_BOOST_ATTACK/_DEFENSE/_SPEED/_SPECIAL, see
  // above) - always 0 for a wild/trainer opponent's own BattleCombatant;
  // only ever set for the player's side, once at battle setup, from
  // whichever of the 4 stat-boosting badges the player currently owns.
  // Not reset by a fresh BattleCombatant the way most fields above are,
  // since PokemonActivity.cpp re-sets it explicitly every time it builds
  // battlePlayer_ (battle start and every switch) anyway.
  uint8_t badgeBoostMask = 0;
};

// Index into BattleCombatant::iv/ev (and BaseStats' own fields) - HP,
// Attack, Defense, Special, Speed, matching STAT_COUNT's order everywhere
// else in this project.
enum class StatIndex : uint8_t {
  Hp = 0,
  Attack,
  Defense,
  Special,
  Speed,
};

enum class BattleOutcome : uint8_t {
  InProgress = 0,
  PlayerWon = 1,
  OpponentWon = 2,
};

enum class BattleLogEvent : uint8_t {
  None = 0,
  MoveMissed,
  MoveNoEffect,
  MoveHit,
  MoveSuperEffective,
  MoveNotVeryEffective,
  MoveHadNoPp,
  StatusPreventedMove,  // asleep, frozen, fully paralyzed
  ConfusionSelfHit,
  InflictedStatus,
  StatusCured,   // woke up / thawed / snapped out of confusion
  StatusDamage,  // poison/burn tick
  Fainted,
  StatRaised,       // a stat-changing move successfully raised a stage
  StatLowered,      // a stat-changing move successfully lowered a stage
  StatChangeFailed,  // the target stat was already at +6/-6 - no further change possible
  StatsReset,       // Haze - both sides' stages (and this engine's status slot) reset
  OneHitKo,          // Fissure/Horn Drill/Guillotine connected - instant faint
  MoveFailed,        // Counter with nothing to reflect this turn ("But it failed!")
  Flinched,          // hit by a flinch-inducing move last turn (Stomp, Bite, ...) - this turn's action is skipped
  ChargingMove,      // a two-turn move's charge turn (Fly, Dig, Solar Beam, ...) or Bide bracing - no effect yet
  Seeded,            // Leech Seed took hold
  BuffApplied,       // Reflect/Light Screen/Mist/Focus Energy went up
  ForcedSwitch,      // Whirlwind/Roar connected - see PokemonActivity.cpp for what that actually does
  MoveDisabled,      // Disable took hold on one of the target's moves
  SubstituteUp,      // Substitute was created
  Transformed,       // Transform copied the opponent's form
  MimicCopied,       // Mimic copied one of the opponent's moves into a slot
  ConversionApplied,  // Conversion copied the opponent's type
  MustRecharge,      // Hyper Beam's recharge turn - no action taken
  Trapped,           // immobilized by an opponent's Wrap/Bind/Fire Spin/Clamp - no action taken
  NothingHappened,   // a pure-flavor status move with no ailment/effect (Splash, ...) - "But nothing happened!"
};

// Which stat/accuracy-or-evasion axis a status move affects. Combined with
// Special (not split into Attack/Defense-style special stats - Gen 1 has one
// shared Special stat, see the Gen 1 authenticity roadmap doc).
enum class StatKind : uint8_t {
  Attack = 0,
  Defense,
  Special,
  Speed,
  Accuracy,
  Evasion,
};

// A hand-authored table entry for one of the ~22 Gen 1 status moves that
// change a stat stage (Growl, Swords Dance, Reflect-adjacent moves like
// Barrier/Acid-Armor, etc.) - see statChangeForMove(). `stages` is signed
// (negative = lowers); `targetsSelf` distinguishes Swords-Dance-style
// self-buffs from Growl-style opponent-debuffs. Exposed publicly (not just
// to the engine) so the UI can name which stat changed for its log line,
// the same way it already looks up MoveData for the "X used Y!" line.
struct StatChangeEffect {
  StatKind stat = StatKind::Attack;
  int8_t stages = 0;
  bool targetsSelf = true;
};

// nullptr if moveId isn't one of the ~22 hand-authored stat-changing status
// moves (or is Haze, id 114, handled as a special case in stepBattle()/
// resolveAction() since it resets every stage on both sides rather than
// changing one stat by one amount).
const StatChangeEffect* statChangeForMove(uint8_t moveId);

// Applies Gen 1's real stat-stage multiplier table to a computed Attack/
// Defense/Special/Speed value: stage>=0 multiplies by (2+stage)/2, stage<0
// divides by (2-stage)/2 - i.e. 25% at -6 up to 400% at +6. `stage` is
// clamped to [-6,6] first in case of caller error.
uint16_t applyStatStage(uint16_t baseValue, int8_t stage);

// Same idea but Gen 1's separate Accuracy/Evasion table (25% floor is 33% here
// instead, and the ceiling is 300% instead of 400%): stage>=0 multiplies by
// (3+stage)/3, stage<0 divides by (3-stage)/3.
uint32_t applyAccuracyEvasionStage(uint32_t baseValue, int8_t stage);

// Haze (move id 114) - the one stat-changing status move that doesn't fit
// StatChangeEffect's single-stat model: resets all 6 of a combatant's
// stages to 0.
void resetBattleStages(BattleCombatant& combatant);

// Applies a battle-boost item (ItemCategory::BattleBoost, ids
// BATTLE_BOOST_ITEM_ID_FIRST..LAST) directly to a live BattleCombatant -
// these only ever affect the active battler for the rest of the current
// battle, so unlike Medicine/StatusCure/PPRestore items there's no
// persisted BattleRecordEntry to update. Returns false if the item had
// nothing to do (a stat already at +6, or Guard Spec./Dire Hit already
// active on this combatant) - the same "NotApplicable" meaning
// UseConsumableOutcome::NotApplicable carries for every other item category.
bool applyBattleBoostItem(BattleCombatant& combatant, uint8_t itemId);

// One combat side's result for a single simultaneous turn: which side acted,
// what happened, and whether either combatant fainted or was cured by the
// end of it. The UI maps these enums to STR_* text; the engine never emits
// strings itself.
struct BattleActionResult {
  bool acted = false;  // false if this side had already fainted or was skipped (opponent already won, etc.)
  BattleLogEvent event = BattleLogEvent::None;
  uint8_t moveSlot = 0;
  // True on a damaging move that landed a critical hit (2x damage) - kept
  // separate from `event` rather than folded into it, since a hit can be
  // both critical and super/not-very effective at once. Never true for a
  // Status move or a miss/immune hit (nothing to double).
  bool critical = false;
  // How many times a multi-hit move (Double Slap, Fury Attack, Twineedle,
  // ...) actually connected this turn - 0 for any move that isn't one of the
  // hand-authored multi-hit moves (see MULTI_HIT_TABLE), even if it landed
  // exactly once, so the UI can tell "a normal single-hit move" apart from
  // "a multi-hit move that happened to roll its minimum." Stops short of the
  // move's intended hit count if the defender faints partway through.
  uint8_t hitCount = 0;
  // True if this action dealt recoil damage back to the attacker (Take
  // Down/Double-Edge/Submission's fixed 1/4-of-damage-dealt recoil, or
  // Struggle's 1/2-of-damage-dealt recoil) - independent of `event` for the
  // same reason `critical` is, since recoil can accompany any damage event.
  bool recoilApplied = false;
  // True if this action drained HP back to the attacker (Absorb/Mega Drain/
  // Leech Life/Dream Eater's real Gen 1 effect: heal half the damage dealt,
  // minimum 1) - independent of `event` for the same reason `critical`/
  // `recoilApplied` are.
  bool drainApplied = false;
  // The real move id actually executed this action, if it differs from the
  // slot's own move - i.e. Metronome/Mirror Move redirecting to whatever
  // move they picked/mirrored. 0 when no redirection happened (the normal
  // case for every other move, including a forced Struggle). The UI uses
  // this to show what Metronome/Mirror Move actually turned into, on top of
  // the usual "X used METRONOME!" framing.
  uint8_t redirectedMoveId = 0;
};

struct BattleTurnResult {
  BattleActionResult player{};
  BattleActionResult opponent{};
  BattleOutcome outcome = BattleOutcome::InProgress;
};

// Generation I-derived stat formulas, now with IV/EV: `iv` is 0-15 (real Gen
// 1 range), `ev` is 0-255 (this project's simplified range - see
// docs/development/pokemon-iv-ev-plan.md). `iv=0, ev=0` reproduces exactly
// what these formulas returned before IV/EV existed - every existing caller
// that hasn't been taught about a Pokemon's real IV/EV yet can still pass
// 0/0 and see no behavior change.
//
//   HP    = floor((2*(baseHp+iv)   + ev/4) * level / 100) + level + 10
//   other = floor((2*(baseStat+iv) + ev/4) * level / 100) + 5
//
// The EV/4 term is a deliberate simplification of real Gen 1's
// floor(sqrt(EV)/4) - both reach the same maximum bonus (63), just via a
// flat climb instead of a square-root curve, at 1 byte/stat of storage
// instead of 2.
uint16_t battleMaxHp(uint8_t baseHp, uint8_t level, uint8_t iv = 0, uint8_t ev = 0);
uint16_t battleWorkingStat(uint8_t baseStat, uint8_t level, uint8_t iv = 0, uint8_t ev = 0);

// Rolls a fresh IV set (0-15 per stat, STAT_COUNT/HP-Attack-Defense-Special-
// Speed order) - Gen 1's real IV range, meant to be assigned once and never
// changed again. Used both for a brand-new permanent record (see
// PokemonService::ensureIvEv(), which also backfills a pre-existing record
// from before this mechanic existed) and for a wild encounter's own
// BattleCombatant at fight time (not persisted unless the catch succeeds).
void rollIvSet(const RandomSource& random, std::array<uint8_t, STAT_COUNT>& output);

// PP Up: a real Gen 1 item that permanently raises one move slot's max PP by
// 1/5 of its base PP (floored) per use, up to 3 uses. `basePp` is the move's
// own PP (MoveData::pp); `ppUpCount` is that slot's accumulated use count
// (BattleRecordEntry::ppUp, 0-3 - clamped here too in case of caller error).
// A 40-PP move goes 40 -> 48 -> 56 -> 64, matching the real games exactly.
uint8_t maxPpFor(uint8_t basePp, uint8_t ppUpCount);

// Picks up to BATTLE_MOVE_SLOTS moves for `speciesId` at `level`: the
// learnset is stored ascending by level, so walking it backwards yields the
// most-recently-learned moves at or below the current level first, matching
// how the real games pick a newly-caught/leveled Pokemon's active moveset.
// Used both to synthesize a Party member's first-ever battle entry
// (PokemonService) and to build a fresh wild encounter's moveset (never
// persisted - a wild Pokemon has no battle-store entry to begin with).
// Unfilled trailing slots are left at 0/0 (empty).
void defaultMovesetForLevel(uint16_t speciesId, uint8_t level, std::array<uint8_t, BATTLE_MOVE_SLOTS>& moveIds,
                            std::array<uint8_t, BATTLE_MOVE_SLOTS>& pp);

// Resolves one simultaneous turn: faster combatant (by working Speed, ties
// favor the player) acts first; if that action faints the other side, the
// slower side never gets to act. `playerMoveSlot` must reference a
// non-empty, non-zero-PP slot in player.moves (validated by the caller/UI
// before calling in) or the turn is treated as MoveHadNoPp with no effect -
// *unless* it's STRUGGLE_MOVE_ID's sentinel (any value >= BATTLE_MOVE_SLOTS),
// which forces a real Struggle turn instead (matching Gen 1: once every
// learned move is out of PP, a Pokemon Struggles rather than doing nothing).
BattleTurnResult stepBattle(BattleCombatant& player, BattleCombatant& opponent, uint8_t playerMoveSlot,
                            const RandomSource& random);

// Resolves a turn where the player spent their whole turn on something other
// than a move - switching Pokemon, or using an item mid-battle. Matches
// Gen 1: both of those always take the entire turn with no Speed
// comparison, so only the opponent acts here (`result.player` stays
// acted=false/None, matching what a caller already fainted looks like in
// stepBattle()); end-of-turn status damage still applies to both sides
// exactly as it does after a normal stepBattle() turn.
BattleTurnResult stepOpponentOnlyTurn(BattleCombatant& player, BattleCombatant& opponent, const RandomSource& random);

enum class BallKind : uint8_t {
  Poke = 0,
  Great = 1,
  Ultra = 2,
  Master = 3,
};

// True if the throw succeeds. Master Ball always succeeds; the other three
// follow Gen 1's real two-roll catch algorithm: a ball-ranged R1 roll
// (narrower for a better ball) that status can push into an automatic
// catch or, short of that, must still clear the species' own
// SpeciesData::captureRate; then, only if that clears, a second roll
// against an HP-based factor (higher for a more-damaged target, with
// Great Ball using a more forgiving divisor than Poke/Ultra) decides the
// catch. Does not model the real games' separate "how many times the ball
// shakes before breaking free" cosmetic animation, since this project has
// no such animation to drive.
bool attemptCatch(const BattleCombatant& wild, BallKind ball, const RandomSource& random);

// XP awarded to the Pokemon active when a battle is won (defeating or
// catching a wild Pokemon, or defeating one gym/Elite Four/Champion team
// member) - a deliberately simple `level * multiplier` rather than Gen 1's
// real species-yield formula, scaled to stay a meaningful bonus on top of
// reading-driven XP without dwarfing it (see docs/development's battle
// roadmap for the reasoning). Trainer battles use a higher multiplier than
// wild ones, loosely mirroring the real games' 1.5x trainer-battle bonus.
uint32_t battleVictoryXp(uint8_t opponentLevel, bool isTrainerBattle);

}  // namespace pokemon
