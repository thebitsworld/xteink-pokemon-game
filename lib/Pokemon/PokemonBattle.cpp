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
        const uint16_t selfDamage = clampToUint16(std::max<uint32_t>(1U, combatant.maxHp / 8U));
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

uint16_t computeDamage(const BattleCombatant& attacker, const BattleCombatant& defender, const MoveData& move,
                       const RandomSource& random, const bool critical) {
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
  const uint16_t attackWorking =
      battleWorkingStat(attackBase, attacker.level, attacker.iv[attackStatIndex], attacker.ev[attackStatIndex]);
  const uint16_t defenseWorking =
      battleWorkingStat(defenseBase, defender.level, defender.iv[defenseStatIndex], defender.ev[defenseStatIndex]);
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

  const bool stab = move.type == attackerSpecies->primaryType || move.type == attackerSpecies->secondaryType;
  if (stab) damage = damage * STAB_BONUS_PERCENT / 100U;

  const uint16_t effectivenessPercent =
      typeEffectivenessPercent(move.type, defenderSpecies->primaryType, defenderSpecies->secondaryType);
  damage = damage * effectivenessPercent / 100U;
  if (damage == 0) return 0;

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

BattleActionResult resolveAction(BattleCombatant& attacker, BattleCombatant& defender, const uint8_t moveSlot,
                                 const RandomSource& random) {
  BattleActionResult result{};
  result.acted = true;
  result.moveSlot = moveSlot;

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

  if (statusPreventsAction(attacker, random, result.event)) return result;

  const MoveData* move = moveData(moveId);
  if (move == nullptr) {
    result.event = BattleLogEvent::MoveHadNoPp;
    return result;
  }
  if (slot != nullptr) --slot->currentPp;

  // accuracy == 0 in this dataset means "never misses" (Swift, Aerial Ace-style
  // moves, and also how Struggle's own accuracy is recorded) - stat stages
  // never apply to those either, matching the real games.
  if (move->accuracy != 0) {
    const int8_t combinedStage =
        std::clamp<int8_t>(attacker.accuracyStage - defender.evasionStage, -6, 6);
    const uint32_t effectiveAccuracy = applyAccuracyEvasionStage(move->accuracy, combinedStage);
    uint32_t accuracyRoll = 0;
    if (rollBelow(random, 100U, accuracyRoll) && accuracyRoll >= effectiveAccuracy) {
      result.event = BattleLogEvent::MoveMissed;
      return result;
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
    const uint16_t effectivenessPercent =
        defenderSpecies == nullptr
            ? 100
            : typeEffectivenessPercent(move->type, defenderSpecies->primaryType, defenderSpecies->secondaryType);

    // A move that's immune (0% effectiveness) never gets to try more than
    // once - real Gen 1 shows "doesn't affect" a single time, not per hit.
    const MultiHitTableEntry* multiHit = effectivenessPercent == 0 ? nullptr : multiHitEntryForMove(moveId);
    const uint8_t hitsToAttempt =
        multiHit == nullptr ? 1 : multiHit->fixedHits != 0 ? multiHit->fixedHits : rollMultiHitCount(random);

    bool anyCritical = false;
    uint8_t hitsLanded = 0;
    uint32_t totalDamage = 0;
    for (uint8_t hit = 0; hit < hitsToAttempt && defender.currentHp > 0; ++hit) {
      const bool critical = attackerStats != nullptr &&
                            rollCriticalHit(attackerStats->speed, isHighCritRatioMove(moveId), random);
      const uint16_t damage = computeDamage(attacker, defender, *move, random, critical);
      defender.currentHp = defender.currentHp > damage ? static_cast<uint16_t>(defender.currentHp - damage) : 0;
      totalDamage += damage;
      if (critical) anyCritical = true;
      ++hitsLanded;
    }

    result.event = effectivenessEvent(effectivenessPercent);
    if (totalDamage == 0 && effectivenessPercent != 0) result.event = BattleLogEvent::MoveHit;
    // Immune (0% effectiveness) hits never actually land, so there's nothing
    // to have been "critical" about even if a roll succeeded.
    result.critical = anyCritical && effectivenessPercent != 0;
    result.hitCount = multiHit != nullptr && effectivenessPercent != 0 ? hitsLanded : 0;

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
  }

  if (moveId == HAZE_MOVE_ID) {
    resetBattleStages(attacker);
    resetBattleStages(defender);
    result.event = BattleLogEvent::StatsReset;
  } else if (const StatChangeEffect* statEffect = statChangeForMove(moveId); statEffect != nullptr) {
    BattleCombatant& target = statEffect->targetsSelf ? attacker : defender;
    int8_t& stage = statStageRef(target, statEffect->stat);
    const int8_t before = stage;
    stage = std::clamp<int8_t>(static_cast<int8_t>(stage + statEffect->stages), -6, 6);
    result.event = stage == before ? BattleLogEvent::StatChangeFailed
                   : statEffect->stages > 0 ? BattleLogEvent::StatRaised
                                            : BattleLogEvent::StatLowered;
  }

  // PokeAPI's ailment_chance is 0 for a pure status move's guaranteed main
  // effect (Toxic, Sleep Powder, ...) and only meaningful as a real
  // percentage for a damaging move's secondary effect (Ember's 10% burn,
  // Thunder Shock's 10% paralysis). Treat 0 as "always" for Status moves.
  const uint8_t effectiveAilmentChance =
      (move->category == MoveCategory::Status && move->ailmentChance == 0) ? 100 : move->ailmentChance;
  if (defender.status == Ailment::None && defender.currentHp > 0 && move->ailment != Ailment::None &&
      rollPercentChance(random, effectiveAilmentChance)) {
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

  return result;
}

// A crude but serviceable AI: mostly prefers whichever usable move(s) are
// most effective against the player (ties broken randomly instead of
// always the lowest slot index), but 1 in 4 turns picks among ALL usable
// moves instead - without this a Pokemon with several strong options would
// throw the exact same move every single turn, which is exactly the
// "opponent always uses one move" complaint this was written to fix.
uint8_t chooseOpponentMoveSlot(const BattleCombatant& player, const BattleCombatant& opponent,
                               const RandomSource& random) {
  const SpeciesData* playerSpecies = speciesData(player.speciesId);
  std::array<uint8_t, BATTLE_MOVE_SLOTS> usable{};
  uint8_t usableCount = 0;
  uint16_t bestEffectiveness = 0;
  for (uint8_t index = 0; index < BATTLE_MOVE_SLOTS; ++index) {
    const BattleMoveSlot& slot = opponent.moves[index];
    if (slot.moveId == 0 || slot.currentPp == 0) continue;
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
  BattleLogEvent playerDotEvent = BattleLogEvent::None;
  BattleLogEvent opponentDotEvent = BattleLogEvent::None;
  applyEndOfTurnStatusDamage(player, playerDotEvent);
  applyEndOfTurnStatusDamage(opponent, opponentDotEvent);
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
  if (player.status == Ailment::Paralysis) playerSpeed /= 2U;
  if (opponent.status == Ailment::Paralysis) opponentSpeed /= 2U;
  const bool playerFirst = playerSpeed >= opponentSpeed;

  if (playerFirst) {
    result.player = resolveAction(player, opponent, playerMoveSlot, random);
    if (opponent.currentHp == 0) {
      result.opponent.event = BattleLogEvent::Fainted;
      result.outcome = BattleOutcome::PlayerWon;
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
    if (player.currentHp == 0) {
      result.player.event = BattleLogEvent::Fainted;
      result.outcome = BattleOutcome::OpponentWon;
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

  const uint32_t hpFactor = ((3U * wild.maxHp - 2U * wild.currentHp) * 255U) / (3U * wild.maxHp);
  uint32_t catchValue = (static_cast<uint32_t>(species->captureRate) * hpFactor) / 255U;

  const uint32_t ballPercent = ball == BallKind::Great ? 150U : ball == BallKind::Ultra ? 200U : 100U;
  catchValue = catchValue * ballPercent / 100U;

  uint32_t statusPercent = 100U;
  if (wild.status == Ailment::Sleep || wild.status == Ailment::Freeze) {
    statusPercent = 200U;
  } else if (wild.status == Ailment::Paralysis || wild.status == Ailment::Poison || wild.status == Ailment::Burn) {
    statusPercent = 150U;
  }
  catchValue = catchValue * statusPercent / 100U;
  catchValue = std::min<uint32_t>(catchValue, 255U);

  uint32_t roll = 0;
  if (!rollBelow(random, 255U, roll)) return false;
  return roll < catchValue;
}

uint32_t battleVictoryXp(const uint8_t opponentLevel, const bool isTrainerBattle) {
  constexpr uint32_t WILD_MULTIPLIER = 4U;
  constexpr uint32_t TRAINER_MULTIPLIER = 6U;
  return static_cast<uint32_t>(opponentLevel) * (isTrainerBattle ? TRAINER_MULTIPLIER : WILD_MULTIPLIER);
}

}  // namespace pokemon

#endif
