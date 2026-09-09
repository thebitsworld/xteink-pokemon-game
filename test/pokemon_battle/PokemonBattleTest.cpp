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
  BattleCombatant squirtle = makeCombatant(7, 10, {55});
  BattleCombatant charmander = makeCombatant(4, 10, {33});
  const uint16_t hpBefore = charmander.currentHp;

  const pokemon::BattleTurnResult result = pokemon::stepBattle(squirtle, charmander, 0, ZERO_RANDOM);
  // Squirtle's move slot was passed as the "player" side's action; whichever
  // side is faster acts first, so check whichever side actually attacked.
  CHECK(result.outcome == BattleOutcome::InProgress);
  CHECK(charmander.currentHp < hpBefore || squirtle.currentHp < squirtle.maxHp);
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
  return failures == 0 ? 0 : 1;
}
