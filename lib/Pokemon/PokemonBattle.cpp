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
                       const RandomSource& random) {
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
  const uint16_t attackStat = battleWorkingStat(attackBase, attacker.level);
  const uint16_t defenseStat = std::max<uint16_t>(1, battleWorkingStat(defenseBase, defender.level));

  uint32_t damage = ((2U * attacker.level / 5U + 2U) * move.power * attackStat) / (50U * defenseStat) + 2U;

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

  if (moveSlot >= BATTLE_MOVE_SLOTS) {
    result.event = BattleLogEvent::MoveHadNoPp;
    return result;
  }
  BattleMoveSlot& slot = attacker.moves[moveSlot];
  if (slot.moveId == 0 || slot.currentPp == 0) {
    result.event = BattleLogEvent::MoveHadNoPp;
    return result;
  }

  if (statusPreventsAction(attacker, random, result.event)) return result;

  const MoveData* move = moveData(slot.moveId);
  if (move == nullptr) {
    result.event = BattleLogEvent::MoveHadNoPp;
    return result;
  }
  --slot.currentPp;

  // accuracy == 0 in this dataset means "never misses" (Swift, Aerial Ace-style moves).
  if (move->accuracy != 0) {
    uint32_t accuracyRoll = 0;
    if (rollBelow(random, 100U, accuracyRoll) && accuracyRoll >= move->accuracy) {
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
    const uint16_t damage = computeDamage(attacker, defender, *move, random);
    defender.currentHp = defender.currentHp > damage ? static_cast<uint16_t>(defender.currentHp - damage) : 0;
    const SpeciesData* defenderSpecies = speciesData(defender.speciesId);
    const uint16_t effectivenessPercent =
        defenderSpecies == nullptr
            ? 100
            : typeEffectivenessPercent(move->type, defenderSpecies->primaryType, defenderSpecies->secondaryType);
    result.event = effectivenessEvent(effectivenessPercent);
    if (damage == 0 && effectivenessPercent != 0) result.event = BattleLogEvent::MoveHit;
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
  if (usableCount == 0) return BATTLE_MOVE_SLOTS;  // no PP left anywhere - Struggle-equivalent fallback

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

uint16_t battleMaxHp(const uint8_t baseHp, const uint8_t level) {
  return static_cast<uint16_t>((2U * baseHp * level) / 100U + level + 10U);
}

uint16_t battleWorkingStat(const uint8_t baseStat, const uint8_t level) {
  return static_cast<uint16_t>((2U * baseStat * level) / 100U + 5U);
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
  uint16_t playerSpeed = playerStats == nullptr ? 0 : battleWorkingStat(playerStats->speed, player.level);
  uint16_t opponentSpeed = opponentStats == nullptr ? 0 : battleWorkingStat(opponentStats->speed, opponent.level);
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
