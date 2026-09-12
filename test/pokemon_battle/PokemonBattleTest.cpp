#include <algorithm>
#include <cstdio>

#include "Pokemon/PokemonBattle.h"
#include "Pokemon/PokemonSpecies.h"

namespace {

using pokemon::Ailment;
using pokemon::BallKind;
using pokemon::BattleCombatant;
using pokemon::BattleLogEvent;
using pokemon::BattleMoveSlot;
using pokemon::BattleOutcome;
using pokemon::MoveCategory;
using pokemon::RandomSource;
using pokemon::StatKind;

int failures = 0;

#define CHECK(condition)                                                                \
  do {                                                                                  \
    if (!(condition)) {                                                                 \
      std::fprintf(stderr, "%s:%d check failed: %s\n", __FILE__, __LINE__, #condition); \
      ++failures;                                                                       \
    }                                                                                   \
  } while (false)

uint32_t alwaysZero(void*, const uint32_t) { return 0; }
uint32_t alwaysMax(void*, const uint32_t upperExclusive) { return upperExclusive == 0 ? 0 : upperExclusive - 1U; }
uint32_t fixedRoll(void* context, const uint32_t upperExclusive) {
  const uint32_t value = *static_cast<const uint32_t*>(context);
  return upperExclusive == 0 ? 0 : std::min(value, upperExclusive - 1U);
}

const RandomSource ZERO_RANDOM{nullptr, alwaysZero};
const RandomSource MAX_RANDOM{nullptr, alwaysMax};

// Species ids: 1 Bulbasaur (Grass/Poison), 4 Charmander (Fire), 7 Squirtle (Water).
// Move ids: 33 Tackle (Normal/Physical/40pow/100acc), 55 Water Gun
// (Water/Special/40pow/100acc), 52 Ember (Fire/Special/40pow/100acc/10% Burn),
// 77 Poison Powder (Poison/Status/0pow/75acc/Poison, ailment_chance=0 meaning
// "always" per the PokeAPI convention this engine special-cases).

BattleCombatant makeCombatant(const uint16_t speciesId, const uint8_t level,
                              const std::initializer_list<uint8_t> moveIds) {
  BattleCombatant combatant{};
  combatant.speciesId = speciesId;
  combatant.level = level;
  const pokemon::BaseStats* stats = pokemon::baseStatsFor(speciesId);
  combatant.maxHp = stats == nullptr ? 1 : pokemon::battleMaxHp(stats->hp, level);
  combatant.currentHp = combatant.maxHp;
  uint8_t slot = 0;
  for (const uint8_t moveId : moveIds) {
    if (slot >= pokemon::BATTLE_MOVE_SLOTS) break;
    const pokemon::MoveData* move = pokemon::moveData(moveId);
    combatant.moves[slot] = BattleMoveSlot{moveId, move == nullptr ? static_cast<uint8_t>(0) : move->pp};
    ++slot;
  }
  return combatant;
}

void statFormulasScaleWithLevel() {
  CHECK(pokemon::battleMaxHp(45, 5) > 10);  // at least the level/base floor terms
  CHECK(pokemon::battleMaxHp(45, 50) > pokemon::battleMaxHp(45, 5));
  CHECK(pokemon::battleWorkingStat(49, 50) > pokemon::battleWorkingStat(49, 5));
}

void damagingMoveReducesDefenderHpAndReportsSuperEffective() {
  // Squirtle (Water) uses Water Gun on Charmander (Fire): 200% effectiveness.
  // Give Squirtle a much higher level so it's guaranteed to act first
  // regardless of base Speed (same technique the next test uses) - ZERO_RANDOM
  // now also guarantees a critical hit on every attack (see rollCriticalHit:
  // a roll of 0 is always below any positive threshold), so without this,
  // Charmander's own move could go first and faint the low-level Squirtle
  // before Water Gun ever gets a turn.
  BattleCombatant squirtle = makeCombatant(7, 30, {55});
  BattleCombatant charmander = makeCombatant(4, 5, {33});
  const uint16_t hpBefore = charmander.currentHp;

  const pokemon::BattleTurnResult result = pokemon::stepBattle(squirtle, charmander, 0, ZERO_RANDOM);
  CHECK(result.player.event == BattleLogEvent::MoveSuperEffective);
  CHECK(charmander.currentHp < hpBefore);
}

void fireMoveIsNotVeryEffectiveAgainstWaterAndCanBurn() {
  // Charmander (Fire, Ember) attacking Squirtle (Water): 50% effectiveness.
  // Use MAX_RANDOM so the 10% secondary burn chance does not fire, keeping
  // the assertion purely about type effectiveness.
  BattleCombatant charmander = makeCombatant(4, 20, {52});
  BattleCombatant squirtle = makeCombatant(7, 5, {33});  // slower dummy target
  // Force Charmander to act first regardless of speed by giving it a much
  // higher level; the assertion only cares about the Ember side's event.
  const pokemon::BattleTurnResult result = pokemon::stepBattle(charmander, squirtle, 0, MAX_RANDOM);
  CHECK(result.player.event == BattleLogEvent::MoveNotVeryEffective);
  CHECK(squirtle.status == Ailment::None);
}

void statusMoveWithZeroAilmentChanceAlwaysAppliesItsAilment() {
  // Poison Powder has ailment_chance == 0 in the source data, which this
  // engine must treat as "always poisons on hit" per the PokeAPI convention
  // (see the comment in PokemonBattle.cpp), not "never poisons."
  BattleCombatant bulbasaur = makeCombatant(1, 30, {77});  // fast enough to act first
  BattleCombatant charmander = makeCombatant(4, 5, {33});
  const pokemon::BattleTurnResult result = pokemon::stepBattle(bulbasaur, charmander, 0, ZERO_RANDOM);
  CHECK(result.player.event == BattleLogEvent::InflictedStatus);
  CHECK(charmander.status == Ailment::Poison);
}

void paralysisCanPreventAnActionAndSpeedIsHalved() {
  BattleCombatant player = makeCombatant(4, 50, {33});
  player.status = Ailment::Paralysis;
  BattleCombatant opponent = makeCombatant(7, 5, {33});
  // ZERO_RANDOM rolls 0, which is < PARALYSIS_FAIL_CHANCE_PERCENT (25), so
  // the paralyzed side must fail to act this turn.
  const pokemon::BattleTurnResult result = pokemon::stepBattle(player, opponent, 0, ZERO_RANDOM);
  CHECK(result.player.event == BattleLogEvent::StatusPreventedMove);
  CHECK(opponent.currentHp == opponent.maxHp);  // player never got to attack
  CHECK(player.currentHp < player.maxHp);       // but the non-paralyzed opponent still got its hit in
}

void poisonAndBurnTickAtEndOfTurn() {
  BattleCombatant player = makeCombatant(4, 50, {45});  // Growl: 0-power status move, no side effect on target
  player.status = Ailment::Poison;
  BattleCombatant opponent = makeCombatant(7, 50, {45});
  const uint16_t hpBefore = player.currentHp;
  pokemon::stepBattle(player, opponent, 0, MAX_RANDOM);
  CHECK(player.currentHp < hpBefore);
}

void aiVariesItsMoveChoiceAcrossRandomSeedsInsteadOfAlwaysTheSameSlot() {
  // Stage 12: a Gym Leader's Pokemon with several equally-effective moves
  // (Tackle and Scratch are both Normal/40 power - identical effectiveness
  // against a Fire-type target) must not always throw the same one turn
  // after turn just because it happens to sit in the lowest slot index.
  bool sawTackle = false;
  bool sawScratch = false;
  for (uint32_t seed = 0; seed < 12 && !(sawTackle && sawScratch); ++seed) {
    BattleCombatant player = makeCombatant(4, 50, {45});        // Charmander, Growl: harmless filler, never KOs
    BattleCombatant opponent = makeCombatant(4, 50, {33, 10});  // Tackle + Scratch, tied effectiveness
    const RandomSource seeded{&seed, fixedRoll};
    pokemon::stepBattle(player, opponent, 0, seeded);
    if (opponent.moves[0].currentPp < pokemon::moveData(33)->pp) sawTackle = true;
    if (opponent.moves[1].currentPp < pokemon::moveData(10)->pp) sawScratch = true;
  }
  CHECK(sawTackle);
  CHECK(sawScratch);
}

void faintingEndsTheBattleImmediatelyWithoutARetaliation() {
  BattleCombatant strong = makeCombatant(1, 100, {33});
  BattleCombatant weak = makeCombatant(4, 2, {33});
  weak.currentHp = 1;
  weak.maxHp = 1;
  const pokemon::BattleTurnResult result = pokemon::stepBattle(strong, weak, 0, ZERO_RANDOM);
  CHECK(weak.currentHp == 0);
  CHECK(result.outcome == BattleOutcome::PlayerWon);
  CHECK(result.opponent.event == BattleLogEvent::Fainted);
}

void alreadyFaintedCombatantsShortCircuitToAnOutcome() {
  BattleCombatant player = makeCombatant(1, 10, {33});
  BattleCombatant opponent = makeCombatant(4, 10, {33});
  player.currentHp = 0;
  const pokemon::BattleTurnResult result = pokemon::stepBattle(player, opponent, 0, ZERO_RANDOM);
  CHECK(result.outcome == BattleOutcome::OpponentWon);
  CHECK(!result.player.acted);
  CHECK(!result.opponent.acted);
}

void opponentOnlyTurnActsRegardlessOfSpeedSinceThePlayerAlreadySpentTheTurn() {
  // Switching or using an item mid-battle costs the whole turn (Gen 1), so
  // stepOpponentOnlyTurn() never compares Speed - it's called only for the
  // opponent's own action, full stop. Charmander (4, faster) as "player"
  // proves this: if Speed were compared, the faster side would go first and
  // nothing would touch it, but here only the opponent's Squirtle acts.
  BattleCombatant charmander = makeCombatant(4, 20, {33});
  BattleCombatant squirtle = makeCombatant(7, 5, {55});
  const uint16_t playerHpBefore = charmander.currentHp;
  const pokemon::BattleTurnResult result = pokemon::stepOpponentOnlyTurn(charmander, squirtle, ZERO_RANDOM);
  CHECK(result.outcome == BattleOutcome::InProgress);
  CHECK(!result.player.acted);
  CHECK(result.opponent.acted);
  CHECK(charmander.currentHp < playerHpBefore);
  CHECK(squirtle.currentHp == squirtle.maxHp);  // player's side never acted, so it's untouched
}

void opponentOnlyTurnCanFaintThePlayer() {
  BattleCombatant player = makeCombatant(1, 5, {33});
  BattleCombatant opponent = makeCombatant(4, 60, {52});  // Ember, way overleveled
  const pokemon::BattleTurnResult result = pokemon::stepOpponentOnlyTurn(player, opponent, ZERO_RANDOM);
  CHECK(result.outcome == BattleOutcome::OpponentWon);
  CHECK(result.player.event == BattleLogEvent::Fainted);
}

void opponentOnlyTurnStillAppliesEndOfTurnStatusDamage() {
  BattleCombatant player = makeCombatant(1, 20, {33});
  BattleCombatant opponent = makeCombatant(4, 20, {33});
  opponent.status = Ailment::Poison;
  const uint16_t opponentHpBefore = opponent.currentHp;
  const pokemon::BattleTurnResult result = pokemon::stepOpponentOnlyTurn(player, opponent, ZERO_RANDOM);
  CHECK(result.outcome == BattleOutcome::InProgress);
  CHECK(opponent.currentHp < opponentHpBefore);  // poison ticked even though only the opponent acted this turn
}

void opponentOnlyTurnShortCircuitsWhenAlreadyFainted() {
  BattleCombatant player = makeCombatant(1, 10, {33});
  BattleCombatant opponent = makeCombatant(4, 10, {33});
  player.currentHp = 0;
  const pokemon::BattleTurnResult result = pokemon::stepOpponentOnlyTurn(player, opponent, ZERO_RANDOM);
  CHECK(result.outcome == BattleOutcome::OpponentWon);
  CHECK(!result.player.acted);
  CHECK(!result.opponent.acted);
}

void masterBallAlwaysCatchesRegardlessOfRandomness() {
  BattleCombatant wild = makeCombatant(1, 50, {});
  CHECK(pokemon::attemptCatch(wild, BallKind::Master, MAX_RANDOM));
}

void pokeBallCatchOddsScaleWithHpAndCaptureRate() {
  // Bulbasaur: SpeciesData::captureRate == 45, level 20 -> maxHp 48. At full
  // HP with a Poke Ball, catchValue works out to exactly 15/255 (hpFactor
  // 85%, 45*85/255=15). ZERO_RANDOM rolls 0 (< 15, caught); MAX_RANDOM rolls
  // 254 (>= 15, not caught).
  BattleCombatant fullHp = makeCombatant(1, 20, {});
  CHECK(pokemon::attemptCatch(fullHp, BallKind::Poke, ZERO_RANDOM));
  CHECK(!pokemon::attemptCatch(fullHp, BallKind::Poke, MAX_RANDOM));

  // A wounded (HP/10), paralyzed target's catchValue works out to 63/255 -
  // strictly higher than the full-HP case above. A fixed roll of 20 sits
  // between the two thresholds: it must fail against the healthy target but
  // succeed against the wounded, statused one, demonstrating the intended
  // "easier to catch when hurt/statused" effect without relying on an
  // extreme roll that would swamp the difference either way.
  uint32_t roll = 20;
  const RandomSource midRandom{&roll, fixedRoll};
  CHECK(!pokemon::attemptCatch(fullHp, BallKind::Poke, midRandom));

  BattleCombatant wounded = makeCombatant(1, 20, {});
  wounded.currentHp = wounded.maxHp / 10;
  wounded.status = Ailment::Paralysis;
  CHECK(pokemon::attemptCatch(wounded, BallKind::Poke, midRandom));
}

void criticalHitExactlyDoublesDamageForAHighCritRatioMove() {
  // Bulbasaur (base Speed 45) uses Slash (move 163, a real Gen 1 high-crit
  // move: threshold min(45*8,511)=360) on Charmander. A fixedRoll context of
  // 15 saturates the 16-wide damage-variance roll to its max (100%) *and*
  // lands under the 360 crit threshold (guaranteed crit); a context of 400
  // still saturates variance to 100% (same upperExclusive-1 either way) but
  // clears the crit threshold (guaranteed no crit) - isolating the crit
  // multiplier as the only difference between the two calls.
  uint32_t critContext = 15;
  uint32_t noCritContext = 400;
  const RandomSource critRandom{&critContext, fixedRoll};
  const RandomSource noCritRandom{&noCritContext, fixedRoll};

  BattleCombatant attackerCrit = makeCombatant(1, 20, {163});
  BattleCombatant defenderCrit = makeCombatant(4, 20, {33});
  BattleCombatant attackerNoCrit = makeCombatant(1, 20, {163});
  BattleCombatant defenderNoCrit = makeCombatant(4, 20, {33});

  const pokemon::BattleTurnResult critResult = pokemon::stepBattle(attackerCrit, defenderCrit, 0, critRandom);
  const pokemon::BattleTurnResult noCritResult = pokemon::stepBattle(attackerNoCrit, defenderNoCrit, 0, noCritRandom);

  CHECK(critResult.player.critical);
  CHECK(!noCritResult.player.critical);
  const uint16_t critDamage = defenderCrit.maxHp - defenderCrit.currentHp;
  const uint16_t noCritDamage = defenderNoCrit.maxHp - defenderNoCrit.currentHp;
  CHECK(critDamage == noCritDamage * 2U);

  // A move outside the high-crit list uses the plain baseSpeed/512 threshold
  // (45 for Bulbasaur) rather than baseSpeed/64 (360) - a roll of 100 sits
  // between the two, demonstrating the high-crit list genuinely lowers the
  // bar rather than every move sharing one threshold.
  uint32_t midContext = 100;
  const RandomSource midRandom{&midContext, fixedRoll};
  BattleCombatant tackleUser = makeCombatant(1, 20, {33});
  BattleCombatant tackleTarget = makeCombatant(4, 20, {33});
  const pokemon::BattleTurnResult tackleResult = pokemon::stepBattle(tackleUser, tackleTarget, 0, midRandom);
  CHECK(!tackleResult.player.critical);  // 100 >= Tackle's threshold of 45
}

void applyStatStageMultiplierMatchesGen1Table() {
  CHECK(pokemon::applyStatStage(100, 0) == 100);
  CHECK(pokemon::applyStatStage(100, 1) == 150);
  CHECK(pokemon::applyStatStage(100, 2) == 200);
  CHECK(pokemon::applyStatStage(100, 6) == 400);
  CHECK(pokemon::applyStatStage(100, -1) == 66);
  CHECK(pokemon::applyStatStage(100, -6) == 25);
  // Out-of-range stages are clamped to +-6 rather than trusted verbatim.
  CHECK(pokemon::applyStatStage(100, 10) == pokemon::applyStatStage(100, 6));
  CHECK(pokemon::applyStatStage(100, -10) == pokemon::applyStatStage(100, -6));
}

void applyAccuracyEvasionStageMultiplierMatchesGen1Table() {
  CHECK(pokemon::applyAccuracyEvasionStage(100, 0) == 100);
  CHECK(pokemon::applyAccuracyEvasionStage(100, 1) == 133);
  CHECK(pokemon::applyAccuracyEvasionStage(100, 6) == 300);
  CHECK(pokemon::applyAccuracyEvasionStage(100, -1) == 75);
  CHECK(pokemon::applyAccuracyEvasionStage(100, -6) == 33);
}

void statChangeForMoveIdentifiesSelfBuffsAndOpponentDebuffs() {
  const pokemon::StatChangeEffect* swordsDance = pokemon::statChangeForMove(14);
  CHECK(swordsDance != nullptr);
  CHECK(swordsDance->targetsSelf);
  CHECK(swordsDance->stat == StatKind::Attack);
  CHECK(swordsDance->stages == 2);

  const pokemon::StatChangeEffect* growl = pokemon::statChangeForMove(45);
  CHECK(growl != nullptr);
  CHECK(!growl->targetsSelf);
  CHECK(growl->stat == StatKind::Attack);
  CHECK(growl->stages == -1);

  CHECK(pokemon::statChangeForMove(114) == nullptr);  // Haze is handled separately, not table-driven
  CHECK(pokemon::statChangeForMove(33) == nullptr);   // Tackle: not a stat-changing move at all
}

void selfBuffMoveRaisesAttackStageAndReportsEvent() {
  BattleCombatant attacker = makeCombatant(1, 20, {14});  // Swords Dance
  BattleCombatant defender = makeCombatant(4, 20, {33});
  const pokemon::BattleTurnResult result = pokemon::stepBattle(attacker, defender, 0, ZERO_RANDOM);
  CHECK(result.player.event == BattleLogEvent::StatRaised);
  CHECK(attacker.attackStage == 2);
  CHECK(defender.attackStage == 0);
}

void opponentDebuffMoveLowersDefendersStageNotTheUsers() {
  BattleCombatant attacker = makeCombatant(1, 20, {45});  // Growl
  BattleCombatant defender = makeCombatant(4, 20, {33});
  const pokemon::BattleTurnResult result = pokemon::stepBattle(attacker, defender, 0, ZERO_RANDOM);
  CHECK(result.player.event == BattleLogEvent::StatLowered);
  CHECK(defender.attackStage == -1);
  CHECK(attacker.attackStage == 0);
}

void statChangeAtCapReportsFailureInsteadOfExceedingBounds() {
  BattleCombatant attacker = makeCombatant(1, 20, {14});  // Swords Dance, +2 Attack
  attacker.attackStage = 6;                               // already at the cap
  BattleCombatant defender = makeCombatant(4, 20, {33});
  const pokemon::BattleTurnResult result = pokemon::stepBattle(attacker, defender, 0, ZERO_RANDOM);
  CHECK(result.player.event == BattleLogEvent::StatChangeFailed);
  CHECK(attacker.attackStage == 6);
}

void hazeResetsBothSidesStatStages() {
  BattleCombatant attacker = makeCombatant(1, 20, {114});  // Haze
  attacker.attackStage = 2;
  BattleCombatant defender = makeCombatant(4, 20, {33});
  defender.defenseStage = -3;
  const pokemon::BattleTurnResult result = pokemon::stepBattle(attacker, defender, 0, ZERO_RANDOM);
  CHECK(result.player.event == BattleLogEvent::StatsReset);
  CHECK(attacker.attackStage == 0);
  CHECK(defender.defenseStage == 0);
}

void applyBattleBoostItemRaisesEachXStatByOneStageUpToTheCap() {
  BattleCombatant combatant{};
  CHECK(pokemon::applyBattleBoostItem(combatant, pokemon::ITEM_X_ATTACK));
  CHECK(combatant.attackStage == 1);
  CHECK(pokemon::applyBattleBoostItem(combatant, pokemon::ITEM_X_DEFENSE));
  CHECK(combatant.defenseStage == 1);
  CHECK(pokemon::applyBattleBoostItem(combatant, pokemon::ITEM_X_SPEED));
  CHECK(combatant.speedStage == 1);
  CHECK(pokemon::applyBattleBoostItem(combatant, pokemon::ITEM_X_SPECIAL));
  CHECK(combatant.specialStage == 1);

  combatant.attackStage = 6;
  CHECK(!pokemon::applyBattleBoostItem(combatant, pokemon::ITEM_X_ATTACK));  // already at the +6 cap
  CHECK(combatant.attackStage == 6);
}

void guardSpecItemActivatesOnceAndBlocksOpponentStatLoweringMoves() {
  BattleCombatant combatant{};
  CHECK(pokemon::applyBattleBoostItem(combatant, pokemon::ITEM_GUARD_SPEC));
  CHECK(combatant.guardSpecActive);
  CHECK(!pokemon::applyBattleBoostItem(combatant, pokemon::ITEM_GUARD_SPEC));  // already active this battle

  // Growl (move 45) lowers the target's Attack by 1 stage - Guard Spec
  // should block that entirely, leaving the stage untouched, rather than
  // just capping it the way an already-at-6 stage would.
  BattleCombatant attacker = makeCombatant(4, 20, {45});
  BattleCombatant defender = makeCombatant(1, 20, {33});
  defender.guardSpecActive = true;
  const pokemon::BattleTurnResult result = pokemon::stepBattle(attacker, defender, 0, ZERO_RANDOM);
  CHECK(defender.attackStage == 0);
  CHECK(result.player.event == BattleLogEvent::StatChangeFailed);
}

void guardSpecDoesNotBlockSelfBuffingStatMoves() {
  // Guard Spec only blocks the OPPONENT from lowering this combatant's own
  // stats - it must not interfere with the combatant's own self-buffs.
  BattleCombatant attacker = makeCombatant(1, 20, {14});  // Swords Dance, self-targeting +2 Attack
  attacker.guardSpecActive = true;
  BattleCombatant defender = makeCombatant(4, 20, {33});
  const pokemon::BattleTurnResult result = pokemon::stepBattle(attacker, defender, 0, ZERO_RANDOM);
  CHECK(result.player.event == BattleLogEvent::StatRaised);
  CHECK(attacker.attackStage == 2);
}

void direHitItemActivatesOnceAndRaisesCritRatioToTheHighTier() {
  BattleCombatant combatant{};
  CHECK(pokemon::applyBattleBoostItem(combatant, pokemon::ITEM_DIRE_HIT));
  CHECK(combatant.direHitActive);
  CHECK(!pokemon::applyBattleBoostItem(combatant, pokemon::ITEM_DIRE_HIT));  // already active this battle

  // Same roll=100 Tackle scenario as criticalHitExactlyDoublesDamageForAHighCritRatioMove's
  // last check (100 sits between Bulbasaur's plain 45 threshold and the
  // high-crit 360 one) - Dire Hit should push a plain Tackle into the
  // high-crit tier exactly the way a move like Slash already is.
  uint32_t midContext = 100;
  const RandomSource midRandom{&midContext, fixedRoll};
  BattleCombatant tackleUser = makeCombatant(1, 20, {33});
  tackleUser.direHitActive = true;
  BattleCombatant tackleTarget = makeCombatant(4, 20, {33});
  const pokemon::BattleTurnResult result = pokemon::stepBattle(tackleUser, tackleTarget, 0, midRandom);
  CHECK(result.player.critical);
}

void applyBattleBoostItemRejectsAnUnknownItemId() {
  BattleCombatant combatant{};
  CHECK(!pokemon::applyBattleBoostItem(combatant, 11));  // Potion - not a battle-boost item at all
}

void speedStageCanFlipWhichSideActsFirst() {
  // Bulbasaur (base Speed 45) is normally slower than Charmander (65) at the
  // same level, so Charmander acts first and its Ember faints a 1-HP
  // Bulbasaur before it ever gets a turn.
  BattleCombatant bulbasaur = makeCombatant(1, 20, {33});
  bulbasaur.currentHp = 1;
  bulbasaur.maxHp = 1;
  BattleCombatant charmander = makeCombatant(4, 20, {52});
  const pokemon::BattleTurnResult baseline = pokemon::stepBattle(bulbasaur, charmander, 0, ZERO_RANDOM);
  CHECK(!baseline.player.acted);

  // +6 Speed stages (45 * 4 = 180) comfortably overtakes Charmander's 65 -
  // now Bulbasaur must act before Charmander gets a turn.
  BattleCombatant fastBulbasaur = makeCombatant(1, 20, {33});
  fastBulbasaur.currentHp = 1;
  fastBulbasaur.maxHp = 1;
  fastBulbasaur.speedStage = 6;
  BattleCombatant slowCharmander = makeCombatant(4, 20, {52});
  const pokemon::BattleTurnResult boosted = pokemon::stepBattle(fastBulbasaur, slowCharmander, 0, ZERO_RANDOM);
  CHECK(boosted.player.acted);
}

void accuracyStageLoweringCanCauseAMissThatWouldOtherwiseHit() {
  uint32_t roll = 50;  // between the staged (33) and unstaged (100) accuracy thresholds
  const RandomSource midRandom{&roll, fixedRoll};

  BattleCombatant plainAttacker = makeCombatant(1, 20, {33});
  BattleCombatant plainDefender = makeCombatant(4, 20, {33});
  CHECK(pokemon::stepBattle(plainAttacker, plainDefender, 0, midRandom).player.event != BattleLogEvent::MoveMissed);

  BattleCombatant debuffedAttacker = makeCombatant(1, 20, {33});
  debuffedAttacker.accuracyStage = -6;
  BattleCombatant defender = makeCombatant(4, 20, {33});
  CHECK(pokemon::stepBattle(debuffedAttacker, defender, 0, midRandom).player.event == BattleLogEvent::MoveMissed);
}

void evasionStageRaisingCanCauseAMissThatWouldOtherwiseHit() {
  uint32_t roll = 50;  // between the staged (33) and unstaged (100) accuracy thresholds
  const RandomSource midRandom{&roll, fixedRoll};

  BattleCombatant attacker = makeCombatant(1, 20, {33});
  BattleCombatant evasiveDefender = makeCombatant(4, 20, {33});
  evasiveDefender.evasionStage = 6;
  CHECK(pokemon::stepBattle(attacker, evasiveDefender, 0, midRandom).player.event == BattleLogEvent::MoveMissed);
}

void criticalHitIgnoresAttackersUnfavorableNegativeAttackStage() {
  // A crit uses whichever of {unstaged, staged} Attack is higher, so a -1
  // Attack stage has zero effect on a guaranteed crit - the real Gen 1 rule.
  uint32_t critRoll = 15;  // guarantees both max variance and a crit on Slash
  const RandomSource critRandom{&critRoll, fixedRoll};

  BattleCombatant debuffed = makeCombatant(1, 20, {163});  // Slash, high-crit move
  debuffed.attackStage = -1;
  BattleCombatant defenderA = makeCombatant(4, 20, {33});
  pokemon::stepBattle(debuffed, defenderA, 0, critRandom);

  BattleCombatant normal = makeCombatant(1, 20, {163});
  BattleCombatant defenderB = makeCombatant(4, 20, {33});
  pokemon::stepBattle(normal, defenderB, 0, critRandom);

  const uint16_t debuffedDamage = defenderA.maxHp - defenderA.currentHp;
  const uint16_t normalDamage = defenderB.maxHp - defenderB.currentHp;
  CHECK(debuffedDamage == normalDamage);
}

void criticalHitIgnoresDefendersUnfavorablePositiveDefenseStage() {
  uint32_t critRoll = 15;
  const RandomSource critRandom{&critRoll, fixedRoll};

  BattleCombatant attackerA = makeCombatant(1, 20, {163});
  BattleCombatant buffedDefender = makeCombatant(4, 20, {33});
  buffedDefender.defenseStage = 2;
  pokemon::stepBattle(attackerA, buffedDefender, 0, critRandom);

  BattleCombatant attackerB = makeCombatant(1, 20, {163});
  BattleCombatant plainDefender = makeCombatant(4, 20, {33});
  pokemon::stepBattle(attackerB, plainDefender, 0, critRandom);

  const uint16_t buffedDamage = buffedDefender.maxHp - buffedDefender.currentHp;
  const uint16_t plainDamage = plainDefender.maxHp - plainDefender.currentHp;
  CHECK(buffedDamage == plainDamage);
}

void recoilMoveDamagesTheAttackerAfterDealingDamage() {
  // Take Down (move 36): a real Gen 1 recoil move, 1/4 of damage dealt back
  // to the user. A fixedRoll context of 70 lands under Take Down's 85
  // accuracy (hit), at/above Charmander's 65 base-Speed crit threshold (no
  // crit, keeping the damage/recoil math simple to verify exactly), and
  // saturates the 16-wide damage-variance roll to its max (100%).
  uint32_t context = 70;
  const RandomSource fixedRandom{&context, fixedRoll};
  BattleCombatant attacker = makeCombatant(4, 20, {36});  // Charmander, Take Down
  // Same level as the attacker - Charmander's higher base Speed (65 vs 45)
  // keeps it acting first regardless - with HP overridden well above
  // anything Take Down could deal in one hit, so the measured damage/recoil
  // never gets skewed by HP clamping at 0.
  BattleCombatant defender = makeCombatant(1, 20, {33});
  defender.maxHp = 200;
  defender.currentHp = 200;
  // Asleep so it never counter-attacks - stepBattle() resolves a full
  // two-sided turn, and an awake defender's own hit back would inflate the
  // measured "recoil" with unrelated damage from its counter-attack.
  defender.status = Ailment::Sleep;
  defender.statusTurns = 5;
  const uint16_t attackerHpBefore = attacker.currentHp;
  const uint16_t defenderHpBefore = defender.currentHp;
  const pokemon::BattleTurnResult result = pokemon::stepBattle(attacker, defender, 0, fixedRandom);
  CHECK(defender.currentHp < defenderHpBefore);
  CHECK(result.player.recoilApplied);
  CHECK(attacker.currentHp < attackerHpBefore);
  const uint16_t damageDealt = defenderHpBefore - defender.currentHp;
  const uint16_t recoilTaken = attackerHpBefore - attacker.currentHp;
  CHECK(recoilTaken == std::max<uint16_t>(1, static_cast<uint16_t>(damageDealt / 4U)));
}

void nonRecoilMoveNeverAppliesRecoil() {
  BattleCombatant attacker = makeCombatant(4, 20, {33});  // Tackle: not a recoil move
  BattleCombatant defender = makeCombatant(1, 20, {33});
  const pokemon::BattleTurnResult result = pokemon::stepBattle(attacker, defender, 0, MAX_RANDOM);
  CHECK(!result.player.recoilApplied);
}

void multiHitMoveConnectsMultipleTimesAndReportsHitCount() {
  // Comet Punch (move 4): a real Gen 1 multi-hit move with a random 2/3/4/5
  // hit count. A fixedRoll context of 7 forces every 8-wide roll in
  // rollMultiHitCount() to its max (upperExclusive-1 = 7), landing in the
  // top 1/8 bucket - always exactly 5 hits.
  uint32_t context = 7;
  const RandomSource fixedRandom{&context, fixedRoll};
  BattleCombatant attacker = makeCombatant(4, 30, {4});   // Charmander, Comet Punch
  // Same level as the attacker - Charmander's higher base Speed keeps it
  // acting first - with HP overridden well above what 5 hits could deal.
  BattleCombatant defender = makeCombatant(1, 30, {33});  // Bulbasaur
  defender.maxHp = 300;
  defender.currentHp = 300;
  const pokemon::BattleTurnResult result = pokemon::stepBattle(attacker, defender, 0, fixedRandom);
  CHECK(result.player.hitCount == 5);
}

void twineedleAlwaysHitsExactlyTwice() {
  // Twineedle (move 41) is the one multi-hit move with a fixed count rather
  // than a rolled one - always exactly 2 hits, never 3-5.
  BattleCombatant attacker = makeCombatant(1, 30, {41});  // Bulbasaur, Twineedle
  BattleCombatant defender = makeCombatant(4, 60, {33});
  const pokemon::BattleTurnResult result = pokemon::stepBattle(attacker, defender, 0, MAX_RANDOM);
  CHECK(result.player.hitCount == 2);
}

void nonMultiHitMoveReportsZeroHitCount() {
  BattleCombatant attacker = makeCombatant(4, 20, {33});  // Tackle: single-hit
  BattleCombatant defender = makeCombatant(1, 20, {33});
  const pokemon::BattleTurnResult result = pokemon::stepBattle(attacker, defender, 0, MAX_RANDOM);
  CHECK(result.player.hitCount == 0);
}

void multiHitMoveStopsEarlyIfTheDefenderFaintsPartway() {
  uint32_t context = 7;  // forces 5 intended hits, same as the test above
  const RandomSource fixedRandom{&context, fixedRoll};
  BattleCombatant attacker = makeCombatant(4, 50, {4});  // strong Charmander, Comet Punch
  BattleCombatant defender = makeCombatant(1, 5, {33});  // weak, 1-HP Bulbasaur
  defender.currentHp = 1;
  defender.maxHp = 1;
  const pokemon::BattleTurnResult result = pokemon::stepBattle(attacker, defender, 0, fixedRandom);
  CHECK(defender.currentHp == 0);
  CHECK(result.player.hitCount == 1);  // fainted on the first hit - the rest never got attempted
}

void forcedStruggleSentinelDealsDamageAndRecoilsWithNoLearnedMove() {
  // pokemon::BATTLE_MOVE_SLOTS itself (or any value >= it) as the move slot
  // forces a real Struggle turn - no learned move needed, no PP touched.
  BattleCombatant attacker = makeCombatant(4, 30, {});  // Charmander, no moves at all
  // HP overridden well above what one Struggle hit could deal, so the
  // measured damage/recoil never gets skewed by HP clamping at 0.
  BattleCombatant defender = makeCombatant(7, 20, {33});
  defender.maxHp = 200;
  defender.currentHp = 200;
  // Asleep so it never counter-attacks (see the same note in the Take Down
  // recoil test above).
  defender.status = Ailment::Sleep;
  defender.statusTurns = 5;
  const uint16_t attackerHpBefore = attacker.currentHp;
  const uint16_t defenderHpBefore = defender.currentHp;
  const pokemon::BattleTurnResult result =
      pokemon::stepBattle(attacker, defender, pokemon::BATTLE_MOVE_SLOTS, MAX_RANDOM);
  CHECK(result.player.acted);
  CHECK(result.player.moveSlot == pokemon::BATTLE_MOVE_SLOTS);
  CHECK(defender.currentHp < defenderHpBefore);
  CHECK(result.player.recoilApplied);
  CHECK(attacker.currentHp < attackerHpBefore);
  const uint16_t damageDealt = defenderHpBefore - defender.currentHp;
  const uint16_t recoilTaken = attackerHpBefore - attacker.currentHp;
  CHECK(recoilTaken == std::max<uint16_t>(1, static_cast<uint16_t>(damageDealt / 2U)));
}

void opponentStrugglesWhenAllOfItsLearnedMovesAreOutOfPp() {
  BattleCombatant player = makeCombatant(4, 20, {45});  // Growl: harmless filler, never KOs
  BattleCombatant opponent = makeCombatant(7, 20, {33, 55});
  opponent.moves[0].currentPp = 0;
  opponent.moves[1].currentPp = 0;
  const pokemon::BattleTurnResult result = pokemon::stepBattle(player, opponent, 0, MAX_RANDOM);
  CHECK(result.opponent.acted);
  CHECK(result.opponent.moveSlot == pokemon::BATTLE_MOVE_SLOTS);
  CHECK(result.opponent.event != BattleLogEvent::MoveHadNoPp);
  CHECK(result.opponent.recoilApplied);
}

void ivZeroEvZeroReproducesTheOriginalFormulaExactly() {
  // The backward-compat invariant: every caller that hasn't been taught a
  // Pokemon's real IV/EV yet can pass 0/0 (or rely on the defaults) and see
  // no behavior change from before IV/EV existed.
  CHECK(pokemon::battleMaxHp(45, 50) == static_cast<uint16_t>((2U * 45U * 50U) / 100U + 50U + 10U));
  CHECK(pokemon::battleWorkingStat(49, 50) == static_cast<uint16_t>((2U * 49U * 50U) / 100U + 5U));
  CHECK(pokemon::battleMaxHp(45, 50, 0, 0) == pokemon::battleMaxHp(45, 50));
  CHECK(pokemon::battleWorkingStat(49, 50, 0, 0) == pokemon::battleWorkingStat(49, 50));
}

void ivAndEvRaiseStatsAboveTheZeroBaseline() {
  const uint16_t baseline = pokemon::battleWorkingStat(49, 50);
  CHECK(pokemon::battleWorkingStat(49, 50, 15, 0) > baseline);   // max IV alone helps
  CHECK(pokemon::battleWorkingStat(49, 50, 0, 255) > baseline);  // max EV alone helps
  CHECK(pokemon::battleWorkingStat(49, 50, 15, 255) > pokemon::battleWorkingStat(49, 50, 15, 0));

  const uint16_t hpBaseline = pokemon::battleMaxHp(45, 50);
  CHECK(pokemon::battleMaxHp(45, 50, 15, 0) > hpBaseline);
  CHECK(pokemon::battleMaxHp(45, 50, 0, 255) > hpBaseline);
}

void evBonusMatchesTheFlatDivideByFourFormula() {
  // ev/4 exactly, not the real Gen 1 sqrt(ev)/4 curve - see
  // docs/development/pokemon-iv-ev-plan.md. 100/4=25, 255/4=63 (both floored).
  CHECK(pokemon::battleWorkingStat(50, 100, 0, 100) == static_cast<uint16_t>((2U * 50U + 25U) * 100U / 100U + 5U));
  CHECK(pokemon::battleWorkingStat(50, 100, 0, 255) == static_cast<uint16_t>((2U * 50U + 63U) * 100U / 100U + 5U));
}

void outOfRangeIvIsClampedToTheRealGen1Ceiling() {
  // A caller passing an IV above the real 0-15 ceiling (shouldn't happen -
  // IvEvStoreCodec's own validateIvEvEntry() already rejects it before it's
  // ever persisted - but the formula itself still guards independently)
  // must not get more bonus than a real IV of 15 would give.
  CHECK(pokemon::battleWorkingStat(50, 100, 200, 0) == pokemon::battleWorkingStat(50, 100, 15, 0));
}

void rollIvSetProducesValuesInTheRealGen1Range() {
  uint32_t context = 15;  // saturates a 16-wide roll to its max, 15
  const RandomSource maxIvRandom{&context, fixedRoll};
  std::array<uint8_t, pokemon::STAT_COUNT> iv{};
  pokemon::rollIvSet(maxIvRandom, iv);
  for (const uint8_t value : iv) CHECK(value == 15);

  pokemon::rollIvSet(ZERO_RANDOM, iv);
  for (const uint8_t value : iv) CHECK(value == 0);
}

void maxPpForMatchesTheRealGen1PpUpProgression() {
  // A real Gen 1 example: a 40-PP move goes 40 -> 48 -> 56 -> 64 across the
  // 3 real PP Up uses (each adds floor(basePp/5) = 8).
  CHECK(pokemon::maxPpFor(40, 0) == 40);
  CHECK(pokemon::maxPpFor(40, 1) == 48);
  CHECK(pokemon::maxPpFor(40, 2) == 56);
  CHECK(pokemon::maxPpFor(40, 3) == 64);
  // A caller passing more than the real 3-use cap (shouldn't happen -
  // PokemonService::applyPpUp() refuses a 4th use - but the formula itself
  // still guards independently) must not exceed the 3-use result.
  CHECK(pokemon::maxPpFor(40, 5) == pokemon::maxPpFor(40, 3));
  // A move whose base PP isn't a multiple of 5 still floors the bonus per
  // use rather than accumulating fractional PP.
  CHECK(pokemon::maxPpFor(30, 1) == 36);  // floor(30/5)=6
  CHECK(pokemon::maxPpFor(17, 1) == 20);  // floor(17/5)=3
}

void battleVictoryXpScalesWithLevelAndTrainerBonus() {
  CHECK(pokemon::battleVictoryXp(5, false) == 20);    // wild: level * 4
  CHECK(pokemon::battleVictoryXp(25, false) == 100);
  CHECK(pokemon::battleVictoryXp(12, true) == 72);    // trainer: level * 6
  CHECK(pokemon::battleVictoryXp(65, true) == 390);
  CHECK(pokemon::battleVictoryXp(0, false) == 0);
}

// Move ids for the fixed-damage moves below (PokeAPI's own data stores
// power=0 for every one of these - see FIXED_DAMAGE_TABLE's doc comment in
// PokemonBattle.cpp): 12 Guillotine, 32 Horn Drill, 90 Fissure (OHKO);
// 69 Seismic Toss, 101 Night Shade (damage = user's level); 82 Dragon Rage
// (flat 40), 49 Sonic Boom (flat 20); 149 Psywave (random, scaled to level);
// 162 Super Fang (halves target's current HP); 68 Counter (reflects 2x the
// last physical damage taken this turn); 67 Low Kick (simplified fixed
// power, since this project has no per-species weight data).

void ohkoMoveInstantlyFaintsWhenAttackerLevelIsAtLeastDefenders() {
  // Guillotine at equal level: real Gen 1 hit chance is exactly the move's
  // listed accuracy (30) plus the level difference (0 here) - ZERO_RANDOM
  // (always rolls 0, below any positive threshold) connects and instantly
  // faints the target regardless of its remaining HP.
  BattleCombatant charmander = makeCombatant(4, 20, {12});
  BattleCombatant squirtle = makeCombatant(7, 20, {33});
  const pokemon::BattleTurnResult result = pokemon::stepBattle(charmander, squirtle, 0, ZERO_RANDOM);
  CHECK(result.player.event == BattleLogEvent::OneHitKo);
  CHECK(squirtle.currentHp == 0);
}

void ohkoMoveNeverConnectsWhenAttackerIsALowerLevel() {
  // A real Gen 1 rule: an OHKO move always misses if the user's level is
  // below the target's, no matter how favorable the random roll is. Keep the
  // level gap small so Squirtle's own (normal, non-OHKO) Tackle can't just
  // faint Charmander outright before Guillotine gets a chance to whiff.
  BattleCombatant charmander = makeCombatant(4, 20, {12});
  BattleCombatant squirtle = makeCombatant(7, 25, {33});
  const uint16_t hpBefore = squirtle.currentHp;
  const pokemon::BattleTurnResult result = pokemon::stepBattle(charmander, squirtle, 0, ZERO_RANDOM);
  CHECK(result.player.event == BattleLogEvent::MoveMissed);
  CHECK(squirtle.currentHp == hpBefore);
}

void ohkoMoveHasNoEffectOnAnImmuneType() {
  // Guillotine/Horn Drill are Normal-type - Gastly (Ghost/Poison) is immune
  // to Normal, and that immunity applies to an OHKO move exactly like any
  // other move.
  BattleCombatant charmander = makeCombatant(4, 50, {12});
  BattleCombatant gastly = makeCombatant(92, 5, {33});
  const uint16_t hpBefore = gastly.currentHp;
  const pokemon::BattleTurnResult result = pokemon::stepBattle(charmander, gastly, 0, ZERO_RANDOM);
  CHECK(result.player.event == BattleLogEvent::MoveNoEffect);
  CHECK(gastly.currentHp == hpBefore);
}

void seismicTossAndNightShadeDealDamageEqualToUsersLevel() {
  // Charmander (base Speed 65) is faster than Bulbasaur (base Speed 45) at
  // the same level, so its fixed-damage move always resolves this turn
  // regardless of the random source.
  BattleCombatant charmander = makeCombatant(4, 20, {69});  // Seismic Toss
  BattleCombatant bulbasaur = makeCombatant(1, 20, {33});
  const uint16_t hpBefore = bulbasaur.currentHp;
  const pokemon::BattleTurnResult result = pokemon::stepBattle(charmander, bulbasaur, 0, ZERO_RANDOM);
  CHECK(hpBefore - bulbasaur.currentHp == 20);
  CHECK(result.player.event == BattleLogEvent::MoveHit);

  BattleCombatant charmander2 = makeCombatant(4, 20, {101});  // Night Shade
  BattleCombatant bulbasaur2 = makeCombatant(1, 20, {33});
  const uint16_t hpBefore2 = bulbasaur2.currentHp;
  pokemon::stepBattle(charmander2, bulbasaur2, 0, ZERO_RANDOM);
  CHECK(hpBefore2 - bulbasaur2.currentHp == 20);
}

void dragonRageAndSonicBoomDealFixedFlatDamage() {
  BattleCombatant charmander = makeCombatant(4, 30, {82});  // Dragon Rage
  BattleCombatant bulbasaur = makeCombatant(1, 30, {33});
  const uint16_t hpBefore = bulbasaur.currentHp;
  pokemon::stepBattle(charmander, bulbasaur, 0, ZERO_RANDOM);
  CHECK(hpBefore - bulbasaur.currentHp == 40);

  BattleCombatant charmander2 = makeCombatant(4, 30, {49});  // Sonic Boom
  BattleCombatant bulbasaur2 = makeCombatant(1, 30, {33});
  const uint16_t hpBefore2 = bulbasaur2.currentHp;
  pokemon::stepBattle(charmander2, bulbasaur2, 0, ZERO_RANDOM);
  CHECK(hpBefore2 - bulbasaur2.currentHp == 20);
}

void psywaveDamageIsBoundedBetweenOneAndOnePointFiveTimesLevel() {
  // Real Gen 1 Psywave: random damage from 1 up to 1.5x the user's level.
  BattleCombatant charmander = makeCombatant(4, 20, {149});
  BattleCombatant bulbasaur = makeCombatant(1, 20, {33});
  const uint16_t hpBefore = bulbasaur.currentHp;
  pokemon::stepBattle(charmander, bulbasaur, 0, ZERO_RANDOM);
  CHECK(hpBefore - bulbasaur.currentHp == 1);

  BattleCombatant charmander2 = makeCombatant(4, 20, {149});
  BattleCombatant bulbasaur2 = makeCombatant(1, 20, {33});
  const uint16_t hpBefore2 = bulbasaur2.currentHp;
  pokemon::stepBattle(charmander2, bulbasaur2, 0, MAX_RANDOM);
  CHECK(hpBefore2 - bulbasaur2.currentHp == 30);  // floor(20 * 1.5)
}

void superFangHalvesDefendersCurrentHp() {
  BattleCombatant charmander = makeCombatant(4, 30, {162});  // Super Fang
  BattleCombatant bulbasaur = makeCombatant(1, 5, {33});
  bulbasaur.currentHp = 50;  // override to a clean, easy-to-halve value
  pokemon::stepBattle(charmander, bulbasaur, 0, ZERO_RANDOM);
  CHECK(bulbasaur.currentHp == 25);
}

void lowKickDealsRealDamageInsteadOfTheOldFlatBug() {
  // Before FIXED_DAMAGE_TABLE/LOW_KICK_SIMPLIFIED_POWER existed, Low Kick's
  // power=0 in the source data meant it fell through to the normal formula
  // and dealt a useless ~2 flat damage. It should now deal real damage,
  // comfortably more than that old bug's amount.
  BattleCombatant charmander = makeCombatant(4, 30, {67});
  BattleCombatant bulbasaur = makeCombatant(1, 30, {33});
  const uint16_t hpBefore = bulbasaur.currentHp;
  pokemon::stepBattle(charmander, bulbasaur, 0, ZERO_RANDOM);
  CHECK(hpBefore - bulbasaur.currentHp > 5);
}

void counterReflectsDoubleTheLastPhysicalDamageTakenThisTurn() {
  // Charmander (higher base Speed, so faster even at an equal level) uses
  // Tackle; Squirtle's only move is Counter - it should reflect exactly 2x
  // the damage Tackle just dealt, back at Charmander, this same turn. Keep
  // the level gap small so Tackle doesn't just faint Squirtle outright
  // before its own Counter gets a turn.
  BattleCombatant charmander = makeCombatant(4, 6, {33});  // Tackle
  BattleCombatant squirtle = makeCombatant(7, 5, {68});    // Counter
  const uint16_t squirtleHpBefore = squirtle.currentHp;
  const uint16_t charmanderHpBefore = charmander.currentHp;

  const pokemon::BattleTurnResult result = pokemon::stepBattle(squirtle, charmander, 0, ZERO_RANDOM);

  const uint16_t tackleDamage = squirtleHpBefore - squirtle.currentHp;
  CHECK(tackleDamage > 0);
  const uint16_t counterDamage = charmanderHpBefore - charmander.currentHp;
  CHECK(counterDamage == tackleDamage * 2U);
  CHECK(result.player.event != BattleLogEvent::MoveFailed);
}

void counterFailsWhenUserHasNotTakenPhysicalDamageThisTurn() {
  // Squirtle (much higher level, so faster) uses Counter before Charmander's
  // Tackle has landed - nothing to reflect yet this turn, so it fails
  // ("But it failed!") instead of dealing damage.
  BattleCombatant squirtle = makeCombatant(7, 30, {68});   // Counter
  BattleCombatant charmander = makeCombatant(4, 5, {33});  // Tackle
  const uint16_t charmanderHpBefore = charmander.currentHp;

  const pokemon::BattleTurnResult result = pokemon::stepBattle(squirtle, charmander, 0, ZERO_RANDOM);

  CHECK(result.player.event == BattleLogEvent::MoveFailed);
  CHECK(charmander.currentHp == charmanderHpBefore);
}

}  // namespace

int main() {
  statFormulasScaleWithLevel();
  damagingMoveReducesDefenderHpAndReportsSuperEffective();
  fireMoveIsNotVeryEffectiveAgainstWaterAndCanBurn();
  statusMoveWithZeroAilmentChanceAlwaysAppliesItsAilment();
  paralysisCanPreventAnActionAndSpeedIsHalved();
  poisonAndBurnTickAtEndOfTurn();
  aiVariesItsMoveChoiceAcrossRandomSeedsInsteadOfAlwaysTheSameSlot();
  faintingEndsTheBattleImmediatelyWithoutARetaliation();
  alreadyFaintedCombatantsShortCircuitToAnOutcome();
  opponentOnlyTurnActsRegardlessOfSpeedSinceThePlayerAlreadySpentTheTurn();
  opponentOnlyTurnCanFaintThePlayer();
  opponentOnlyTurnStillAppliesEndOfTurnStatusDamage();
  opponentOnlyTurnShortCircuitsWhenAlreadyFainted();
  masterBallAlwaysCatchesRegardlessOfRandomness();
  pokeBallCatchOddsScaleWithHpAndCaptureRate();
  criticalHitExactlyDoublesDamageForAHighCritRatioMove();
  applyStatStageMultiplierMatchesGen1Table();
  applyAccuracyEvasionStageMultiplierMatchesGen1Table();
  statChangeForMoveIdentifiesSelfBuffsAndOpponentDebuffs();
  selfBuffMoveRaisesAttackStageAndReportsEvent();
  opponentDebuffMoveLowersDefendersStageNotTheUsers();
  statChangeAtCapReportsFailureInsteadOfExceedingBounds();
  hazeResetsBothSidesStatStages();
  applyBattleBoostItemRaisesEachXStatByOneStageUpToTheCap();
  guardSpecItemActivatesOnceAndBlocksOpponentStatLoweringMoves();
  guardSpecDoesNotBlockSelfBuffingStatMoves();
  direHitItemActivatesOnceAndRaisesCritRatioToTheHighTier();
  applyBattleBoostItemRejectsAnUnknownItemId();
  speedStageCanFlipWhichSideActsFirst();
  accuracyStageLoweringCanCauseAMissThatWouldOtherwiseHit();
  evasionStageRaisingCanCauseAMissThatWouldOtherwiseHit();
  criticalHitIgnoresAttackersUnfavorableNegativeAttackStage();
  criticalHitIgnoresDefendersUnfavorablePositiveDefenseStage();
  recoilMoveDamagesTheAttackerAfterDealingDamage();
  nonRecoilMoveNeverAppliesRecoil();
  multiHitMoveConnectsMultipleTimesAndReportsHitCount();
  twineedleAlwaysHitsExactlyTwice();
  nonMultiHitMoveReportsZeroHitCount();
  multiHitMoveStopsEarlyIfTheDefenderFaintsPartway();
  forcedStruggleSentinelDealsDamageAndRecoilsWithNoLearnedMove();
  opponentStrugglesWhenAllOfItsLearnedMovesAreOutOfPp();
  ivZeroEvZeroReproducesTheOriginalFormulaExactly();
  ivAndEvRaiseStatsAboveTheZeroBaseline();
  evBonusMatchesTheFlatDivideByFourFormula();
  outOfRangeIvIsClampedToTheRealGen1Ceiling();
  rollIvSetProducesValuesInTheRealGen1Range();
  maxPpForMatchesTheRealGen1PpUpProgression();
  battleVictoryXpScalesWithLevelAndTrainerBonus();
  ohkoMoveInstantlyFaintsWhenAttackerLevelIsAtLeastDefenders();
  ohkoMoveNeverConnectsWhenAttackerIsALowerLevel();
  ohkoMoveHasNoEffectOnAnImmuneType();
  seismicTossAndNightShadeDealDamageEqualToUsersLevel();
  dragonRageAndSonicBoomDealFixedFlatDamage();
  psywaveDamageIsBoundedBetweenOneAndOnePointFiveTimesLevel();
  superFangHalvesDefendersCurrentHp();
  lowKickDealsRealDamageInsteadOfTheOldFlatBug();
  counterReflectsDoubleTheLastPhysicalDamageTakenThisTurn();
  counterFailsWhenUserHasNotTakenPhysicalDamageThisTurn();
  return failures == 0 ? 0 : 1;
}
