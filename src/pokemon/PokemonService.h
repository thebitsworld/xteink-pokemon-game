#pragma once

#include <PokemonBattle.h>
#include <PokemonTracker.h>

#include <array>
#include <string_view>

#include "PokemonBattleStore.h"
#include "PokemonStore.h"

namespace pokemon {

enum class ServiceStatus : uint8_t {
  Ok,
  Empty,
  AlreadyStarted,
  Invalid,
  NotFound,
  PartyFull,
  LastPokemon,
  NotApplicable,
  StorageError,
};

struct PokemonSnapshot {
  PokemonState state{};
  std::array<PokemonRecord, PARTY_SIZE> party{};
  uint32_t ownedCount = 0;
  uint8_t partyCount = 0;
};

struct PokemonDashboardSnapshot {
  PokemonRecord leader{};
  PendingEvent pending{};
  DashboardNotice notice = DashboardNotice::None;
};

class PokemonService {
 public:
  PokemonService(PokemonStore& store, PokemonBattleStore& battleStore, RandomSource random)
      : store_(store), battleStore_(battleStore), random_(random), tracker_(&PokemonService::creditFromTracker, this) {}

  bool beginReadingSession();
  void setBookProgressPercent(uint8_t percent);
  void onSuccessfulPageTurn(uint32_t nowMs);
  void checkpointIfDue(uint32_t nowMs);
  void flushOnExit(uint32_t nowMs);

  bool creditMinutes(uint16_t minutes, uint8_t bookProgressPercent);

  ServiceStatus loadSnapshot(PokemonSnapshot& output);
  ServiceStatus createStarter(uint16_t speciesId, Gender gender, std::string_view nickname);
  ServiceStatus readRecord(uint32_t recordId, PokemonRecord& output);
  ServiceStatus renamePokemon(uint32_t recordId, std::string_view nickname);
  ServiceStatus movePartyMember(uint8_t fromSlot, uint8_t toSlot);
  ServiceStatus depositPokemon(uint32_t recordId);
  ServiceStatus withdrawPokemon(uint32_t recordId);
  ServiceStatus loadDashboardSnapshot(PokemonDashboardSnapshot& output);
  ServiceStatus readPcPage(PcOrder order, size_t offset, std::span<PokemonRecord> output, size_t& count);
  ServiceStatus resolveEncounter(EncounterChoice choice, uint32_t& caughtRecordId);
  ServiceStatus acknowledgeItem();
  ServiceStatus resolveEvolution(EvolutionChoice choice);
  ServiceStatus setEvolutionPrompts(uint32_t recordId, bool enabled);
  ServiceStatus useEvolutionItem(uint32_t recordId, EvolutionItem item);
  ServiceStatus reset();

  // Bag (items 7..POKEMON_ITEM_ID_MAX; the 6 evolution stones/Link Cable
  // stay reachable through readRecord()'s PokemonState.itemCounts as
  // before). Decrements by 1 (e.g. using a Potion or a TM); returns
  // NotApplicable if the item id is out of range or its count is already 0.
  ServiceStatus consumeBagItem(uint8_t itemId);

  // Gym/Elite Four progress. gymIndex is 1-8 for the gyms in challenge
  // order, 9-GYM_COUNT for the Elite Four. Enforces the linear unlock rule:
  // gym N requires gyms 1..N-1 already defeated; an Elite Four member
  // requires all 8 gym bits set. Badges are a display-only achievement -
  // this does not touch XP, encounter odds, or any PokemonGame.cpp rule.
  ServiceStatus markGymDefeated(uint8_t gymIndex);

  // Loads `recordId`'s live battle state (moves/PP/HP/status). If the
  // battle store has no entry for it (never fought, or the entry was lost
  // to corruption), synthesizes one from the record's current level and
  // species learnset at full HP/PP with no status, persists it, and
  // returns that - callers never need their own fallback path.
  ServiceStatus loadBattleEntry(uint32_t recordId, BattleRecordEntry& output);
  ServiceStatus saveBattleEntry(const BattleRecordEntry& entry);

  // Thin wrappers around the pure engine (PokemonBattle.h) using this
  // service's own RandomSource, so the UI layer never touches RNG directly -
  // consistent with how every PokemonGame.cpp rule is only ever invoked
  // through a PokemonService method.
  BattleTurnResult resolveBattleTurn(BattleCombatant& player, BattleCombatant& opponent, uint8_t playerMoveSlot);
  bool attemptBattleCatch(const BattleCombatant& wild, BallKind ball);

 private:
  ServiceStatus prepareStore();
  ServiceStatus loadReadyState(PokemonState& output);
  BattleRecordEntry synthesizeBattleEntry(const PokemonRecord& record) const;
  void healPartyOnRead(const PokemonState& state, uint16_t minutes);
  static bool creditFromTracker(void* context, uint16_t minutes, uint8_t bookProgressPercent);

  PokemonStore& store_;
  PokemonBattleStore& battleStore_;
  RandomSource random_{};
  PokemonTracker tracker_;
  bool readingSessionActive_ = false;
};

PokemonService& devicePokemonService();

}  // namespace pokemon
