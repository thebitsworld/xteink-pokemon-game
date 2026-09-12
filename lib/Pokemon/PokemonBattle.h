#pragma once

#include <array>
#include <cstdint>

#include "PokemonBattleTypes.h"
#include "PokemonGame.h"

namespace pokemon {

constexpr uint8_t BATTLE_MOVE_SLOTS = 4;
static_assert(BATTLE_MOVE_SLOTS == GYM_MOVE_SLOTS, "GymTeamMember::moves must match BATTLE_MOVE_SLOTS");

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
  // Display-only - never read by the engine (damage/type/catch math is all
  // gender-independent in these games). The player's side copies it straight
  // from the party PokemonRecord; a wild opponent copies it from the
  // PendingEvent's already-rolled gender; a gym/Elite Four/Champion
  // opponent (whose fixed roster carries no gender of its own) gets one
  // rolled fresh via chooseGenderForSpecies() at battle setup.
  Gender gender = Gender::Unknown;
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
};

// One combat side's result for a single simultaneous turn: which side acted,
// what happened, and whether either combatant fainted or was cured by the
// end of it. The UI maps these enums to STR_* text; the engine never emits
// strings itself.
struct BattleActionResult {
  bool acted = false;  // false if this side had already fainted or was skipped (opponent already won, etc.)
  BattleLogEvent event = BattleLogEvent::None;
  uint8_t moveSlot = 0;
};

struct BattleTurnResult {
  BattleActionResult player{};
  BattleActionResult opponent{};
  BattleOutcome outcome = BattleOutcome::InProgress;
};

// Generation I-derived stat formulas without IV/EV (not modeled): a fair,
// deterministic approximation that scales correctly with level.
uint16_t battleMaxHp(uint8_t baseHp, uint8_t level);
uint16_t battleWorkingStat(uint8_t baseStat, uint8_t level);

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
// before calling in) or the turn is treated as MoveHadNoPp with no effect.
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
// scale with the wild Pokemon's SpeciesData::captureRate, its remaining HP
// fraction, and a bonus for Sleep/Freeze/Paralysis/Poison/Burn.
bool attemptCatch(const BattleCombatant& wild, BallKind ball, const RandomSource& random);

}  // namespace pokemon
