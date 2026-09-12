#pragma once

#include <PokemonBattle.h>
#include <PokemonTracker.h>

#include <array>
#include <string_view>

#include "PokemonBattleStore.h"
#include "PokemonStore.h"

namespace pokemon {

enum class TeachMoveOutcome : uint8_t {
  Learned,
  AlreadyKnown,
  Incompatible,  // this species can't learn moveId via TM/HM at all (canLearnViaMachine == false)
  MovesetFull,   // caller must retry with a replaceSlot in [0, BATTLE_MOVE_SLOTS)
  Failed,
};

enum class UseConsumableOutcome : uint8_t {
  Applied,
  NotApplicable,
  Failed,
};

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
  // Resolves the front MoveLearn event (PendingEventKind::MoveLearn):
  // replaceSlot in [0, BATTLE_MOVE_SLOTS) teaches the move into that slot,
  // overwriting whatever was there; any other value (e.g. -1) skips
  // learning it. Either way, the pending event is dequeued.
  ServiceStatus resolveMoveLearn(int replaceSlot);
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

  // Read-only lookup of a Pokemon's currently known moveset for display
  // (Summary screen): returns the persisted battle-store entry if one
  // exists, else what it WOULD synthesize to from the record's current
  // level/learnset - unlike loadBattleEntry, this never creates or
  // persists anything, so merely viewing a Pokemon never writes to SD.
  BattleRecordEntry peekBattleMoves(const PokemonRecord& record) const;

  // Teaches moveId to recordId via TM/HM. Checks canLearnViaMachine() first
  // (Stage 12 - a species can only learn the TMs/HMs real Pokemon Red allows,
  // not every move in the game). Learns straight into an empty move slot
  // when there is one; if the moveset is already full (and doesn't already
  // know the move), returns MovesetFull without changing anything - the
  // caller must then ask the player which slot to overwrite and retry with
  // replaceSlot in [0, BATTLE_MOVE_SLOTS) (Stage 12; mirrors resolveMoveLearn's
  // two-call shape).
  TeachMoveOutcome teachMove(uint32_t recordId, uint8_t moveId, int replaceSlot = -1);

  // Player-driven moveset management (Party > Actions > Moves): overwrites
  // one move slot with moveId unconditionally, at full PP. The caller (the
  // Moveset UI) is responsible for only ever offering moves the Pokemon can
  // actually learn (its own learnset, at or below its current level) and
  // that aren't already known in another slot - unlike teachMove/
  // resolveMoveLearn there is no AlreadyKnown/MovesetFull check here, since
  // the UI's own move-picker list already excludes both cases by construction.
  ServiceStatus learnMoveIntoSlot(uint32_t recordId, uint8_t slot, uint8_t moveId);

  // Clears one move slot to empty (Stage 12 - the Moveset screen's "Forget").
  // Refuses to clear a Pokemon's last remaining move (NotApplicable) - a
  // Pokemon always has at least one move in the real games too.
  ServiceStatus forgetMove(uint32_t recordId, uint8_t slot);

  // Uses one Medicine-pocket item (ItemCategory::Medicine/StatusCure/
  // PPRestore/Candy - the "Bag > Medicine" category; Stone/Ball/Machine
  // items go through useEvolutionItem/teachMove/attemptBattleCatch
  // instead) on recordId. Medicine heals HP by effectValue and cures
  // curesAilment if set; StatusCure only cures; PPRestore restores
  // effectValue PP to every known move slot (Gen1's Ether/Elixir split by
  // single-vs-all move isn't modeled - both simply top up every slot, a
  // deliberate simplification); Candy adds one level's worth of XP and
  // may itself queue a MoveLearn event exactly like reading credit does,
  // but does NOT check for evolution (that only runs inside
  // creditMinutes() - a Candy-earned evolution is caught on the very next
  // reading credit instead of duplicating that private check here).
  // Returns NotApplicable when the item would have no effect (already at
  // full HP/PP, no matching status, already level 100) without consuming
  // anything - the caller is responsible for consumeBagItem() on Applied.
  UseConsumableOutcome useConsumable(uint32_t recordId, uint8_t itemId);

  // Thin wrappers around the pure engine (PokemonBattle.h) using this
  // service's own RandomSource, so the UI layer never touches RNG directly -
  // consistent with how every PokemonGame.cpp rule is only ever invoked
  // through a PokemonService method.
  BattleTurnResult resolveBattleTurn(BattleCombatant& player, BattleCombatant& opponent, uint8_t playerMoveSlot);
  // For a voluntary switch or mid-battle item use - both cost the whole
  // turn in Gen 1 (no Speed check), so only the opponent acts.
  BattleTurnResult resolveOpponentOnlyTurn(BattleCombatant& player, BattleCombatant& opponent);
  bool attemptBattleCatch(const BattleCombatant& wild, BallKind ball);
  // A gym/Elite Four/Champion trainer's fixed roster carries no gender of
  // its own (unlike a wild encounter's PendingEvent, already rolled at
  // encounter time) - rolls one by the species' real gender ratio so the
  // Battle HUD can still show it. Falls back to Gender::Unknown (shown as
  // nothing) if speciesId is invalid.
  Gender rollGenderFor(uint16_t speciesId);

 private:
  ServiceStatus prepareStore();
  ServiceStatus loadReadyState(PokemonState& output);
  BattleRecordEntry synthesizeBattleEntry(const PokemonRecord& record) const;
  void healPartyOnRead(const PokemonState& state, uint16_t minutes);
  // Checks the leader's learnset for any move newly available between
  // previousLevel (exclusive) and currentLevel (inclusive): auto-fills an
  // empty move slot if there's room, or queues a MoveLearn event for the UI
  // to resolve if the moveset is already full. A Pokemon with no battle
  // entry yet is skipped - its first battle synthesizes an up-to-date
  // moveset for its current level already, so there is nothing to catch up.
  void queueMoveLearnIfNeeded(PokemonState& state, const PokemonRecord& leader, uint8_t previousLevel,
                              uint8_t currentLevel);
  static bool creditFromTracker(void* context, uint16_t minutes, uint8_t bookProgressPercent);

  PokemonStore& store_;
  PokemonBattleStore& battleStore_;
  RandomSource random_{};
  PokemonTracker tracker_;
  bool readingSessionActive_ = false;
};

PokemonService& devicePokemonService();

}  // namespace pokemon
