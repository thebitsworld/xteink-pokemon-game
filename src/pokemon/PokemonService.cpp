#if defined(CROSSINK_ENABLE_POKEMON)

#include "PokemonService.h"

#include <Logging.h>
#include <PokemonSpecies.h>

#include <algorithm>

#if !defined(POKEMON_SERVICE_HOST_TEST)
#include <Arduino.h>
#endif

namespace pokemon {
namespace {

#if !defined(POKEMON_SERVICE_HOST_TEST)
uint32_t deviceRandomBelow(void*, const uint32_t upperExclusive) {
  if (upperExclusive == 0) return 0;
  return static_cast<uint32_t>(random(static_cast<long>(upperExclusive)));
}
#endif

}  // namespace

ServiceStatus PokemonService::prepareStore() {
  if (store_.isReady()) return store_.recordCount() == 0 ? ServiceStatus::Empty : ServiceStatus::Ok;
  switch (store_.begin()) {
    case StoreBeginResult::Ready:
      return store_.recordCount() == 0 ? ServiceStatus::Empty : ServiceStatus::Ok;
    case StoreBeginResult::Empty:
      return ServiceStatus::Empty;
    case StoreBeginResult::Corrupt:
    case StoreBeginResult::Unsupported:
      return ServiceStatus::StorageError;
  }
  return ServiceStatus::StorageError;
}

ServiceStatus PokemonService::loadSnapshot(PokemonSnapshot& output) {
  output = {};
  const ServiceStatus prepared = prepareStore();
  if (prepared != ServiceStatus::Ok) return prepared;
  if (!store_.loadState(output.state)) {
    LOG_ERR("PokemonService", "Failed to load Pokemon snapshot state");
    return ServiceStatus::StorageError;
  }
  for (size_t slot = 0; slot < PARTY_SIZE && output.state.partyRecordIds[slot] != 0; ++slot) {
    if (!store_.readRecord(output.state.partyRecordIds[slot], output.party[slot])) {
      LOG_ERR("PokemonService", "Failed to load Pokemon snapshot party");
      output = {};
      return ServiceStatus::StorageError;
    }
    ++output.partyCount;
  }
  output.ownedCount = store_.recordCount();
  return ServiceStatus::Ok;
}

ServiceStatus PokemonService::loadReadyState(PokemonState& output) {
  const ServiceStatus prepared = prepareStore();
  if (prepared != ServiceStatus::Ok) return prepared;
  if (!store_.loadState(output)) {
    LOG_ERR("PokemonService", "Failed to load Pokemon state");
    return ServiceStatus::StorageError;
  }
  return ServiceStatus::Ok;
}

ServiceStatus PokemonService::createStarter(const uint16_t speciesId, const Gender gender,
                                            const std::string_view nickname) {
  const ServiceStatus prepared = prepareStore();
  if (prepared == ServiceStatus::Ok) return ServiceStatus::AlreadyStarted;
  if (prepared != ServiceStatus::Empty) return prepared;
  if ((speciesId != 1 && speciesId != 4 && speciesId != 7 && speciesId != 25) ||
      (gender != Gender::Male && gender != Gender::Female)) {
    return ServiceStatus::Invalid;
  }

  PokemonRecord starter{};
  starter.recordId = 1;
  starter.totalXp = xpRequired(5);
  starter.speciesId = speciesId;
  starter.caughtLevel = 5;
  starter.gender = gender;
  starter.origin = Origin::Starter;
  if (!setNickname(starter, nickname) || !validateRecord(starter)) return ServiceStatus::Invalid;

  PokemonState state{};
  state.partyRecordIds[0] = starter.recordId;
  if (!markSpecies(state.seenSpecies, speciesId) || !markSpecies(state.caughtSpecies, speciesId)) {
    return ServiceStatus::Invalid;
  }
  // Starting gift, same spirit as a new Pokemon game handing over a few
  // balls and a potion before the first real encounter: 10 Poke Balls (item
  // id 7 -> bagCounts[0]) and 1 Potion (item id 11 -> bagCounts[4]). The
  // starter itself needs no explicit full-HP/PP write - peekBattleMoves()
  // already synthesizes full HP/PP (no battle-store entry exists yet for a
  // brand-new record).
  state.bagCounts[0] = 10;
  state.bagCounts[4] = 1;
  const RecordMutation mutation{starter.recordId, starter, RecordMutationKind::Append};
  if (!store_.commit(state, mutation)) {
    LOG_ERR("PokemonService", "Failed to create starter");
    return ServiceStatus::StorageError;
  }
  return ServiceStatus::Ok;
}

ServiceStatus PokemonService::readRecord(const uint32_t recordId, PokemonRecord& output) {
  const ServiceStatus prepared = prepareStore();
  if (prepared != ServiceStatus::Ok) return prepared;
  return store_.readRecord(recordId, output) ? ServiceStatus::Ok : ServiceStatus::NotFound;
}

ServiceStatus PokemonService::renamePokemon(const uint32_t recordId, const std::string_view nickname) {
  PokemonRecord record{};
  const ServiceStatus readStatus = readRecord(recordId, record);
  if (readStatus != ServiceStatus::Ok) return readStatus;
  if (!setNickname(record, nickname)) return ServiceStatus::Invalid;
  PokemonState state{};
  const ServiceStatus stateStatus = loadReadyState(state);
  if (stateStatus != ServiceStatus::Ok) return stateStatus;
  const RecordMutation mutation{record.recordId, record, RecordMutationKind::Replace};
  if (!store_.commit(state, mutation)) {
    LOG_ERR("PokemonService", "Failed to rename Pokemon");
    return ServiceStatus::StorageError;
  }
  return ServiceStatus::Ok;
}

ServiceStatus PokemonService::movePartyMember(const uint8_t fromSlot, const uint8_t toSlot) {
  PokemonState state{};
  const ServiceStatus stateStatus = loadReadyState(state);
  if (stateStatus != ServiceStatus::Ok) return stateStatus;
  uint8_t partyCount = 0;
  while (partyCount < PARTY_SIZE && state.partyRecordIds[partyCount] != 0) ++partyCount;
  if (fromSlot >= partyCount || toSlot >= partyCount) return ServiceStatus::Invalid;
  if (fromSlot == toSlot) return ServiceStatus::Ok;

  const uint32_t moved = state.partyRecordIds[fromSlot];
  if (fromSlot > toSlot) {
    for (uint8_t slot = fromSlot; slot > toSlot; --slot) {
      state.partyRecordIds[slot] = state.partyRecordIds[slot - 1U];
    }
  } else {
    for (uint8_t slot = fromSlot; slot < toSlot; ++slot) {
      state.partyRecordIds[slot] = state.partyRecordIds[slot + 1U];
    }
  }
  state.partyRecordIds[toSlot] = moved;
  if (!store_.commit(state)) {
    LOG_ERR("PokemonService", "Failed to reorder party");
    return ServiceStatus::StorageError;
  }
  return ServiceStatus::Ok;
}

ServiceStatus PokemonService::depositPokemon(const uint32_t recordId) {
  PokemonState state{};
  const ServiceStatus stateStatus = loadReadyState(state);
  if (stateStatus != ServiceStatus::Ok) return stateStatus;
  uint8_t partyCount = 0;
  while (partyCount < PARTY_SIZE && state.partyRecordIds[partyCount] != 0) ++partyCount;
  uint8_t slot = 0;
  while (slot < partyCount && state.partyRecordIds[slot] != recordId) ++slot;
  if (slot == partyCount) return ServiceStatus::NotFound;
  if (partyCount == 1) return ServiceStatus::LastPokemon;
  for (; slot + 1U < partyCount; ++slot) state.partyRecordIds[slot] = state.partyRecordIds[slot + 1U];
  state.partyRecordIds[partyCount - 1U] = 0;
  if (!store_.commit(state)) {
    LOG_ERR("PokemonService", "Failed to deposit Pokemon");
    return ServiceStatus::StorageError;
  }
  return ServiceStatus::Ok;
}

ServiceStatus PokemonService::withdrawPokemon(const uint32_t recordId) {
  PokemonState state{};
  const ServiceStatus stateStatus = loadReadyState(state);
  if (stateStatus != ServiceStatus::Ok) return stateStatus;
  uint8_t partyCount = 0;
  while (partyCount < PARTY_SIZE && state.partyRecordIds[partyCount] != 0) {
    if (state.partyRecordIds[partyCount] == recordId) return ServiceStatus::Invalid;
    ++partyCount;
  }
  if (partyCount == PARTY_SIZE) return ServiceStatus::PartyFull;
  PokemonRecord record{};
  if (!store_.readRecord(recordId, record)) return ServiceStatus::NotFound;
  state.partyRecordIds[partyCount] = recordId;
  if (!store_.commit(state)) {
    LOG_ERR("PokemonService", "Failed to withdraw Pokemon");
    return ServiceStatus::StorageError;
  }
  return ServiceStatus::Ok;
}

ServiceStatus PokemonService::loadDashboardSnapshot(PokemonDashboardSnapshot& output) {
  output = {};
  PokemonState state{};
  const ServiceStatus stateStatus = loadReadyState(state);
  if (stateStatus != ServiceStatus::Ok) return stateStatus;
  if (state.partyRecordIds[0] == 0 || !store_.readRecord(state.partyRecordIds[0], output.leader)) {
    LOG_ERR("PokemonService", "Failed to load dashboard leader");
    output = {};
    return ServiceStatus::StorageError;
  }
  const PendingEvent* pending = pendingEventFront(state);
  output.pending = pending == nullptr ? PendingEvent{} : *pending;
  output.notice = state.dashboardNotice;
  return ServiceStatus::Ok;
}

ServiceStatus PokemonService::readPcPage(const PcOrder order, const size_t offset,
                                         const std::span<PokemonRecord> output, size_t& count) {
  count = 0;
  if (output.empty() || order > PcOrder::Alphabetical) return ServiceStatus::Invalid;
  const ServiceStatus prepared = prepareStore();
  if (prepared != ServiceStatus::Ok) return prepared;
  return store_.readPcPage(order, offset, output, count) ? ServiceStatus::Ok : ServiceStatus::StorageError;
}

ServiceStatus PokemonService::resolveEncounter(const EncounterChoice choice, uint32_t& caughtRecordId) {
  caughtRecordId = 0;
  PokemonState state{};
  const ServiceStatus stateStatus = loadReadyState(state);
  if (stateStatus != ServiceStatus::Ok) return stateStatus;
  PokemonRecord leader{};
  if (state.partyRecordIds[0] == 0 || !store_.readRecord(state.partyRecordIds[0], leader)) {
    return ServiceStatus::StorageError;
  }
  RecordMutation mutation{};
  if (choice == EncounterChoice::Catch) {
    if (store_.recordCount() >= 1024U) return ServiceStatus::StorageError;
    mutation.requestedRecordId = store_.recordCount() + 1U;
  }
  if (!pokemon::resolveEncounter(state, leader, choice, "", mutation)) return ServiceStatus::NotApplicable;
  if (!store_.commit(state, mutation)) {
    LOG_ERR("PokemonService", "Failed to resolve encounter");
    return ServiceStatus::StorageError;
  }
  if (mutation.kind == RecordMutationKind::Append) caughtRecordId = mutation.record.recordId;
  return ServiceStatus::Ok;
}

ServiceStatus PokemonService::acknowledgeItem() {
  PokemonState state{};
  const ServiceStatus stateStatus = loadReadyState(state);
  if (stateStatus != ServiceStatus::Ok) return stateStatus;
  PokemonRecord leader{};
  if (state.partyRecordIds[0] == 0 || !store_.readRecord(state.partyRecordIds[0], leader)) {
    return ServiceStatus::StorageError;
  }
  if (!pokemon::acknowledgeItem(state, leader)) return ServiceStatus::NotApplicable;
  if (!store_.commit(state)) {
    LOG_ERR("PokemonService", "Failed to acknowledge item");
    return ServiceStatus::StorageError;
  }
  return ServiceStatus::Ok;
}

ServiceStatus PokemonService::resolveEvolution(const EvolutionChoice choice) {
  PokemonState state{};
  const ServiceStatus stateStatus = loadReadyState(state);
  if (stateStatus != ServiceStatus::Ok) return stateStatus;
  const PendingEvent* pending = pendingEventFront(state);
  if (pending == nullptr || pending->kind != PendingEventKind::Evolution || pending->recordId == 0) {
    return ServiceStatus::NotApplicable;
  }
  PokemonRecord record{};
  if (!store_.readRecord(pending->recordId, record)) return ServiceStatus::StorageError;
  RecordMutation mutation{};
  if (!pokemon::resolveEvolution(state, record, choice, mutation)) return ServiceStatus::NotApplicable;
  if (!store_.commit(state, mutation)) {
    LOG_ERR("PokemonService", "Failed to resolve evolution");
    return ServiceStatus::StorageError;
  }
  return ServiceStatus::Ok;
}

ServiceStatus PokemonService::resolveMoveLearn(const int replaceSlot) {
  PokemonState state{};
  const ServiceStatus stateStatus = loadReadyState(state);
  if (stateStatus != ServiceStatus::Ok) return stateStatus;
  const PendingEvent* pending = pendingEventFront(state);
  if (pending == nullptr || pending->kind != PendingEventKind::MoveLearn) return ServiceStatus::NotApplicable;
  const PendingEvent event = *pending;
  PokemonRecord record{};
  if (!store_.readRecord(event.recordId, record)) return ServiceStatus::StorageError;

  if (replaceSlot >= 0 && replaceSlot < static_cast<int>(BATTLE_MOVE_SLOTS)) {
    BattleRecordEntry entry{};
    if (loadBattleEntry(event.recordId, entry) != ServiceStatus::Ok) return ServiceStatus::StorageError;
    const MoveData* move = moveData(event.speciesId);
    entry.moves[replaceSlot] = event.speciesId;
    // maxPpFor(), not the move's raw base PP - PP Up is tied to the slot, not
    // the move identity, so a slot that's already been PP-Up'd keeps that
    // bonus for whatever move ends up there (entry.ppUp[replaceSlot] itself
    // is left untouched).
    entry.pp[replaceSlot] = move == nullptr ? 0 : maxPpFor(move->pp, entry.ppUp[replaceSlot]);
    if (!battleStore_.upsertEntry(entry)) {
      LOG_ERR("PokemonService", "Failed to save learned move");
      return ServiceStatus::StorageError;
    }
  }

  if (!pokemon::acknowledgeMoveLearn(state, record)) return ServiceStatus::NotApplicable;
  if (!store_.commit(state)) {
    LOG_ERR("PokemonService", "Failed to acknowledge move learn");
    return ServiceStatus::StorageError;
  }
  return ServiceStatus::Ok;
}

TeachMoveOutcome PokemonService::teachMove(const uint32_t recordId, const uint8_t moveId, const int replaceSlot) {
  BattleRecordEntry entry{};
  if (loadBattleEntry(recordId, entry) != ServiceStatus::Ok) return TeachMoveOutcome::Failed;
  for (size_t slot = 0; slot < BATTLE_MOVE_SLOTS; ++slot) {
    if (entry.moves[slot] == moveId) return TeachMoveOutcome::AlreadyKnown;
  }

  // Only checked once we know the move isn't already known - a Pokemon
  // that already learned a move some other way (level-up, MoveLearn) is
  // never blocked by TM/HM incompatibility for a move it already has.
  PokemonRecord record{};
  if (readRecord(recordId, record) != ServiceStatus::Ok) return TeachMoveOutcome::Failed;
  if (!canLearnViaMachine(record.speciesId, moveId)) return TeachMoveOutcome::Incompatible;

  int targetSlot = -1;
  for (size_t slot = 0; slot < BATTLE_MOVE_SLOTS; ++slot) {
    if (entry.moves[slot] == 0) {
      targetSlot = static_cast<int>(slot);
      break;
    }
  }
  if (targetSlot < 0) {
    if (replaceSlot < 0 || replaceSlot >= static_cast<int>(BATTLE_MOVE_SLOTS)) return TeachMoveOutcome::MovesetFull;
    targetSlot = replaceSlot;
  }
  const MoveData* move = moveData(moveId);
  entry.moves[targetSlot] = moveId;
  // maxPpFor(), not the move's raw base PP - see resolveMoveLearn()'s same
  // comment: PP Up is tied to the slot, not the move identity.
  entry.pp[targetSlot] = move == nullptr ? 0 : maxPpFor(move->pp, entry.ppUp[targetSlot]);
  if (!battleStore_.upsertEntry(entry)) {
    LOG_ERR("PokemonService", "Failed to teach move");
    return TeachMoveOutcome::Failed;
  }
  return TeachMoveOutcome::Learned;
}

ServiceStatus PokemonService::learnMoveIntoSlot(const uint32_t recordId, const uint8_t slot, const uint8_t moveId) {
  if (slot >= BATTLE_MOVE_SLOTS) return ServiceStatus::Invalid;
  const MoveData* move = moveData(moveId);
  if (move == nullptr) return ServiceStatus::Invalid;

  BattleRecordEntry entry{};
  if (loadBattleEntry(recordId, entry) != ServiceStatus::Ok) return ServiceStatus::StorageError;
  entry.moves[slot] = moveId;
  // maxPpFor(), not the move's raw base PP - see teachMove()'s same comment:
  // PP Up is tied to the slot, not the move identity.
  entry.pp[slot] = maxPpFor(move->pp, entry.ppUp[slot]);
  if (!battleStore_.upsertEntry(entry)) {
    LOG_ERR("PokemonService", "Failed to update moveset");
    return ServiceStatus::StorageError;
  }
  return ServiceStatus::Ok;
}

ServiceStatus PokemonService::forgetMove(const uint32_t recordId, const uint8_t slot) {
  if (slot >= BATTLE_MOVE_SLOTS) return ServiceStatus::Invalid;

  BattleRecordEntry entry{};
  if (loadBattleEntry(recordId, entry) != ServiceStatus::Ok) return ServiceStatus::StorageError;
  if (entry.moves[slot] == 0) return ServiceStatus::NotApplicable;
  size_t knownCount = 0;
  for (const uint8_t moveId : entry.moves) {
    if (moveId != 0) ++knownCount;
  }
  if (knownCount <= 1) return ServiceStatus::NotApplicable;  // never leave a Pokemon with 0 moves

  // validateBattleRecordEntry requires moves packed at the front (no gaps,
  // like PokemonState::partyRecordIds) - shift everything after the
  // cleared slot left instead of leaving a hole, or upsertEntry always
  // rejects the write.
  for (size_t i = slot; i + 1 < BATTLE_MOVE_SLOTS; ++i) {
    entry.moves[i] = entry.moves[i + 1];
    entry.pp[i] = entry.pp[i + 1];
    entry.ppUp[i] = entry.ppUp[i + 1];
  }
  entry.moves[BATTLE_MOVE_SLOTS - 1] = 0;
  entry.pp[BATTLE_MOVE_SLOTS - 1] = 0;
  entry.ppUp[BATTLE_MOVE_SLOTS - 1] = 0;

  if (!battleStore_.upsertEntry(entry)) {
    LOG_ERR("PokemonService", "Failed to forget move");
    return ServiceStatus::StorageError;
  }
  return ServiceStatus::Ok;
}

ServiceStatus PokemonService::applyPpUp(const uint32_t recordId, const uint8_t slot) {
  if (slot >= BATTLE_MOVE_SLOTS) return ServiceStatus::Invalid;

  BattleRecordEntry entry{};
  if (loadBattleEntry(recordId, entry) != ServiceStatus::Ok) return ServiceStatus::StorageError;
  if (entry.moves[slot] == 0 || entry.ppUp[slot] >= 3) return ServiceStatus::NotApplicable;

  const MoveData* move = moveData(entry.moves[slot]);
  if (move == nullptr) return ServiceStatus::StorageError;

  const uint8_t oldMaxPp = maxPpFor(move->pp, entry.ppUp[slot]);
  ++entry.ppUp[slot];
  const uint8_t newMaxPp = maxPpFor(move->pp, entry.ppUp[slot]);
  if (entry.pp[slot] >= oldMaxPp) entry.pp[slot] = newMaxPp;  // was already full - stays full

  if (!battleStore_.upsertEntry(entry)) {
    LOG_ERR("PokemonService", "Failed to use PP Up");
    return ServiceStatus::StorageError;
  }
  return ServiceStatus::Ok;
}

UseConsumableOutcome PokemonService::useConsumable(const uint32_t recordId, const uint8_t itemId) {
  const ItemData* item = itemData(itemId);
  if (item == nullptr) return UseConsumableOutcome::Failed;

  PokemonRecord record{};
  if (readRecord(recordId, record) != ServiceStatus::Ok) return UseConsumableOutcome::Failed;
  const uint8_t level = levelForXp(record.totalXp);

  if (item->category == ItemCategory::Candy) {
    if (level >= 100) return UseConsumableOutcome::NotApplicable;
    PokemonState state{};
    if (loadReadyState(state) != ServiceStatus::Ok) return UseConsumableOutcome::Failed;
    const uint8_t nextLevel = static_cast<uint8_t>(level + 1U);
    record.totalXp = xpRequired(nextLevel);
    queueMoveLearnIfNeeded(state, record, level, nextLevel);
    const RecordMutation mutation{record.recordId, record, RecordMutationKind::Replace};
    if (!store_.commit(state, mutation)) {
      LOG_ERR("PokemonService", "Failed to use Rare Candy");
      return UseConsumableOutcome::Failed;
    }
    return UseConsumableOutcome::Applied;
  }

  const BaseStats* stats = baseStatsFor(record.speciesId);
  if (stats == nullptr) return UseConsumableOutcome::Failed;
  BattleRecordEntry entry{};
  if (loadBattleEntry(recordId, entry) != ServiceStatus::Ok) return UseConsumableOutcome::Failed;
  const IvEvEntry ivEv = ensureIvEv(recordId);
  constexpr size_t hpIndex = static_cast<size_t>(StatIndex::Hp);
  const uint16_t maxHp = battleMaxHp(stats->hp, level, ivEv.iv[hpIndex], ivEv.ev[hpIndex]);

  // Revive/Max Revive (ids 15/16, pinned in scripts/data/pokemon-items.csv) are
  // the only items that can act on a fainted Pokemon, and only a fainted one -
  // effectValue is the percent of max HP to restore (50/100). Every other
  // Medicine/StatusCure/PPRestore item requires the Pokemon to still have
  // HP > 0, matching how these items behave in the real games.
  const bool isRevive = itemId == 15 || itemId == 16;

  bool changed = false;
  if (isRevive) {
    if (item->category == ItemCategory::Medicine && entry.currentHp == 0) {
      const uint32_t healed = (static_cast<uint32_t>(maxHp) * item->effectValue) / 100U;
      entry.currentHp = static_cast<uint16_t>(std::max<uint32_t>(1U, std::min<uint32_t>(maxHp, healed)));
      changed = true;
    }
  } else if (entry.currentHp > 0) {
    if (item->category == ItemCategory::Medicine && entry.currentHp < maxHp) {
      const uint32_t healed = static_cast<uint32_t>(entry.currentHp) + item->effectValue;
      entry.currentHp = static_cast<uint16_t>(std::min<uint32_t>(maxHp, healed));
      changed = true;
    }
    if ((item->category == ItemCategory::Medicine || item->category == ItemCategory::StatusCure) &&
        item->curesAilment != Ailment::None && entry.status != Ailment::None &&
        (item->curesAilment == Ailment::All || item->curesAilment == entry.status)) {
      entry.status = Ailment::None;
      entry.statusTurns = 0;
      changed = true;
    }
    if (item->category == ItemCategory::PPRestore) {
      for (size_t slot = 0; slot < BATTLE_MOVE_SLOTS; ++slot) {
        if (entry.moves[slot] == 0) continue;
        const MoveData* move = moveData(entry.moves[slot]);
        const uint8_t maxPp = move == nullptr ? 0 : maxPpFor(move->pp, entry.ppUp[slot]);
        if (entry.pp[slot] >= maxPp) continue;
        entry.pp[slot] = static_cast<uint8_t>(
            std::min<uint32_t>(maxPp, static_cast<uint32_t>(entry.pp[slot]) + item->effectValue));
        changed = true;
      }
    }
  }
  if (!changed) return UseConsumableOutcome::NotApplicable;
  if (!battleStore_.upsertEntry(entry)) {
    LOG_ERR("PokemonService", "Failed to use consumable item");
    return UseConsumableOutcome::Failed;
  }
  return UseConsumableOutcome::Applied;
}

ServiceStatus PokemonService::setEvolutionPrompts(const uint32_t recordId, const bool enabled) {
  PokemonState state{};
  const ServiceStatus stateStatus = loadReadyState(state);
  if (stateStatus != ServiceStatus::Ok) return stateStatus;
  PokemonRecord record{};
  if (!store_.readRecord(recordId, record)) return ServiceStatus::NotFound;
  RecordMutation mutation{};
  if (!pokemon::setEvolutionPrompts(state, record, enabled, mutation)) return ServiceStatus::NotApplicable;
  if (!store_.commit(state, mutation)) {
    LOG_ERR("PokemonService", "Failed to set evolution prompts");
    return ServiceStatus::StorageError;
  }
  return ServiceStatus::Ok;
}

ServiceStatus PokemonService::useEvolutionItem(const uint32_t recordId, const EvolutionItem item) {
  PokemonState state{};
  const ServiceStatus stateStatus = loadReadyState(state);
  if (stateStatus != ServiceStatus::Ok) return stateStatus;
  PokemonRecord record{};
  if (!store_.readRecord(recordId, record)) return ServiceStatus::NotFound;
  RecordMutation mutation{};
  if (!pokemon::useEvolutionItem(state, record, item, mutation)) return ServiceStatus::NotApplicable;
  if (!store_.commit(state, mutation)) {
    LOG_ERR("PokemonService", "Failed to use evolution item");
    return ServiceStatus::StorageError;
  }
  return ServiceStatus::Ok;
}

ServiceStatus PokemonService::consumeBagItem(const uint8_t itemId) {
  if (itemId == 0 || itemId > POKEMON_ITEM_ID_MAX) return ServiceStatus::Invalid;
  PokemonState state{};
  const ServiceStatus stateStatus = loadReadyState(state);
  if (stateStatus != ServiceStatus::Ok) return stateStatus;

  if (itemId <= EVOLUTION_ITEM_COUNT) {
    const size_t index = itemId - 1U;
    if (state.itemCounts[index] == 0) return ServiceStatus::NotApplicable;
    --state.itemCounts[index];
  } else if (itemId == PP_UP_ITEM_ID) {
    if (state.ppUpCount == 0) return ServiceStatus::NotApplicable;
    --state.ppUpCount;
  } else {
    const size_t index = itemId - EVOLUTION_ITEM_COUNT - 1U;
    if (state.bagCounts[index] == 0) return ServiceStatus::NotApplicable;
    --state.bagCounts[index];
  }
  if (!store_.commit(state)) {
    LOG_ERR("PokemonService", "Failed to consume bag item");
    return ServiceStatus::StorageError;
  }
  return ServiceStatus::Ok;
}

ServiceStatus PokemonService::markGymDefeated(const uint8_t gymIndex) {
  if (gymIndex == 0 || gymIndex > POKEMON_GYM_PROGRESS_BITS) return ServiceStatus::Invalid;
  PokemonState state{};
  const ServiceStatus stateStatus = loadReadyState(state);
  if (stateStatus != ServiceStatus::Ok) return stateStatus;

  const GymProgress progress = gymProgressFor(state.battleProgress, gymIndex);
  if (progress == GymProgress::Defeated) return ServiceStatus::Ok;  // idempotent: already recorded
  if (progress == GymProgress::Locked) return ServiceStatus::NotApplicable;

  const uint16_t bit = static_cast<uint16_t>(1U << (gymIndex - 1U));
  state.battleProgress = static_cast<uint16_t>(state.battleProgress | bit);
  if (!store_.commit(state)) {
    LOG_ERR("PokemonService", "Failed to record gym victory");
    return ServiceStatus::StorageError;
  }
  return ServiceStatus::Ok;
}

BattleRecordEntry PokemonService::synthesizeBattleEntry(const PokemonRecord& record, const IvEvEntry& ivEv) const {
  BattleRecordEntry entry{};
  entry.recordId = record.recordId;
  const uint8_t level = levelForXp(record.totalXp);
  const BaseStats* stats = baseStatsFor(record.speciesId);
  constexpr size_t hpIndex = static_cast<size_t>(StatIndex::Hp);
  entry.currentHp = stats == nullptr ? 1 : battleMaxHp(stats->hp, level, ivEv.iv[hpIndex], ivEv.ev[hpIndex]);
  defaultMovesetForLevel(record.speciesId, level, entry.moves, entry.pp);
  entry.status = Ailment::None;
  entry.statusTurns = 0;
  return entry;
}

BattleRecordEntry PokemonService::peekBattleMoves(const PokemonRecord& record) const {
  if (const BattleRecordEntry* existing = battleStore_.findEntry(record.recordId); existing != nullptr) {
    return *existing;
  }
  return synthesizeBattleEntry(record, peekIvEv(record.recordId));
}

ServiceStatus PokemonService::loadBattleEntry(const uint32_t recordId, BattleRecordEntry& output) {
  PokemonRecord record{};
  const ServiceStatus readStatus = readRecord(recordId, record);
  if (readStatus != ServiceStatus::Ok) return readStatus;

  if (const BattleRecordEntry* existing = battleStore_.findEntry(recordId); existing != nullptr) {
    output = *existing;
    return ServiceStatus::Ok;
  }

  const BattleRecordEntry synthesized = synthesizeBattleEntry(record, ensureIvEv(recordId));
  if (!battleStore_.upsertEntry(synthesized)) {
    LOG_ERR("PokemonService", "Failed to persist synthesized battle entry");
    return ServiceStatus::StorageError;
  }
  output = synthesized;
  return ServiceStatus::Ok;
}

IvEvEntry PokemonService::ensureIvEv(const uint32_t recordId) {
  if (recordId == 0) return {};
  if (const IvEvEntry* existing = ivEvStore_.findEntry(recordId); existing != nullptr) return *existing;

  IvEvEntry fresh{};
  fresh.recordId = recordId;
  rollIvSet(random_, fresh.iv);
  if (!ivEvStore_.upsertEntry(fresh)) {
    LOG_ERR("PokemonService", "Failed to persist rolled IV/EV for record %u", recordId);
  }
  return fresh;
}

IvEvEntry PokemonService::peekIvEv(const uint32_t recordId) const {
  if (const IvEvEntry* existing = ivEvStore_.findEntry(recordId); existing != nullptr) return *existing;
  return {};
}

ServiceStatus PokemonService::saveBattleEntry(const BattleRecordEntry& entry) {
  if (!battleStore_.upsertEntry(entry)) {
    LOG_ERR("PokemonService", "Failed to save battle entry");
    return ServiceStatus::StorageError;
  }
  return ServiceStatus::Ok;
}

void PokemonService::healPartyOnRead(const PokemonState& state, const uint16_t minutes) {
  constexpr uint16_t HP_HEAL_PER_MINUTE = 1;
  constexpr uint16_t MINUTES_PER_PP_TICK = 10;

  for (const uint32_t recordId : state.partyRecordIds) {
    if (recordId == 0) continue;
    const BattleRecordEntry* existing = battleStore_.findEntry(recordId);
    // Nothing to heal if it was never fought - it will synthesize at full
    // HP/PP the first time it is needed anyway.
    if (existing == nullptr) continue;

    PokemonRecord record{};
    if (!store_.readRecord(recordId, record)) continue;
    const BaseStats* stats = baseStatsFor(record.speciesId);
    if (stats == nullptr) continue;
    const IvEvEntry ivEv = ensureIvEv(recordId);
    constexpr size_t hpIndex = static_cast<size_t>(StatIndex::Hp);
    const uint16_t maxHp = battleMaxHp(stats->hp, levelForXp(record.totalXp), ivEv.iv[hpIndex], ivEv.ev[hpIndex]);

    BattleRecordEntry healed = *existing;
    const uint32_t healedHp =
        static_cast<uint32_t>(healed.currentHp) + static_cast<uint32_t>(HP_HEAL_PER_MINUTE) * minutes;
    healed.currentHp = static_cast<uint16_t>(std::min<uint32_t>(maxHp, healedHp));

    const uint16_t ppTicks = static_cast<uint16_t>(minutes / MINUTES_PER_PP_TICK);
    if (ppTicks > 0) {
      for (size_t slot = 0; slot < BATTLE_MOVE_SLOTS; ++slot) {
        if (healed.moves[slot] == 0) continue;
        const MoveData* move = moveData(healed.moves[slot]);
        const uint8_t maxPp = move == nullptr ? 0 : maxPpFor(move->pp, healed.ppUp[slot]);
        const uint32_t healedPp = static_cast<uint32_t>(healed.pp[slot]) + ppTicks;
        healed.pp[slot] = static_cast<uint8_t>(std::min<uint32_t>(maxPp, healedPp));
      }
    }

    if (healed.currentHp >= maxHp && healed.status != Ailment::None) {
      healed.status = Ailment::None;
      healed.statusTurns = 0;
    }

    if (healed == *existing) continue;  // avoid a pointless SD write when there was nothing to heal
    // Best-effort: a battle-store write failure here should not fail the
    // reading-credit commit that already succeeded.
    battleStore_.upsertEntry(healed);
  }
}

void PokemonService::queueMoveLearnIfNeeded(PokemonState& state, const PokemonRecord& leader,
                                            const uint8_t previousLevel, const uint8_t currentLevel) {
  if (currentLevel <= previousLevel) return;
  const BattleRecordEntry* existing = battleStore_.findEntry(leader.recordId);
  if (existing == nullptr) return;

  BattleRecordEntry entry = *existing;
  bool changed = false;
  for (const LearnsetEntry& learn : learnsetFor(leader.speciesId)) {
    if (learn.level <= previousLevel || learn.level > currentLevel) continue;
    bool known = false;
    for (size_t slot = 0; slot < BATTLE_MOVE_SLOTS; ++slot) {
      if (entry.moves[slot] == learn.moveId) {
        known = true;
        break;
      }
    }
    if (known) continue;

    bool placed = false;
    for (size_t slot = 0; slot < BATTLE_MOVE_SLOTS; ++slot) {
      if (entry.moves[slot] != 0) continue;
      const MoveData* move = moveData(learn.moveId);
      entry.moves[slot] = learn.moveId;
      // Raw move->pp, not maxPpFor() - this only ever fills a currently-empty
      // slot, and validateBattleRecordEntry requires ppUp to already be 0
      // there, so the two are equivalent; written this way to match the
      // other empty-slot-fill sites.
      entry.pp[slot] = move == nullptr ? 0 : move->pp;
      changed = true;
      placed = true;
      break;
    }
    if (!placed) {
      const PendingEvent event{leader.recordId, learn.moveId,        learn.level,
                               Gender::Unknown, EvolutionItem::None, PendingEventKind::MoveLearn};
      enqueuePendingEvent(state, event);  // best-effort: a full queue just skips this one
    }
  }
  if (changed) battleStore_.upsertEntry(entry);
}

BattleTurnResult PokemonService::resolveBattleTurn(BattleCombatant& player, BattleCombatant& opponent,
                                                   const uint8_t playerMoveSlot) {
  return stepBattle(player, opponent, playerMoveSlot, random_);
}

BattleTurnResult PokemonService::resolveOpponentOnlyTurn(BattleCombatant& player, BattleCombatant& opponent) {
  return stepOpponentOnlyTurn(player, opponent, random_);
}

bool PokemonService::attemptBattleCatch(const BattleCombatant& wild, const BallKind ball) {
  return attemptCatch(wild, ball, random_);
}

Gender PokemonService::rollGenderFor(const uint16_t speciesId) {
  Gender gender = Gender::Unknown;
  chooseGenderForSpecies(speciesId, random_, gender);
  return gender;
}

std::array<uint8_t, STAT_COUNT> PokemonService::rollWildIv() {
  std::array<uint8_t, STAT_COUNT> iv{};
  rollIvSet(random_, iv);
  return iv;
}

ServiceStatus PokemonService::awardBattleXp(const uint32_t recordId, const uint8_t opponentLevel,
                                            const bool isTrainerBattle, const uint16_t opponentSpeciesId) {
  PokemonRecord record{};
  const ServiceStatus readStatus = readRecord(recordId, record);
  if (readStatus != ServiceStatus::Ok) return readStatus;

  // EVs accumulate independently of the XP/level cap below - unlike XP,
  // real Gen 1 EVs keep accruing even once a Pokemon is at its maximum
  // level, so this runs first and unconditionally.
  if (const BaseStats* opponentStats = baseStatsFor(opponentSpeciesId); opponentStats != nullptr) {
    IvEvEntry ivEv = ensureIvEv(recordId);
    const std::array<uint8_t, STAT_COUNT> yield{opponentStats->evHp, opponentStats->evAttack,
                                                 opponentStats->evDefense, opponentStats->evSpecial,
                                                 opponentStats->evSpeed};
    for (size_t index = 0; index < STAT_COUNT; ++index) {
      ivEv.ev[index] = static_cast<uint8_t>(std::min<uint32_t>(255U, ivEv.ev[index] + yield[index]));
    }
    if (!ivEvStore_.upsertEntry(ivEv)) {
      LOG_ERR("PokemonService", "Failed to persist EV gain for record %u", recordId);
    }
  }

  const uint8_t previousLevel = levelForXp(record.totalXp);
  if (record.totalXp >= MAXIMUM_TOTAL_XP) return ServiceStatus::Ok;  // already level 100 - nothing to gain

  const uint32_t xpGained = battleVictoryXp(opponentLevel, isTrainerBattle);
  record.totalXp = std::min<uint32_t>(record.totalXp + xpGained, MAXIMUM_TOTAL_XP);
  const uint8_t currentLevel = levelForXp(record.totalXp);

  PokemonState state{};
  const ServiceStatus stateStatus = loadReadyState(state);
  if (stateStatus != ServiceStatus::Ok) return stateStatus;
  queueMoveLearnIfNeeded(state, record, previousLevel, currentLevel);
  const RecordMutation mutation{record.recordId, record, RecordMutationKind::Replace};
  if (!store_.commit(state, mutation)) {
    LOG_ERR("PokemonService", "Failed to award battle XP");
    return ServiceStatus::StorageError;
  }
  return ServiceStatus::Ok;
}

ServiceStatus PokemonService::reset() {
  readingSessionActive_ = false;
  if (!store_.reset()) {
    LOG_ERR("PokemonService", "Failed to reset Pokemon save");
    return ServiceStatus::StorageError;
  }
  // Best-effort: a fresh game reassigns record ids starting from 1 again, so
  // leaving a previous playthrough's HP/PP/moveset behind under those same
  // ids would silently hand new Pokemon someone else's battle data the first
  // time they're looked up. Still report the reset itself as successful if
  // only this part fails - the main save (the part the player actually sees
  // and cares about) is already wiped either way, and a stray leftover
  // battle-store entry gets overwritten the moment its Pokemon fights once
  // for real.
  if (!battleStore_.reset()) {
    LOG_ERR("PokemonService", "Failed to reset Pokemon battle store");
  }
  // Same best-effort spirit as the battle store reset above - a stray
  // leftover IV/EV entry under a reused record id just gets overwritten the
  // moment ensureIvEv() runs for that id again.
  if (!ivEvStore_.reset()) {
    LOG_ERR("PokemonService", "Failed to reset Pokemon IV/EV store");
  }
  return ServiceStatus::Ok;
}

bool PokemonService::beginReadingSession() {
  readingSessionActive_ = false;
  if (!store_.isReady() && store_.begin() != StoreBeginResult::Ready) return false;

  PokemonState state{};
  PokemonRecord leader{};
  if (!store_.loadState(state) || state.partyRecordIds[0] == 0 || !store_.readRecord(state.partyRecordIds[0], leader)) {
    return false;
  }

  if (!tracker_.beginSession()) {
    LOG_ERR("PokemonService", "Failed to retry unsaved reading credit");
    return false;
  }
  readingSessionActive_ = true;
  return true;
}

void PokemonService::setBookProgressPercent(const uint8_t percent) {
  if (readingSessionActive_) tracker_.setBookProgressPercent(percent);
}

void PokemonService::onSuccessfulPageTurn(const uint32_t nowMs) {
  if (readingSessionActive_) tracker_.onSuccessfulPageTurn(nowMs);
}

void PokemonService::checkpointIfDue(const uint32_t nowMs) {
  if (readingSessionActive_) tracker_.checkpointIfDue(nowMs);
}

void PokemonService::flushOnExit(const uint32_t nowMs) {
  if (!readingSessionActive_) return;
  if (!tracker_.flushOnExit(nowMs)) {
    LOG_ERR("PokemonService", "Retaining unsaved reading credit for the next session");
  }
  readingSessionActive_ = false;
}

bool PokemonService::creditFromTracker(void* context, const uint16_t minutes, const uint8_t bookProgressPercent) {
  return static_cast<PokemonService*>(context)->creditMinutes(minutes, bookProgressPercent);
}

bool PokemonService::creditMinutes(const uint16_t minutes, const uint8_t bookProgressPercent) {
  PokemonState state{};
  if (minutes == 0 || !store_.loadState(state) || state.partyRecordIds[0] == 0) return false;

  PokemonRecord leader{};
  if (!store_.readRecord(state.partyRecordIds[0], leader)) {
    LOG_ERR("PokemonService", "Failed to load party leader");
    return false;
  }

  OwnedEvolutionNeeds ownedEvolutionNeeds{};
  if (static_cast<uint32_t>(state.readingMinuteRemainder) + minutes >= 60U &&
      !store_.loadOwnedEvolutionNeeds(ownedEvolutionNeeds)) {
    LOG_ERR("PokemonService", "Failed to load owned evolution needs");
    return false;
  }

  const uint32_t originalLeaderXp = leader.totalXp;
  const CreditResult result =
      applyCreditedMinutes(state, leader, minutes, bookProgressPercent, ownedEvolutionNeeds, random_);
  if (result.status != CreditStatus::Applied) return false;

  RecordMutation mutation{};
  if (leader.totalXp != originalLeaderXp) {
    mutation.requestedRecordId = leader.recordId;
    mutation.record = leader;
    mutation.kind = RecordMutationKind::Replace;
  }
  queueMoveLearnIfNeeded(state, leader, result.previousLevel, result.currentLevel);
  if (!store_.commit(state, mutation)) {
    LOG_ERR("PokemonService", "Failed to commit credited reading");
    return false;
  }
  healPartyOnRead(state, minutes);
  return true;
}

#if !defined(POKEMON_SERVICE_HOST_TEST)
PokemonService& devicePokemonService() {
  static PokemonStore store;
  static PokemonBattleStore battleStore;
  static PokemonIvEvStore ivEvStore;
  static PokemonService service(store, battleStore, ivEvStore, {nullptr, deviceRandomBelow});
  return service;
}
#endif

}  // namespace pokemon

#endif
