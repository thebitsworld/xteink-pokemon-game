#pragma once

#include <cstdint>
#include <span>

#include "PokemonSpecies.h"

namespace pokemon {

constexpr uint8_t MOVE_COUNT = 165;
constexpr uint8_t ITEM_COUNT = 83;
constexpr uint8_t GYM_COUNT = 12;  // 8 gyms + 4 Elite Four, in challenge order
constexpr uint8_t MAX_GYM_TEAM_SIZE = 3;

enum class MoveCategory : uint8_t {
  Physical = 0,
  Special = 1,
  Status = 2,
};

enum class Ailment : uint8_t {
  None = 0,
  Paralysis = 1,
  Sleep = 2,
  Freeze = 3,
  Burn = 4,
  Poison = 5,
  Confusion = 6,
  All = 7,  // Full Heal / Full Restore: cures whichever ailment is active
};

struct MoveData {
  uint8_t moveId;
  const char* name;
  PokemonType type;
  uint8_t power;
  uint8_t accuracy;
  uint8_t pp;
  MoveCategory category;
  Ailment ailment;
  uint8_t ailmentChance;
};

// Generation I base stats. Special stands in for the single Gen I "Special"
// stat (PokeAPI's special-attack). All Gen I base stats fit uint8_t (max 255).
struct BaseStats {
  uint8_t hp;
  uint8_t attack;
  uint8_t defense;
  uint8_t special;
  uint8_t speed;
};

struct LearnsetEntry {
  uint8_t level;
  uint8_t moveId;
};

// Offset/count indirection into a flat per-species table (learnset entries or
// TM/HM-compatible move IDs), the same pattern SpeciesData uses for
// evolutions. uint16_t offset because the TM/HM table alone has 3000+ rows.
struct MoveListRef {
  uint16_t offset;
  uint8_t count;
};

enum class ItemCategory : uint8_t {
  Stone = 0,  // ids 1-6, pinned to match EvolutionItem exactly
  Ball = 1,
  Medicine = 2,
  StatusCure = 3,
  Candy = 4,
  PPRestore = 5,
  Machine = 6,  // TM/HM
};

struct ItemData {
  uint8_t itemId;
  const char* name;
  ItemCategory category;
  uint8_t effectValue;
  Ailment curesAilment;
  uint8_t teachesMoveId;  // Machine category only, else 0
  uint8_t dropWeight;
};

struct GymTeamMember {
  uint16_t speciesId;
  uint8_t level;
};

struct GymData {
  const char* leaderName;
  const char* badgeName;  // empty for Elite Four entries
  PokemonType specialty;
  uint8_t teamOffset;
  uint8_t teamCount;
};

// Combined type-effectiveness multiplier as a percent (0/25/50/100/200/400),
// folding in the defender's second type when present. 100 = neutral.
uint16_t typeEffectivenessPercent(PokemonType attackerType, PokemonType defenderPrimary, PokemonType defenderSecondary);

const MoveData* moveData(uint8_t moveId);
const BaseStats* baseStatsFor(uint16_t speciesId);
std::span<const LearnsetEntry> learnsetFor(uint16_t speciesId);
bool canLearnViaMachine(uint16_t speciesId, uint8_t moveId);
const ItemData* itemData(uint8_t itemId);
const GymData* gymData(uint8_t gymIndex);

enum class GymProgress : uint8_t {
  Locked,
  Available,
  Defeated,
};

// Pure read of a battleProgress bitfield (PokemonState::battleProgress):
// gym N (1-8) requires gyms 1..N-1 already defeated; an Elite Four member
// (9-GYM_COUNT) requires all 8 gym bits set. Shared by
// PokemonService::markGymDefeated (the mutating check) and the UI's gym
// list display (read-only) so the unlock rule lives in exactly one place.
GymProgress gymProgressFor(uint16_t battleProgress, uint8_t gymIndex);
std::span<const GymTeamMember> gymTeamFor(uint8_t gymIndex);

}  // namespace pokemon
