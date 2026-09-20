#pragma once

#include <array>
#include <cstdint>

#include "PokemonSpecies.h"
#include "PokemonTypes.h"

namespace pokemon {

// Pity-counter thresholds: how many consecutive misses on a given 15/60-
// minute drop track guarantee a hit on the next check (see rollPityGate()'s
// callers in PokemonGame.cpp for how each track actually applies these).
// Public (not file-local to PokemonGame.cpp) so the UI can show a player
// their own progress toward each guarantee without duplicating these
// numbers - see PokemonActivity::renderMenuPityBars().
constexpr uint8_t ENCOUNTER_MISSES_BEFORE_GUARANTEE = 3;
constexpr uint8_t BALL_MISSES_BEFORE_GUARANTEE = 3;
// Medicine and TM/HM each run their own independent pity counter (see
// processTrackDrop() in PokemonGame.cpp) but share this same threshold.
constexpr uint8_t ITEM_TRACK_MISSES_BEFORE_GUARANTEE = 3;
// The hourly evolution-stone/Link Cable roll (processHourlyItem()) checks
// far less often than the other 4 tracks, so it tolerates a much longer
// losing streak before guaranteeing a hit.
constexpr uint8_t HOURLY_ITEM_MISSES_BEFORE_GUARANTEE = 19;

enum class CreditStatus : uint8_t {
  Rejected = 0,
  NoChange = 1,
  Applied = 2,
};

struct CreditResult {
  uint16_t creditedMinutes = 0;
  uint8_t previousLevel = 0;
  uint8_t currentLevel = 0;
  PendingEventKind generatedEvent = PendingEventKind::None;
  CreditStatus status = CreditStatus::Rejected;
};

struct OwnedEvolutionNeeds {
  uint8_t mask = 0;
};

using RandomBelowFn = uint32_t (*)(void* context, uint32_t upperExclusive);

struct RandomSource {
  void* context = nullptr;
  RandomBelowFn below = nullptr;
};

enum class EncounterChoice : uint8_t {
  Catch = 0,
  Pass = 1,
};

enum class EvolutionChoice : uint8_t {
  Evolve = 0,
  Cancel = 1,
};

enum class CollectionAction : uint8_t {
  Summary = 0,
  Moveset = 1,
  Move = 2,
  Deposit = 3,
  Withdraw = 4,
  Rename = 5,
  EvolutionPrompts = 6,
  // Manual level evolution (Party > Actions), only offered when one is available.
  Evolve = 8,
  // Box context only (collectionActions()'s !party branch) - permanently
  // deletes the Pokemon, see releaseRecord().
  Release = 7,
};

struct CollectionActionSet {
  std::array<CollectionAction, 8> items{};
  uint8_t count = 0;
};

enum class RecordMutationKind : uint8_t {
  None = 0,
  Append = 1,
  Replace = 2,
  // Removes the record matching `requestedRecordId` entirely - see
  // releaseRecord()'s doc comment. Introduced for the Release feature; every
  // earlier mutation kind only ever added or changed a record, never
  // deleted one.
  Remove = 3,
};

struct RecordMutation {
  uint32_t requestedRecordId = 0;
  PokemonRecord record{};
  RecordMutationKind kind = RecordMutationKind::None;
};

CreditResult applyCreditedMinutes(PokemonState& state, PokemonRecord& leader, uint16_t minutes,
                                  uint8_t bookProgressPercent, OwnedEvolutionNeeds ownedEvolutionNeeds,
                                  const RandomSource& random);
bool acknowledgeItem(PokemonState& state, const PokemonRecord& leader);
// Dequeues a MoveLearn event once the UI has resolved it (learned into a
// slot, or skipped) - the slot mutation itself lives in the battle store
// (PokemonService), out of reach of this pure, storage-agnostic layer, so
// this only ever pops the queue entry.
bool acknowledgeMoveLearn(PokemonState& state, const PokemonRecord& record);
// Queues a level-evolution prompt if `record` is at/above a Level rule's minimum
// level (no-op when prompts are disabled, one is already pending, or the queue
// is full). Safe to call after any level gain (reading, battle XP, Rare Candy)
// and idempotent, so it also backfills a Pokemon that already passed its level.
bool queueEvolutionIfEligible(PokemonState& state, const PokemonRecord& record, bool& queued);

bool setEvolutionPrompts(PokemonState& state, PokemonRecord& record, bool enabled, RecordMutation& mutation);
bool resolveEncounter(PokemonState& state, const PokemonRecord& leader, EncounterChoice choice, const char* nickname,
                      RecordMutation& mutation);
// Releases (permanently deletes) a PC Box Pokemon - the Box-capacity
// counterpart to catching one. Rejects (returns false, no mutation) a
// record currently in the party - withdraw to the Box first, matching real
// games. Any pending Evolution event for this record is silently dropped
// (same as disabling evolution prompts already does via
// removePendingEvolutionsForRecord()) rather than blocking the release -
// no other PendingEventKind can reference a non-party record (Encounter
// carries no meaningful recordId of its own, and MoveLearn only ever
// targets the party leader, since only party members gain XP). Does NOT
// clear seenSpecies/caughtSpecies - a released Pokemon stays in the
// Pokedex forever, matching every mainline game.
bool releaseRecord(PokemonState& state, const PokemonRecord& record, RecordMutation& mutation);
bool resolveEvolution(PokemonState& state, PokemonRecord& record, EvolutionChoice choice, RecordMutation& mutation);
// Drops the front pending event unconditionally (refreshing the dashboard notice). Used to clear a stale
// prompt whose record no longer exists, so it can't block the rest of the queue.
bool discardFrontPendingEvent(PokemonState& state);
// The Level-trigger rule this Pokemon already meets (level >= minimumLevel),
// or nullptr. Drives the manual "Evolve" action.
const EvolutionRule* levelEvolutionAvailable(const PokemonRecord& record);
// Evolves `record` right now via its available Level rule, without waiting for
// a queued prompt (and dropping any prompt already queued for it).
bool evolveByLevelNow(PokemonState& state, PokemonRecord& record, RecordMutation& mutation);
bool useEvolutionItem(PokemonState& state, PokemonRecord& record, EvolutionItem item, RecordMutation& mutation);
CollectionActionSet collectionActions(bool party, uint8_t partyCount, bool canEvolve = false);

// Rolls a gender for a species by its real gender ratio, the same rule a
// wild encounter uses (chooseGender(), file-local to PokemonGame.cpp) -
// exported so a gym/Elite Four/Champion trainer's fixed roster (which
// carries no gender of its own in scripts/data/pokemon-gyms.csv, unlike a
// wild encounter's PendingEvent) can still show a real gender in the Battle
// HUD instead of omitting it. Returns false (leaving gender untouched) only
// if speciesId is out of range or the RNG call itself fails.
bool chooseGenderForSpecies(uint16_t speciesId, const RandomSource& random, Gender& gender);

}  // namespace pokemon
