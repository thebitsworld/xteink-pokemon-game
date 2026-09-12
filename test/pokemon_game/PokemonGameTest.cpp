#include <array>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <string_view>

#include "Pokemon/PokemonGame.h"
#include "Pokemon/PokemonSpecies.h"

namespace {

int failures = 0;

#define CHECK(condition)                                                                \
  do {                                                                                  \
    if (!(condition)) {                                                                 \
      std::fprintf(stderr, "%s:%d check failed: %s\n", __FILE__, __LINE__, #condition); \
      ++failures;                                                                       \
    }                                                                                   \
  } while (false)

uint32_t chooseFirst(void*, uint32_t) { return 0; }

uint32_t chooseLast(void*, const uint32_t upperExclusive) { return upperExclusive - 1U; }

struct SequenceRandom {
  const uint32_t* values;
  size_t count;
  size_t index = 0;

  static uint32_t next(void* context, const uint32_t upperExclusive) {
    auto& sequence = *static_cast<SequenceRandom*>(context);
    if (sequence.index >= sequence.count) {
      std::fprintf(stderr, "unexpected random draw: upper=%u after %zu values\n", upperExclusive, sequence.count);
      CHECK(false);
      return 0;
    }
    const uint32_t value = sequence.values[sequence.index++];
    CHECK(value < upperExclusive);
    return value;
  }
};

uint32_t rejectUnexpectedDraw(void*, uint32_t) {
  CHECK(false);
  return 0;
}

pokemon::PokemonRecord leaderAtLevelFive() {
  pokemon::PokemonRecord leader{};
  leader.recordId = 7;
  leader.totalXp = 52;
  leader.speciesId = 25;
  leader.caughtLevel = 5;
  leader.gender = pokemon::Gender::Female;
  leader.origin = pokemon::Origin::Starter;
  return leader;
}

pokemon::PokemonState stateWithLeader(const pokemon::PokemonRecord& leader) {
  pokemon::PokemonState state{};
  state.partyRecordIds[0] = leader.recordId;
  return state;
}

// Ball/medicine/TM-HM/encounter all now check at the same 15-minute cadence,
// each with its own independent pity counter (Stage 23) - a test that spans
// enough 15-minute windows for more than one track to reach its own pity can
// have more than one land in the 3-slot pending queue in the same window, in
// whichever order those tracks happen to run. Tests that care about one
// specific track's event use this to find it regardless of which slot the
// interleaving left it in, rather than assuming a fixed index.
const pokemon::PendingEvent* findEventOfKind(const pokemon::PokemonState& state, const pokemon::PendingEventKind kind) {
  for (const pokemon::PendingEvent& event : state.pendingEvents) {
    if (event.kind == kind) return &event;
  }
  return nullptr;
}

void collectionActionsExcludeOperationsThatCannotSucceed() {
  const pokemon::CollectionActionSet loneParty = pokemon::collectionActions(true, 1);
  CHECK(loneParty.count == 4);
  CHECK(loneParty.items[0] == pokemon::CollectionAction::Summary);
  CHECK(loneParty.items[1] == pokemon::CollectionAction::Moveset);
  CHECK(loneParty.items[2] == pokemon::CollectionAction::Rename);
  CHECK(loneParty.items[3] == pokemon::CollectionAction::EvolutionPrompts);

  const pokemon::CollectionActionSet reorderableParty = pokemon::collectionActions(true, 2);
  CHECK(reorderableParty.count == 6);
  CHECK(reorderableParty.items[0] == pokemon::CollectionAction::Summary);
  CHECK(reorderableParty.items[1] == pokemon::CollectionAction::Moveset);
  CHECK(reorderableParty.items[2] == pokemon::CollectionAction::Move);
  CHECK(reorderableParty.items[3] == pokemon::CollectionAction::Deposit);
  CHECK(reorderableParty.items[4] == pokemon::CollectionAction::Rename);
  CHECK(reorderableParty.items[5] == pokemon::CollectionAction::EvolutionPrompts);

  const pokemon::CollectionActionSet fullPartyPc = pokemon::collectionActions(false, pokemon::PARTY_SIZE);
  CHECK(fullPartyPc.count == 4);
  CHECK(fullPartyPc.items[0] == pokemon::CollectionAction::Summary);
  CHECK(fullPartyPc.items[1] == pokemon::CollectionAction::Moveset);
  CHECK(fullPartyPc.items[2] == pokemon::CollectionAction::Rename);
  CHECK(fullPartyPc.items[3] == pokemon::CollectionAction::EvolutionPrompts);

  const pokemon::CollectionActionSet pcWithRoom = pokemon::collectionActions(false, pokemon::PARTY_SIZE - 1U);
  CHECK(pcWithRoom.count == 5);
  CHECK(pcWithRoom.items[0] == pokemon::CollectionAction::Summary);
  CHECK(pcWithRoom.items[1] == pokemon::CollectionAction::Moveset);
  CHECK(pcWithRoom.items[2] == pokemon::CollectionAction::Withdraw);
  CHECK(pcWithRoom.items[3] == pokemon::CollectionAction::Rename);
  CHECK(pcWithRoom.items[4] == pokemon::CollectionAction::EvolutionPrompts);
}

pokemon::Gender validGenderForSpecies(const uint16_t speciesId) {
  const uint8_t genderRate = pokemon::speciesData(speciesId)->genderRate;
  if (genderRate == 255) return pokemon::Gender::Genderless;
  if (genderRate == 0) return pokemon::Gender::Male;
  return pokemon::Gender::Female;
}

void creditedMinutesAdvanceLeaderAndReadingCounters() {
  pokemon::PokemonRecord leader = leaderAtLevelFive();
  pokemon::PokemonState state = stateWithLeader(leader);
  pokemon::RandomSource random{nullptr, chooseLast};

  const pokemon::CreditResult result =
      pokemon::applyCreditedMinutes(state, leader, 15, 10, pokemon::OwnedEvolutionNeeds{}, random);

  CHECK(result.status == pokemon::CreditStatus::Applied);
  CHECK(result.creditedMinutes == 15);
  CHECK(result.previousLevel == 5);
  CHECK(result.currentLevel == 5);
  CHECK(result.generatedEvent == pokemon::PendingEventKind::None);
  CHECK(leader.totalXp == 67);
  CHECK(state.lifetimeMinutes == 15);
  CHECK(state.readingMinuteRemainder == 15);
  CHECK(state.encounterMisses == 1);
}

void creditClampsAndRejectsInvalidCallsWithoutMutation() {
  pokemon::PokemonRecord leader = leaderAtLevelFive();
  leader.totalXp = 8339;
  pokemon::PokemonState state = stateWithLeader(leader);
  state.lifetimeMinutes = UINT32_MAX - 2U;
  pokemon::RandomSource random{nullptr, chooseFirst};

  const pokemon::CreditResult clamped =
      pokemon::applyCreditedMinutes(state, leader, 10, 20, pokemon::OwnedEvolutionNeeds{}, random);
  CHECK(clamped.status == pokemon::CreditStatus::Applied);
  CHECK(clamped.previousLevel == 99);
  CHECK(clamped.currentLevel == 100);
  CHECK(leader.totalXp == pokemon::MAXIMUM_TOTAL_XP);
  CHECK(state.lifetimeMinutes == UINT32_MAX);

  pokemon::PokemonRecord rejectedLeader = leaderAtLevelFive();
  pokemon::PokemonState rejectedState = stateWithLeader(rejectedLeader);
  rejectedState.partyRecordIds[0] = 99;
  const pokemon::PokemonRecord leaderBefore = rejectedLeader;
  const pokemon::PokemonState stateBefore = rejectedState;
  const pokemon::CreditResult rejected =
      pokemon::applyCreditedMinutes(rejectedState, rejectedLeader, 10, 20, pokemon::OwnedEvolutionNeeds{}, random);
  CHECK(rejected.status == pokemon::CreditStatus::Rejected);
  CHECK(rejectedLeader == leaderBefore);
  CHECK(rejectedState == stateBefore);

  rejectedState = stateWithLeader(rejectedLeader);
  const pokemon::CreditResult noChange =
      pokemon::applyCreditedMinutes(rejectedState, rejectedLeader, 0, 20, pokemon::OwnedEvolutionNeeds{}, random);
  CHECK(noChange.status == pokemon::CreditStatus::NoChange);
  CHECK(rejectedLeader == leaderBefore);
  CHECK(rejectedState == stateWithLeader(rejectedLeader));
}

void fourthEncounterCheckIsForcedAndCreatesOnlyOneEvent() {
  // Ball/medicine/TM-HM share the same pity threshold (3) as encounter and
  // all start fresh, so missing every 15-minute check for 3 calls in a row
  // means all four reach their own guarantee on the same (4th) call -
  // medicine and TM/HM land their Item events ahead of Encounter in the
  // queue (ball doesn't use the queue at all), which is why this asserts via
  // findEventOfKind() instead of assuming pendingEvents[0].
  pokemon::PokemonRecord leader = leaderAtLevelFive();
  pokemon::PokemonState state = stateWithLeader(leader);
  constexpr uint32_t draws[] = {
      3, 2, 2, 2,  // Call 1: ball/medicine/TM-HM/encounter all miss.
      3, 2, 2, 2,  // Call 2: same.
      3, 2, 2, 2,  // Call 3: same - all four now sit at their pity threshold.
      1,           // Call 4: hourly evolution-stone miss.
      0,           // Medicine forced (no roll) - Potion wins the item pick.
      0,           // TM/HM forced (no roll) - TM01 wins the item pick.
      1, 0, 0, 0,  // Encounter forced - legendary miss, first weighted species, minimum level, female roll.
  };
  SequenceRandom sequence{draws, std::size(draws)};
  pokemon::RandomSource random{&sequence, SequenceRandom::next};

  for (uint8_t check = 1; check <= 3; ++check) {
    const pokemon::CreditResult result =
        pokemon::applyCreditedMinutes(state, leader, 15, 10, pokemon::OwnedEvolutionNeeds{}, random);
    CHECK(result.status == pokemon::CreditStatus::Applied);
    CHECK(result.generatedEvent == pokemon::PendingEventKind::None);
    CHECK(state.encounterMisses == check);
    CHECK(state.itemMisses == 0);
  }

  const pokemon::CreditResult forced =
      pokemon::applyCreditedMinutes(state, leader, 15, 10, pokemon::OwnedEvolutionNeeds{}, random);
  CHECK(forced.status == pokemon::CreditStatus::Applied);
  CHECK(forced.generatedEvent == pokemon::PendingEventKind::Encounter);
  const pokemon::PendingEvent* encounterEvent = findEventOfKind(state, pokemon::PendingEventKind::Encounter);
  CHECK(encounterEvent != nullptr);
  if (encounterEvent != nullptr) {
    CHECK(encounterEvent->speciesId == 1);
    CHECK(encounterEvent->level == 2);
    CHECK(encounterEvent->gender == pokemon::Gender::Female);
  }
  CHECK(pokemon::pendingEventCount(state) == 3);  // medicine + TM/HM + encounter, all forced together
  CHECK(state.bagCounts[0] == 1);                 // Poke Ball, from the also-forced ball roll
  CHECK(state.encounterMisses == 0);
  CHECK(state.itemMisses == 1);
  CHECK(state.readingMinuteRemainder == 0);
  CHECK(state.lifetimeMinutes == 60);
  CHECK(sequence.index == std::size(draws));
}

void fifteenMinuteCheckUsesFortyPercentEncounterChance() {
  pokemon::PokemonRecord leader = leaderAtLevelFive();
  pokemon::PokemonState state = stateWithLeader(leader);
  constexpr uint32_t draws[] = {
      3,           // Ball miss.
      2,           // Medicine miss.
      2,           // TM/HM miss.
      1,           // Second successful value in a 2-in-5 encounter roll.
      1, 0, 0, 0,  // Legendary miss, first weighted species, minimum level, female roll.
  };
  SequenceRandom sequence{draws, std::size(draws)};
  pokemon::RandomSource random{&sequence, SequenceRandom::next};

  const pokemon::CreditResult result =
      pokemon::applyCreditedMinutes(state, leader, 15, 10, pokemon::OwnedEvolutionNeeds{}, random);

  CHECK(result.status == pokemon::CreditStatus::Applied);
  CHECK(result.generatedEvent == pokemon::PendingEventKind::Encounter);
  CHECK(state.pendingEvents[0].kind == pokemon::PendingEventKind::Encounter);
  CHECK(state.readingMinuteRemainder == 15);
  CHECK(sequence.index == std::size(draws));
}

void fullQueuePrimesGuaranteesButStillHandsOutBalls() {
  pokemon::PokemonRecord leader = leaderAtLevelFive();
  pokemon::PokemonState state = stateWithLeader(leader);
  state.pendingEvents[0] = {
      0, 81, 24, pokemon::Gender::Genderless, pokemon::EvolutionItem::None, pokemon::PendingEventKind::Encounter};
  state.pendingEvents[1] = {
      0, 0, 0, pokemon::Gender::Unknown, pokemon::EvolutionItem::MoonStone, pokemon::PendingEventKind::Item};
  state.pendingEvents[2] = {leader.recordId,
                            26,
                            0,
                            pokemon::Gender::Unknown,
                            pokemon::EvolutionItem::None,
                            pokemon::PendingEventKind::Evolution};
  state.readingMinuteRemainder = 59;
  const auto pendingBefore = state.pendingEvents;
  const uint32_t xpBefore = leader.totalXp;
  // A full queue short-circuits all three event-producing tracks (stone,
  // medicine, TM/HM, encounter) before they roll at all, so the only draws
  // left are the ball's own pity-gate roll and its kind pick - balls never
  // touch the queue precisely so a player who has not opened the app in a
  // while still ends up with something to throw. At 50% progress Poke and
  // Great Balls are both unlocked, so the kind pick needs its own roll too.
  constexpr uint32_t draws[] = {
      0,  // Ball pity-gate roll hits (0 < 3-in-5).
      0,  // Poke Ball wins the kind pick.
  };
  SequenceRandom sequence{draws, std::size(draws)};
  pokemon::RandomSource random{&sequence, SequenceRandom::next};

  const pokemon::CreditResult result =
      pokemon::applyCreditedMinutes(state, leader, 1, 50, pokemon::OwnedEvolutionNeeds{}, random);

  CHECK(result.status == pokemon::CreditStatus::Applied);
  CHECK(result.generatedEvent == pokemon::PendingEventKind::None);
  CHECK(state.pendingEvents == pendingBefore);
  CHECK(state.encounterMisses == 3);
  CHECK(state.itemMisses == 19);
  CHECK(state.readingMinuteRemainder == 0);
  CHECK(state.lifetimeMinutes == 1);
  CHECK(leader.totalXp == xpBefore + 1U);
  CHECK(state.bagCounts[0] == 1);
  CHECK(sequence.index == std::size(draws));
}

void oneBoundaryQueuesItemEncounterAndEvolutionInOrder() {
  pokemon::PokemonRecord leader = leaderAtLevelFive();
  leader.speciesId = 1;
  leader.totalXp = pokemon::xpRequired(16) - 1U;
  pokemon::PokemonState state = stateWithLeader(leader);
  state.readingMinuteRemainder = 59;
  state.encounterMisses = 3;
  state.itemMisses = 19;
  constexpr uint32_t draws[] = {
      0,           // Forced hourly stone: Moon Stone, the first one still with room.
      3,           // Ball miss.
      2, 2,        // Medicine miss, TM/HM miss.
      1, 0, 0, 0,  // Legendary miss, first species, minimum level, female roll.
  };
  SequenceRandom sequence{draws, std::size(draws)};
  pokemon::RandomSource random{&sequence, SequenceRandom::next};

  const pokemon::CreditResult result =
      pokemon::applyCreditedMinutes(state, leader, 1, 25, pokemon::OwnedEvolutionNeeds{}, random);

  CHECK(result.status == pokemon::CreditStatus::Applied);
  CHECK(result.generatedEvent == pokemon::PendingEventKind::Evolution);
  CHECK(pokemon::pendingEventCount(state) == 3);
  CHECK(state.pendingEvents[0].kind == pokemon::PendingEventKind::Item);
  CHECK(state.pendingEvents[1].kind == pokemon::PendingEventKind::Encounter);
  CHECK(state.pendingEvents[2].kind == pokemon::PendingEventKind::Evolution);
  CHECK(state.pendingEvents[2].recordId == leader.recordId);
  CHECK(state.pendingEvents[2].speciesId == 2);
  CHECK(state.dashboardNotice == pokemon::DashboardNotice::ItemFound);
  CHECK(sequence.index == std::size(draws));
}

void resolvingEventsPopsOnlyTheFrontAndRefreshesTheNotice() {
  pokemon::PokemonRecord leader = leaderAtLevelFive();
  leader.speciesId = 1;
  leader.totalXp = pokemon::xpRequired(16);
  pokemon::PokemonState state = stateWithLeader(leader);
  state.pendingEvents[0] = {
      0, 0, 0, pokemon::Gender::Unknown, pokemon::EvolutionItem::MoonStone, pokemon::PendingEventKind::Item};
  state.pendingEvents[1] = {
      0, 4, 6, pokemon::Gender::Female, pokemon::EvolutionItem::None, pokemon::PendingEventKind::Encounter};
  state.pendingEvents[2] = {leader.recordId,
                            2,
                            0,
                            pokemon::Gender::Unknown,
                            pokemon::EvolutionItem::None,
                            pokemon::PendingEventKind::Evolution};
  state.dashboardNotice = pokemon::DashboardNotice::ItemFound;

  CHECK(pokemon::acknowledgeItem(state, leader));
  CHECK(state.pendingEvents[0].kind == pokemon::PendingEventKind::Encounter);
  CHECK(state.dashboardNotice == pokemon::DashboardNotice::NewPokemon);

  pokemon::RecordMutation mutation{};
  CHECK(pokemon::resolveEncounter(state, leader, pokemon::EncounterChoice::Pass, nullptr, mutation));
  CHECK(state.pendingEvents[0].kind == pokemon::PendingEventKind::Evolution);
  CHECK(state.dashboardNotice == pokemon::DashboardNotice::WhatsThis);

  CHECK(pokemon::resolveEvolution(state, leader, pokemon::EvolutionChoice::Cancel, mutation));
  CHECK(pokemon::pendingEventCount(state) == 0);
  CHECK(state.dashboardNotice == pokemon::DashboardNotice::None);
}

void encounterDueWithLevelGainQueuesEncounterBeforeEvolution() {
  pokemon::PokemonRecord bulbasaur = leaderAtLevelFive();
  bulbasaur.speciesId = 1;
  bulbasaur.totalXp = pokemon::xpRequired(16) - 1U;
  pokemon::PokemonState state = stateWithLeader(bulbasaur);
  state.readingMinuteRemainder = 59;
  state.encounterMisses = 3;
  constexpr uint32_t draws[] = {
      1,           // Hourly evolution-stone miss.
      3,           // Ball miss.
      2, 2,        // Medicine miss, TM/HM miss.
      1, 0, 0, 0,  // Legendary miss, first species, minimum level, female roll.
  };
  SequenceRandom sequence{draws, std::size(draws)};
  pokemon::RandomSource random{&sequence, SequenceRandom::next};

  const pokemon::CreditResult result =
      pokemon::applyCreditedMinutes(state, bulbasaur, 1, 0, pokemon::OwnedEvolutionNeeds{}, random);

  CHECK(result.generatedEvent == pokemon::PendingEventKind::Evolution);
  CHECK(state.pendingEvents[0].kind == pokemon::PendingEventKind::Encounter);
  CHECK(state.pendingEvents[0].speciesId == 1);
  CHECK(state.pendingEvents[1].kind == pokemon::PendingEventKind::Evolution);
  CHECK(state.pendingEvents[1].speciesId == 2);
  CHECK(state.itemMisses == 1);
}

void forcedItemWithLevelGainQueuesItemBeforeEvolution() {
  pokemon::PokemonRecord bulbasaur = leaderAtLevelFive();
  bulbasaur.speciesId = 1;
  bulbasaur.totalXp = pokemon::xpRequired(16) - 1U;
  pokemon::PokemonState state = stateWithLeader(bulbasaur);
  state.readingMinuteRemainder = 59;
  state.itemMisses = 19;
  constexpr uint32_t draws[] = {
      0,  // Forced hourly stone: Moon Stone, the first one still with room.
      0,  // Ball pity-gate roll hits (0 < 3-in-5); only Poke Ball is unlocked
          // at 0% progress, so the kind pick itself needs no extra draw.
      2,
      2,  // Medicine miss, TM/HM miss.
      2,  // Miss the encounter due at the hourly boundary.
  };
  SequenceRandom sequence{draws, std::size(draws)};
  pokemon::RandomSource random{&sequence, SequenceRandom::next};

  const pokemon::CreditResult result =
      pokemon::applyCreditedMinutes(state, bulbasaur, 1, 0, pokemon::OwnedEvolutionNeeds{}, random);

  CHECK(result.generatedEvent == pokemon::PendingEventKind::Evolution);
  CHECK(state.pendingEvents[0].kind == pokemon::PendingEventKind::Item);
  CHECK(state.pendingEvents[1].kind == pokemon::PendingEventKind::Evolution);
  CHECK(state.itemCounts[0] == 1);  // the stone that dropped (Moon Stone)
  // The ball handed out at this same check - queues no event of its own.
  CHECK(state.bagCounts[0] == 1);
  CHECK(sequence.index == std::size(draws));
}

void multiHourCreditUsesLifetimeAtEachHourlyBoundary() {
  // Ball/medicine/TM-HM now check at the same 15-minute cadence as encounter,
  // each with its own pity counter - across 121 minutes (9 such checks) any
  // of them that actually enqueues an Item would compete for the same 3-slot
  // queue this test needs reserved for its 3 expected encounters. Medicine
  // and TM/HM are neutralized here the same way "everything sold out" would
  // in the real game: every one of their items maxed out, so even a forced
  // (pity) trigger finds zero candidates and enqueues nothing - see
  // processTrackDrop's `candidateCount == 0` early return. Ball still rolls
  // normally (it never touches the queue at all).
  pokemon::PokemonRecord leader = leaderAtLevelFive();
  pokemon::PokemonState state = stateWithLeader(leader);
  state.lifetimeMinutes = 1079;
  state.readingMinuteRemainder = 59;
  state.encounterMisses = 3;
  state.bagCounts.fill(UINT8_MAX);
  state.bagCounts[0] = state.bagCounts[1] = state.bagCounts[2] = state.bagCounts[3] = 0;  // balls stay available
  // PP Up and the 6 battle-boost items live outside bagCounts (their own
  // ppUpCount/battleBoostCounts fields), so they need their own "sold out"
  // cap to stay neutralized like every other medicine-track item here.
  state.ppUpCount = UINT8_MAX;
  state.battleBoostCounts.fill(UINT8_MAX);
  constexpr uint32_t draws[] = {
      1,
      3,
      2,
      2,
      1,
      0,
      0,
      0,  // 1080 minutes: stone/ball/medicine/TM-HM misses, then a regular encounter (forced).
      3,
      2,
      2,
      2,  // 1095 minutes: ball/medicine/TM-HM/encounter all miss.
      3,
      2,
      2,
      2,  // 1110 minutes: same.
      0,
      2,  // 1125 minutes: ball forced (Poke Ball), medicine/TM-HM forced but sold out
          // (0 draws), encounter misses (now at its own pity for 1140).
      1,
      3,
      2,
      2,
      1,
      0,
      0,
      0,  // 1140 minutes: same shape as 1080 (ball/medicine/TM-HM just reset).
      3,
      2,
      2,
      2,  // 1155 minutes.
      3,
      2,
      2,
      2,  // 1170 minutes.
      0,
      2,  // 1185 minutes: ball forced, medicine/TM-HM forced-but-sold-out, encounter misses
          // (now at its own pity for 1200).
      1,
      3,
      2,
      2,
      0,
      0,
      0,  // 1200 minutes: stone/ball/medicine/TM-HM misses, then Articuno (forced, legendary
          // hit, first uncaught bird, minimum level - no gender roll, birds are genderless).
  };
  SequenceRandom sequence{draws, std::size(draws)};
  pokemon::RandomSource random{&sequence, SequenceRandom::next};

  const pokemon::CreditResult result =
      pokemon::applyCreditedMinutes(state, leader, 121, 75, pokemon::OwnedEvolutionNeeds{}, random);

  CHECK(result.status == pokemon::CreditStatus::Applied);
  CHECK(result.generatedEvent == pokemon::PendingEventKind::Encounter);
  CHECK(state.pendingEvents[0].speciesId == 1);
  CHECK(state.pendingEvents[0].level == 14);
  CHECK(state.pendingEvents[2].speciesId == 144);
  CHECK(state.pendingEvents[2].level == 14);
  CHECK(state.lifetimeMinutes == 1200);
  CHECK(sequence.index == std::size(draws));
}

pokemon::PendingEvent forceEncounterAtProgress(const uint8_t progress, const uint32_t* draws, const size_t drawCount) {
  pokemon::PokemonRecord leader = leaderAtLevelFive();
  pokemon::PokemonState state = stateWithLeader(leader);
  state.encounterMisses = 3;
  // Ball/medicine/TM-HM all check at the same 15-minute cadence now, each
  // with its own fresh pity counter - a single 15-minute credit only ever
  // exercises their own trigger roll (never their own pity, which needs 3
  // misses in a row to reach), so each just needs one "miss" draw ahead of
  // the encounter's own (forced, via the preset encounterMisses) chain.
  std::array<uint32_t, 11> allDraws{
      3,  // Ball miss.
      2,  // Medicine miss.
      2,  // TM/HM miss.
  };
  size_t allCount = 3;
  for (size_t index = 0; index < drawCount; ++index) allDraws[allCount++] = draws[index];
  SequenceRandom sequence{allDraws.data(), allCount};
  pokemon::RandomSource random{&sequence, SequenceRandom::next};
  const pokemon::CreditResult result =
      pokemon::applyCreditedMinutes(state, leader, 15, progress, pokemon::OwnedEvolutionNeeds{}, random);
  CHECK(result.status == pokemon::CreditStatus::Applied);
  CHECK(result.generatedEvent == pokemon::PendingEventKind::Encounter);
  CHECK(sequence.index == allCount);
  return state.pendingEvents[0];
}

void progressBandsGateStagesAndEncounterLevels() {
  constexpr uint32_t earlyDraws[] = {1, 1, 4, 0};
  const pokemon::PendingEvent early = forceEncounterAtProgress(0, earlyDraws, std::size(earlyDraws));
  CHECK(early.speciesId == 4);
  CHECK(early.level == 6);
  CHECK(early.gender == pokemon::Gender::Female);

  constexpr uint32_t middleDraws[] = {1, 1, 7, 7};
  const pokemon::PendingEvent middle = forceEncounterAtProgress(50, middleDraws, std::size(middleDraws));
  CHECK(middle.speciesId == 2);
  CHECK(middle.level == 16);
  CHECK(middle.gender == pokemon::Gender::Male);

  constexpr uint32_t finalDraws[] = {1, 2, 10, 0};
  const pokemon::PendingEvent finalStage = forceEncounterAtProgress(75, finalDraws, std::size(finalDraws));
  CHECK(finalStage.speciesId == 3);
  CHECK(finalStage.level == 24);
  CHECK(finalStage.gender == pokemon::Gender::Female);
}

void everyEligibleRegularEncounterWeightIntervalSelectsItsSpecies() {
  constexpr uint8_t progressBands[] = {0, 25, 50, 75, 95};
  for (const uint8_t progress : progressBands) {
    uint32_t cumulativeWeight = 0;
    for (uint16_t speciesId = 1; speciesId <= pokemon::KANTO_SPECIES_COUNT; ++speciesId) {
      const pokemon::SpeciesData& species = *pokemon::speciesData(speciesId);
      const bool eligible = species.acquisition == pokemon::Acquisition::Wild &&
                            (species.stage != pokemon::EvolutionStage::Middle || progress >= 50) &&
                            (species.stage != pokemon::EvolutionStage::Final || progress >= 75);
      if (!eligible) continue;

      const uint32_t draws[] = {1, cumulativeWeight, 0, 0};
      const size_t drawCount = species.genderRate == 0 || species.genderRate == 8 || species.genderRate == 255 ? 3 : 4;
      const pokemon::PendingEvent event = forceEncounterAtProgress(progress, draws, drawCount);
      CHECK(event.speciesId == speciesId);
      uint8_t expectedMinimumLevel = 2;
      if (progress >= 95)
        expectedMinimumLevel = 18;
      else if (progress >= 75)
        expectedMinimumLevel = 14;
      else if (progress >= 50)
        expectedMinimumLevel = 9;
      else if (progress >= 25)
        expectedMinimumLevel = 5;
      CHECK(event.level == expectedMinimumLevel);
      const bool speciallyRare = speciesId == 1 || speciesId == 4 || speciesId == 7 || speciesId == 25;
      uint8_t weight = 1;
      if (!speciallyRare && species.captureRate >= 200)
        weight = 4;
      else if (!speciallyRare && species.captureRate >= 120)
        weight = 3;
      else if (!speciallyRare && species.captureRate >= 60)
        weight = 2;
      cumulativeWeight += weight;
    }
  }
}

void everyEncounterCheckHandsOutABallWithoutQueueingAnEvent() {
  // The whole point of pacing balls off the encounter clock: a reader who
  // keeps meeting Pokemon keeps getting something to throw. Four checks an
  // hour against ~1.8 encounters means the reserve grows instead of running
  // dry, and none of it costs a slot in the 3-deep pending-event queue.
  pokemon::PokemonRecord leader = leaderAtLevelFive();
  pokemon::PokemonState state = stateWithLeader(leader);
  // Two 15-minute checks in 30 minutes; ball hits both (only Poke Ball is
  // unlocked at 10% progress, so no separate kind-pick draw), medicine/TM-HM/
  // encounter all miss both times so nothing else queues.
  constexpr uint32_t draws[] = {
      0, 2, 2, 2,  // Check 1: ball hits, medicine/TM-HM/encounter miss.
      0, 2, 2, 2,  // Check 2: same.
  };
  SequenceRandom sequence{draws, std::size(draws)};
  pokemon::RandomSource random{&sequence, SequenceRandom::next};

  const pokemon::CreditResult result =
      pokemon::applyCreditedMinutes(state, leader, 30, 10, pokemon::OwnedEvolutionNeeds{}, random);

  CHECK(result.status == pokemon::CreditStatus::Applied);
  CHECK(result.generatedEvent == pokemon::PendingEventKind::None);
  CHECK(state.bagCounts[0] == 2);  // one Poke Ball per encounter check, 2 checks in 30 minutes
  CHECK(pokemon::pendingEventCount(state) == 0);
  CHECK(state.dashboardNotice == pokemon::DashboardNotice::None);
  CHECK(sequence.index == std::size(draws));
}

void ballKindsUnlockWithProgressAndMasterBallStaysOneAtATime() {
  pokemon::PokemonRecord leader = leaderAtLevelFive();

  // Below 50% only Poke Balls exist, so there is nothing to roll between.
  // Every case here has the ball's own pity-gate roll hit (0 < 3-in-5) and
  // medicine/TM-HM/encounter all miss, so only the ball's kind pick (when
  // there is more than one unlocked kind to choose from) varies.
  pokemon::PokemonState earlyState = stateWithLeader(leader);
  constexpr uint32_t earlyDraws[] = {0, 2, 2, 2};  // Ball hits (no pick needed), medicine/TM-HM/encounter miss.
  SequenceRandom earlySequence{earlyDraws, std::size(earlyDraws)};
  pokemon::RandomSource earlyRandom{&earlySequence, SequenceRandom::next};
  CHECK(pokemon::applyCreditedMinutes(earlyState, leader, 15, 49, pokemon::OwnedEvolutionNeeds{}, earlyRandom).status ==
        pokemon::CreditStatus::Applied);
  CHECK(earlyState.bagCounts[0] == 1);
  CHECK(earlySequence.index == std::size(earlyDraws));

  // From 50% Great Balls join in: weights are Poke 40 then Great 20, so a
  // roll of 40 is the first value that lands past Poke Ball.
  pokemon::PokemonState greatState = stateWithLeader(leader);
  constexpr uint32_t greatDraws[] = {0, 40, 2, 2, 2};
  SequenceRandom greatSequence{greatDraws, std::size(greatDraws)};
  pokemon::RandomSource greatRandom{&greatSequence, SequenceRandom::next};
  CHECK(pokemon::applyCreditedMinutes(greatState, leader, 15, 50, pokemon::OwnedEvolutionNeeds{}, greatRandom).status ==
        pokemon::CreditStatus::Applied);
  CHECK(greatState.bagCounts[1] == 1);  // bagCounts[1] = item id 8 (Great Ball)
  CHECK(greatSequence.index == std::size(greatDraws));

  // At 95% all four are unlocked (total weight 69) and Master Ball sits at
  // the very top, [68,69).
  pokemon::PokemonState masterState = stateWithLeader(leader);
  constexpr uint32_t masterDraws[] = {0, 68, 2, 2, 2};
  SequenceRandom masterSequence{masterDraws, std::size(masterDraws)};
  pokemon::RandomSource masterRandom{&masterSequence, SequenceRandom::next};
  CHECK(
      pokemon::applyCreditedMinutes(masterState, leader, 15, 95, pokemon::OwnedEvolutionNeeds{}, masterRandom).status ==
      pokemon::CreditStatus::Applied);
  CHECK(masterState.bagCounts[3] == 1);  // bagCounts[3] = item id 10 (Master Ball)
  CHECK(masterSequence.index == std::size(masterDraws));

  // Holding one takes it back out of the pool, so the next check is back to
  // three kinds (total weight 68) and 68 is no longer a legal roll there.
  constexpr uint32_t heldDraws[] = {0, 67, 2, 2, 2};
  SequenceRandom heldSequence{heldDraws, std::size(heldDraws)};
  pokemon::RandomSource heldRandom{&heldSequence, SequenceRandom::next};
  CHECK(pokemon::applyCreditedMinutes(masterState, leader, 15, 95, pokemon::OwnedEvolutionNeeds{}, heldRandom).status ==
        pokemon::CreditStatus::Applied);
  CHECK(masterState.bagCounts[3] == 1);  // still exactly one - no second Master Ball
  CHECK(masterState.bagCounts[2] == 1);  // bagCounts[2] = item id 9 (Ultra Ball), [60,68)
  CHECK(heldSequence.index == std::size(heldDraws));
}

void medicineAndMachineRollOnSeparateFifteenMinuteTracks() {
  // Each track owns its own 15-minute-cadence pity roll, so Machine's 55 ids
  // can no longer swallow the odds that used to be shared with every other
  // category, and tuning medicine's 2-in-5 chance never touches TM/HM's.
  pokemon::PokemonRecord leader = leaderAtLevelFive();
  pokemon::PokemonState state = stateWithLeader(leader);
  state.readingMinuteRemainder = 59;
  constexpr uint32_t medicineDraws[] = {
      1,  // Hourly evolution-stone miss.
      3,  // Ball miss.
      0,  // Medicine track hits (2-in-5).
      0,  // Potion, the first medicine id and the heaviest.
      2,  // TM/HM track misses.
      2,  // Miss the encounter due at the same boundary.
  };
  SequenceRandom medicineSequence{medicineDraws, std::size(medicineDraws)};
  pokemon::RandomSource medicineRandom{&medicineSequence, SequenceRandom::next};
  const pokemon::CreditResult medicineResult =
      pokemon::applyCreditedMinutes(state, leader, 1, 25, pokemon::OwnedEvolutionNeeds{}, medicineRandom);
  CHECK(medicineResult.generatedEvent == pokemon::PendingEventKind::Item);
  CHECK(static_cast<uint8_t>(state.pendingEvents[0].item) == 11);  // Potion
  CHECK(state.bagCounts[4] == 1);                                  // bagCounts[4] = item id 11 (Potion)
  CHECK(medicineSequence.index == std::size(medicineDraws));

  state = stateWithLeader(leader);
  state.readingMinuteRemainder = 59;
  constexpr uint32_t machineDraws[] = {
      1,  // Hourly evolution-stone miss.
      3,  // Ball miss.
      2,  // Medicine track misses.
      0,  // TM/HM track hits (2-in-5).
      0,  // TM01, the first machine id.
      2,  // Miss the encounter due at the same boundary.
  };
  SequenceRandom machineSequence{machineDraws, std::size(machineDraws)};
  pokemon::RandomSource machineRandom{&machineSequence, SequenceRandom::next};
  const pokemon::CreditResult machineResult =
      pokemon::applyCreditedMinutes(state, leader, 1, 25, pokemon::OwnedEvolutionNeeds{}, machineRandom);
  CHECK(machineResult.generatedEvent == pokemon::PendingEventKind::Item);
  CHECK(static_cast<uint8_t>(state.pendingEvents[0].item) == 29);  // TM01
  CHECK(state.bagCounts[22] == 1);                                 // bagCounts[22] = item id 29 (TM01)
  CHECK(machineSequence.index == std::size(machineDraws));
}

void itemRollAndPityPreferAnOwnedEvolutionNeed() {
  pokemon::PokemonRecord leader = leaderAtLevelFive();
  pokemon::PokemonState state = stateWithLeader(leader);
  state.readingMinuteRemainder = 59;
  constexpr uint32_t randomItemDraws[] = {
      0,  // Trigger the hourly evolution-stone roll (Thunder Stone is the
          // only candidate the mask allows, so no separate pick draw).
      3,  // Ball miss.
      2,  // Medicine miss.
      2,  // TM/HM miss.
      2,  // Miss the encounter due at the same boundary.
  };
  SequenceRandom randomItemSequence{randomItemDraws, std::size(randomItemDraws)};
  pokemon::RandomSource randomItem{&randomItemSequence, SequenceRandom::next};
  const pokemon::OwnedEvolutionNeeds thunderNeeded{1U << 2U};

  pokemon::CreditResult result = pokemon::applyCreditedMinutes(state, leader, 1, 25, thunderNeeded, randomItem);
  CHECK(result.status == pokemon::CreditStatus::Applied);
  CHECK(result.generatedEvent == pokemon::PendingEventKind::Item);
  CHECK(state.pendingEvents[0].kind == pokemon::PendingEventKind::Item);
  CHECK(state.pendingEvents[0].item == pokemon::EvolutionItem::ThunderStone);
  CHECK(state.itemCounts[2] == 1);
  CHECK(state.itemMisses == 0);
  CHECK(state.encounterMisses == 1);
  CHECK(state.dashboardNotice == pokemon::DashboardNotice::ItemFound);

  CHECK(pokemon::acknowledgeItem(state, leader));
  CHECK(state.pendingEvents[0].kind == pokemon::PendingEventKind::None);
  CHECK(state.dashboardNotice == pokemon::DashboardNotice::None);

  state.itemMisses = 19;
  state.readingMinuteRemainder = 59;
  pokemon::RandomSource pityRandom{nullptr, chooseLast};
  result = pokemon::applyCreditedMinutes(state, leader, 1, 25, thunderNeeded, pityRandom);
  CHECK(result.generatedEvent == pokemon::PendingEventKind::Item);
  CHECK(state.pendingEvents[0].item == pokemon::EvolutionItem::ThunderStone);
  CHECK(state.itemCounts[2] == 2);
}

void saturatedItemsDoNotRejectReadingCredit() {
  // All six evolution stones saturated means the stone track simply has
  // nothing to give this hour - it no longer spills over into the other
  // categories, because those collect on their own schedules now. Reading
  // credit still applies, the other tracks still fire, and the ball handed
  // out at the same check lands regardless.
  pokemon::PokemonRecord leader = leaderAtLevelFive();
  pokemon::PokemonState state = stateWithLeader(leader);
  state.itemCounts.fill(UINT16_MAX);
  state.itemMisses = 19;
  state.readingMinuteRemainder = 59;
  constexpr uint32_t draws[] = {
      0,  // Ball hits (only Poke Ball unlocked at 25%, so no pick draw needed).
      0,  // Medicine track hits - the forced stone roll found nothing to give.
      0,  // Potion, the first medicine id.
      2,  // TM/HM track misses.
      2,  // Miss the encounter due at the same boundary.
  };
  SequenceRandom sequence{draws, std::size(draws)};
  pokemon::RandomSource random{&sequence, SequenceRandom::next};

  const pokemon::CreditResult result =
      pokemon::applyCreditedMinutes(state, leader, 1, 25, pokemon::OwnedEvolutionNeeds{}, random);

  CHECK(result.status == pokemon::CreditStatus::Applied);
  CHECK(result.generatedEvent == pokemon::PendingEventKind::Item);
  CHECK(state.pendingEvents[0].kind == pokemon::PendingEventKind::Item);
  CHECK(static_cast<uint8_t>(state.pendingEvents[0].item) == 11);  // Potion, not a stone
  CHECK(leader.totalXp == 53);
  CHECK(state.lifetimeMinutes == 1);
  CHECK(state.itemMisses == 0);
  CHECK(state.encounterMisses == 1);
  CHECK(sequence.index == std::size(draws));
  for (const uint16_t count : state.itemCounts) CHECK(count == UINT16_MAX);  // stones themselves stay untouched
  CHECK(state.bagCounts[4] == 1);                                            // bagCounts[4] = item id 11 (Potion)
  CHECK(state.bagCounts[0] == 1);                                            // bagCounts[0] = item id 7 (Poke Ball)

  state = stateWithLeader(leader);
  state.itemCounts[2] = UINT16_MAX;
  state.itemMisses = 19;
  state.readingMinuteRemainder = 59;
  constexpr uint32_t fallbackDraws[] = {
      0,  // Moon Stone: Thunder Stone is needed but saturated, so the roll
          // widens to any stone with room and lands on the first of them.
      3,  // Ball miss.
      2,  // Medicine miss.
      2,  // TM/HM miss.
      2,  // Miss the encounter due at the same boundary.
  };
  SequenceRandom fallbackSequence{fallbackDraws, std::size(fallbackDraws)};
  pokemon::RandomSource fallbackRandom{&fallbackSequence, SequenceRandom::next};
  const pokemon::OwnedEvolutionNeeds thunderNeeded{1U << 2U};
  const pokemon::CreditResult fallback =
      pokemon::applyCreditedMinutes(state, leader, 1, 25, thunderNeeded, fallbackRandom);
  CHECK(fallback.status == pokemon::CreditStatus::Applied);
  CHECK(state.pendingEvents[0].item == pokemon::EvolutionItem::MoonStone);
  CHECK(state.itemCounts[0] == 1);
  CHECK(state.itemCounts[2] == UINT16_MAX);
}

void legendaryEligibilityAndMewOverrideRegularEncounters() {
  pokemon::PokemonRecord leader = leaderAtLevelFive();
  pokemon::PokemonState state = stateWithLeader(leader);
  state.encounterMisses = 3;
  state.readingMinuteRemainder = 59;
  state.lifetimeMinutes = 1199;
  constexpr uint32_t birdDraws[] = {
      1,     // Hourly evolution-stone miss.
      3,     // Ball miss.
      2, 2,  // Medicine miss, TM/HM miss.
      0,     // Legendary roll hits.
      0,     // Articuno, first of the three uncaught birds.
      0,     // Minimum level for the band.
  };
  SequenceRandom birdSequence{birdDraws, std::size(birdDraws)};
  pokemon::RandomSource birdRandom{&birdSequence, SequenceRandom::next};

  pokemon::CreditResult result =
      pokemon::applyCreditedMinutes(state, leader, 1, 75, pokemon::OwnedEvolutionNeeds{}, birdRandom);
  CHECK(result.generatedEvent == pokemon::PendingEventKind::Encounter);
  CHECK(state.pendingEvents[0].speciesId == 144);
  CHECK(state.pendingEvents[0].level == 14);
  CHECK(state.pendingEvents[0].gender == pokemon::Gender::Genderless);

  // Ball/medicine/TM-HM misses carry over between these calls (unlike
  // encounterMisses, which each block re-primes to 3) - reset them too so
  // each block's "miss" draws don't accidentally accumulate into one of
  // those tracks reaching its own pity and enqueueing an unrelated Item
  // event ahead of the encounter this block is actually testing.
  state.pendingEvents[0] = {};
  state.dashboardNotice = pokemon::DashboardNotice::None;
  state.encounterMisses = 3;
  state.ballMisses = 0;
  state.medicineMisses = 0;
  state.machineMisses = 0;
  state.readingMinuteRemainder = 59;
  state.lifetimeMinutes = 2999;
  CHECK(pokemon::markSpecies(state.caughtSpecies, 144));
  CHECK(pokemon::markSpecies(state.seenSpecies, 144));
  CHECK(pokemon::markSpecies(state.caughtSpecies, 145));
  CHECK(pokemon::markSpecies(state.seenSpecies, 145));
  CHECK(pokemon::markSpecies(state.caughtSpecies, 146));
  CHECK(pokemon::markSpecies(state.seenSpecies, 146));
  constexpr uint32_t mewtwoDraws[] = {
      1,     // Hourly evolution-stone miss.
      3,     // Ball miss.
      2, 2,  // Medicine miss, TM/HM miss.
      0,     // Legendary roll hits - Mewtwo is the only uncaught one left.
      0,     // Minimum level for the band.
  };
  SequenceRandom mewtwoSequence{mewtwoDraws, std::size(mewtwoDraws)};
  pokemon::RandomSource mewtwoRandom{&mewtwoSequence, SequenceRandom::next};
  result = pokemon::applyCreditedMinutes(state, leader, 1, 95, pokemon::OwnedEvolutionNeeds{}, mewtwoRandom);
  CHECK(result.generatedEvent == pokemon::PendingEventKind::Encounter);
  CHECK(state.pendingEvents[0].speciesId == 150);
  CHECK(state.pendingEvents[0].level == 18);

  // Ball/medicine/TM-HM misses carry over between these calls (unlike
  // encounterMisses, which each block re-primes to 3) - reset them too so
  // each block's "miss" draws don't accidentally accumulate into one of
  // those tracks reaching its own pity and enqueueing an unrelated Item
  // event ahead of the encounter this block is actually testing.
  state.pendingEvents[0] = {};
  state.dashboardNotice = pokemon::DashboardNotice::None;
  state.encounterMisses = 3;
  state.ballMisses = 0;
  state.medicineMisses = 0;
  state.machineMisses = 0;
  state.readingMinuteRemainder = 59;
  CHECK(pokemon::markSpecies(state.caughtSpecies, 150));
  CHECK(pokemon::markSpecies(state.seenSpecies, 150));
  constexpr uint32_t duplicateDraws[] = {
      1,     // Hourly evolution-stone miss.
      3,     // Ball miss.
      2, 2,  // Medicine miss, TM/HM miss.
      0,     // Legendary roll hits.
      3,     // All four are caught already, so the pick falls back to the full list.
      0,     // Minimum level for the band.
  };
  SequenceRandom duplicateSequence{duplicateDraws, std::size(duplicateDraws)};
  pokemon::RandomSource duplicateRandom{&duplicateSequence, SequenceRandom::next};
  result = pokemon::applyCreditedMinutes(state, leader, 1, 95, pokemon::OwnedEvolutionNeeds{}, duplicateRandom);
  CHECK(result.generatedEvent == pokemon::PendingEventKind::Encounter);
  CHECK(state.pendingEvents[0].speciesId == 150);
  CHECK(duplicateSequence.index == std::size(duplicateDraws));

  // Ball/medicine/TM-HM misses carry over between these calls (unlike
  // encounterMisses, which each block re-primes to 3) - reset them too so
  // each block's "miss" draws don't accidentally accumulate into one of
  // those tracks reaching its own pity and enqueueing an unrelated Item
  // event ahead of the encounter this block is actually testing.
  state.pendingEvents[0] = {};
  state.dashboardNotice = pokemon::DashboardNotice::None;
  state.encounterMisses = 3;
  state.ballMisses = 0;
  state.medicineMisses = 0;
  state.machineMisses = 0;
  state.readingMinuteRemainder = 59;
  for (uint16_t speciesId = 1; speciesId <= 150; ++speciesId) {
    CHECK(pokemon::markSpecies(state.seenSpecies, speciesId));
    CHECK(pokemon::markSpecies(state.caughtSpecies, speciesId));
  }
  constexpr uint32_t mewDraws[] = {
      1,     // Hourly evolution-stone miss.
      3,     // Ball miss.
      2, 2,  // Medicine miss, TM/HM miss.
      0,     // Minimum level - Mew skips the legendary roll entirely.
  };
  SequenceRandom mewSequence{mewDraws, std::size(mewDraws)};
  pokemon::RandomSource mewRandom{&mewSequence, SequenceRandom::next};
  result = pokemon::applyCreditedMinutes(state, leader, 1, 95, pokemon::OwnedEvolutionNeeds{}, mewRandom);
  CHECK(result.generatedEvent == pokemon::PendingEventKind::Encounter);
  CHECK(state.pendingEvents[0].speciesId == 151);
  CHECK(state.pendingEvents[0].level == 18);
  CHECK(mewSequence.index == std::size(mewDraws));
}

pokemon::PokemonState stateWithEncounter(const pokemon::PokemonRecord& leader) {
  pokemon::PokemonState state = stateWithLeader(leader);
  state.pendingEvents[0].kind = pokemon::PendingEventKind::Encounter;
  state.pendingEvents[0].speciesId = 4;
  state.pendingEvents[0].level = 6;
  state.pendingEvents[0].gender = pokemon::Gender::Female;
  CHECK(pokemon::markSpecies(state.seenSpecies, 4));
  return state;
}

void catchCreatesOneAppendAndPassCreatesNone() {
  const pokemon::PokemonRecord leader = leaderAtLevelFive();
  pokemon::PokemonState state = stateWithEncounter(leader);
  pokemon::RecordMutation mutation{};
  mutation.requestedRecordId = 42;

  CHECK(pokemon::resolveEncounter(state, leader, pokemon::EncounterChoice::Catch, "Ember", mutation));
  CHECK(mutation.kind == pokemon::RecordMutationKind::Append);
  CHECK(mutation.record.recordId == 42);
  CHECK(mutation.record.speciesId == 4);
  CHECK(mutation.record.totalXp == 68);
  CHECK(mutation.record.caughtLevel == 6);
  CHECK(mutation.record.gender == pokemon::Gender::Female);
  CHECK(mutation.record.origin == pokemon::Origin::Caught);
  CHECK(std::string_view(mutation.record.nickname.data()) == "Ember");
  CHECK(state.partyRecordIds[1] == 42);
  CHECK(pokemon::isSpeciesMarked(state.caughtSpecies, 4));
  CHECK(state.pendingEvents[0].kind == pokemon::PendingEventKind::None);

  state = stateWithEncounter(leader);
  const pokemon::PokemonState beforeInvalidNickname = state;
  mutation = {};
  mutation.requestedRecordId = 43;
  CHECK(!pokemon::resolveEncounter(state, leader, pokemon::EncounterChoice::Catch, "123456789012345678901234567890123",
                                   mutation));
  CHECK(state == beforeInvalidNickname);
  CHECK(mutation.kind == pokemon::RecordMutationKind::None);

  state = stateWithEncounter(leader);
  mutation = {};
  CHECK(pokemon::resolveEncounter(state, leader, pokemon::EncounterChoice::Pass, nullptr, mutation));
  CHECK(mutation.kind == pokemon::RecordMutationKind::None);
  CHECK(!pokemon::isSpeciesMarked(state.caughtSpecies, 4));
  CHECK(state.pendingEvents[0].kind == pokemon::PendingEventKind::None);
}

void fullPartyCatchLeavesTheNewRecordForPcStorage() {
  const pokemon::PokemonRecord leader = leaderAtLevelFive();
  pokemon::PokemonState state = stateWithEncounter(leader);
  state.partyRecordIds = {7, 8, 9, 10, 11, 12};
  const auto partyBefore = state.partyRecordIds;
  pokemon::RecordMutation mutation{};
  mutation.requestedRecordId = 99;
  CHECK(pokemon::resolveEncounter(state, leader, pokemon::EncounterChoice::Catch, nullptr, mutation));
  CHECK(state.partyRecordIds == partyBefore);
  CHECK(mutation.kind == pokemon::RecordMutationKind::Append);
  CHECK(mutation.record.recordId == 99);
}

void resolvingABlockingEncounterWaitsForTheNextLevel() {
  pokemon::PokemonRecord bulbasaur = leaderAtLevelFive();
  bulbasaur.speciesId = 1;
  bulbasaur.totalXp = pokemon::xpRequired(17) - 1U;
  pokemon::PokemonState state = stateWithEncounter(bulbasaur);
  pokemon::RecordMutation mutation{};

  CHECK(pokemon::resolveEncounter(state, bulbasaur, pokemon::EncounterChoice::Pass, nullptr, mutation));
  CHECK(state.pendingEvents[0].kind == pokemon::PendingEventKind::None);

  const pokemon::RandomSource random{nullptr, rejectUnexpectedDraw};
  const pokemon::CreditResult result =
      pokemon::applyCreditedMinutes(state, bulbasaur, 1, 10, pokemon::OwnedEvolutionNeeds{}, random);
  CHECK(result.generatedEvent == pokemon::PendingEventKind::Evolution);
  CHECK(state.pendingEvents[0].kind == pokemon::PendingEventKind::Evolution);
  CHECK(state.pendingEvents[0].speciesId == 2);
}

void acknowledgingABlockingItemWaitsForTheNextLevel() {
  pokemon::PokemonRecord bulbasaur = leaderAtLevelFive();
  bulbasaur.speciesId = 1;
  bulbasaur.totalXp = pokemon::xpRequired(17) - 1U;
  pokemon::PokemonState state = stateWithLeader(bulbasaur);
  state.pendingEvents[0].kind = pokemon::PendingEventKind::Item;
  state.pendingEvents[0].item = pokemon::EvolutionItem::MoonStone;
  state.dashboardNotice = pokemon::DashboardNotice::ItemFound;

  CHECK(pokemon::acknowledgeItem(state, bulbasaur));
  CHECK(state.pendingEvents[0].kind == pokemon::PendingEventKind::None);

  const pokemon::RandomSource random{nullptr, rejectUnexpectedDraw};
  const pokemon::CreditResult result =
      pokemon::applyCreditedMinutes(state, bulbasaur, 1, 10, pokemon::OwnedEvolutionNeeds{}, random);
  CHECK(result.generatedEvent == pokemon::PendingEventKind::Evolution);
  CHECK(state.pendingEvents[0].kind == pokemon::PendingEventKind::Evolution);
  CHECK(state.pendingEvents[0].speciesId == 2);
}

void levelEvolutionPromptsOnceAndCanBeConfirmedOrBlocked() {
  pokemon::PokemonRecord bulbasaur = leaderAtLevelFive();
  bulbasaur.speciesId = 1;
  bulbasaur.totalXp = 287;
  pokemon::PokemonState state = stateWithLeader(bulbasaur);
  pokemon::RandomSource random{nullptr, chooseLast};

  pokemon::CreditResult result =
      pokemon::applyCreditedMinutes(state, bulbasaur, 31, 10, pokemon::OwnedEvolutionNeeds{}, random);
  CHECK(result.generatedEvent == pokemon::PendingEventKind::Evolution);
  CHECK(state.pendingEvents[0].kind == pokemon::PendingEventKind::Evolution);
  CHECK(state.pendingEvents[0].recordId == bulbasaur.recordId);
  CHECK(state.pendingEvents[0].speciesId == 2);
  CHECK(state.dashboardNotice == pokemon::DashboardNotice::WhatsThis);

  pokemon::RecordMutation mutation{};
  CHECK(pokemon::resolveEvolution(state, bulbasaur, pokemon::EvolutionChoice::Evolve, mutation));
  CHECK(mutation.kind == pokemon::RecordMutationKind::Replace);
  CHECK(mutation.record.speciesId == 2);
  CHECK(bulbasaur.speciesId == 2);
  CHECK(pokemon::isSpeciesMarked(state.caughtSpecies, 2));
  CHECK(state.pendingEvents[0].kind == pokemon::PendingEventKind::None);

  bulbasaur = leaderAtLevelFive();
  bulbasaur.speciesId = 1;
  bulbasaur.totalXp = 287;
  bulbasaur.flags = pokemon::recordFlag(pokemon::RecordFlag::EvolutionPromptsDisabled);
  state = stateWithLeader(bulbasaur);
  result = pokemon::applyCreditedMinutes(state, bulbasaur, 31, 10, pokemon::OwnedEvolutionNeeds{}, random);
  CHECK(result.generatedEvent == pokemon::PendingEventKind::None);
  CHECK(state.pendingEvents[0].kind == pokemon::PendingEventKind::None);
}

void reenablingPromptsWaitsForTheNextLevel() {
  pokemon::PokemonRecord bulbasaur = leaderAtLevelFive();
  bulbasaur.speciesId = 1;
  bulbasaur.totalXp = pokemon::xpRequired(17) - 1U;
  bulbasaur.flags = pokemon::recordFlag(pokemon::RecordFlag::EvolutionPromptsDisabled);
  pokemon::PokemonState state = stateWithLeader(bulbasaur);
  pokemon::RecordMutation mutation{};

  CHECK(pokemon::setEvolutionPrompts(state, bulbasaur, true, mutation));
  CHECK((bulbasaur.flags & pokemon::recordFlag(pokemon::RecordFlag::EvolutionPromptsDisabled)) == 0);
  CHECK(mutation.requestedRecordId == bulbasaur.recordId);
  CHECK(mutation.kind == pokemon::RecordMutationKind::Replace);
  CHECK(state.pendingEvents[0].kind == pokemon::PendingEventKind::None);

  const pokemon::RandomSource random{nullptr, rejectUnexpectedDraw};
  const pokemon::CreditResult result =
      pokemon::applyCreditedMinutes(state, bulbasaur, 1, 10, pokemon::OwnedEvolutionNeeds{}, random);
  CHECK(result.generatedEvent == pokemon::PendingEventKind::Evolution);
  CHECK(state.pendingEvents[0].kind == pokemon::PendingEventKind::Evolution);
  CHECK(state.pendingEvents[0].speciesId == 2);

  mutation = {};
  CHECK(pokemon::setEvolutionPrompts(state, bulbasaur, false, mutation));
  CHECK((bulbasaur.flags & pokemon::recordFlag(pokemon::RecordFlag::EvolutionPromptsDisabled)) != 0);
  CHECK(mutation.kind == pokemon::RecordMutationKind::Replace);
  CHECK(state.pendingEvents[0].kind == pokemon::PendingEventKind::None);
}

void levelHundredDoesNotCatchUpOrChainLevelEvolutions() {
  pokemon::PokemonRecord bulbasaur = leaderAtLevelFive();
  bulbasaur.speciesId = 1;
  bulbasaur.totalXp = pokemon::MAXIMUM_TOTAL_XP;
  bulbasaur.flags = pokemon::recordFlag(pokemon::RecordFlag::EvolutionPromptsDisabled);
  pokemon::PokemonState state = stateWithLeader(bulbasaur);
  pokemon::RecordMutation mutation{};

  CHECK(pokemon::setEvolutionPrompts(state, bulbasaur, true, mutation));
  CHECK(state.pendingEvents[0].kind == pokemon::PendingEventKind::None);

  state.pendingEvents[0].kind = pokemon::PendingEventKind::Evolution;
  state.pendingEvents[0].recordId = bulbasaur.recordId;
  state.pendingEvents[0].speciesId = 2;
  state.dashboardNotice = pokemon::DashboardNotice::WhatsThis;
  mutation = {};
  CHECK(pokemon::resolveEvolution(state, bulbasaur, pokemon::EvolutionChoice::Evolve, mutation));
  CHECK(bulbasaur.speciesId == 2);
  CHECK(state.pendingEvents[0].kind == pokemon::PendingEventKind::None);

  const pokemon::RandomSource random{nullptr, rejectUnexpectedDraw};
  const pokemon::CreditResult result =
      pokemon::applyCreditedMinutes(state, bulbasaur, 1, 10, pokemon::OwnedEvolutionNeeds{}, random);
  CHECK(result.currentLevel == 100);
  CHECK(result.generatedEvent == pokemon::PendingEventKind::None);
  CHECK(state.pendingEvents[0].kind == pokemon::PendingEventKind::None);
}

void promptTogglePreservesAnUnrelatedPendingEvent() {
  pokemon::PokemonRecord leader = leaderAtLevelFive();
  leader.flags = pokemon::recordFlag(pokemon::RecordFlag::EvolutionPromptsDisabled);
  pokemon::PokemonState state = stateWithEncounter(leader);
  const pokemon::PendingEvent pendingBefore = state.pendingEvents[0];
  pokemon::RecordMutation mutation{};

  CHECK(pokemon::setEvolutionPrompts(state, leader, true, mutation));
  CHECK((leader.flags & pokemon::recordFlag(pokemon::RecordFlag::EvolutionPromptsDisabled)) == 0);
  CHECK(state.pendingEvents[0] == pendingBefore);

  mutation = {};
  CHECK(pokemon::setEvolutionPrompts(state, leader, false, mutation));
  CHECK((leader.flags & pokemon::recordFlag(pokemon::RecordFlag::EvolutionPromptsDisabled)) != 0);
  CHECK(state.pendingEvents[0] == pendingBefore);
}

void disablingPromptsRemovesOnlyMatchingQueuedEvolutions() {
  pokemon::PokemonRecord leader = leaderAtLevelFive();
  pokemon::PokemonState state = stateWithLeader(leader);
  const pokemon::PendingEvent encounter{
      0, 4, 6, pokemon::Gender::Female, pokemon::EvolutionItem::None, pokemon::PendingEventKind::Encounter};
  const pokemon::PendingEvent evolution{leader.recordId,
                                        26,
                                        0,
                                        pokemon::Gender::Unknown,
                                        pokemon::EvolutionItem::None,
                                        pokemon::PendingEventKind::Evolution};
  const pokemon::PendingEvent item{
      0, 0, 0, pokemon::Gender::Unknown, pokemon::EvolutionItem::ThunderStone, pokemon::PendingEventKind::Item};
  state.pendingEvents = {encounter, evolution, item};
  state.dashboardNotice = pokemon::DashboardNotice::NewPokemon;
  pokemon::RecordMutation mutation{};

  CHECK(pokemon::setEvolutionPrompts(state, leader, false, mutation));
  CHECK(pokemon::pendingEventCount(state) == 2);
  CHECK(state.pendingEvents[0] == encounter);
  CHECK(state.pendingEvents[1] == item);
  CHECK(state.dashboardNotice == pokemon::DashboardNotice::NewPokemon);
  CHECK(mutation.kind == pokemon::RecordMutationKind::Replace);
}

void rejectedPromptToggleDoesNotPartiallyMutate() {
  pokemon::PokemonRecord leader = leaderAtLevelFive();
  leader.flags = pokemon::recordFlag(pokemon::RecordFlag::EvolutionPromptsDisabled);
  pokemon::PokemonState state = stateWithLeader(leader);
  pokemon::RecordMutation mutation{};
  mutation.requestedRecordId = 99;
  mutation.record = leaderAtLevelFive();
  mutation.kind = pokemon::RecordMutationKind::Append;
  const pokemon::PokemonRecord leaderBefore = leader;
  const pokemon::PokemonState stateBefore = state;
  const pokemon::RecordMutation mutationBefore = mutation;

  CHECK(!pokemon::setEvolutionPrompts(state, leader, true, mutation));
  CHECK(leader == leaderBefore);
  CHECK(state == stateBefore);
  CHECK(mutation.requestedRecordId == mutationBefore.requestedRecordId);
  CHECK(mutation.record == mutationBefore.record);
  CHECK(mutation.kind == mutationBefore.kind);
}

void everyLevelEvolutionQueuesAtItsThreshold() {
  size_t checked = 0;
  for (uint16_t speciesId = 1; speciesId <= pokemon::KANTO_SPECIES_COUNT; ++speciesId) {
    for (const pokemon::EvolutionRule& rule : pokemon::evolutionsFor(speciesId)) {
      if (rule.trigger != pokemon::EvolutionTrigger::Level) continue;
      pokemon::PokemonRecord record = leaderAtLevelFive();
      record.speciesId = speciesId;
      record.totalXp = pokemon::xpRequired(rule.minimumLevel) - 1U;
      record.gender = validGenderForSpecies(speciesId);
      pokemon::PokemonState state = stateWithLeader(record);
      pokemon::RandomSource random{nullptr, rejectUnexpectedDraw};

      const pokemon::CreditResult result =
          pokemon::applyCreditedMinutes(state, record, 1, 0, pokemon::OwnedEvolutionNeeds{}, random);
      CHECK(result.status == pokemon::CreditStatus::Applied);
      CHECK(state.pendingEvents[0].kind == pokemon::PendingEventKind::Evolution);
      CHECK(state.pendingEvents[0].speciesId == rule.targetSpeciesId);
      pokemon::RecordMutation mutation{};
      CHECK(pokemon::resolveEvolution(state, record, pokemon::EvolutionChoice::Evolve, mutation));
      CHECK(record.speciesId == rule.targetSpeciesId);
      CHECK(mutation.kind == pokemon::RecordMutationKind::Replace);
      CHECK(mutation.requestedRecordId == record.recordId);
      CHECK(mutation.record.speciesId == rule.targetSpeciesId);
      CHECK(pokemon::isSpeciesMarked(state.seenSpecies, rule.targetSpeciesId));
      CHECK(pokemon::isSpeciesMarked(state.caughtSpecies, rule.targetSpeciesId));
      ++checked;
    }
  }
  CHECK(checked == 52);
}

void everyItemEvolutionConsumesExactlyOneItem() {
  struct ItemEvolutionCase {
    uint16_t source;
    uint16_t target;
    pokemon::EvolutionItem item;
  };
  constexpr ItemEvolutionCase cases[] = {
      {25, 26, pokemon::EvolutionItem::ThunderStone},   {30, 31, pokemon::EvolutionItem::MoonStone},
      {33, 34, pokemon::EvolutionItem::MoonStone},      {35, 36, pokemon::EvolutionItem::MoonStone},
      {37, 38, pokemon::EvolutionItem::FireStone},      {39, 40, pokemon::EvolutionItem::MoonStone},
      {44, 45, pokemon::EvolutionItem::LeafStone},      {58, 59, pokemon::EvolutionItem::FireStone},
      {61, 62, pokemon::EvolutionItem::WaterStone},     {64, 65, pokemon::EvolutionItem::LinkCable},
      {67, 68, pokemon::EvolutionItem::LinkCable},      {70, 71, pokemon::EvolutionItem::LeafStone},
      {75, 76, pokemon::EvolutionItem::LinkCable},      {90, 91, pokemon::EvolutionItem::WaterStone},
      {93, 94, pokemon::EvolutionItem::LinkCable},      {102, 103, pokemon::EvolutionItem::LeafStone},
      {120, 121, pokemon::EvolutionItem::WaterStone},   {133, 134, pokemon::EvolutionItem::WaterStone},
      {133, 135, pokemon::EvolutionItem::ThunderStone}, {133, 136, pokemon::EvolutionItem::FireStone},
  };

  for (const ItemEvolutionCase& itemCase : cases) {
    pokemon::PokemonRecord record = leaderAtLevelFive();
    record.speciesId = itemCase.source;
    record.gender = validGenderForSpecies(itemCase.source);
    pokemon::PokemonState state{};
    const size_t itemIndex = static_cast<size_t>(itemCase.item) - 1U;
    state.itemCounts[itemIndex] = 1;
    pokemon::RecordMutation mutation{};
    CHECK(pokemon::useEvolutionItem(state, record, itemCase.item, mutation));
    CHECK(record.speciesId == itemCase.target);
    CHECK(state.itemCounts[itemIndex] == 0);
    CHECK(mutation.kind == pokemon::RecordMutationKind::Replace);
    CHECK(mutation.requestedRecordId == record.recordId);
    CHECK(mutation.record.speciesId == itemCase.target);
    CHECK(pokemon::isSpeciesMarked(state.caughtSpecies, itemCase.target));
  }
}

void rejectedInputsAndCancelledEvolutionDoNotPartiallyMutate() {
  pokemon::PokemonRecord leader = leaderAtLevelFive();
  pokemon::PokemonState state = stateWithLeader(leader);
  state.readingMinuteRemainder = 59;
  const pokemon::PokemonRecord leaderBefore = leader;
  const pokemon::PokemonState stateBefore = state;
  const pokemon::RandomSource unavailableRandom{};
  const pokemon::CreditResult rejected =
      pokemon::applyCreditedMinutes(state, leader, 1, 10, pokemon::OwnedEvolutionNeeds{}, unavailableRandom);
  CHECK(rejected.status == pokemon::CreditStatus::Rejected);
  CHECK(leader == leaderBefore);
  CHECK(state == stateBefore);

  state = stateWithEncounter(leader);
  const pokemon::PokemonState encounterBefore = state;
  pokemon::RecordMutation mutation{};
  CHECK(!pokemon::resolveEncounter(state, leader, static_cast<pokemon::EncounterChoice>(99), nullptr, mutation));
  CHECK(state == encounterBefore);
  CHECK(mutation.kind == pokemon::RecordMutationKind::None);

  leader.speciesId = 1;
  leader.totalXp = pokemon::xpRequired(16);
  state = stateWithLeader(leader);
  state.pendingEvents[0].kind = pokemon::PendingEventKind::Evolution;
  state.pendingEvents[0].recordId = leader.recordId;
  state.pendingEvents[0].speciesId = 2;
  state.dashboardNotice = pokemon::DashboardNotice::WhatsThis;
  CHECK(pokemon::resolveEvolution(state, leader, pokemon::EvolutionChoice::Cancel, mutation));
  CHECK(leader.speciesId == 1);
  CHECK(state.pendingEvents[0].kind == pokemon::PendingEventKind::None);
  CHECK(mutation.kind == pokemon::RecordMutationKind::None);
}

}  // namespace

int main() {
  collectionActionsExcludeOperationsThatCannotSucceed();
  creditedMinutesAdvanceLeaderAndReadingCounters();
  creditClampsAndRejectsInvalidCallsWithoutMutation();
  fifteenMinuteCheckUsesFortyPercentEncounterChance();
  fourthEncounterCheckIsForcedAndCreatesOnlyOneEvent();
  fullQueuePrimesGuaranteesButStillHandsOutBalls();
  oneBoundaryQueuesItemEncounterAndEvolutionInOrder();
  resolvingEventsPopsOnlyTheFrontAndRefreshesTheNotice();
  encounterDueWithLevelGainQueuesEncounterBeforeEvolution();
  forcedItemWithLevelGainQueuesItemBeforeEvolution();
  multiHourCreditUsesLifetimeAtEachHourlyBoundary();
  progressBandsGateStagesAndEncounterLevels();
  everyEligibleRegularEncounterWeightIntervalSelectsItsSpecies();
  everyEncounterCheckHandsOutABallWithoutQueueingAnEvent();
  ballKindsUnlockWithProgressAndMasterBallStaysOneAtATime();
  medicineAndMachineRollOnSeparateFifteenMinuteTracks();
  itemRollAndPityPreferAnOwnedEvolutionNeed();
  saturatedItemsDoNotRejectReadingCredit();
  legendaryEligibilityAndMewOverrideRegularEncounters();
  catchCreatesOneAppendAndPassCreatesNone();
  fullPartyCatchLeavesTheNewRecordForPcStorage();
  resolvingABlockingEncounterWaitsForTheNextLevel();
  acknowledgingABlockingItemWaitsForTheNextLevel();
  levelEvolutionPromptsOnceAndCanBeConfirmedOrBlocked();
  reenablingPromptsWaitsForTheNextLevel();
  levelHundredDoesNotCatchUpOrChainLevelEvolutions();
  promptTogglePreservesAnUnrelatedPendingEvent();
  disablingPromptsRemovesOnlyMatchingQueuedEvolutions();
  rejectedPromptToggleDoesNotPartiallyMutate();
  everyLevelEvolutionQueuesAtItsThreshold();
  everyItemEvolutionConsumesExactlyOneItem();
  rejectedInputsAndCancelledEvolutionDoNotPartiallyMutate();
  return failures == 0 ? 0 : 1;
}
