#if defined(CROSSINK_ENABLE_POKEMON)

#include "PokemonBattle.h"

#include <algorithm>

#include "PokemonSpecies.h"

namespace pokemon {
namespace {

constexpr uint8_t PARALYSIS_FAIL_CHANCE_PERCENT = 25;
constexpr uint8_t CONFUSION_SELF_HIT_CHANCE_PERCENT = 33;
constexpr uint8_t FREEZE_THAW_CHANCE_PERCENT = 20;
constexpr uint8_t STATUS_DAMAGE_FRACTION = 8;  // poison/burn: 1/8 max HP per turn, floor 1
constexpr uint8_t DAMAGE_RANDOM_MIN_PERCENT = 85;
constexpr uint8_t STAB_BONUS_PERCENT = 150;

bool rollBelow(const RandomSource& random, const uint32_t upperExclusive, uint32_t& output) {
  if (random.below == nullptr || upperExclusive == 0) return false;
  const uint32_t candidate = random.below(random.context, upperExclusive);
  if (candidate >= upperExclusive) return false;
  output = candidate;
  return true;
}

bool rollPercentChance(const RandomSource& random, const uint8_t chancePercent) {
  if (chancePercent == 0) return false;
  if (chancePercent >= 100) return true;
  uint32_t roll = 0;
  if (!rollBelow(random, 100, roll)) return false;
  return roll < chancePercent;
}

uint16_t clampToUint16(const uint32_t value) { return static_cast<uint16_t>(std::min<uint32_t>(value, UINT16_MAX)); }

// The 4 real Gen 1 high-critical-hit-ratio moves (Karate Chop, Razor Leaf,
// Crabhammer, Slash) - a fixed, well-known list rather than a CSV column,
// same rationale as PokemonTypeChart.cpp being hand-written.
bool isHighCritRatioMove(const uint8_t moveId) {
  return moveId == 2 || moveId == 75 || moveId == 152 || moveId == 163;
}

// Gen 1's real crit formula: floor(baseSpeed/2)/256 normally, x8 for a
// high-crit-ratio move - simplified here to baseSpeed/512 and baseSpeed/64
// (equivalent for baseSpeed even, off by a rounding half-step otherwise,
// which doesn't matter at this scale). Capped so no realistic base Speed
// (max in Gen 1 is 140, Electrode) can push the threshold past the 512-wide
// roll.
bool rollCriticalHit(const uint8_t baseSpeed, const bool highCritRatio, const RandomSource& random) {
  const uint32_t threshold =
      std::min<uint32_t>(static_cast<uint32_t>(baseSpeed) * (highCritRatio ? 8U : 1U), 511U);
  uint32_t roll = 0;
  if (!rollBelow(random, 512U, roll)) return false;
  return roll < threshold;
}

constexpr uint8_t HAZE_MOVE_ID = 114;

// The ~22 real Gen 1 status moves that change a single stat stage - see
// docs/development/pokemon-gen1-authenticity-roadmap.md item 1 for why this
// list stops here (screens/Mist/self-heal/switch-forcing/move-copying moves
// are separate, larger features tracked as follow-ups on that same doc).
// Hand-authored rather than a new CSV column, same rationale as
// PokemonTypeChart.cpp and the crit move list above.
struct StatChangeTableEntry {
  uint8_t moveId;
  StatChangeEffect effect;
};
constexpr StatChangeTableEntry STAT_CHANGE_TABLE[] = {
    {14, {StatKind::Attack, 2, true}},     // Swords Dance
    {28, {StatKind::Accuracy, -1, false}},  // Sand Attack
    {39, {StatKind::Defense, -1, false}},   // Tail Whip
    {43, {StatKind::Defense, -1, false}},   // Leer
    {45, {StatKind::Attack, -1, false}},    // Growl
    {74, {StatKind::Special, 1, true}},    // Growth
    {81, {StatKind::Speed, -1, false}},     // String Shot
    {96, {StatKind::Attack, 1, true}},     // Meditate
    {97, {StatKind::Speed, 2, true}},      // Agility
    {103, {StatKind::Defense, -2, false}},  // Screech
    {104, {StatKind::Evasion, 1, true}},   // Double Team
    {106, {StatKind::Defense, 1, true}},   // Harden
    {107, {StatKind::Evasion, 1, true}},   // Minimize
    {108, {StatKind::Accuracy, -1, false}}, // Smokescreen
    {110, {StatKind::Defense, 1, true}},   // Withdraw
    {111, {StatKind::Defense, 1, true}},   // Defense Curl
    {112, {StatKind::Defense, 2, true}},   // Barrier
    {133, {StatKind::Special, 2, true}},   // Amnesia
    {134, {StatKind::Accuracy, -1, false}}, // Kinesis
    {148, {StatKind::Accuracy, -1, false}}, // Flash
    {151, {StatKind::Defense, 2, true}},   // Acid Armor
    {159, {StatKind::Attack, 1, true}},    // Sharpen
};

// The 3 real Gen 1 recoil moves, plus Struggle itself (STRUGGLE_MOVE_ID,
// declared in PokemonBattle.h) - recoil is `recoilNumerator/recoilDenominator`
// of the *total damage dealt this turn* (all hits summed, for a move that's
// also multi-hit - none of these three are, but the formula generalizes),
// floored, minimum 1 if any damage was dealt at all. Take Down/Double-Edge/
// Submission recoil 1/4 of damage dealt (Gen 1's real fraction); Struggle
// recoils a full 1/2 (also Gen 1's real fraction - later generations changed
// both of these to fixed fractions of max HP instead, not modeled here).
struct RecoilTableEntry {
  uint8_t moveId;
  uint8_t recoilNumerator;
  uint8_t recoilDenominator;
};
constexpr RecoilTableEntry RECOIL_TABLE[] = {
    {36, 1, 4},              // Take Down
    {38, 1, 4},              // Double-Edge
    {66, 1, 4},              // Submission
    {STRUGGLE_MOVE_ID, 1, 2},  // Struggle
};

const RecoilTableEntry* recoilEntryForMove(const uint8_t moveId) {
  for (const RecoilTableEntry& entry : RECOIL_TABLE) {
    if (entry.moveId == moveId) return &entry;
  }
  return nullptr;
}

// The real Gen 1 multi-hit moves. `fixedHits == 0` means "roll it" via
// rollMultiHitCount()'s real 2/3/4/5-hit distribution; Twineedle is the one
// exception that always hits exactly twice rather than rolling. Hand-authored
// rather than a new CSV column, same rationale as the crit/stat-change lists
// above - PokeAPI's own move data doesn't carry a hit-count field for these.
struct MultiHitTableEntry {
  uint8_t moveId;
  uint8_t fixedHits;  // 0 = roll via rollMultiHitCount()
};
constexpr MultiHitTableEntry MULTI_HIT_TABLE[] = {
    {3, 0},    // Double Slap
    {4, 0},    // Comet Punch
    {31, 0},   // Fury Attack
    {41, 2},   // Twineedle - always exactly 2 hits
    {42, 0},   // Pin Missile
    {140, 0},  // Barrage
    {154, 0},  // Fury Swipes
};

const MultiHitTableEntry* multiHitEntryForMove(const uint8_t moveId) {
  for (const MultiHitTableEntry& entry : MULTI_HIT_TABLE) {
    if (entry.moveId == moveId) return &entry;
  }
  return nullptr;
}

// Gen 1's real multi-hit distribution: 2 hits and 3 hits are each 3/8 likely,
// 4 and 5 hits are each 1/8 - not a flat 2-5 spread.
uint8_t rollMultiHitCount(const RandomSource& random) {
  uint32_t roll = 0;
  if (!rollBelow(random, 8U, roll)) return 2;
  if (roll < 3U) return 2;
  if (roll < 6U) return 3;
  if (roll < 7U) return 4;
  return 5;
}

// A handful of real Gen 1 moves compute their damage a completely different
// way than the normal level/power/Attack/Defense formula in computeDamage()
// - PokeAPI's own move data (scripts/data/pokemon-moves.csv) reflects this by
// storing `power = 0` for every one of them, since there's no single "power"
// number that would produce their real behavior. Before this table existed,
// that meant these moves fell through to the normal formula anyway and dealt
// a useless ~2 flat damage regardless of the target - a real bug, not just
// an authenticity gap. Hand-authored rather than a new CSV column, same
// rationale as the crit/stat-change/multi-hit/recoil tables above.
//
// Low Kick (id 67) is deliberately NOT in this table: its real damage scales
// with the target's weight, but this project has no per-species weight data
// (nothing here fetches it from PokeAPI, and this sandbox can't reach the
// network to add it). It's handled as a simpler fixed-power override instead
// - see LOW_KICK_MOVE_ID below - a documented simplification, not the exact
// mechanic.
//
// Bide (id 117) is also deliberately NOT here: it stores damage across two
// turns before releasing it, which needs the same kind of multi-turn
// persisted state that trapping/two-turn moves do - tracked as a separate,
// larger follow-up (see the Gen 1 mechanics gaps note), not this pass.
enum class FixedDamageKind : uint8_t {
  Ohko,             // Fissure/Horn Drill/Guillotine - always faints on a hit
  LevelDamage,      // Seismic Toss/Night Shade - damage equals the user's level
  FlatDamage,       // Dragon Rage/Sonic Boom - a fixed amount regardless of level or stats
  Psywave,          // random damage from 1 up to 1.5x the user's level
  HalveDefenderHp,  // Super Fang - halves the target's current HP
  Counter,          // reflects 2x the last physical damage this user took, this same turn
};
struct FixedDamageTableEntry {
  uint8_t moveId;
  FixedDamageKind kind;
  uint16_t flatAmount;  // only meaningful for FlatDamage
};
constexpr FixedDamageTableEntry FIXED_DAMAGE_TABLE[] = {
    {12, FixedDamageKind::Ohko, 0},              // Guillotine
    {32, FixedDamageKind::Ohko, 0},              // Horn Drill
    {90, FixedDamageKind::Ohko, 0},              // Fissure
    {69, FixedDamageKind::LevelDamage, 0},       // Seismic Toss
    {101, FixedDamageKind::LevelDamage, 0},      // Night Shade
    {82, FixedDamageKind::FlatDamage, 40},       // Dragon Rage
    {49, FixedDamageKind::FlatDamage, 20},       // Sonic Boom
    {149, FixedDamageKind::Psywave, 0},          // Psywave
    {162, FixedDamageKind::HalveDefenderHp, 0},  // Super Fang
    {68, FixedDamageKind::Counter, 0},           // Counter
};

const FixedDamageTableEntry* fixedDamageEntryForMove(const uint8_t moveId) {
  for (const FixedDamageTableEntry& entry : FIXED_DAMAGE_TABLE) {
    if (entry.moveId == moveId) return &entry;
  }
  return nullptr;
}

// Low Kick's real Gen 1 damage scales with the target's weight (20-120
// power across 5 weight bands) - simplified here to a fixed mid-band power,
// since this project has no per-species weight data (see FIXED_DAMAGE_TABLE's
// doc comment above). Still goes through the normal damage formula/type
// chart/STAB/crit, unlike the table above - only its power is overridden.
constexpr uint8_t LOW_KICK_MOVE_ID = 67;
constexpr uint8_t LOW_KICK_SIMPLIFIED_POWER = 50;

// The real Gen 1 moves with a flinch secondary effect. PokeAPI's own move
// data doesn't carry flinch chance at all (this project's Ailment enum only
// models the 6 real status conditions, not the transient "flinch" effect),
// so this is hand-authored, same rationale as the tables above.
struct FlinchTableEntry {
  uint8_t moveId;
  uint8_t chancePercent;
};
constexpr FlinchTableEntry FLINCH_TABLE[] = {
    {23, 30},   // Stomp
    {27, 30},   // Rolling Kick
    {29, 30},   // Headbutt
    {44, 10},   // Bite
    {125, 10},  // Bone Club
    {158, 10},  // Hyper Fang
};

const FlinchTableEntry* flinchEntryForMove(const uint8_t moveId) {
  for (const FlinchTableEntry& entry : FLINCH_TABLE) {
    if (entry.moveId == moveId) return &entry;
  }
  return nullptr;
}

// The 4 real Gen 1 HP-drain moves - the attacker heals half the damage dealt
// (minimum 1), on top of the normal damage formula (unlike FIXED_DAMAGE_TABLE's
// entries, these don't change how damage itself is computed).
bool isDrainMove(const uint8_t moveId) {
  return moveId == 71 ||    // Absorb
         moveId == 72 ||    // Mega Drain
         moveId == 138 ||   // Dream Eater
         moveId == 141;     // Leech Life
}

// Dream Eater's real Gen 1 quirk: it fails outright ("But it failed!", no
// damage/drain at all) unless the target is currently asleep - checked
// separately from isDrainMove() above, which only governs the HP-drain
// side effect once a hit is already known to land.
constexpr uint8_t DREAM_EATER_MOVE_ID = 138;

// Hyper Beam's real Gen 1 recharge turn: a hit (not a miss) forces the user
// to skip its entire next turn ("must recharge!") before it can act again -
// see BattleCombatant::mustRecharge's doc comment.
constexpr uint8_t HYPER_BEAM_MOVE_ID = 63;

// Rage's real Gen 1 quirk: while enraged (see BattleCombatant::enraged),
// every hit taken raises the user's own Attack by one stage - checked
// wherever damage lands on a combatant, alongside the Bide-damage-
// accumulation check those same sites already do.
constexpr uint8_t RAGE_MOVE_ID = 99;

// Thrash/Petal Dance: like the two-turn-charge/trapping moves below, lock
// the ATTACKER into automatically repeating the same move for 2-3 turns
// (BattleCombatant::forcedMoveId/forcedTurnsRemaining, shared storage) -
// but unlike a trapping move, the user becomes confused once the lock ends,
// and each turn (including repeats) still rolls accuracy normally rather
// than auto-hitting.
bool isThrashMove(const uint8_t moveId) {
  return moveId == 37 ||  // Thrash
         moveId == 80;    // Petal Dance
}

// Explosion/Self-Destruct: two real Gen 1 quirks modeled here - the target's
// Defense is halved for this one hit (see computeDamage()'s moveId parameter),
// and the user faints as an unconditional side effect of using the move,
// even on a miss (see resolveAction() - applied right after the PP is spent,
// before the accuracy roll).
constexpr uint8_t SELF_DESTRUCT_MOVE_ID = 120;
constexpr uint8_t EXPLOSION_MOVE_ID = 153;
bool isSelfDestructMove(const uint8_t moveId) { return moveId == SELF_DESTRUCT_MOVE_ID || moveId == EXPLOSION_MOVE_ID; }

// The 5 real Gen 1 two-turn charge moves: turn 1 charges (no damage, see
// resolveAction()'s dispatch), turn 2 automatically releases the attack
// without the player/AI choosing again (BattleCombatant::forcedMoveId).
// Fly/Dig additionally grant semi-invulnerability during the charge turn
// (simplified to "everything just misses" - see
// BattleCombatant::invulnerable's doc comment).
struct TwoTurnTableEntry {
  uint8_t moveId;
  bool grantsInvulnerability;
};
constexpr TwoTurnTableEntry TWO_TURN_TABLE[] = {
    {19, true},    // Fly
    {91, true},    // Dig
    {76, false},   // Solar Beam
    {130, false},  // Skull Bash
    {143, false},  // Sky Attack
};
const TwoTurnTableEntry* twoTurnEntryForMove(const uint8_t moveId) {
  for (const TwoTurnTableEntry& entry : TWO_TURN_TABLE) {
    if (entry.moveId == moveId) return &entry;
  }
  return nullptr;
}

// The 4 real Gen 1 partial-trapping moves - already deal real damage via the
// normal formula (nonzero power in the move data), but on a successful first
// hit also lock the ATTACKER into automatically repeating the same move for
// 1-4 further turns (2-5 total, the same real Gen 1 duration distribution as
// rollMultiHitCount()) without a fresh accuracy roll - see resolveAction();
// BattleCombatant::forcedMoveId/forcedTurnsRemaining double as the storage
// for this, same as the two-turn moves above. Real Gen 1 traps BOTH sides at
// once, though: the TARGET is also immobilized for roughly the same
// duration (BattleCombatant::trappedTurnsRemaining) - see
// resolveGenericMoveEffect()'s dispatch and resolveAction()'s own
// trapped-immobilization check.
bool isTrapMove(const uint8_t moveId) {
  return moveId == 20 ||   // Bind
         moveId == 35 ||   // Wrap
         moveId == 83 ||   // Fire Spin
         moveId == 128;    // Clamp
}

constexpr uint8_t LEECH_SEED_MOVE_ID = 73;
constexpr uint8_t REFLECT_MOVE_ID = 115;
constexpr uint8_t LIGHT_SCREEN_MOVE_ID = 113;
constexpr uint8_t MIST_MOVE_ID = 54;
constexpr uint8_t FOCUS_ENERGY_MOVE_ID = 116;
constexpr uint8_t RECOVER_MOVE_ID = 105;
constexpr uint8_t SOFT_BOILED_MOVE_ID = 135;
constexpr uint8_t REST_MOVE_ID = 156;
// Whirlwind/Roar: force a switch. The engine has no idea whether the target
// actually has anywhere to switch TO (a wild Pokemon fleeing, a trainer's
// last team member, the player's own remaining party) - that's a roster
// concern PokemonActivity.cpp alone can answer - so this just reports
// BattleLogEvent::ForcedSwitch on a successful hit and leaves what actually
// happens to the caller.
constexpr uint8_t WHIRLWIND_MOVE_ID = 18;
constexpr uint8_t ROAR_MOVE_ID = 46;
constexpr uint8_t DISABLE_MOVE_ID = 50;
constexpr uint8_t SUBSTITUTE_MOVE_ID = 164;
// MIMIC_MOVE_ID (102) is declared publicly in PokemonBattle.h - see its own
// doc comment for why.
constexpr uint8_t METRONOME_MOVE_ID = 118;
constexpr uint8_t MIRROR_MOVE_MOVE_ID = 119;
constexpr uint8_t TRANSFORM_MOVE_ID = 144;
constexpr uint8_t CONVERSION_MOVE_ID = 160;

// True for the handful of moves whose effect needs context Metronome/Mirror
// Move can't supply indirectly - a real move slot to spend PP from and
// persist into (Mimic), the defender's actual moveset (Mimic), their own
// separate multi-turn state machine (Bide), or their own recursive dispatch
// (Metronome/Mirror Move themselves, and Transform/Conversion, whose
// battle-duration side effects don't make sense triggered by proxy).
// Excluded from Metronome's random pick and from what Mirror Move is willing
// to replay back.
bool isMoveCopyingOrSpecialMove(const uint8_t moveId) {
  return moveId == MIMIC_MOVE_ID || moveId == METRONOME_MOVE_ID || moveId == MIRROR_MOVE_MOVE_ID ||
         moveId == TRANSFORM_MOVE_ID || moveId == CONVERSION_MOVE_ID || moveId == BIDE_MOVE_ID;
}

// Rejection-samples a uniformly random real, selectable move id (1..
// MOVE_COUNT) for Metronome, retrying (bounded, so a pathological RandomSource
// can't loop forever) if it lands on Struggle or one of the special/move-
// copying moves above.
uint8_t pickRandomMetronomeMove(const RandomSource& random) {
  for (uint8_t attempt = 0; attempt < 20U; ++attempt) {
    uint32_t roll = 0;
    if (!rollBelow(random, MOVE_COUNT, roll)) return 0;
    const uint8_t candidate = static_cast<uint8_t>(roll + 1U);
    if (candidate == STRUGGLE_MOVE_ID || isMoveCopyingOrSpecialMove(candidate)) continue;
    return candidate;
  }
  return 0;
}

// A combatant's effective type for STAB/type-effectiveness purposes only -
// Conversion overrides it with whatever it copied from the opponent (see
// BattleCombatant::conversionType1's doc comment); every other combatant
// just uses its species' real type.
struct EffectiveTypes {
  PokemonType primary;
  PokemonType secondary;
};
EffectiveTypes effectiveTypesFor(const BattleCombatant& combatant, const SpeciesData& species) {
  if (combatant.conversionType1 != PokemonType::None) {
    return {combatant.conversionType1, combatant.conversionType2};
  }
  return {species.primaryType, species.secondaryType};
}

// Applies damage to whichever of `defender`'s two HP pools is currently
// active - a Substitute (if one is up) absorbs it instead of the real
// Pokemon, and a hit that would deal more than the Substitute's remaining
// HP just breaks it outright rather than overflowing onto currentHp,
// matching the real games.
void applyDamageRespectingSubstitute(BattleCombatant& defender, const uint16_t damage) {
  if (defender.substituteHp > 0) {
    defender.substituteHp = defender.substituteHp > damage ? static_cast<uint16_t>(defender.substituteHp - damage) : 0;
    return;
  }
  defender.currentHp = defender.currentHp > damage ? static_cast<uint16_t>(defender.currentHp - damage) : 0;
}

// Rage (move 99): raises the attacked combatant's own Attack stage by 1
// every time it takes nonzero damage while enraged (see
// BattleCombatant::enraged's doc comment) - called from both damage-
// application paths in resolveGenericMoveEffect() (the FIXED_DAMAGE_TABLE
// branch and the generic per-hit loop), same as the Bide-damage-
// accumulation check those already do.
void raiseAttackIfEnraged(BattleCombatant& target, const uint16_t damage) {
  if (!target.enraged || damage == 0) return;
  target.attackStage = std::clamp<int8_t>(static_cast<int8_t>(target.attackStage + 1), -6, 6);
}

int8_t& statStageRef(BattleCombatant& combatant, const StatKind stat) {
  switch (stat) {
    case StatKind::Attack:
      return combatant.attackStage;
    case StatKind::Defense:
      return combatant.defenseStage;
    case StatKind::Special:
      return combatant.specialStage;
    case StatKind::Speed:
      return combatant.speedStage;
    case StatKind::Accuracy:
      return combatant.accuracyStage;
    case StatKind::Evasion:
      return combatant.evasionStage;
  }
  return combatant.attackStage;  // unreachable - silences a missing-return warning
}

// Sleep/confusion durations: 1-3 turns average, kept short because a turn
// here also costs a full e-ink refresh.
uint8_t rollStatusDuration(const RandomSource& random, const uint8_t minTurns, const uint8_t maxTurns) {
  uint32_t roll = 0;
  if (!rollBelow(random, static_cast<uint32_t>(maxTurns - minTurns + 1U), roll)) return minTurns;
  return static_cast<uint8_t>(minTurns + roll);
}

// Real Gen 1 confusion self-hit power - a typeless 40-power physical hit
// computed via the normal damage formula (the confused Pokemon's own
// Attack vs its own Defense, at its own level), not a flat max-HP fraction.
// See statusPreventsAction()'s Confusion case below.
constexpr uint8_t CONFUSION_SELF_HIT_POWER = 40;

// Forward-declared so statusPreventsAction() (defined here, ahead of
// computeDamage() further down) can reuse the real damage formula for
// confusion's self-hit instead of a separate ad hoc calculation.
uint16_t computeDamage(const BattleCombatant& attacker, const BattleCombatant& defender, const MoveData& move,
                       uint8_t moveId, const RandomSource& random, bool critical);

// Non-volatile (Paralysis/Sleep/Freeze/Burn/Poison) vs. the volatile
// Confusion this engine folds into the same slot (see PokemonBattle.h).
bool statusPreventsAction(BattleCombatant& combatant, const RandomSource& random, BattleLogEvent& event) {
  switch (combatant.status) {
    case Ailment::Sleep:
      if (combatant.statusTurns > 0) {
        --combatant.statusTurns;
        event = BattleLogEvent::StatusPreventedMove;
        return true;
      }
      combatant.status = Ailment::None;
      event = BattleLogEvent::StatusCured;
      return false;
    case Ailment::Freeze:
      if (rollPercentChance(random, FREEZE_THAW_CHANCE_PERCENT)) {
        combatant.status = Ailment::None;
        event = BattleLogEvent::StatusCured;
        return false;
      }
      event = BattleLogEvent::StatusPreventedMove;
      return true;
    case Ailment::Paralysis:
      if (rollPercentChance(random, PARALYSIS_FAIL_CHANCE_PERCENT)) {
        event = BattleLogEvent::StatusPreventedMove;
        return true;
      }
      return false;
    case Ailment::Confusion:
      if (combatant.statusTurns > 0) --combatant.statusTurns;
      if (combatant.statusTurns == 0) {
        combatant.status = Ailment::None;
        event = BattleLogEvent::StatusCured;
        return false;
      }
      if (rollPercentChance(random, CONFUSION_SELF_HIT_CHANCE_PERCENT)) {
        // Real Gen 1: no STAB, no type-effectiveness, never a critical hit -
        // just the raw formula with a typeless 40-power hit against the
        // user's own stats.
        MoveData confusionHit{};
        confusionHit.type = PokemonType::None;
        confusionHit.power = CONFUSION_SELF_HIT_POWER;
        confusionHit.category = MoveCategory::Physical;
        const uint16_t selfDamage = computeDamage(combatant, combatant, confusionHit, 0, random, false);
        combatant.currentHp =
            combatant.currentHp > selfDamage ? static_cast<uint16_t>(combatant.currentHp - selfDamage) : 0;
        event = BattleLogEvent::ConfusionSelfHit;
        return true;
      }
      return false;
    case Ailment::None:
    case Ailment::Burn:
    case Ailment::Poison:
    case Ailment::All:
      return false;
  }
  return false;
}

void applyEndOfTurnStatusDamage(BattleCombatant& combatant, BattleLogEvent& event) {
  if (combatant.currentHp == 0) return;
  if (combatant.status != Ailment::Poison && combatant.status != Ailment::Burn) return;
  const uint16_t damage = clampToUint16(std::max<uint32_t>(1U, combatant.maxHp / STATUS_DAMAGE_FRACTION));
  combatant.currentHp = combatant.currentHp > damage ? static_cast<uint16_t>(combatant.currentHp - damage) : 0;
  event = BattleLogEvent::StatusDamage;
}

// Leech Seed: the seeded side loses 1/8 max HP at the end of the turn,
// healing whoever planted it (`otherSide`) by that same amount - unless
// otherSide has already fainted, in which case the drain still happens but
// there's no one left to receive it.
void applyLeechSeedDamage(BattleCombatant& seededSide, BattleCombatant& otherSide, BattleLogEvent& event) {
  if (!seededSide.seeded || seededSide.currentHp == 0) return;
  const uint16_t drained =
      std::min(seededSide.currentHp, clampToUint16(std::max<uint32_t>(1U, seededSide.maxHp / STATUS_DAMAGE_FRACTION)));
  seededSide.currentHp = static_cast<uint16_t>(seededSide.currentHp - drained);
  if (otherSide.currentHp > 0) {
    otherSide.currentHp =
        clampToUint16(std::min<uint32_t>(otherSide.maxHp, static_cast<uint32_t>(otherSide.currentHp) + drained));
  }
  event = BattleLogEvent::Seeded;
}

// Gen 1's real badge boost: a flat +12.5% (see BADGE_BOOST_ATTACK's doc
// comment in PokemonBattle.h for what this deliberately doesn't replicate).
uint16_t applyBadgeBoost(const uint16_t value, const uint8_t badgeBoostMask, const uint8_t bit) {
  if ((badgeBoostMask & bit) == 0) return value;
  return clampToUint16(static_cast<uint32_t>(value) + value / 8U);
}

uint16_t computeDamage(const BattleCombatant& attacker, const BattleCombatant& defender, const MoveData& move,
                       const uint8_t moveId, const RandomSource& random, const bool critical) {
  const SpeciesData* attackerSpecies = speciesData(attacker.speciesId);
  const SpeciesData* defenderSpecies = speciesData(defender.speciesId);
  const BaseStats* attackerStats = baseStatsFor(attacker.speciesId);
  const BaseStats* defenderStats = baseStatsFor(defender.speciesId);
  if (attackerSpecies == nullptr || defenderSpecies == nullptr || attackerStats == nullptr ||
      defenderStats == nullptr) {
    return 0;
  }

  const bool physical = move.category == MoveCategory::Physical;
  const uint8_t attackBase = physical ? attackerStats->attack : attackerStats->special;
  const uint8_t defenseBase = physical ? defenderStats->defense : defenderStats->special;
  const size_t attackStatIndex =
      physical ? static_cast<size_t>(StatIndex::Attack) : static_cast<size_t>(StatIndex::Special);
  const size_t defenseStatIndex =
      physical ? static_cast<size_t>(StatIndex::Defense) : static_cast<size_t>(StatIndex::Special);
  uint16_t attackWorking =
      battleWorkingStat(attackBase, attacker.level, attacker.iv[attackStatIndex], attacker.ev[attackStatIndex]);
  // Badge boost (Attack/Special, whichever this move actually uses) -
  // applied to the raw stat, same tier as Burn's halving just below.
  attackWorking = applyBadgeBoost(attackWorking, attacker.badgeBoostMask,
                                  physical ? BADGE_BOOST_ATTACK : BADGE_BOOST_SPECIAL);
  // Real Gen 1 quirk: Burn halves the burned Pokemon's Attack for physical
  // damage (the same way Paralysis halves Speed for turn order, see
  // stepBattle()) - applied to the raw stat before staging, so unlike a
  // negative stat stage, a critical hit does NOT bypass this.
  if (physical && attacker.status == Ailment::Burn) attackWorking = std::max<uint16_t>(1, attackWorking / 2U);
  uint16_t defenseWorking =
      battleWorkingStat(defenseBase, defender.level, defender.iv[defenseStatIndex], defender.ev[defenseStatIndex]);
  defenseWorking = applyBadgeBoost(defenseWorking, defender.badgeBoostMask,
                                   physical ? BADGE_BOOST_DEFENSE : BADGE_BOOST_SPECIAL);
  // Real Gen 1 quirk: Explosion/Self-Destruct halve the target's Defense for
  // this one hit.
  if (isSelfDestructMove(moveId)) defenseWorking = std::max<uint16_t>(1, defenseWorking / 2U);
  const uint16_t attackStaged =
      applyStatStage(attackWorking, physical ? attacker.attackStage : attacker.specialStage);
  const uint16_t defenseStaged =
      applyStatStage(defenseWorking, physical ? defender.defenseStage : defender.specialStage);
  // Real Gen 1 crit quirk: a critical hit ignores a stage that would hurt the
  // attacker - a negative Attack/Special stage on the attacker, or a
  // positive Defense/Special stage on the defender - while still applying
  // any stage that helps. Not an approximation; this is the actual rule.
  const uint16_t attackStat = critical ? std::max(attackWorking, attackStaged) : attackStaged;
  const uint16_t defenseStat =
      std::max<uint16_t>(1, critical ? std::min(defenseWorking, defenseStaged) : defenseStaged);

  uint32_t damage = ((2U * attacker.level / 5U + 2U) * move.power * attackStat) / (50U * defenseStat) + 2U;
  // A crit effectively doubles Gen 1's level term in the formula above -
  // mathematically equivalent to doubling the whole base result here, since
  // level only ever appears as that one multiplicative factor. No IVs/EVs are
  // modeled (see the Gen 1 authenticity roadmap), so there's nothing further
  // to account for there.
  if (critical) damage *= 2U;

  const EffectiveTypes attackerTypes = effectiveTypesFor(attacker, *attackerSpecies);
  const bool stab = move.type == attackerTypes.primary || move.type == attackerTypes.secondary;
  if (stab) damage = damage * STAB_BONUS_PERCENT / 100U;

  const EffectiveTypes defenderTypes = effectiveTypesFor(defender, *defenderSpecies);
  const uint16_t effectivenessPercent = typeEffectivenessPercent(move.type, defenderTypes.primary, defenderTypes.secondary);
  damage = damage * effectivenessPercent / 100U;
  if (damage == 0) return 0;

  // Reflect/Light Screen halve incoming Physical/Special damage respectively
  // - a critical hit bypasses both, matching the real games.
  if (!critical) {
    if (physical && defender.reflectActive) damage /= 2U;
    if (!physical && defender.lightScreenActive) damage /= 2U;
  }

  uint32_t randomPercent = 100U;
  uint32_t roll = 0;
  if (rollBelow(random, 100U - DAMAGE_RANDOM_MIN_PERCENT + 1U, roll)) randomPercent = DAMAGE_RANDOM_MIN_PERCENT + roll;
  damage = std::max<uint32_t>(1U, damage * randomPercent / 100U);
  return clampToUint16(damage);
}

BattleLogEvent effectivenessEvent(const uint16_t effectivenessPercent) {
  if (effectivenessPercent == 0) return BattleLogEvent::MoveNoEffect;
  if (effectivenessPercent > 100) return BattleLogEvent::MoveSuperEffective;
  if (effectivenessPercent < 100) return BattleLogEvent::MoveNotVeryEffective;
  return BattleLogEvent::MoveHit;
}

// Forward-declared so resolveAction() (and this function's own recursive
// Metronome/Mirror Move calls) can call it ahead of its definition below.
void resolveGenericMoveEffect(BattleCombatant& attacker, BattleCombatant& defender, uint8_t moveId,
                              const MoveData* move, const RandomSource& random, bool allowMultiTurnLock,
                              uint8_t moveSlotOfMimicUser, BattleActionResult& result);

BattleActionResult resolveAction(BattleCombatant& attacker, BattleCombatant& defender, const uint8_t moveSlot,
                                 const RandomSource& random) {
  BattleActionResult result{};
  result.acted = true;
  result.moveSlot = moveSlot;

  // Hyper Beam's recharge turn takes priority over everything else below -
  // no move selection even matters this turn, matching the real games.
  if (attacker.mustRecharge) {
    attacker.mustRecharge = false;
    result.event = BattleLogEvent::MustRecharge;
    return result;
  }
  // A Wrap/Bind/Fire Spin/Clamp target is fully immobilized while trapped -
  // checked next (ahead of flinch/status), same reasoning as mustRecharge
  // above: nothing else about this turn's chosen move matters.
  if (attacker.trappedTurnsRemaining > 0) {
    result.event = BattleLogEvent::Trapped;
    return result;
  }

  // moveSlot >= BATTLE_MOVE_SLOTS is the Struggle sentinel (STRUGGLE_MOVE_ID's
  // doc comment) rather than a real slot index - both stepBattle()'s player
  // path (once every learned move is out of PP, PokemonActivity.cpp forces
  // this) and chooseOpponentMoveSlot() (once the AI has no usable move
  // either) can hand this in. There's no BattleMoveSlot/PP to touch for it -
  // Struggle isn't a learned move and never runs out.
  const bool forcedStruggle = moveSlot >= BATTLE_MOVE_SLOTS;
  BattleMoveSlot* slot = nullptr;
  uint8_t moveId = STRUGGLE_MOVE_ID;
  if (!forcedStruggle) {
    slot = &attacker.moves[moveSlot];
    if (slot->moveId == 0 || slot->currentPp == 0) {
      result.event = BattleLogEvent::MoveHadNoPp;
      return result;
    }
    moveId = slot->moveId;
  }

  // Flinch (Stomp, Bite, ...) takes priority over even a status check below -
  // like paralysis/sleep, it skips the move entirely (no PP spent), but it's
  // its own transient per-turn flag rather than a persisted Ailment.
  if (attacker.flinched) {
    attacker.flinched = false;
    result.event = BattleLogEvent::Flinched;
    return result;
  }

  if (statusPreventsAction(attacker, random, result.event)) return result;

  const MoveData* move = moveData(moveId);
  if (move == nullptr) {
    result.event = BattleLogEvent::MoveHadNoPp;
    return result;
  }
  // Multi-turn moves (Bide, two-turn charge moves, trapping moves) only
  // spend PP on the turn the player/AI actually chooses them - every
  // automatic follow-up turn re-executes the same move for free, matching
  // the real games. isReleasingTwoTurn/isContinuingTrap detect "this is a
  // follow-up turn, not a fresh use" by checking forcedMoveId; isContinuingBide
  // is simpler since Bide never lets go of the move id across its 2 turns.
  const bool isContinuingBide = moveId == BIDE_MOVE_ID && attacker.bideTurnsRemaining > 0;
  const TwoTurnTableEntry* twoTurn = twoTurnEntryForMove(moveId);
  const bool isReleasingTwoTurn = twoTurn != nullptr && attacker.forcedMoveId == moveId;
  const bool isContinuingTrap = isTrapMove(moveId) && attacker.forcedMoveId == moveId;
  const bool isContinuingThrash = isThrashMove(moveId) && attacker.forcedMoveId == moveId;
  if (!isContinuingBide && !isReleasingTwoTurn && !isContinuingTrap && !isContinuingThrash && slot != nullptr) {
    --slot->currentPp;
  }

  // Bide: turn 1 (and 2) just brace, storing whatever damage lands in the
  // meantime (see the generic damage block's accumulateBideDamage() call);
  // the turn the countdown reaches 0, it unleashes double that back,
  // ignoring type effectiveness/accuracy/crit entirely - same "raw
  // reflected damage" precedent as Counter.
  if (moveId == BIDE_MOVE_ID) {
    if (attacker.bideTurnsRemaining == 0) {
      attacker.bideDamageStored = 0;
      attacker.bideTurnsRemaining = 2;
    }
    // Decrements on the SAME turn it's (re)armed too, so "2 turns" means
    // exactly 2 total activations (brace, then release) rather than 3.
    --attacker.bideTurnsRemaining;
    if (attacker.bideTurnsRemaining > 0) {
      result.event = BattleLogEvent::ChargingMove;
      return result;
    }
    const uint16_t bideDamage = clampToUint16(static_cast<uint32_t>(attacker.bideDamageStored) * 2U);
    applyDamageRespectingSubstitute(defender, bideDamage);
    result.event = bideDamage > 0 ? BattleLogEvent::MoveHit : BattleLogEvent::MoveNoEffect;
    return result;
  }

  // Two-turn charge moves: the first use just charges (no damage/accuracy
  // roll at all this turn), setting up the automatic release next turn; the
  // release turn clears the forced state and falls through to the normal
  // resolution below, exactly like any other attack.
  if (twoTurn != nullptr && attacker.forcedMoveId != moveId) {
    attacker.forcedMoveId = moveId;
    attacker.forcedTurnsRemaining = 1;
    attacker.invulnerable = twoTurn->grantsInvulnerability;
    result.event = BattleLogEvent::ChargingMove;
    return result;
  }
  if (isReleasingTwoTurn) {
    attacker.forcedMoveId = 0;
    attacker.forcedTurnsRemaining = 0;
    attacker.invulnerable = false;
  }

  // Explosion/Self-Destruct: the user faints as an unconditional side effect
  // of using the move - a real Gen 1 quirk, not merely "recoil after a hit."
  // This applies even on a miss, so it happens here, before the accuracy
  // roll below, rather than alongside the damage-dealing block.
  if (isSelfDestructMove(moveId)) attacker.currentHp = 0;

  // moveSlotOfMimicUser threads through which real slot (if any) this action
  // started from - Mimic needs to know which of its own slots to overwrite,
  // and it's also forwarded into a Metronome/Mirror Move-redirected
  // recursive call so a redirected Mimic (excluded, see
  // isMoveCopyingOrSpecialMove()) would still have it available in
  // principle. BATTLE_MOVE_SLOTS itself means "no real slot" (a forced
  // Struggle, or already inside a redirect).
  resolveGenericMoveEffect(attacker, defender, moveId, move, random, /*allowMultiTurnLock=*/true,
                           forcedStruggle ? BATTLE_MOVE_SLOTS : moveSlot, result);
  return result;
}

// The shared tail of resolveAction(): everything from the invulnerability/
// accuracy check through damage/ailment resolution, parameterized directly
// on `moveId`/`move` rather than a slot index. Metronome and Mirror Move
// both invoke this recursively for whatever move they end up executing,
// bypassing resolveAction()'s own slot lookup/PP spend/Bide/two-turn-charge
// preamble entirely - see their dispatch branches below.
// `allowMultiTurnLock` is false for such a redirected call: a trapping move
// picked this way has no real slot for the attacker to keep "choosing" it
// from on a follow-up turn, so it only ever deals its one hit (a documented
// simplification). `moveSlotOfMimicUser` is BATTLE_MOVE_SLOTS (no real slot)
// for a redirected call.
void resolveGenericMoveEffect(BattleCombatant& attacker, BattleCombatant& defender, const uint8_t moveId,
                              const MoveData* move, const RandomSource& random, const bool allowMultiTurnLock,
                              const uint8_t moveSlotOfMimicUser, BattleActionResult& result) {
  // Mirror Move's memory: tracks the last real move id used against
  // `defender`, regardless of hit/miss - see lastMoveUsedAgainstMe's doc
  // comment. Recorded here (rather than in resolveAction()'s preamble) so a
  // Metronome/Mirror Move redirect naturally overwrites this with the real
  // underlying move actually executed, not the wrapper move's own id.
  defender.lastMoveUsedAgainstMe = moveId;

  // Rage: using any other move cancels it; using Rage (re)activates it -
  // see BattleCombatant::enraged's doc comment. A redirected Metronome/
  // Mirror Move call re-executes this same line with the real underlying
  // moveId, so it naturally cancels/activates correctly there too.
  attacker.enraged = moveId == RAGE_MOVE_ID;

  const bool isContinuingTrap = isTrapMove(moveId) && attacker.forcedMoveId == moveId;
  const bool isContinuingThrash = isThrashMove(moveId) && attacker.forcedMoveId == moveId;
  const FixedDamageTableEntry* fixedDamage = fixedDamageEntryForMove(moveId);
  const bool isOhko = fixedDamage != nullptr && fixedDamage->kind == FixedDamageKind::Ohko;

  // Fly/Dig's semi-invulnerable charge turn: simplified to "everything just
  // misses" regardless of accuracy/OHKO rules, rather than modeling the
  // specific real-game exceptions (Swift, Earthquake-vs-Dig, ...).
  if (defender.invulnerable) {
    result.event = BattleLogEvent::MoveMissed;
    return;
  }

  // Dream Eater's real Gen 1 quirk: fails outright, no accuracy roll or
  // damage at all, unless the target is currently asleep.
  if (moveId == DREAM_EATER_MOVE_ID && defender.status != Ailment::Sleep) {
    result.event = BattleLogEvent::MoveFailed;
    return;
  }

  // accuracy == 0 in this dataset means "never misses" (Swift, Aerial Ace-style
  // moves, and also how Struggle's own accuracy is recorded) - stat stages
  // never apply to those either, matching the real games. A trapping move's
  // automatic follow-up turns always hit - no fresh accuracy roll once
  // locked in, a documented simplification of the real games' own repeat
  // roll.
  if (isContinuingTrap) {
    // fall straight through to damage resolution below
  } else if (isOhko) {
    // Real Gen 1 OHKO accuracy: always misses if the user's level is lower
    // than the target's, otherwise hit chance is the move's listed accuracy
    // (30) plus the level difference - a Pokemon 20 levels higher connects
    // essentially every time. Stat stages never apply to this roll.
    if (attacker.level < defender.level) {
      result.event = BattleLogEvent::MoveMissed;
      return;
    }
    const int32_t hitChance = std::clamp<int32_t>(
        static_cast<int32_t>(move->accuracy) + (static_cast<int32_t>(attacker.level) - defender.level), 0, 100);
    uint32_t accuracyRoll = 0;
    if (rollBelow(random, 100U, accuracyRoll) && accuracyRoll >= static_cast<uint32_t>(hitChance)) {
      result.event = BattleLogEvent::MoveMissed;
      return;
    }
  } else if (move->accuracy != 0) {
    const int8_t combinedStage =
        std::clamp<int8_t>(attacker.accuracyStage - defender.evasionStage, -6, 6);
    const uint32_t effectiveAccuracy = applyAccuracyEvasionStage(move->accuracy, combinedStage);
    uint32_t accuracyRoll = 0;
    if (rollBelow(random, 100U, accuracyRoll) && accuracyRoll >= effectiveAccuracy) {
      result.event = BattleLogEvent::MoveMissed;
      return;
    }
  }

  // Baseline for a successful, non-missed use; damaging moves refine this to
  // an effectiveness-specific event below. Status moves keep this baseline
  // so the ailment block's "did it actually do something" upgrade below has
  // a MoveHit to promote to InflictedStatus instead of silently staying None.
  result.event = BattleLogEvent::MoveHit;

  if (move->category != MoveCategory::Status) {
    const BaseStats* attackerStats = baseStatsFor(attacker.speciesId);
    const SpeciesData* defenderSpecies = speciesData(defender.speciesId);
    uint16_t effectivenessPercent = 100;
    if (defenderSpecies != nullptr) {
      const EffectiveTypes defenderTypes = effectiveTypesFor(defender, *defenderSpecies);
      effectivenessPercent = typeEffectivenessPercent(move->type, defenderTypes.primary, defenderTypes.secondary);
    }

    if (fixedDamage != nullptr) {
      // These moves skip the normal level/power/Attack/Defense formula
      // entirely (see FIXED_DAMAGE_TABLE's doc comment) - no crit, no STAB,
      // no multi-hit, no random 85-100% damage roll - but still respect type
      // immunity (0% effectiveness) the same way the real games do, e.g.
      // Ghost blocks Guillotine/Horn Drill, Flying blocks Fissure.
      bool noEffect = effectivenessPercent == 0;
      uint32_t damage = 0;
      if (!noEffect) {
        switch (fixedDamage->kind) {
          case FixedDamageKind::Ohko:
            damage = defender.currentHp;
            result.event = BattleLogEvent::OneHitKo;
            break;
          case FixedDamageKind::LevelDamage:
            damage = attacker.level;
            break;
          case FixedDamageKind::FlatDamage:
            damage = fixedDamage->flatAmount;
            break;
          case FixedDamageKind::Psywave: {
            // Real Gen 1: random damage from 1 up to 1.5x the user's level.
            const uint32_t upperExclusive = std::max<uint32_t>(1U, static_cast<uint32_t>(attacker.level) * 3U / 2U);
            uint32_t roll = 0;
            damage = (rollBelow(random, upperExclusive, roll) ? roll : 0) + 1U;
            break;
          }
          case FixedDamageKind::HalveDefenderHp:
            damage = std::max<uint32_t>(1U, defender.currentHp / 2U);
            break;
          case FixedDamageKind::Counter:
            // Only succeeds if this attacker already took physical damage
            // earlier this same turn (the opponent moved first and hit with
            // a physical move) - "But it failed!" otherwise. No chaining off
            // a Counter itself, since lastPhysicalDamageTaken is reset at the
            // top of every turn (see stepBattle()/stepOpponentOnlyTurn()) and
            // only ever set again by that turn's own physical hits.
            if (attacker.lastPhysicalDamageTaken == 0) {
              noEffect = true;
              result.event = BattleLogEvent::MoveFailed;
            } else {
              damage = static_cast<uint32_t>(attacker.lastPhysicalDamageTaken) * 2U;
            }
            break;
        }
      }
      if (noEffect && result.event != BattleLogEvent::MoveFailed) result.event = BattleLogEvent::MoveNoEffect;
      const uint16_t clampedDamage = clampToUint16(damage);
      applyDamageRespectingSubstitute(defender, clampedDamage);
      raiseAttackIfEnraged(defender, clampedDamage);
      if (move->category == MoveCategory::Physical && clampedDamage > 0) {
        defender.lastPhysicalDamageTaken = clampedDamage;
      }
    } else {
      const uint8_t effectivePower = moveId == LOW_KICK_MOVE_ID ? LOW_KICK_SIMPLIFIED_POWER : move->power;
      MoveData effectiveMove = *move;
      effectiveMove.power = effectivePower;

      // A move that's immune (0% effectiveness) never gets to try more than
      // once - real Gen 1 shows "doesn't affect" a single time, not per hit.
      const MultiHitTableEntry* multiHit = effectivenessPercent == 0 ? nullptr : multiHitEntryForMove(moveId);
      const uint8_t hitsToAttempt =
          multiHit == nullptr ? 1 : multiHit->fixedHits != 0 ? multiHit->fixedHits : rollMultiHitCount(random);

      // Snapshotted before any hit lands - a trapping move's own damage can
      // break the defender's Substitute in this same action, but the trap
      // should still be judged against whatever was true when the hit
      // landed (see isTrapMove(moveId)'s dispatch below), not the
      // just-broken aftermath.
      const bool defenderHadSubstitute = defender.substituteHp > 0;
      bool anyCritical = false;
      uint8_t hitsLanded = 0;
      uint32_t totalDamage = 0;
      for (uint8_t hit = 0; hit < hitsToAttempt && defender.currentHp > 0; ++hit) {
        // Dire Hit (a battle-boost item) raises the user's own crit ratio to
        // the same high-crit tier a move like Slash gets, for the rest of
        // the battle - stacks with (rather than doubling past) an
        // already-high-crit move.
        const bool critical =
            attackerStats != nullptr &&
            rollCriticalHit(attackerStats->speed, isHighCritRatioMove(moveId) || attacker.direHitActive, random);
        const uint16_t damage = computeDamage(attacker, defender, effectiveMove, moveId, random, critical);
        applyDamageRespectingSubstitute(defender, damage);
        raiseAttackIfEnraged(defender, damage);
        totalDamage += damage;
        // Bide (move 117) accumulates whatever damage its user takes while
        // bracing - only from this generic power-based path, not the
        // FIXED_DAMAGE_TABLE moves (Counter, OHKO, ...), a scoped
        // simplification for a fairly rare combination either way.
        if (defender.bideTurnsRemaining > 0) {
          defender.bideDamageStored = clampToUint16(static_cast<uint32_t>(defender.bideDamageStored) + damage);
        }
        if (critical) anyCritical = true;
        ++hitsLanded;
      }

      result.event = effectivenessEvent(effectivenessPercent);
      if (totalDamage == 0 && effectivenessPercent != 0) result.event = BattleLogEvent::MoveHit;
      // Immune (0% effectiveness) hits never actually land, so there's nothing
      // to have been "critical" about even if a roll succeeded.
      result.critical = anyCritical && effectivenessPercent != 0;
      result.hitCount = multiHit != nullptr && effectivenessPercent != 0 ? hitsLanded : 0;
      if (move->category == MoveCategory::Physical && totalDamage > 0) {
        defender.lastPhysicalDamageTaken = clampToUint16(totalDamage);
      }

      // Recoil is based on the *total* damage this action dealt (all hits
      // summed, for the rare case a recoil move were ever also multi-hit -
      // none of Take Down/Double-Edge/Submission/Struggle actually are, but the
      // formula generalizes), floored, minimum 1 if any damage landed at all.
      if (const RecoilTableEntry* recoil = recoilEntryForMove(moveId); recoil != nullptr && totalDamage > 0) {
        const uint16_t recoilDamage =
            clampToUint16(std::max<uint32_t>(1U, totalDamage * recoil->recoilNumerator / recoil->recoilDenominator));
        attacker.currentHp =
            attacker.currentHp > recoilDamage ? static_cast<uint16_t>(attacker.currentHp - recoilDamage) : 0;
        result.recoilApplied = true;
      }

      // Absorb/Mega Drain/Leech Life/Dream Eater: heal half the damage dealt
      // (minimum 1), on top of whatever else the hit already did.
      if (isDrainMove(moveId) && totalDamage > 0) {
        const uint16_t healAmount = clampToUint16(std::max<uint32_t>(1U, totalDamage / 2U));
        attacker.currentHp = clampToUint16(
            std::min<uint32_t>(attacker.maxHp, static_cast<uint32_t>(attacker.currentHp) + healAmount));
        result.drainApplied = true;
      }

      // Stomp/Bite/... : a chance to flinch the target, preventing its next
      // action - only if the hit actually landed (not immune) and the target
      // is still standing (a fainted target has nothing left to flinch).
      if (const FlinchTableEntry* flinch = flinchEntryForMove(moveId);
          flinch != nullptr && effectivenessPercent != 0 && totalDamage > 0 && defender.currentHp > 0 &&
          defender.substituteHp == 0 && rollPercentChance(random, flinch->chancePercent)) {
        defender.flinched = true;
      }

      // Wrap/Bind/Fire Spin/Clamp: a successful first hit locks the
      // ATTACKER into repeating this same move automatically for 1-4 more
      // turns (2-5 total - the same real Gen 1 duration distribution
      // rollMultiHitCount() already models). isContinuingTrap turns just
      // count down; the lock releases early if the target faints.
      if (isTrapMove(moveId)) {
        // allowMultiTurnLock is false for a Metronome/Mirror Move-redirected
        // trap move: there's no real slot for the attacker to keep
        // "choosing" it from on a follow-up turn, so it just deals its one
        // hit instead of locking the attacker in - a documented
        // simplification.
        if (allowMultiTurnLock && !isContinuingTrap && effectivenessPercent != 0 && totalDamage > 0 &&
            defender.currentHp > 0) {
          attacker.forcedMoveId = moveId;
          attacker.forcedTurnsRemaining = static_cast<uint8_t>(rollMultiHitCount(random) - 1U);
          // Real Gen 1 traps BOTH sides at once: the target is immobilized
          // for roughly the same duration the attacker keeps auto-repeating
          // (+1, same off-by-one reasoning as Disable's own duration, so it
          // survives finishTurn()'s unconditional same-turn decrement rather
          // than losing a turn to it). A Substitute blocks this the same
          // way it already blocks status/stat-lowering/flinch/Leech Seed -
          // the hit only ever touched the decoy, never the real Pokemon
          // (checked against the pre-hit snapshot, since this same hit can
          // break the substitute without that retroactively un-blocking it).
          if (!defenderHadSubstitute) {
            defender.trappedTurnsRemaining = static_cast<uint8_t>(attacker.forcedTurnsRemaining + 1U);
          }
        } else if (isContinuingTrap) {
          if (attacker.forcedTurnsRemaining > 0) --attacker.forcedTurnsRemaining;
          if (attacker.forcedTurnsRemaining == 0 || defender.currentHp == 0) attacker.forcedMoveId = 0;
        }
      }

      // Thrash/Petal Dance: a successful use locks the ATTACKER into
      // repeating this same move automatically for 1-2 more turns (2-3
      // total), same forcedMoveId/forcedTurnsRemaining mechanism the trap
      // moves above use - but unlike those, each repeat still rolls
      // accuracy normally (no auto-hit), and the user becomes confused the
      // instant the lock ends, matching the real games.
      if (isThrashMove(moveId)) {
        if (allowMultiTurnLock && !isContinuingThrash) {
          attacker.forcedMoveId = moveId;
          attacker.forcedTurnsRemaining = rollStatusDuration(random, 1, 2);
        } else if (isContinuingThrash) {
          if (attacker.forcedTurnsRemaining > 0) --attacker.forcedTurnsRemaining;
          if (attacker.forcedTurnsRemaining == 0) {
            attacker.forcedMoveId = 0;
            if (attacker.status == Ailment::None) {
              attacker.status = Ailment::Confusion;
              attacker.statusTurns = rollStatusDuration(random, 2, 4);
            }
          }
        }
      }
    }
  }

  // Hyper Beam: reaching this point means the move already didn't miss (a
  // miss returns early above), so a real hit - including one that's immune
  // for 0 damage - always forces a recharge next turn, matching the real
  // games.
  if (moveId == HYPER_BEAM_MOVE_ID) attacker.mustRecharge = true;

  if (moveId == HAZE_MOVE_ID) {
    resetBattleStages(attacker);
    resetBattleStages(defender);
    result.event = BattleLogEvent::StatsReset;
  } else if (const StatChangeEffect* statEffect = statChangeForMove(moveId); statEffect != nullptr) {
    BattleCombatant& target = statEffect->targetsSelf ? attacker : defender;
    // Guard Spec. (a battle-boost item) blocks an opponent's stat-lowering
    // move from affecting whoever activated it - the same "no change, no
    // further effect" outcome as already being at the -6 floor, so this
    // reuses StatChangeFailed rather than adding a dedicated message.
    if (!statEffect->targetsSelf && statEffect->stages < 0 && (target.guardSpecActive || target.substituteHp > 0)) {
      // A Substitute blocks an opponent's stat-lowering move the same way
      // Guard Spec. does - it's the decoy that would be affected, not the
      // real Pokemon, so nothing happens.
      result.event = BattleLogEvent::StatChangeFailed;
    } else {
      int8_t& stage = statStageRef(target, statEffect->stat);
      const int8_t before = stage;
      stage = std::clamp<int8_t>(static_cast<int8_t>(stage + statEffect->stages), -6, 6);
      result.event = stage == before ? BattleLogEvent::StatChangeFailed
                     : statEffect->stages > 0 ? BattleLogEvent::StatRaised
                                              : BattleLogEvent::StatLowered;
    }
  } else if (moveId == REFLECT_MOVE_ID || moveId == LIGHT_SCREEN_MOVE_ID) {
    bool& active = moveId == REFLECT_MOVE_ID ? attacker.reflectActive : attacker.lightScreenActive;
    if (active) {
      result.event = BattleLogEvent::MoveNoEffect;
    } else {
      active = true;
      result.event = BattleLogEvent::BuffApplied;
    }
  } else if (moveId == MIST_MOVE_ID || moveId == FOCUS_ENERGY_MOVE_ID) {
    // Mist and Focus Energy reuse the exact same battle-boost-item fields as
    // Guard Spec./Dire Hit (guardSpecActive/direHitActive) - both pairs do
    // the identical thing (block the user's own stats from being lowered;
    // raise the user's own crit ratio to the high-crit tier), just reached
    // via a move instead of an item.
    bool& active = moveId == MIST_MOVE_ID ? attacker.guardSpecActive : attacker.direHitActive;
    if (active) {
      result.event = BattleLogEvent::MoveNoEffect;
    } else {
      active = true;
      result.event = BattleLogEvent::BuffApplied;
    }
  } else if (moveId == RECOVER_MOVE_ID || moveId == SOFT_BOILED_MOVE_ID) {
    if (attacker.currentHp >= attacker.maxHp) {
      result.event = BattleLogEvent::MoveNoEffect;
    } else {
      const uint16_t healAmount = clampToUint16(std::max<uint32_t>(1U, attacker.maxHp / 2U));
      attacker.currentHp =
          clampToUint16(std::min<uint32_t>(attacker.maxHp, static_cast<uint32_t>(attacker.currentHp) + healAmount));
      result.drainApplied = true;  // reuses the "It regained health!" clause
    }
  } else if (moveId == REST_MOVE_ID) {
    attacker.currentHp = attacker.maxHp;
    attacker.status = Ailment::Sleep;
    // Rest's real Gen 1 duration is a fixed 2 turns, unlike the random 1-3
    // (simplified from the real 1-7) this engine rolls for every other
    // sleep-inducing move.
    attacker.statusTurns = 2;
    result.event = BattleLogEvent::InflictedStatus;
    result.drainApplied = true;
  } else if (moveId == LEECH_SEED_MOVE_ID) {
    // Grass-type targets are immune to Leech Seed in the real games (a
    // Conversion-converted Grass type included - see effectiveTypesFor()).
    const SpeciesData* defenderSpecies = speciesData(defender.speciesId);
    bool isGrassType = false;
    if (defenderSpecies != nullptr) {
      const EffectiveTypes defenderTypes = effectiveTypesFor(defender, *defenderSpecies);
      isGrassType = defenderTypes.primary == PokemonType::Grass || defenderTypes.secondary == PokemonType::Grass;
    }
    if (isGrassType || defender.seeded || defender.substituteHp > 0) {
      result.event = BattleLogEvent::MoveNoEffect;
    } else {
      defender.seeded = true;
      result.event = BattleLogEvent::Seeded;
    }
  } else if (moveId == WHIRLWIND_MOVE_ID || moveId == ROAR_MOVE_ID) {
    result.event = BattleLogEvent::ForcedSwitch;
  } else if (moveId == DISABLE_MOVE_ID) {
    // Picks a random one of the target's moves that still has PP and isn't
    // already disabled - fails with no effect if none qualify.
    uint8_t candidates[BATTLE_MOVE_SLOTS];
    uint8_t candidateCount = 0;
    for (uint8_t index = 0; index < BATTLE_MOVE_SLOTS; ++index) {
      if (defender.moves[index].moveId == 0 || defender.moves[index].currentPp == 0) continue;
      if (defender.disableTurnsRemaining > 0 && defender.disabledMoveSlot == index) continue;
      candidates[candidateCount++] = index;
    }
    if (candidateCount == 0) {
      result.event = BattleLogEvent::MoveNoEffect;
    } else {
      uint32_t pick = 0;
      if (!rollBelow(random, candidateCount, pick)) pick = 0;
      defender.disabledMoveSlot = candidates[pick];
      // Simplified from the real 1-7 turns to 1-3, the same range this
      // engine already uses for Sleep.
      // +1: finishTurn() unconditionally counts every disable down once per
      // turn (including this one, since Disable itself doesn't get a
      // special exemption there), so this keeps the roll's real 1-3 meaning
      // "turns disabled AFTER this one" rather than losing a turn to the
      // immediate same-turn decrement.
      defender.disableTurnsRemaining = static_cast<uint8_t>(rollStatusDuration(random, 1, 3) + 1U);
      result.event = BattleLogEvent::MoveDisabled;
    }
  } else if (moveId == SUBSTITUTE_MOVE_ID) {
    // Costs 1/4 of the user's own max HP (minimum 1) - fails if one is
    // already up, or the user doesn't have enough HP left to spare.
    const uint16_t cost = std::max<uint16_t>(1, static_cast<uint16_t>(attacker.maxHp / 4U));
    if (attacker.substituteHp > 0 || attacker.currentHp <= cost) {
      result.event = BattleLogEvent::MoveNoEffect;
    } else {
      attacker.currentHp = static_cast<uint16_t>(attacker.currentHp - cost);
      attacker.substituteHp = cost;
      result.event = BattleLogEvent::SubstituteUp;
    }
  } else if (moveId == MIMIC_MOVE_ID) {
    // Picks one of the defender's known moves at random and temporarily
    // overwrites the slot Mimic itself was used from - real Gen 1 sets that
    // copy's PP to 5 (capped by the copied move's own base PP if lower,
    // e.g. a 5-PP move stays 5); the slot reverts to Mimic once the battle
    // ends or this combatant switches out (see mimicActive's doc comment
    // and PokemonActivity::savePlayerBattleEntry()).
    std::array<uint8_t, BATTLE_MOVE_SLOTS> candidates{};
    uint8_t candidateCount = 0;
    for (uint8_t index = 0; index < BATTLE_MOVE_SLOTS; ++index) {
      if (defender.moves[index].moveId == 0 || defender.moves[index].moveId == MIMIC_MOVE_ID) continue;
      candidates[candidateCount++] = defender.moves[index].moveId;
    }
    if (candidateCount == 0 || moveSlotOfMimicUser == BATTLE_MOVE_SLOTS) {
      result.event = BattleLogEvent::MoveNoEffect;
    } else {
      uint32_t pick = 0;
      if (!rollBelow(random, candidateCount, pick)) pick = 0;
      const uint8_t copiedMoveId = candidates[pick];
      const MoveData* copiedMove = moveData(copiedMoveId);
      const uint8_t copiedPp = copiedMove == nullptr ? 0 : std::min<uint8_t>(copiedMove->pp, 5U);
      attacker.mimicOriginalPp = attacker.moves[moveSlotOfMimicUser].currentPp;
      attacker.moves[moveSlotOfMimicUser].moveId = copiedMoveId;
      attacker.moves[moveSlotOfMimicUser].currentPp = copiedPp;
      attacker.mimicActive = true;
      attacker.mimicSlot = moveSlotOfMimicUser;
      result.event = BattleLogEvent::MimicCopied;
      result.redirectedMoveId = copiedMoveId;
    }
  } else if (moveId == TRANSFORM_MOVE_ID) {
    // Copies the opponent's species (and so its type and, combined with the
    // attacker's own unchanged level, its Attack/Defense/Special/Speed via
    // the normal stat formula), current stat stages, and moveset (each copied
    // move gets 5 PP, or its own base PP if lower - the real Gen 1 rule).
    // HP, level, and status are deliberately left untouched, matching the
    // real games; never persisted (see PokemonActivity::
    // savePlayerBattleEntry()) since it only lasts this one battle.
    const SpeciesData* defenderSpecies = speciesData(defender.speciesId);
    if (attacker.transformed || defenderSpecies == nullptr) {
      result.event = BattleLogEvent::MoveFailed;
    } else {
      attacker.speciesId = defender.speciesId;
      attacker.attackStage = defender.attackStage;
      attacker.defenseStage = defender.defenseStage;
      attacker.specialStage = defender.specialStage;
      attacker.speedStage = defender.speedStage;
      attacker.accuracyStage = defender.accuracyStage;
      attacker.evasionStage = defender.evasionStage;
      for (uint8_t index = 0; index < BATTLE_MOVE_SLOTS; ++index) {
        attacker.moves[index].moveId = defender.moves[index].moveId;
        const MoveData* copiedMove = moveData(defender.moves[index].moveId);
        attacker.moves[index].currentPp = copiedMove == nullptr ? 0 : std::min<uint8_t>(copiedMove->pp, 5U);
        attacker.ppUp[index] = 0;
      }
      attacker.mimicActive = false;
      attacker.transformed = true;
      result.event = BattleLogEvent::Transformed;
    }
  } else if (moveId == CONVERSION_MOVE_ID) {
    // Copies the defender's current effective type onto the attacker for
    // the rest of the battle - see BattleCombatant::conversionType1's doc
    // comment and effectiveTypesFor().
    const SpeciesData* defenderSpecies = speciesData(defender.speciesId);
    if (defenderSpecies == nullptr) {
      result.event = BattleLogEvent::MoveNoEffect;
    } else {
      const EffectiveTypes defenderTypes = effectiveTypesFor(defender, *defenderSpecies);
      attacker.conversionType1 = defenderTypes.primary;
      attacker.conversionType2 = defenderTypes.secondary;
      result.event = BattleLogEvent::ConversionApplied;
    }
  } else if (moveId == METRONOME_MOVE_ID) {
    // Picks a uniformly random real move (excluding Struggle and the other
    // move-copying/special moves - see pickRandomMetronomeMove()) and
    // executes its full effect directly, without spending any PP beyond
    // Metronome's own. A two-turn charge move or trapping move picked this
    // way just resolves as a single instant hit rather than starting its
    // usual multi-turn state machine - see allowMultiTurnLock's uses above
    // and the two-turn charge check earlier in resolveAction(), which this
    // recursive call bypasses entirely.
    const uint8_t pickedMoveId = pickRandomMetronomeMove(random);
    const MoveData* pickedMove = pickedMoveId == 0 ? nullptr : moveData(pickedMoveId);
    if (pickedMove == nullptr) {
      result.event = BattleLogEvent::MoveNoEffect;
    } else {
      result.redirectedMoveId = pickedMoveId;
      resolveGenericMoveEffect(attacker, defender, pickedMoveId, pickedMove, random,
                               /*allowMultiTurnLock=*/false, moveSlotOfMimicUser, result);
    }
  } else if (moveId == MIRROR_MOVE_MOVE_ID) {
    // Replays the last real move used against this attacker, if anything
    // qualifies (see lastMoveUsedAgainstMe's doc comment and
    // isMoveCopyingOrSpecialMove()) - "But it failed!" otherwise.
    const uint8_t mirroredMoveId = attacker.lastMoveUsedAgainstMe;
    const MoveData* mirroredMove =
        mirroredMoveId == 0 || isMoveCopyingOrSpecialMove(mirroredMoveId) ? nullptr : moveData(mirroredMoveId);
    if (mirroredMove == nullptr) {
      result.event = BattleLogEvent::MoveFailed;
    } else {
      result.redirectedMoveId = mirroredMoveId;
      resolveGenericMoveEffect(attacker, defender, mirroredMoveId, mirroredMove, random,
                               /*allowMultiTurnLock=*/false, moveSlotOfMimicUser, result);
    }
  } else if (move->category == MoveCategory::Status && move->ailment == Ailment::None) {
    // A pure-flavor status move with no dispatch above and no ailment to
    // inflict (Splash, and any other move like it) - real Gen 1 reports
    // "But nothing happened!" rather than the generic MoveHit baseline.
    result.event = BattleLogEvent::NothingHappened;
  }

  // PokeAPI's ailment_chance is 0 for a pure status move's guaranteed main
  // effect (Toxic, Sleep Powder, ...) and only meaningful as a real
  // percentage for a damaging move's secondary effect (Ember's 10% burn,
  // Thunder Shock's 10% paralysis). Treat 0 as "always" for Status moves.
  const uint8_t effectiveAilmentChance =
      (move->category == MoveCategory::Status && move->ailmentChance == 0) ? 100 : move->ailmentChance;
  if (defender.status == Ailment::None && defender.currentHp > 0 && defender.substituteHp == 0 &&
      move->ailment != Ailment::None && rollPercentChance(random, effectiveAilmentChance)) {
    defender.status = move->ailment;
    if (move->ailment == Ailment::Sleep) {
      defender.statusTurns = rollStatusDuration(random, 1, 3);
    } else if (move->ailment == Ailment::Confusion) {
      defender.statusTurns = rollStatusDuration(random, 2, 4);
    } else {
      defender.statusTurns = 0;
    }
    if (result.event == BattleLogEvent::MoveHit) result.event = BattleLogEvent::InflictedStatus;
  }
}

// A crude but serviceable AI: mostly prefers whichever usable move(s) are
// most effective against the player (ties broken randomly instead of
// always the lowest slot index), but 1 in 4 turns picks among ALL usable
// moves instead - without this a Pokemon with several strong options would
// throw the exact same move every single turn, which is exactly the
// "opponent always uses one move" complaint this was written to fix.
uint8_t chooseOpponentMoveSlot(const BattleCombatant& player, const BattleCombatant& opponent,
                               const RandomSource& random) {
  // Mid-charge (Fly/Dig/...), mid-trap (Wrap/Bind/...), or bracing for Bide -
  // the AI has no real choice this turn, it must keep using the same move.
  // PokemonActivity.cpp's Screen::Battle handling does the equivalent check
  // for the player's own side.
  if (opponent.bideTurnsRemaining > 0) {
    for (uint8_t index = 0; index < BATTLE_MOVE_SLOTS; ++index) {
      if (opponent.moves[index].moveId == BIDE_MOVE_ID) return index;
    }
  }
  if (opponent.forcedMoveId != 0) {
    for (uint8_t index = 0; index < BATTLE_MOVE_SLOTS; ++index) {
      if (opponent.moves[index].moveId == opponent.forcedMoveId) return index;
    }
  }

  const SpeciesData* playerSpecies = speciesData(player.speciesId);
  std::array<uint8_t, BATTLE_MOVE_SLOTS> usable{};
  uint8_t usableCount = 0;
  uint16_t bestEffectiveness = 0;
  for (uint8_t index = 0; index < BATTLE_MOVE_SLOTS; ++index) {
    const BattleMoveSlot& slot = opponent.moves[index];
    if (slot.moveId == 0 || slot.currentPp == 0) continue;
    if (opponent.disableTurnsRemaining > 0 && opponent.disabledMoveSlot == index) continue;
    usable[usableCount++] = index;
    const MoveData* move = moveData(slot.moveId);
    if (move == nullptr || playerSpecies == nullptr) continue;
    const uint16_t effectiveness =
        typeEffectivenessPercent(move->type, playerSpecies->primaryType, playerSpecies->secondaryType);
    if (effectiveness > bestEffectiveness) bestEffectiveness = effectiveness;
  }
  // No PP left anywhere - the BATTLE_MOVE_SLOTS sentinel forces a real
  // Struggle turn in resolveAction() (see STRUGGLE_MOVE_ID's doc comment),
  // not just a skipped/no-op turn.
  if (usableCount == 0) return BATTLE_MOVE_SLOTS;

  uint32_t wildcardRoll = 0;
  const bool considerAnyUsable = rollBelow(random, 4U, wildcardRoll) && wildcardRoll == 0;
  std::array<uint8_t, BATTLE_MOVE_SLOTS> candidates{};
  uint8_t candidateCount = 0;
  for (uint8_t i = 0; i < usableCount; ++i) {
    const uint8_t index = usable[i];
    if (considerAnyUsable) {
      candidates[candidateCount++] = index;
      continue;
    }
    const MoveData* move = moveData(opponent.moves[index].moveId);
    const uint16_t effectiveness =
        move == nullptr || playerSpecies == nullptr
            ? 0
            : typeEffectivenessPercent(move->type, playerSpecies->primaryType, playerSpecies->secondaryType);
    if (effectiveness == bestEffectiveness) candidates[candidateCount++] = index;
  }
  if (candidateCount == 0) return usable[0];

  uint32_t pick = 0;
  if (!rollBelow(random, candidateCount, pick) || pick >= candidateCount) pick = 0;
  return candidates[pick];
}

// Shared tail for both stepBattle() and stepOpponentOnlyTurn(): end-of-turn
// poison/burn ticks for whichever combatant(s) have them, then the
// win/loss/still-in-progress outcome. Assumes both actions for the turn (or
// the single opponent action, for the skip-player-turn case) already ran
// and any immediate faint from those was already handled by the caller.
void finishTurn(BattleCombatant& player, BattleCombatant& opponent, BattleTurnResult& result) {
  if (player.disableTurnsRemaining > 0) --player.disableTurnsRemaining;
  if (opponent.disableTurnsRemaining > 0) --opponent.disableTurnsRemaining;
  if (player.trappedTurnsRemaining > 0) --player.trappedTurnsRemaining;
  if (opponent.trappedTurnsRemaining > 0) --opponent.trappedTurnsRemaining;

  BattleLogEvent playerDotEvent = BattleLogEvent::None;
  BattleLogEvent opponentDotEvent = BattleLogEvent::None;
  applyEndOfTurnStatusDamage(player, playerDotEvent);
  applyEndOfTurnStatusDamage(opponent, opponentDotEvent);
  applyLeechSeedDamage(player, opponent, playerDotEvent);
  applyLeechSeedDamage(opponent, player, opponentDotEvent);
  if (playerDotEvent != BattleLogEvent::None) result.player.event = playerDotEvent;
  if (opponentDotEvent != BattleLogEvent::None) result.opponent.event = opponentDotEvent;

  if (player.currentHp == 0 && opponent.currentHp == 0) {
    result.outcome = BattleOutcome::OpponentWon;  // simultaneous KO: wild Pokemon is still standing in spirit
  } else if (player.currentHp == 0) {
    result.player.event = BattleLogEvent::Fainted;
    result.outcome = BattleOutcome::OpponentWon;
  } else if (opponent.currentHp == 0) {
    result.opponent.event = BattleLogEvent::Fainted;
    result.outcome = BattleOutcome::PlayerWon;
  }
}

}  // namespace

uint16_t battleMaxHp(const uint8_t baseHp, const uint8_t level, const uint8_t iv, const uint8_t ev) {
  const uint32_t effectiveBase = static_cast<uint32_t>(baseHp) + std::min<uint32_t>(iv, 15U);
  const uint32_t evBonus = std::min<uint32_t>(ev, 255U) / 4U;
  return static_cast<uint16_t>((2U * effectiveBase + evBonus) * level / 100U + level + 10U);
}

uint16_t battleWorkingStat(const uint8_t baseStat, const uint8_t level, const uint8_t iv, const uint8_t ev) {
  const uint32_t effectiveBase = static_cast<uint32_t>(baseStat) + std::min<uint32_t>(iv, 15U);
  const uint32_t evBonus = std::min<uint32_t>(ev, 255U) / 4U;
  return static_cast<uint16_t>((2U * effectiveBase + evBonus) * level / 100U + 5U);
}

void rollIvSet(const RandomSource& random, std::array<uint8_t, STAT_COUNT>& output) {
  for (uint8_t& iv : output) {
    uint32_t roll = 0;
    iv = rollBelow(random, 16U, roll) ? static_cast<uint8_t>(roll) : 0;
  }
}

uint8_t maxPpFor(const uint8_t basePp, uint8_t ppUpCount) {
  ppUpCount = std::min<uint8_t>(ppUpCount, 3U);
  const uint32_t bonus = (static_cast<uint32_t>(basePp) / 5U) * ppUpCount;
  return static_cast<uint8_t>(std::min<uint32_t>(static_cast<uint32_t>(basePp) + bonus, 255U));
}

uint16_t applyStatStage(const uint16_t baseValue, int8_t stage) {
  stage = std::clamp<int8_t>(stage, -6, 6);
  const uint32_t value = stage >= 0 ? static_cast<uint32_t>(baseValue) * (2U + static_cast<uint32_t>(stage)) / 2U
                                    : static_cast<uint32_t>(baseValue) * 2U / (2U + static_cast<uint32_t>(-stage));
  return clampToUint16(value);
}

uint32_t applyAccuracyEvasionStage(const uint32_t baseValue, int8_t stage) {
  stage = std::clamp<int8_t>(stage, -6, 6);
  return stage >= 0 ? baseValue * (3U + static_cast<uint32_t>(stage)) / 3U
                    : baseValue * 3U / (3U + static_cast<uint32_t>(-stage));
}

const StatChangeEffect* statChangeForMove(const uint8_t moveId) {
  for (const StatChangeTableEntry& entry : STAT_CHANGE_TABLE) {
    if (entry.moveId == moveId) return &entry.effect;
  }
  return nullptr;
}

void resetBattleStages(BattleCombatant& combatant) {
  combatant.attackStage = 0;
  combatant.defenseStage = 0;
  combatant.specialStage = 0;
  combatant.speedStage = 0;
  combatant.accuracyStage = 0;
  combatant.evasionStage = 0;
}

namespace {
// Shared by all 4 X items below: raises the given stage by 1, clamped at
// +6 - returns false (no change) if it was already there, matching how a
// stat-raising move reports StatChangeFailed at the cap.
bool raiseStageByOne(int8_t& stage) {
  if (stage >= 6) return false;
  ++stage;
  return true;
}
}  // namespace

bool applyBattleBoostItem(BattleCombatant& combatant, const uint8_t itemId) {
  switch (itemId) {
    case ITEM_X_ATTACK:
      return raiseStageByOne(combatant.attackStage);
    case ITEM_X_DEFENSE:
      return raiseStageByOne(combatant.defenseStage);
    case ITEM_X_SPEED:
      return raiseStageByOne(combatant.speedStage);
    case ITEM_X_SPECIAL:
      return raiseStageByOne(combatant.specialStage);
    case ITEM_GUARD_SPEC:
      if (combatant.guardSpecActive) return false;
      combatant.guardSpecActive = true;
      return true;
    case ITEM_DIRE_HIT:
      if (combatant.direHitActive) return false;
      combatant.direHitActive = true;
      return true;
    default:
      return false;
  }
}

void defaultMovesetForLevel(const uint16_t speciesId, const uint8_t level,
                            std::array<uint8_t, BATTLE_MOVE_SLOTS>& moveIds,
                            std::array<uint8_t, BATTLE_MOVE_SLOTS>& pp) {
  moveIds.fill(0);
  pp.fill(0);
  const std::span<const LearnsetEntry> learnset = learnsetFor(speciesId);
  size_t filled = 0;
  for (size_t index = learnset.size(); index-- > 0 && filled < BATTLE_MOVE_SLOTS;) {
    if (learnset[index].level > level) continue;
    moveIds[filled] = learnset[index].moveId;
    const MoveData* move = moveData(learnset[index].moveId);
    pp[filled] = move == nullptr ? 0 : move->pp;
    ++filled;
  }
}

BattleTurnResult stepBattle(BattleCombatant& player, BattleCombatant& opponent, const uint8_t playerMoveSlot,
                            const RandomSource& random) {
  BattleTurnResult result{};
  if (player.currentHp == 0 || opponent.currentHp == 0) {
    result.outcome = player.currentHp == 0 ? BattleOutcome::OpponentWon : BattleOutcome::PlayerWon;
    return result;
  }

  const BaseStats* playerStats = baseStatsFor(player.speciesId);
  const BaseStats* opponentStats = baseStatsFor(opponent.speciesId);
  constexpr size_t speedIndex = static_cast<size_t>(StatIndex::Speed);
  uint16_t playerSpeed = playerStats == nullptr
                             ? 0
                             : applyStatStage(battleWorkingStat(playerStats->speed, player.level,
                                                                player.iv[speedIndex], player.ev[speedIndex]),
                                              player.speedStage);
  uint16_t opponentSpeed =
      opponentStats == nullptr
          ? 0
          : applyStatStage(battleWorkingStat(opponentStats->speed, opponent.level, opponent.iv[speedIndex],
                                             opponent.ev[speedIndex]),
                           opponent.speedStage);
  // Soul Badge boost (see BADGE_BOOST_SPEED's doc comment) - only ever set
  // on the player's own side, applied before paralysis's halving (same
  // ordering as Burn/badge-boost-Attack in computeDamage()).
  if ((player.badgeBoostMask & BADGE_BOOST_SPEED) != 0) {
    playerSpeed = static_cast<uint16_t>(playerSpeed + playerSpeed / 8U);
  }
  if ((opponent.badgeBoostMask & BADGE_BOOST_SPEED) != 0) {
    opponentSpeed = static_cast<uint16_t>(opponentSpeed + opponentSpeed / 8U);
  }
  if (player.status == Ailment::Paralysis) playerSpeed /= 2U;
  if (opponent.status == Ailment::Paralysis) opponentSpeed /= 2U;
  const bool playerFirst = playerSpeed >= opponentSpeed;

  // Counter only reflects a physical hit taken *this same turn* - clear last
  // turn's record before either side acts (see BattleCombatant::
  // lastPhysicalDamageTaken's doc comment). Flinch is likewise a same-turn
  // effect - clear any stale flag from a turn where the flinched side never
  // got to act (e.g. it fainted first).
  player.lastPhysicalDamageTaken = 0;
  opponent.lastPhysicalDamageTaken = 0;
  player.flinched = false;
  opponent.flinched = false;

  if (playerFirst) {
    result.player = resolveAction(player, opponent, playerMoveSlot, random);
    // Explosion/Self-Destruct can now faint the attacker too - check for a
    // simultaneous KO before either side-specific branch below, matching
    // finishTurn()'s own "wild Pokemon still standing in spirit" convention
    // for the equivalent end-of-turn case.
    if (player.currentHp == 0 && opponent.currentHp == 0) {
      result.player.event = BattleLogEvent::Fainted;
      result.opponent.event = BattleLogEvent::Fainted;
      result.outcome = BattleOutcome::OpponentWon;
      return result;
    }
    if (opponent.currentHp == 0) {
      result.opponent.event = BattleLogEvent::Fainted;
      result.outcome = BattleOutcome::PlayerWon;
      return result;
    }
    if (player.currentHp == 0) {
      result.player.event = BattleLogEvent::Fainted;
      result.outcome = BattleOutcome::OpponentWon;
      return result;
    }
    result.opponent = resolveAction(opponent, player, chooseOpponentMoveSlot(player, opponent, random), random);
    if (player.currentHp == 0) {
      result.player.event = BattleLogEvent::Fainted;
      result.outcome = BattleOutcome::OpponentWon;
      return result;
    }
  } else {
    result.opponent = resolveAction(opponent, player, chooseOpponentMoveSlot(player, opponent, random), random);
    if (player.currentHp == 0 && opponent.currentHp == 0) {
      result.player.event = BattleLogEvent::Fainted;
      result.opponent.event = BattleLogEvent::Fainted;
      result.outcome = BattleOutcome::OpponentWon;
      return result;
    }
    if (player.currentHp == 0) {
      result.player.event = BattleLogEvent::Fainted;
      result.outcome = BattleOutcome::OpponentWon;
      return result;
    }
    if (opponent.currentHp == 0) {
      result.opponent.event = BattleLogEvent::Fainted;
      result.outcome = BattleOutcome::PlayerWon;
      return result;
    }
    result.player = resolveAction(player, opponent, playerMoveSlot, random);
    if (opponent.currentHp == 0) {
      result.opponent.event = BattleLogEvent::Fainted;
      result.outcome = BattleOutcome::PlayerWon;
      return result;
    }
  }

  finishTurn(player, opponent, result);
  return result;
}

BattleTurnResult stepOpponentOnlyTurn(BattleCombatant& player, BattleCombatant& opponent, const RandomSource& random) {
  BattleTurnResult result{};
  if (player.currentHp == 0 || opponent.currentHp == 0) {
    result.outcome = player.currentHp == 0 ? BattleOutcome::OpponentWon : BattleOutcome::PlayerWon;
    return result;
  }

  // See stepBattle()'s matching reset - the player skips their own action
  // here (using an item mid-battle), so only the opponent's own Counter
  // could ever have anything to reflect, and only from a hit earlier this
  // same turn (there isn't one, since the opponent is the only one acting).
  // Any flinch from a prior turn is likewise stale by now - it should only
  // ever block the action immediately following the hit that caused it.
  player.lastPhysicalDamageTaken = 0;
  opponent.lastPhysicalDamageTaken = 0;
  player.flinched = false;
  opponent.flinched = false;

  result.opponent = resolveAction(opponent, player, chooseOpponentMoveSlot(player, opponent, random), random);
  if (player.currentHp == 0) {
    result.player.event = BattleLogEvent::Fainted;
    result.outcome = BattleOutcome::OpponentWon;
    return result;
  }

  finishTurn(player, opponent, result);
  return result;
}

bool attemptCatch(const BattleCombatant& wild, const BallKind ball, const RandomSource& random) {
  if (ball == BallKind::Master) return true;
  if (wild.currentHp == 0 || wild.maxHp == 0) return false;

  const SpeciesData* species = speciesData(wild.speciesId);
  if (species == nullptr) return false;

  // Real Gen 1 catch algorithm (two rolls, not one) - a previous version of
  // this project used a simplified single-roll formula that turned out to
  // actually be a LATER generation's HP-based formula, not Gen 1's own.
  // R1 is drawn from a ball-specific range (narrower for a better ball);
  // status can auto-catch outright (R1 < statusBonus) or, short of that,
  // makes the catch-rate breakout check below more forgiving.
  const uint32_t r1UpperExclusive = ball == BallKind::Great ? 201U : ball == BallKind::Ultra ? 151U : 256U;
  uint32_t r1 = 0;
  if (!rollBelow(random, r1UpperExclusive, r1)) return false;

  uint32_t statusBonus = 0;
  if (wild.status == Ailment::Sleep || wild.status == Ailment::Freeze) {
    statusBonus = 25U;
  } else if (wild.status == Ailment::Paralysis || wild.status == Ailment::Poison || wild.status == Ailment::Burn) {
    statusBonus = 12U;
  }
  if (r1 < statusBonus) return true;  // R* would be negative - an automatic catch
  const uint32_t rStar = r1 - statusBonus;

  // Breaks free immediately if the species' own base catch rate can't even
  // clear this ball/status-adjusted threshold - no second roll needed.
  if (static_cast<uint32_t>(species->captureRate) < rStar) return false;

  // HP factor: higher for a more-damaged target, capped at 255. Great Ball
  // uses a smaller divisor (8 vs 12) - real Gen 1's actual reason Great
  // Ball catches meaningfully better than its narrower R1 range alone
  // would suggest.
  const uint32_t ballDivisor = ball == BallKind::Great ? 8U : 12U;
  const uint32_t hpQuarter = std::max<uint32_t>(1U, static_cast<uint32_t>(wild.currentHp) / 4U);
  const uint32_t hpFactor =
      std::min<uint32_t>(255U, (static_cast<uint32_t>(wild.maxHp) * 255U / ballDivisor) / hpQuarter);

  uint32_t r2 = 0;
  if (!rollBelow(random, 256U, r2)) return false;
  return r2 <= hpFactor;
}

uint32_t battleVictoryXp(const uint8_t opponentLevel, const bool isTrainerBattle) {
  constexpr uint32_t WILD_MULTIPLIER = 4U;
  constexpr uint32_t TRAINER_MULTIPLIER = 6U;
  return static_cast<uint32_t>(opponentLevel) * (isTrainerBattle ? TRAINER_MULTIPLIER : WILD_MULTIPLIER);
}

}  // namespace pokemon

#endif
