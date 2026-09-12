#if defined(CROSSINK_ENABLE_POKEMON)

#include "PokemonGame.h"

#include <array>
#include <cstdint>
#include <span>

#include "PokemonBattleTypes.h"
#include "PokemonSpecies.h"

namespace pokemon {
namespace {

constexpr uint8_t OWNED_NEEDS_MASK = 0x3FU;
constexpr uint8_t ENCOUNTER_CHECK_MINUTES = 15;
constexpr uint8_t ENCOUNTER_CHANCE_DENOMINATOR = 5;
constexpr uint8_t ENCOUNTER_CHANCE_SUCCESSES = 2;
constexpr uint8_t ENCOUNTER_MISSES_BEFORE_GUARANTEE = 3;
// Ball, medicine and TM/HM (Stage 23) all check at the same 15-minute cadence as
// the encounter roll itself, each with its own pity counter so a streak of
// bad luck on one track never touches the others. Ball is a notch more
// generous (3-in-5) than medicine/TM-HM (2-in-5, same shape as the encounter
// roll) since it needs to comfortably outpace catch consumption (2-3 balls
// per catch against ~1.84 encounters/hour).
constexpr uint32_t BALL_CHANCE_NUMERATOR = 3;
constexpr uint32_t BALL_CHANCE_DENOMINATOR = 5;
constexpr uint8_t BALL_MISSES_BEFORE_GUARANTEE = 3;
constexpr uint32_t ITEM_TRACK_CHANCE_NUMERATOR = 2;
constexpr uint32_t ITEM_TRACK_CHANCE_DENOMINATOR = 5;
constexpr uint8_t ITEM_TRACK_MISSES_BEFORE_GUARANTEE = 3;

bool randomBelow(const RandomSource& random, const uint32_t upperExclusive, uint32_t& output) {
  if (random.below == nullptr || upperExclusive == 0) return false;
  const uint32_t candidate = random.below(random.context, upperExclusive);
  if (candidate >= upperExclusive) return false;
  output = candidate;
  return true;
}

void incrementCapped(uint8_t& value, const uint8_t maximum) {
  if (value < maximum) ++value;
}

// Shared shape behind every "roll a chance, but guarantee a hit after enough
// consecutive misses" track (encounter, evolution stone, ball, medicine,
// TM/HM): `triggered` reports whether this check produced a hit; the pity
// roll is skipped entirely once `misses` reaches `pity`, keeping this
// call-for-call compatible with a scripted exact RandomSource sequence.
// Returns false only on RNG failure.
bool rollPityGate(const RandomSource& random, uint8_t& misses, const uint8_t pity, const uint32_t chanceNumerator,
                  const uint32_t chanceDenominator, bool& triggered) {
  triggered = misses >= pity;
  if (!triggered) {
    uint32_t roll = 0;
    if (!randomBelow(random, chanceDenominator, roll)) return false;
    triggered = roll < chanceNumerator;
  }
  if (triggered) {
    misses = 0;
  } else {
    incrementCapped(misses, pity);
  }
  return true;
}

bool regularEncounterEligible(const SpeciesData& species, const uint8_t bookProgressPercent) {
  if (species.acquisition != Acquisition::Wild) return false;
  if (species.stage == EvolutionStage::Middle && bookProgressPercent < 50) return false;
  if (species.stage == EvolutionStage::Final && bookProgressPercent < 75) return false;
  return true;
}

uint8_t encounterWeight(const SpeciesData& species) {
  if (species.speciesId == 1 || species.speciesId == 4 || species.speciesId == 7 || species.speciesId == 25) {
    return 1;
  }
  if (species.captureRate >= 200) return 4;
  if (species.captureRate >= 120) return 3;
  if (species.captureRate >= 60) return 2;
  return 1;
}

bool chooseRegularSpecies(const uint8_t bookProgressPercent, const RandomSource& random, uint16_t& speciesId) {
  uint32_t totalWeight = 0;
  for (uint16_t candidateId = 1; candidateId <= KANTO_SPECIES_COUNT; ++candidateId) {
    const SpeciesData* candidate = speciesData(candidateId);
    if (candidate != nullptr && regularEncounterEligible(*candidate, bookProgressPercent)) {
      totalWeight += encounterWeight(*candidate);
    }
  }

  uint32_t selectedWeight = 0;
  if (!randomBelow(random, totalWeight, selectedWeight)) return false;
  for (uint16_t candidateId = 1; candidateId <= KANTO_SPECIES_COUNT; ++candidateId) {
    const SpeciesData* candidate = speciesData(candidateId);
    if (candidate == nullptr || !regularEncounterEligible(*candidate, bookProgressPercent)) continue;
    const uint8_t weight = encounterWeight(*candidate);
    if (selectedWeight < weight) {
      speciesId = candidateId;
      return true;
    }
    selectedWeight -= weight;
  }
  return false;
}

bool chooseEncounterLevel(const uint8_t bookProgressPercent, const RandomSource& random, uint8_t& level) {
  struct LevelBand {
    uint8_t progress;
    uint8_t minimum;
    uint8_t maximum;
  };
  static constexpr LevelBand BANDS[] = {{95, 18, 30}, {75, 14, 24}, {50, 9, 16}, {25, 5, 10}, {0, 2, 6}};
  const LevelBand* band = &BANDS[4];
  for (const LevelBand& candidate : BANDS) {
    if (bookProgressPercent >= candidate.progress) {
      band = &candidate;
      break;
    }
  }
  uint32_t offset = 0;
  if (!randomBelow(random, static_cast<uint32_t>(band->maximum - band->minimum + 1U), offset)) return false;
  level = static_cast<uint8_t>(band->minimum + offset);
  return true;
}

bool chooseGender(const SpeciesData& species, const RandomSource& random, Gender& gender) {
  if (species.genderRate == 255)
    gender = Gender::Genderless;
  else if (species.genderRate == 0)
    gender = Gender::Male;
  else if (species.genderRate == 8)
    gender = Gender::Female;
  else {
    uint32_t roll = 0;
    if (!randomBelow(random, 8, roll)) return false;
    gender = roll < species.genderRate ? Gender::Female : Gender::Male;
  }
  return true;
}

DashboardNotice noticeForEvent(const PendingEvent* event) {
  if (event == nullptr) return DashboardNotice::None;
  switch (event->kind) {
    case PendingEventKind::Encounter:
      return DashboardNotice::NewPokemon;
    case PendingEventKind::Item:
      return DashboardNotice::ItemFound;
    case PendingEventKind::Evolution:
      return DashboardNotice::WhatsThis;
    case PendingEventKind::MoveLearn:
      // Reuses the same notice as Evolution for now; a dedicated icon is a
      // Stage 7 (UI) concern, not a storage-layer one.
      return DashboardNotice::WhatsThis;
    case PendingEventKind::None:
      return DashboardNotice::None;
  }
  return DashboardNotice::None;
}

void refreshDashboardNotice(PokemonState& state) { state.dashboardNotice = noticeForEvent(pendingEventFront(state)); }

bool popPendingEvent(PokemonState& state) {
  if (!dequeuePendingEvent(state)) return false;
  refreshDashboardNotice(state);
  return true;
}

const EvolutionRule* findEvolution(const PokemonRecord& record, const EvolutionTrigger trigger,
                                   const EvolutionItem item, const uint16_t targetSpeciesId = 0) {
  for (const EvolutionRule& rule : evolutionsFor(record.speciesId)) {
    if (rule.trigger == trigger && (item == EvolutionItem::None || rule.item == item) &&
        (targetSpeciesId == 0 || rule.targetSpeciesId == targetSpeciesId)) {
      return &rule;
    }
  }
  return nullptr;
}

bool evolveCandidate(PokemonState& state, PokemonRecord& record, const uint16_t targetSpeciesId,
                     RecordMutation& mutation) {
  record.speciesId = targetSpeciesId;
  if (!validateRecord(record) || !markSpecies(state.seenSpecies, targetSpeciesId) ||
      !markSpecies(state.caughtSpecies, targetSpeciesId)) {
    return false;
  }
  mutation.requestedRecordId = record.recordId;
  mutation.record = record;
  mutation.kind = RecordMutationKind::Replace;
  return true;
}

bool queueEvolutionAfterLevelGain(PokemonState& state, const PokemonRecord& record, bool& queued) {
  queued = false;
  if ((record.flags & recordFlag(RecordFlag::EvolutionPromptsDisabled)) != 0) {
    return true;
  }
  for (const PendingEvent& event : state.pendingEvents) {
    if (event.kind == PendingEventKind::Evolution && event.recordId == record.recordId) return true;
  }
  if (pendingEventCount(state) == PENDING_EVENT_CAPACITY) return true;
  const uint8_t level = levelForXp(record.totalXp);
  for (const EvolutionRule& rule : evolutionsFor(record.speciesId)) {
    if (rule.trigger != EvolutionTrigger::Level || level < rule.minimumLevel) continue;
    const PendingEvent event{record.recordId, rule.targetSpeciesId, 0,
                             Gender::Unknown, EvolutionItem::None,  PendingEventKind::Evolution};
    if (!enqueuePendingEvent(state, event)) return false;
    if (!markSpecies(state.seenSpecies, rule.targetSpeciesId)) return false;
    refreshDashboardNotice(state);
    queued = true;
    return true;
  }
  return true;
}

bool finalizeEncounter(PokemonState& state, const uint16_t speciesId, const uint8_t bookProgressPercent,
                       const RandomSource& random) {
  uint8_t level = 0;
  if (!chooseEncounterLevel(bookProgressPercent, random, level)) return false;
  const SpeciesData* species = speciesData(speciesId);
  Gender gender = Gender::Unknown;
  if (species == nullptr || !chooseGender(*species, random, gender)) return false;

  const PendingEvent event{0, speciesId, level, gender, EvolutionItem::None, PendingEventKind::Encounter};
  if (!enqueuePendingEvent(state, event) || !markSpecies(state.seenSpecies, speciesId)) return false;
  refreshDashboardNotice(state);
  return true;
}

bool mewIsReady(const PokemonState& state) {
  if (isSpeciesMarked(state.caughtSpecies, 151)) return false;
  for (uint16_t speciesId = 1; speciesId <= 150; ++speciesId) {
    if (!isSpeciesMarked(state.caughtSpecies, speciesId)) return false;
  }
  return true;
}

bool chooseLegendary(const PokemonState& state, const uint8_t bookProgressPercent, const RandomSource& random,
                     uint16_t& selectedSpeciesId) {
  std::array<uint16_t, 4> eligible{};
  size_t eligibleCount = 0;
  if (state.lifetimeMinutes >= 1200U && bookProgressPercent >= 75U) {
    eligible[eligibleCount++] = 144;
    eligible[eligibleCount++] = 145;
    eligible[eligibleCount++] = 146;
  }
  if (state.lifetimeMinutes >= 3000U && bookProgressPercent >= 95U) eligible[eligibleCount++] = 150;
  if (eligibleCount == 0) return false;

  std::array<uint16_t, 4> uncaught{};
  size_t uncaughtCount = 0;
  for (size_t index = 0; index < eligibleCount; ++index) {
    if (!isSpeciesMarked(state.caughtSpecies, eligible[index])) uncaught[uncaughtCount++] = eligible[index];
  }
  const uint16_t* candidates = uncaughtCount == 0 ? eligible.data() : uncaught.data();
  const size_t candidateCount = uncaughtCount == 0 ? eligibleCount : uncaughtCount;
  uint32_t selectedIndex = 0;
  if (candidateCount > 1 && !randomBelow(random, static_cast<uint32_t>(candidateCount), selectedIndex)) return false;
  selectedSpeciesId = candidates[selectedIndex];
  return true;
}

bool createEncounter(PokemonState& state, const uint8_t bookProgressPercent, const RandomSource& random) {
  if (mewIsReady(state)) return finalizeEncounter(state, 151, bookProgressPercent, random);

  uint32_t legendaryRoll = 0;
  if (!randomBelow(random, 20, legendaryRoll)) return false;
  uint16_t speciesId = 0;
  if (legendaryRoll == 0 && chooseLegendary(state, bookProgressPercent, random, speciesId)) {
    return finalizeEncounter(state, speciesId, bookProgressPercent, random);
  }
  if (!chooseRegularSpecies(bookProgressPercent, random, speciesId)) return false;
  return finalizeEncounter(state, speciesId, bookProgressPercent, random);
}

bool itemCountIsFull(const PokemonState& state, const uint8_t itemId) {
  if (itemId <= EVOLUTION_ITEM_COUNT) return state.itemCounts[itemId - 1U] == UINT16_MAX;
  if (itemId == PP_UP_ITEM_ID) return state.ppUpCount == UINT8_MAX;
  return state.bagCounts[itemId - EVOLUTION_ITEM_COUNT - 1U] == UINT8_MAX;
}

void incrementItemCount(PokemonState& state, const uint8_t itemId) {
  if (itemId <= EVOLUTION_ITEM_COUNT) {
    ++state.itemCounts[itemId - 1U];
  } else if (itemId == PP_UP_ITEM_ID) {
    ++state.ppUpCount;
  } else {
    ++state.bagCounts[itemId - EVOLUTION_ITEM_COUNT - 1U];
  }
}

bool isStoneCategory(const ItemCategory category) { return category == ItemCategory::Stone; }

// Matches what the Bag > Medicine screen shows, so "the medicine you collect"
// means the same set in the drop rules as it does on screen. PP Up
// (ItemCategory::PpUp) is deliberately NOT included here - it's not part of
// the automatic reading-time drop pool at all yet (see
// docs/development/pokemon-gen1-authenticity-roadmap.md's PP Up item) since
// PP Up (ItemCategory::PpUp) is included here so it drops from reading the
// same way every other medicine-track item does - itemCountIsFull()/
// incrementItemCount() above already route it to state.ppUpCount instead of
// bagCounts, so the generic buildItemCandidates()/incrementItemCount() flow
// handles it correctly with no special-casing needed here.
bool isMedicineCategory(const ItemCategory category) {
  return category == ItemCategory::Medicine || category == ItemCategory::StatusCure ||
         category == ItemCategory::PPRestore || category == ItemCategory::Candy ||
         category == ItemCategory::PpUp;
}

bool isMachineCategory(const ItemCategory category) { return category == ItemCategory::Machine; }

// Builds a drop_weight-weighted candidate list, skipping any item already at
// its per-slot storage cap. When `preferOwnedNeeds` is set, only evolution
// stones/Link Cable a currently-owned Pokemon can actually evolve with are
// offered - mirrors the pre-item-expansion behavior exactly. Otherwise
// `categoryMatches` (never null outside that path) narrows the pool to one
// collection track; each track rolls on its own schedule so a category with
// many ids (Machine has 55) can no longer crowd out one with few (Ball has 4).
// `weightOverride`, when given, replaces an item's catalog drop_weight for
// this roll only (see medicineItemWeight()) - the CSV stays the "official"
// per-item rarity, this is purely how much of one track's own roll a
// sub-category gets to claim.
uint32_t buildItemCandidates(const PokemonState& state, const OwnedEvolutionNeeds ownedEvolutionNeeds,
                             const bool preferOwnedNeeds, bool (*categoryMatches)(ItemCategory),
                             uint32_t (*weightOverride)(const ItemData&),
                             std::array<uint8_t, ITEM_COUNT>& candidateItemIds,
                             std::array<uint32_t, ITEM_COUNT>& candidateWeights, size_t& candidateCount) {
  candidateCount = 0;
  uint32_t totalWeight = 0;
  for (uint16_t itemId = 1; itemId <= ITEM_COUNT; ++itemId) {
    if (preferOwnedNeeds) {
      if (itemId > EVOLUTION_ITEM_COUNT) break;  // stones are ids 1..EVOLUTION_ITEM_COUNT, tried first in order
      if ((ownedEvolutionNeeds.mask & static_cast<uint8_t>(1U << (itemId - 1U))) == 0) continue;
    }
    if (itemCountIsFull(state, static_cast<uint8_t>(itemId))) continue;
    const ItemData* item = itemData(static_cast<uint8_t>(itemId));
    if (item == nullptr || item->dropWeight == 0) continue;
    if (categoryMatches != nullptr && !categoryMatches(item->category)) continue;
    const uint32_t weight = weightOverride != nullptr ? weightOverride(*item) : item->dropWeight;
    if (weight == 0) continue;
    candidateItemIds[candidateCount] = static_cast<uint8_t>(itemId);
    candidateWeights[candidateCount] = weight;
    totalWeight += weight;
    ++candidateCount;
  }
  return totalWeight;
}

// Cuts StatusCure/PPRestore down to a third of their catalog drop_weight so
// the medicine track leans toward HP healing/revival - what an active gym
// run actually consumes, since HP/PP already regenerate passively while
// reading (see PokemonService::healPartyOnRead) but nothing regenerates mid-
// battle. Medicine (HP) and Candy keep their full catalog weight.
uint32_t medicineItemWeight(const ItemData& item) {
  if (item.category == ItemCategory::StatusCure || item.category == ItemCategory::PPRestore) {
    return item.dropWeight / 3U;
  }
  return item.dropWeight;
}

// Picks one id out of a drop_weight-weighted candidate list. Skips the roll
// entirely when there is only one possible outcome - both an efficiency win
// and what keeps this call-for-call compatible with the pre-item-expansion
// behavior (which only rolled when there was an actual choice to make),
// preserving determinism for anything that scripts an exact call sequence.
bool pickWeightedItem(std::span<const uint8_t> candidateItemIds, std::span<const uint32_t> candidateWeights,
                      const uint32_t totalWeight, const RandomSource& random, uint8_t& selectedItemId) {
  selectedItemId = candidateItemIds[0];
  if (candidateItemIds.size() == 1) return true;
  uint32_t roll = 0;
  if (!randomBelow(random, totalWeight, roll)) return false;
  selectedItemId = candidateItemIds[candidateItemIds.size() - 1U];
  for (size_t index = 0; index < candidateItemIds.size(); ++index) {
    if (roll < candidateWeights[index]) {
      selectedItemId = candidateItemIds[index];
      break;
    }
    roll -= candidateWeights[index];
  }
  return true;
}

bool createItem(PokemonState& state, const OwnedEvolutionNeeds ownedEvolutionNeeds, const RandomSource& random,
                bool& created) {
  created = false;
  std::array<uint8_t, ITEM_COUNT> candidateItemIds{};
  std::array<uint32_t, ITEM_COUNT> candidateWeights{};
  size_t candidateCount = 0;
  uint32_t totalWeight = buildItemCandidates(state, ownedEvolutionNeeds, true, nullptr, nullptr, candidateItemIds,
                                             candidateWeights, candidateCount);
  if (candidateCount == 0) {
    // Nothing an owned Pokemon can actually evolve with right now - widen to
    // any stone that still has room so the player can stock up ahead of a
    // future evolution, but never past Stone: medicine, TM/HM and balls are
    // separate collection tracks with their own schedules (Stage 22), and
    // folding them back in here is exactly what used to starve the ball
    // supply.
    totalWeight = buildItemCandidates(state, ownedEvolutionNeeds, false, isStoneCategory, nullptr, candidateItemIds,
                                      candidateWeights, candidateCount);
  }
  if (candidateCount == 0 || totalWeight == 0) return true;

  uint8_t selectedItemId = 0;
  if (!pickWeightedItem(std::span{candidateItemIds}.first(candidateCount),
                        std::span{candidateWeights}.first(candidateCount), totalWeight, random, selectedItemId)) {
    return false;
  }

  incrementItemCount(state, selectedItemId);
  const PendingEvent event{
      0, 0, 0, Gender::Unknown, static_cast<EvolutionItem>(selectedItemId), PendingEventKind::Item};
  if (!enqueuePendingEvent(state, event)) return false;
  refreshDashboardNotice(state);
  created = true;
  return true;
}

// Ball ids sit contiguously right after the six evolution stones (7..10 =
// Poke/Great/Ultra/Master), the same fixed layout Screen::BattleBalls indexes.
constexpr uint8_t FIRST_BALL_ITEM_ID = EVOLUTION_ITEM_COUNT + 1U;
constexpr uint8_t BALL_KIND_COUNT = 4;

// Which ball kinds a reader can find yet. Mirrors how regularEncounterEligible
// gates evolved species behind book progress: early on it is Poke Balls only.
// Master Ball is both the last unlock and strictly one-at-a-time - it stops
// dropping while the player still holds one, so it stays an emergency button
// instead of something to stockpile.
bool ballKindDroppable(const PokemonState& state, const uint8_t itemId, const uint8_t bookProgressPercent) {
  switch (itemId - FIRST_BALL_ITEM_ID) {
    case 0:
      return true;
    case 1:
      return bookProgressPercent >= 50;
    case 2:
      return bookProgressPercent >= 75;
    case 3:
      return bookProgressPercent >= 95 && state.bagCounts[itemId - EVOLUTION_ITEM_COUNT - 1U] == 0;
    default:
      return false;
  }
}

// Ball is checked at the same 15-minute cadence as the encounter roll (3-in-5
// + pity after 3 misses, see BALL_CHANCE_NUMERATOR et al.), so the supply is
// paced by the same clock that decides how often a Pokemon shows up (~1.84
// encounters/hour). A catch costs 2-3 throws on average, so that ratio leaves
// a slowly growing reserve rather than stranding the player in front of a
// Pokemon with nothing to throw.
//
// Deliberately does NOT queue a PendingEvent: balls at this cadence would
// swamp the 3-slot queue and start being dropped on the floor exactly when
// they matter most. They land straight in the bag the way buying a stack at a
// Mart would, and the player reads the count in Bag > Balls or on the throw
// screen.
bool grantBall(PokemonState& state, const uint8_t bookProgressPercent, const RandomSource& random) {
  bool triggered = false;
  if (!rollPityGate(random, state.ballMisses, BALL_MISSES_BEFORE_GUARANTEE, BALL_CHANCE_NUMERATOR,
                    BALL_CHANCE_DENOMINATOR, triggered)) {
    return false;
  }
  if (!triggered) return true;

  std::array<uint8_t, BALL_KIND_COUNT> candidateItemIds{};
  std::array<uint32_t, BALL_KIND_COUNT> candidateWeights{};
  size_t candidateCount = 0;
  uint32_t totalWeight = 0;
  for (uint8_t offset = 0; offset < BALL_KIND_COUNT; ++offset) {
    const uint8_t itemId = static_cast<uint8_t>(FIRST_BALL_ITEM_ID + offset);
    if (itemCountIsFull(state, itemId) || !ballKindDroppable(state, itemId, bookProgressPercent)) continue;
    const ItemData* item = itemData(itemId);
    if (item == nullptr || item->dropWeight == 0) continue;
    candidateItemIds[candidateCount] = itemId;
    candidateWeights[candidateCount] = item->dropWeight;
    totalWeight += item->dropWeight;
    ++candidateCount;
  }
  if (candidateCount == 0 || totalWeight == 0) return true;

  uint8_t selectedItemId = 0;
  if (!pickWeightedItem(std::span{candidateItemIds}.first(candidateCount),
                        std::span{candidateWeights}.first(candidateCount), totalWeight, random, selectedItemId)) {
    return false;
  }
  incrementItemCount(state, selectedItemId);
  return true;
}

// Medicine and TM/HM each get their own 15-minute-cadence pity roll rather
// than competing for one shared slot, so tuning either leaves the other (and
// the stone track, and the ball track) untouched.
bool processTrackDrop(PokemonState& state, uint8_t& misses, bool (*categoryMatches)(ItemCategory),
                      uint32_t (*weightOverride)(const ItemData&), const RandomSource& random,
                      PendingEventKind& generatedEvent) {
  if (pendingEventCount(state) == PENDING_EVENT_CAPACITY) {
    misses = ITEM_TRACK_MISSES_BEFORE_GUARANTEE;
    return true;
  }
  bool triggered = false;
  if (!rollPityGate(random, misses, ITEM_TRACK_MISSES_BEFORE_GUARANTEE, ITEM_TRACK_CHANCE_NUMERATOR,
                    ITEM_TRACK_CHANCE_DENOMINATOR, triggered)) {
    return false;
  }
  if (!triggered) return true;

  std::array<uint8_t, ITEM_COUNT> candidateItemIds{};
  std::array<uint32_t, ITEM_COUNT> candidateWeights{};
  size_t candidateCount = 0;
  const uint32_t totalWeight = buildItemCandidates(state, OwnedEvolutionNeeds{}, false, categoryMatches, weightOverride,
                                                   candidateItemIds, candidateWeights, candidateCount);
  if (candidateCount == 0 || totalWeight == 0) return true;

  uint8_t selectedItemId = 0;
  if (!pickWeightedItem(std::span{candidateItemIds}.first(candidateCount),
                        std::span{candidateWeights}.first(candidateCount), totalWeight, random, selectedItemId)) {
    return false;
  }

  incrementItemCount(state, selectedItemId);
  const PendingEvent event{
      0, 0, 0, Gender::Unknown, static_cast<EvolutionItem>(selectedItemId), PendingEventKind::Item};
  if (!enqueuePendingEvent(state, event)) return false;
  refreshDashboardNotice(state);
  generatedEvent = PendingEventKind::Item;
  return true;
}

bool processEncounterCheck(PokemonState& state, const uint8_t bookProgressPercent, const RandomSource& random,
                           PendingEventKind& generatedEvent) {
  if (pendingEventCount(state) == PENDING_EVENT_CAPACITY) {
    state.encounterMisses = ENCOUNTER_MISSES_BEFORE_GUARANTEE;
    return true;
  }
  bool triggered = false;
  if (!rollPityGate(random, state.encounterMisses, ENCOUNTER_MISSES_BEFORE_GUARANTEE, ENCOUNTER_CHANCE_SUCCESSES,
                    ENCOUNTER_CHANCE_DENOMINATOR, triggered)) {
    return false;
  }
  if (!triggered) return true;
  if (!createEncounter(state, bookProgressPercent, random)) return false;
  generatedEvent = PendingEventKind::Encounter;
  return true;
}

bool processHourlyItem(PokemonState& state, const RandomSource& random, const OwnedEvolutionNeeds ownedEvolutionNeeds,
                       PendingEventKind& generatedEvent) {
  if (pendingEventCount(state) == PENDING_EVENT_CAPACITY) {
    state.itemMisses = 19;
    return true;
  }
  bool triggered = false;
  if (!rollPityGate(random, state.itemMisses, 19, 1, 20, triggered)) return false;
  if (!triggered) return true;
  bool itemCreated = false;
  if (!createItem(state, ownedEvolutionNeeds, random, itemCreated)) return false;
  if (itemCreated) generatedEvent = PendingEventKind::Item;
  return true;
}

}  // namespace

CreditResult applyCreditedMinutes(PokemonState& state, PokemonRecord& leader, const uint16_t minutes,
                                  const uint8_t bookProgressPercent, const OwnedEvolutionNeeds ownedEvolutionNeeds,
                                  const RandomSource& random) {
  CreditResult result{};
  if (!validateState(state) || !validateRecord(leader) || state.partyRecordIds[0] != leader.recordId ||
      bookProgressPercent > 100 || (ownedEvolutionNeeds.mask & static_cast<uint8_t>(~OWNED_NEEDS_MASK)) != 0) {
    return result;
  }

  result.previousLevel = levelForXp(leader.totalXp);
  result.currentLevel = result.previousLevel;
  if (minutes == 0) {
    result.status = CreditStatus::NoChange;
    return result;
  }

  PokemonState stateCandidate = state;
  PokemonRecord leaderCandidate = leader;
  PendingEventKind generatedEvent = PendingEventKind::None;
  for (uint32_t minute = 0; minute < minutes; ++minute) {
    if (leaderCandidate.totalXp < MAXIMUM_TOTAL_XP) ++leaderCandidate.totalXp;
    if (stateCandidate.lifetimeMinutes < UINT32_MAX) ++stateCandidate.lifetimeMinutes;
    ++stateCandidate.readingMinuteRemainder;

    const uint8_t minuteLevel = levelForXp(leaderCandidate.totalXp);
    const bool gainedLevel = minuteLevel > result.currentLevel;
    result.currentLevel = minuteLevel;

    const bool hourlyBoundary = stateCandidate.readingMinuteRemainder == 60;
    if (hourlyBoundary) {
      stateCandidate.readingMinuteRemainder = 0;
      // Evolution stones are the one track still on an hourly clock (Stage 22
      // left this alone on purpose - see processHourlyItem/createItem).
      if (!processHourlyItem(stateCandidate, random, ownedEvolutionNeeds, generatedEvent)) return result;
    }

    if (hourlyBoundary || stateCandidate.readingMinuteRemainder % ENCOUNTER_CHECK_MINUTES == 0) {
      // Ball first, so one found on this tick is already in the bag if this
      // same tick also turns up a Pokemon to throw it at.
      if (!grantBall(stateCandidate, bookProgressPercent, random)) return result;
      if (!processTrackDrop(stateCandidate, stateCandidate.medicineMisses, isMedicineCategory, medicineItemWeight,
                            random, generatedEvent)) {
        return result;
      }
      if (!processTrackDrop(stateCandidate, stateCandidate.machineMisses, isMachineCategory, nullptr, random,
                            generatedEvent)) {
        return result;
      }
      if (!processEncounterCheck(stateCandidate, bookProgressPercent, random, generatedEvent)) {
        return result;
      }
    }

    if (gainedLevel) {
      bool queued = false;
      if (!queueEvolutionAfterLevelGain(stateCandidate, leaderCandidate, queued)) return result;
      if (queued) generatedEvent = PendingEventKind::Evolution;
    }
  }

  result.creditedMinutes = minutes;
  result.generatedEvent = generatedEvent;
  result.status = CreditStatus::Applied;
  state = stateCandidate;
  leader = leaderCandidate;
  return result;
}

bool acknowledgeItem(PokemonState& state, const PokemonRecord& leader) {
  const PendingEvent* pending = pendingEventFront(state);
  if (!validateState(state) || !validateRecord(leader) || state.partyRecordIds[0] != leader.recordId ||
      pending == nullptr || pending->kind != PendingEventKind::Item) {
    return false;
  }
  PokemonState candidate = state;
  if (!popPendingEvent(candidate)) return false;
  state = candidate;
  return true;
}

bool acknowledgeMoveLearn(PokemonState& state, const PokemonRecord& record) {
  const PendingEvent* pending = pendingEventFront(state);
  if (!validateState(state) || !validateRecord(record) || pending == nullptr ||
      pending->kind != PendingEventKind::MoveLearn || pending->recordId != record.recordId) {
    return false;
  }
  PokemonState candidate = state;
  if (!popPendingEvent(candidate)) return false;
  state = candidate;
  return true;
}

bool setEvolutionPrompts(PokemonState& state, PokemonRecord& record, const bool enabled, RecordMutation& mutation) {
  if (!validateState(state) || !validateRecord(record) || mutation.kind != RecordMutationKind::None) {
    return false;
  }

  PokemonState stateCandidate = state;
  PokemonRecord recordCandidate = record;
  if (enabled) {
    recordCandidate.flags &= static_cast<uint8_t>(~recordFlag(RecordFlag::EvolutionPromptsDisabled));
  } else {
    recordCandidate.flags |= recordFlag(RecordFlag::EvolutionPromptsDisabled);
    removePendingEvolutionsForRecord(stateCandidate, record.recordId);
    refreshDashboardNotice(stateCandidate);
  }
  if (!validateRecord(recordCandidate)) return false;

  RecordMutation mutationCandidate = mutation;
  mutationCandidate.requestedRecordId = recordCandidate.recordId;
  mutationCandidate.record = recordCandidate;
  mutationCandidate.kind = RecordMutationKind::Replace;
  state = stateCandidate;
  record = recordCandidate;
  mutation = mutationCandidate;
  return true;
}

bool resolveEncounter(PokemonState& state, const PokemonRecord& leader, const EncounterChoice choice,
                      const char* nickname, RecordMutation& mutation) {
  const PendingEvent* front = pendingEventFront(state);
  if (!validateState(state) || !validateRecord(leader) || state.partyRecordIds[0] != leader.recordId ||
      front == nullptr || front->kind != PendingEventKind::Encounter || mutation.kind != RecordMutationKind::None) {
    return false;
  }
  const PendingEvent pending = *front;

  PokemonState stateCandidate = state;
  RecordMutation mutationCandidate = mutation;
  if (choice == EncounterChoice::Catch) {
    if (mutation.requestedRecordId == 0) return false;
    PokemonRecord caught{};
    caught.recordId = mutation.requestedRecordId;
    caught.totalXp = xpRequired(pending.level);
    caught.speciesId = pending.speciesId;
    caught.caughtLevel = pending.level;
    caught.gender = pending.gender;
    caught.origin = Origin::Caught;
    if (!setNickname(caught, nickname == nullptr ? "" : nickname) || !validateRecord(caught) ||
        !markSpecies(stateCandidate.seenSpecies, caught.speciesId) ||
        !markSpecies(stateCandidate.caughtSpecies, caught.speciesId)) {
      return false;
    }
    for (uint32_t& partyRecordId : stateCandidate.partyRecordIds) {
      if (partyRecordId == 0) {
        partyRecordId = caught.recordId;
        break;
      }
    }
    mutationCandidate.record = caught;
    mutationCandidate.kind = RecordMutationKind::Append;
  } else if (choice != EncounterChoice::Pass) {
    return false;
  }

  if (!popPendingEvent(stateCandidate)) return false;
  if (!validateState(stateCandidate)) return false;
  state = stateCandidate;
  mutation = mutationCandidate;
  return true;
}

bool resolveEvolution(PokemonState& state, PokemonRecord& record, const EvolutionChoice choice,
                      RecordMutation& mutation) {
  const PendingEvent* front = pendingEventFront(state);
  if (!validateState(state) || !validateRecord(record) || front == nullptr ||
      front->kind != PendingEventKind::Evolution || front->recordId != record.recordId ||
      mutation.kind != RecordMutationKind::None) {
    return false;
  }
  const PendingEvent pending = *front;

  const EvolutionRule* rule = findEvolution(record, EvolutionTrigger::Level, EvolutionItem::None, pending.speciesId);
  if (rule == nullptr || levelForXp(record.totalXp) < rule->minimumLevel) return false;

  PokemonState stateCandidate = state;
  PokemonRecord recordCandidate = record;
  RecordMutation mutationCandidate = mutation;
  if (choice == EvolutionChoice::Evolve) {
    if (!evolveCandidate(stateCandidate, recordCandidate, rule->targetSpeciesId, mutationCandidate)) return false;
  } else if (choice != EvolutionChoice::Cancel) {
    return false;
  }

  if (!popPendingEvent(stateCandidate)) return false;
  state = stateCandidate;
  record = recordCandidate;
  mutation = mutationCandidate;
  return true;
}

bool useEvolutionItem(PokemonState& state, PokemonRecord& record, const EvolutionItem item, RecordMutation& mutation) {
  if (!validateState(state) || !validateRecord(record) || pendingEventFront(state) != nullptr ||
      item < EvolutionItem::MoonStone || item > EvolutionItem::LinkCable || mutation.kind != RecordMutationKind::None) {
    return false;
  }

  const EvolutionRule* rule = findEvolution(record, EvolutionTrigger::Item, item);
  if (rule == nullptr) return false;
  const size_t itemIndex = static_cast<size_t>(item) - 1U;
  if (state.itemCounts[itemIndex] == 0) return false;

  PokemonState stateCandidate = state;
  PokemonRecord recordCandidate = record;
  RecordMutation mutationCandidate = mutation;
  if (!evolveCandidate(stateCandidate, recordCandidate, rule->targetSpeciesId, mutationCandidate)) return false;
  --stateCandidate.itemCounts[itemIndex];
  state = stateCandidate;
  record = recordCandidate;
  mutation = mutationCandidate;
  return true;
}

CollectionActionSet collectionActions(const bool party, const uint8_t partyCount) {
  CollectionActionSet actions{};
  const auto append = [&actions](const CollectionAction action) { actions.items[actions.count++] = action; };

  append(CollectionAction::Summary);
  append(CollectionAction::Moveset);
  if (party) {
    if (partyCount > 1) {
      append(CollectionAction::Move);
      append(CollectionAction::Deposit);
    }
  } else if (partyCount < PARTY_SIZE) {
    append(CollectionAction::Withdraw);
  }
  append(CollectionAction::Rename);
  append(CollectionAction::EvolutionPrompts);
  return actions;
}

bool chooseGenderForSpecies(const uint16_t speciesId, const RandomSource& random, Gender& gender) {
  const SpeciesData* species = speciesData(speciesId);
  if (species == nullptr) return false;
  return chooseGender(*species, random, gender);
}

}  // namespace pokemon

#endif
