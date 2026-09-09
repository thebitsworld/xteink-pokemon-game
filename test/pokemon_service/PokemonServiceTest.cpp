#include <HalStorage.h>
#include <gtest/gtest.h>

#include "pokemon/PokemonService.h"

namespace {

uint32_t zeroRandom(void*, uint32_t) { return 0; }

uint32_t itemEventRandom(void*, const uint32_t upperExclusive) { return upperExclusive == 5 ? 2U : 0U; }

pokemon::PokemonRecord starterPikachu() {
  pokemon::PokemonRecord record{};
  record.recordId = 1;
  record.totalXp = pokemon::xpRequired(5);
  record.speciesId = 25;
  record.caughtLevel = 5;
  record.gender = pokemon::Gender::Female;
  record.origin = pokemon::Origin::Starter;
  return record;
}

void seedStarter(pokemon::PokemonStore& store) {
  ASSERT_EQ(store.begin(), pokemon::StoreBeginResult::Empty);
  const pokemon::PokemonRecord starter = starterPikachu();
  pokemon::PokemonState state{};
  state.partyRecordIds[0] = starter.recordId;
  ASSERT_TRUE(pokemon::markSpecies(state.seenSpecies, starter.speciesId));
  ASSERT_TRUE(pokemon::markSpecies(state.caughtSpecies, starter.speciesId));
  const pokemon::RecordMutation mutation{starter.recordId, starter, pokemon::RecordMutationKind::Append};
  ASSERT_TRUE(store.commit(state, mutation));
}

pokemon::PokemonRecord caughtPokemon(const uint32_t recordId, const uint16_t speciesId) {
  pokemon::PokemonRecord record{};
  record.recordId = recordId;
  record.totalXp = pokemon::xpRequired(5);
  record.speciesId = speciesId;
  record.caughtLevel = 5;
  record.gender = pokemon::Gender::Male;
  record.origin = pokemon::Origin::Caught;
  return record;
}

void appendOwnedPokemon(pokemon::PokemonStore& store, const pokemon::PokemonRecord& record, const bool addToParty) {
  pokemon::PokemonState state{};
  ASSERT_TRUE(store.loadState(state));
  ASSERT_TRUE(pokemon::markSpecies(state.seenSpecies, record.speciesId));
  ASSERT_TRUE(pokemon::markSpecies(state.caughtSpecies, record.speciesId));
  if (addToParty) {
    size_t slot = 0;
    while (slot < pokemon::PARTY_SIZE && state.partyRecordIds[slot] != 0) ++slot;
    ASSERT_LT(slot, pokemon::PARTY_SIZE);
    state.partyRecordIds[slot] = record.recordId;
  }
  const pokemon::RecordMutation mutation{record.recordId, record, pokemon::RecordMutationKind::Append};
  ASSERT_TRUE(store.commit(state, mutation));
}

TEST(PokemonService, VerifiedCheckpointDurablyCreditsStateAndLeader) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  seedStarter(store);
  pokemon::PokemonService service(store, battleStore, {nullptr, zeroRandom});

  ASSERT_TRUE(service.beginReadingSession());
  service.setBookProgressPercent(64);
  service.onSuccessfulPageTurn(0);
  service.checkpointIfDue(300000);

  pokemon::PokemonStore reopened;
  ASSERT_EQ(reopened.begin(), pokemon::StoreBeginResult::Ready);
  pokemon::PokemonState state{};
  pokemon::PokemonRecord leader{};
  ASSERT_TRUE(reopened.loadState(state));
  ASSERT_TRUE(reopened.readRecord(1, leader));
  EXPECT_EQ(state.lifetimeMinutes, 5U);
  EXPECT_EQ(leader.totalXp, pokemon::xpRequired(5) + 5U);
}

TEST(PokemonService, FailedSnapshotWriteAdvancesNeitherStateNorLeader) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  seedStarter(store);
  pokemon::PokemonService service(store, battleStore, {nullptr, zeroRandom});
  ASSERT_TRUE(service.beginReadingSession());

  Storage.setFailWritableOpen(true);
  EXPECT_FALSE(service.creditMinutes(5, 64));
  Storage.setFailWritableOpen(false);

  pokemon::PokemonStore reopened;
  ASSERT_EQ(reopened.begin(), pokemon::StoreBeginResult::Ready);
  pokemon::PokemonState state{};
  pokemon::PokemonRecord leader{};
  ASSERT_TRUE(reopened.loadState(state));
  ASSERT_TRUE(reopened.readRecord(1, leader));
  EXPECT_EQ(state.lifetimeMinutes, 0U);
  EXPECT_EQ(leader.totalXp, pokemon::xpRequired(5));
}

TEST(PokemonService, FailedSyncRetryCannotDoubleCreditOnTheNextReaderSession) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  seedStarter(store);
  pokemon::PokemonService service(store, battleStore, {nullptr, zeroRandom});
  ASSERT_TRUE(service.beginReadingSession());
  service.setBookProgressPercent(64);
  service.onSuccessfulPageTurn(0);

  Storage.setFailSync(true);
  service.flushOnExit(300000);
  Storage.setFailSync(false);

  ASSERT_TRUE(service.beginReadingSession());
  pokemon::PokemonState state{};
  pokemon::PokemonRecord leader{};
  ASSERT_TRUE(store.loadState(state));
  ASSERT_TRUE(store.readRecord(1, leader));
  EXPECT_EQ(state.lifetimeMinutes, 5U);
  EXPECT_EQ(leader.totalXp, pokemon::xpRequired(5) + 5U);
}

TEST(PokemonService, ReadingSessionDoesNotStartBeforeStarterExists) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  ASSERT_EQ(store.begin(), pokemon::StoreBeginResult::Empty);
  ASSERT_TRUE(store.commit({}));
  pokemon::PokemonService service(store, battleStore, {nullptr, zeroRandom});

  EXPECT_FALSE(service.beginReadingSession());
}

TEST(PokemonService, CreatesOneDurableStarterWithChosenIdentity) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonService service(store, battleStore, {nullptr, zeroRandom});

  EXPECT_EQ(service.createStarter(25, pokemon::Gender::Female, "CinderVolt"), pokemon::ServiceStatus::Ok);

  pokemon::PokemonSnapshot snapshot{};
  ASSERT_EQ(service.loadSnapshot(snapshot), pokemon::ServiceStatus::Ok);
  ASSERT_EQ(snapshot.partyCount, 1U);
  ASSERT_EQ(snapshot.ownedCount, 1U);
  EXPECT_EQ(snapshot.party[0].recordId, 1U);
  EXPECT_EQ(snapshot.party[0].speciesId, 25U);
  EXPECT_EQ(snapshot.party[0].gender, pokemon::Gender::Female);
  EXPECT_EQ(snapshot.party[0].origin, pokemon::Origin::Starter);
  EXPECT_EQ(snapshot.party[0].totalXp, pokemon::xpRequired(5));
  EXPECT_STREQ(snapshot.party[0].nickname.data(), "CinderVolt");
  EXPECT_TRUE(pokemon::isSpeciesMarked(snapshot.state.seenSpecies, 25));
  EXPECT_TRUE(pokemon::isSpeciesMarked(snapshot.state.caughtSpecies, 25));

  pokemon::PokemonStore reopened;
  ASSERT_EQ(reopened.begin(), pokemon::StoreBeginResult::Ready);
  pokemon::PokemonRecord durable{};
  ASSERT_TRUE(reopened.readRecord(1, durable));
  EXPECT_EQ(durable, snapshot.party[0]);
}

TEST(PokemonService, RejectsSecondStarterWithoutChangingTheSave) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonService service(store, battleStore, {nullptr, zeroRandom});
  ASSERT_EQ(service.createStarter(1, pokemon::Gender::Male, ""), pokemon::ServiceStatus::Ok);

  EXPECT_EQ(service.createStarter(4, pokemon::Gender::Female, ""), pokemon::ServiceStatus::AlreadyStarted);

  pokemon::PokemonSnapshot snapshot{};
  ASSERT_EQ(service.loadSnapshot(snapshot), pokemon::ServiceStatus::Ok);
  ASSERT_EQ(snapshot.partyCount, 1U);
  EXPECT_EQ(snapshot.party[0].speciesId, 1U);
  EXPECT_EQ(snapshot.ownedCount, 1U);
}

TEST(PokemonService, RenamesDurablyAndLeavesTheOldNameWhenSavingFails) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  seedStarter(store);
  pokemon::PokemonService service(store, battleStore, {nullptr, zeroRandom});

  ASSERT_EQ(service.renamePokemon(1, "Sparky"), pokemon::ServiceStatus::Ok);
  pokemon::PokemonRecord renamed{};
  ASSERT_EQ(service.readRecord(1, renamed), pokemon::ServiceStatus::Ok);
  EXPECT_STREQ(renamed.nickname.data(), "Sparky");

  Storage.setFailWritableOpen(true);
  EXPECT_EQ(service.renamePokemon(1, "Static"), pokemon::ServiceStatus::StorageError);
  Storage.setFailWritableOpen(false);

  pokemon::PokemonStore reopened;
  ASSERT_EQ(reopened.begin(), pokemon::StoreBeginResult::Ready);
  ASSERT_TRUE(reopened.readRecord(1, renamed));
  EXPECT_STREQ(renamed.nickname.data(), "Sparky");
}

TEST(PokemonService, RejectsMovingOnlyMemberIntoAnEmptySlotWithoutWriting) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  seedStarter(store);
  pokemon::PokemonService service(store, battleStore, {nullptr, zeroRandom});

  EXPECT_EQ(service.movePartyMember(0, 5), pokemon::ServiceStatus::Invalid);

  pokemon::PokemonSnapshot snapshot{};
  ASSERT_EQ(service.loadSnapshot(snapshot), pokemon::ServiceStatus::Ok);
  ASSERT_EQ(snapshot.partyCount, 1U);
  EXPECT_EQ(snapshot.state.partyRecordIds[0], 1U);
  for (size_t slot = 1; slot < pokemon::PARTY_SIZE; ++slot) {
    EXPECT_EQ(snapshot.state.partyRecordIds[slot], 0U);
  }
}

TEST(PokemonService, ReordersOnlyOccupiedPartySlotsAndChangesTheLeader) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  seedStarter(store);
  appendOwnedPokemon(store, caughtPokemon(2, 4), true);
  appendOwnedPokemon(store, caughtPokemon(3, 7), true);
  pokemon::PokemonService service(store, battleStore, {nullptr, zeroRandom});

  ASSERT_EQ(service.movePartyMember(2, 0), pokemon::ServiceStatus::Ok);

  pokemon::PokemonSnapshot snapshot{};
  ASSERT_EQ(service.loadSnapshot(snapshot), pokemon::ServiceStatus::Ok);
  ASSERT_EQ(snapshot.partyCount, 3U);
  EXPECT_EQ(snapshot.state.partyRecordIds[0], 3U);
  EXPECT_EQ(snapshot.state.partyRecordIds[1], 1U);
  EXPECT_EQ(snapshot.state.partyRecordIds[2], 2U);
  EXPECT_EQ(snapshot.party[0].speciesId, 7U);
}

TEST(PokemonService, ProtectsTheLastPartyMemberAndSupportsDepositWithdraw) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  seedStarter(store);
  pokemon::PokemonService service(store, battleStore, {nullptr, zeroRandom});
  EXPECT_EQ(service.depositPokemon(1), pokemon::ServiceStatus::LastPokemon);

  appendOwnedPokemon(store, caughtPokemon(2, 4), true);
  ASSERT_EQ(service.depositPokemon(1), pokemon::ServiceStatus::Ok);
  pokemon::PokemonSnapshot snapshot{};
  ASSERT_EQ(service.loadSnapshot(snapshot), pokemon::ServiceStatus::Ok);
  ASSERT_EQ(snapshot.partyCount, 1U);
  EXPECT_EQ(snapshot.state.partyRecordIds[0], 2U);

  ASSERT_EQ(service.withdrawPokemon(1), pokemon::ServiceStatus::Ok);
  ASSERT_EQ(service.loadSnapshot(snapshot), pokemon::ServiceStatus::Ok);
  ASSERT_EQ(snapshot.partyCount, 2U);
  EXPECT_EQ(snapshot.state.partyRecordIds[1], 1U);
}

TEST(PokemonService, RejectsWithdrawalWhenThePartyIsFull) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  seedStarter(store);
  for (uint32_t id = 2; id <= 7; ++id) {
    appendOwnedPokemon(store, caughtPokemon(id, static_cast<uint16_t>(id + 3)), id <= 6);
  }
  pokemon::PokemonService service(store, battleStore, {nullptr, zeroRandom});

  EXPECT_EQ(service.withdrawPokemon(7), pokemon::ServiceStatus::PartyFull);

  pokemon::PokemonSnapshot snapshot{};
  ASSERT_EQ(service.loadSnapshot(snapshot), pokemon::ServiceStatus::Ok);
  EXPECT_EQ(snapshot.partyCount, pokemon::PARTY_SIZE);
}

TEST(PokemonService, ResolvesEncounterCatchThenAllowsNickname) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  seedStarter(store);
  pokemon::PokemonState state{};
  ASSERT_TRUE(store.loadState(state));
  state.pendingEvents[0].kind = pokemon::PendingEventKind::Encounter;
  state.pendingEvents[0].speciesId = 133;
  state.pendingEvents[0].level = 12;
  state.pendingEvents[0].gender = pokemon::Gender::Female;
  state.dashboardNotice = pokemon::DashboardNotice::NewPokemon;
  ASSERT_TRUE(store.commit(state));
  pokemon::PokemonService service(store, battleStore, {nullptr, zeroRandom});

  uint32_t caughtRecordId = 0;
  ASSERT_EQ(service.resolveEncounter(pokemon::EncounterChoice::Catch, caughtRecordId), pokemon::ServiceStatus::Ok);
  EXPECT_EQ(caughtRecordId, 2U);
  ASSERT_EQ(service.renamePokemon(caughtRecordId, "Nova"), pokemon::ServiceStatus::Ok);

  pokemon::PokemonSnapshot snapshot{};
  ASSERT_EQ(service.loadSnapshot(snapshot), pokemon::ServiceStatus::Ok);
  ASSERT_EQ(snapshot.partyCount, 2U);
  EXPECT_EQ(snapshot.state.pendingEvents[0].kind, pokemon::PendingEventKind::None);
  EXPECT_EQ(snapshot.state.dashboardNotice, pokemon::DashboardNotice::None);
  EXPECT_EQ(snapshot.party[1].speciesId, 133U);
  EXPECT_STREQ(snapshot.party[1].nickname.data(), "Nova");
}

TEST(PokemonService, ResolvesEncounterPassWithoutCreatingARecord) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  seedStarter(store);
  pokemon::PokemonState state{};
  ASSERT_TRUE(store.loadState(state));
  state.pendingEvents[0].kind = pokemon::PendingEventKind::Encounter;
  state.pendingEvents[0].speciesId = 4;
  state.pendingEvents[0].level = 9;
  state.pendingEvents[0].gender = pokemon::Gender::Male;
  state.dashboardNotice = pokemon::DashboardNotice::NewPokemon;
  ASSERT_TRUE(store.commit(state));
  pokemon::PokemonService service(store, battleStore, {nullptr, zeroRandom});

  uint32_t caughtRecordId = 99;
  ASSERT_EQ(service.resolveEncounter(pokemon::EncounterChoice::Pass, caughtRecordId), pokemon::ServiceStatus::Ok);
  EXPECT_EQ(caughtRecordId, 0U);
  pokemon::PokemonSnapshot snapshot{};
  ASSERT_EQ(service.loadSnapshot(snapshot), pokemon::ServiceStatus::Ok);
  EXPECT_EQ(snapshot.ownedCount, 1U);
  EXPECT_EQ(snapshot.state.pendingEvents[0].kind, pokemon::PendingEventKind::None);
}

TEST(PokemonService, AcknowledgesItemAndPublishesBoundedDashboardSnapshot) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  seedStarter(store);
  pokemon::PokemonState state{};
  ASSERT_TRUE(store.loadState(state));
  state.pendingEvents[0].kind = pokemon::PendingEventKind::Item;
  state.pendingEvents[0].item = pokemon::EvolutionItem::ThunderStone;
  state.pendingEvents[1].kind = pokemon::PendingEventKind::Encounter;
  state.pendingEvents[1].speciesId = 4;
  state.pendingEvents[1].level = 9;
  state.pendingEvents[1].gender = pokemon::Gender::Male;
  state.itemCounts[2] = 1;
  state.dashboardNotice = pokemon::DashboardNotice::ItemFound;
  ASSERT_TRUE(store.commit(state));
  pokemon::PokemonService service(store, battleStore, {nullptr, zeroRandom});

  pokemon::PokemonDashboardSnapshot dashboard{};
  ASSERT_EQ(service.loadDashboardSnapshot(dashboard), pokemon::ServiceStatus::Ok);
  EXPECT_EQ(dashboard.leader.speciesId, 25U);
  EXPECT_EQ(dashboard.notice, pokemon::DashboardNotice::ItemFound);
  EXPECT_EQ(dashboard.pending.kind, pokemon::PendingEventKind::Item);

  ASSERT_EQ(service.acknowledgeItem(), pokemon::ServiceStatus::Ok);
  ASSERT_EQ(service.loadDashboardSnapshot(dashboard), pokemon::ServiceStatus::Ok);
  EXPECT_EQ(dashboard.pending.kind, pokemon::PendingEventKind::Encounter);
  EXPECT_EQ(dashboard.pending.speciesId, 4U);
  EXPECT_EQ(dashboard.notice, pokemon::DashboardNotice::NewPokemon);
  pokemon::PokemonSnapshot snapshot{};
  ASSERT_EQ(service.loadSnapshot(snapshot), pokemon::ServiceStatus::Ok);
  EXPECT_EQ(snapshot.state.pendingEvents[0].kind, pokemon::PendingEventKind::Encounter);
  EXPECT_EQ(snapshot.state.pendingEvents[1].kind, pokemon::PendingEventKind::None);
  EXPECT_EQ(snapshot.state.itemCounts[2], 1U);
}

TEST(PokemonService, EvolvesByLevelAndCanDisableFuturePrompts) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  ASSERT_EQ(store.begin(), pokemon::StoreBeginResult::Empty);
  pokemon::PokemonRecord bulbasaur = caughtPokemon(1, 1);
  bulbasaur.origin = pokemon::Origin::Starter;
  bulbasaur.totalXp = pokemon::xpRequired(16);
  pokemon::PokemonState state{};
  state.partyRecordIds[0] = 1;
  state.pendingEvents[0].kind = pokemon::PendingEventKind::Evolution;
  state.pendingEvents[0].recordId = 1;
  state.pendingEvents[0].speciesId = 2;
  state.dashboardNotice = pokemon::DashboardNotice::WhatsThis;
  ASSERT_TRUE(pokemon::markSpecies(state.seenSpecies, 1));
  ASSERT_TRUE(pokemon::markSpecies(state.caughtSpecies, 1));
  ASSERT_TRUE(store.commit(state, {1, bulbasaur, pokemon::RecordMutationKind::Append}));
  pokemon::PokemonService service(store, battleStore, {nullptr, zeroRandom});

  ASSERT_EQ(service.resolveEvolution(pokemon::EvolutionChoice::Evolve), pokemon::ServiceStatus::Ok);
  pokemon::PokemonRecord evolved{};
  ASSERT_EQ(service.readRecord(1, evolved), pokemon::ServiceStatus::Ok);
  EXPECT_EQ(evolved.speciesId, 2U);
  ASSERT_EQ(service.setEvolutionPrompts(1, false), pokemon::ServiceStatus::Ok);
  ASSERT_EQ(service.readRecord(1, evolved), pokemon::ServiceStatus::Ok);
  EXPECT_NE(evolved.flags & pokemon::recordFlag(pokemon::RecordFlag::EvolutionPromptsDisabled), 0U);
}

TEST(PokemonService, ConsumesStoneOnlyForApplicableEvolution) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  seedStarter(store);
  pokemon::PokemonState state{};
  ASSERT_TRUE(store.loadState(state));
  state.itemCounts[2] = 1;
  ASSERT_TRUE(store.commit(state));
  pokemon::PokemonService service(store, battleStore, {nullptr, zeroRandom});

  EXPECT_EQ(service.useEvolutionItem(1, pokemon::EvolutionItem::WaterStone), pokemon::ServiceStatus::NotApplicable);
  ASSERT_EQ(service.useEvolutionItem(1, pokemon::EvolutionItem::ThunderStone), pokemon::ServiceStatus::Ok);

  pokemon::PokemonSnapshot snapshot{};
  ASSERT_EQ(service.loadSnapshot(snapshot), pokemon::ServiceStatus::Ok);
  EXPECT_EQ(snapshot.party[0].speciesId, 26U);
  EXPECT_EQ(snapshot.state.itemCounts[2], 0U);
}

TEST(PokemonService, ReadsBoundedPcPagesAndResetsToEmpty) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  seedStarter(store);
  appendOwnedPokemon(store, caughtPokemon(2, 7), false);
  appendOwnedPokemon(store, caughtPokemon(3, 4), false);
  pokemon::PokemonService service(store, battleStore, {nullptr, zeroRandom});

  std::array<pokemon::PokemonRecord, 6> page{};
  size_t count = 0;
  ASSERT_EQ(service.readPcPage(pokemon::PcOrder::PokedexNumber, 0, page, count), pokemon::ServiceStatus::Ok);
  ASSERT_EQ(count, 2U);
  EXPECT_EQ(page[0].speciesId, 4U);
  EXPECT_EQ(page[1].speciesId, 7U);

  ASSERT_EQ(service.reset(), pokemon::ServiceStatus::Ok);
  pokemon::PokemonSnapshot snapshot{};
  EXPECT_EQ(service.loadSnapshot(snapshot), pokemon::ServiceStatus::Empty);

  ASSERT_EQ(service.createStarter(1, pokemon::Gender::Male, ""), pokemon::ServiceStatus::Ok);
  ASSERT_EQ(service.loadSnapshot(snapshot), pokemon::ServiceStatus::Ok);
  ASSERT_EQ(snapshot.partyCount, 1U);
  EXPECT_EQ(snapshot.party[0].speciesId, 1U);
}

TEST(PokemonService, PcReadFailureReturnsStorageErrorInsteadOfAnEmptyPage) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  seedStarter(store);
  pokemon::PokemonService service(store, battleStore, {nullptr, zeroRandom});
  std::array<pokemon::PokemonRecord, 6> page{};
  size_t count = 99;

  Storage.setFailRead(true);
  EXPECT_EQ(service.readPcPage(pokemon::PcOrder::CatchDate, 0, page, count), pokemon::ServiceStatus::StorageError);
  Storage.setFailRead(false);
  EXPECT_EQ(count, 0U);
}

TEST(PokemonService, LoadBattleEntrySynthesizesFromTheLearnsetWhenNoneExists) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  seedStarter(store);  // Pikachu (species 25), level 5
  pokemon::PokemonService service(store, battleStore, {nullptr, zeroRandom});

  pokemon::BattleRecordEntry entry{};
  ASSERT_EQ(service.loadBattleEntry(1, entry), pokemon::ServiceStatus::Ok);
  EXPECT_EQ(entry.recordId, 1U);
  EXPECT_EQ(entry.currentHp, 18U);  // battleMaxHp(baseHp=35, level=5)
  EXPECT_EQ(entry.moves[0], 84U);   // Thunder Shock: most recently learned move at or below level 5
  EXPECT_EQ(entry.moves[1], 45U);   // Growl: the other level-1 move
  EXPECT_EQ(entry.moves[2], 0U);
  EXPECT_EQ(entry.pp[0], 30U);
  EXPECT_EQ(entry.pp[1], 40U);
  EXPECT_EQ(entry.status, pokemon::Ailment::None);

  // The synthesized entry is persisted, not just returned.
  const pokemon::BattleRecordEntry* persisted = battleStore.findEntry(1);
  ASSERT_NE(persisted, nullptr);
  EXPECT_EQ(*persisted, entry);
}

TEST(PokemonService, SaveBattleEntryPersistsAndAFollowingLoadReturnsTheSavedVersionNotAFreshSynthesis) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  seedStarter(store);
  pokemon::PokemonService service(store, battleStore, {nullptr, zeroRandom});

  pokemon::BattleRecordEntry entry{};
  ASSERT_EQ(service.loadBattleEntry(1, entry), pokemon::ServiceStatus::Ok);
  entry.currentHp = 5;
  entry.status = pokemon::Ailment::Poison;
  ASSERT_EQ(service.saveBattleEntry(entry), pokemon::ServiceStatus::Ok);

  pokemon::BattleRecordEntry reloaded{};
  ASSERT_EQ(service.loadBattleEntry(1, reloaded), pokemon::ServiceStatus::Ok);
  EXPECT_EQ(reloaded.currentHp, 5U);
  EXPECT_EQ(reloaded.status, pokemon::Ailment::Poison);
}

TEST(PokemonService, ConsumeBagItemDecrementsStonesAndNonStoneItemsInTheirOwnArrays) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  seedStarter(store);
  pokemon::PokemonService service(store, battleStore, {nullptr, zeroRandom});

  EXPECT_EQ(service.consumeBagItem(1), pokemon::ServiceStatus::NotApplicable);  // Moon Stone count is 0
  EXPECT_EQ(service.consumeBagItem(7), pokemon::ServiceStatus::NotApplicable);  // Poke Ball count is 0
  EXPECT_EQ(service.consumeBagItem(0), pokemon::ServiceStatus::Invalid);
  EXPECT_EQ(service.consumeBagItem(pokemon::POKEMON_ITEM_ID_MAX + 1), pokemon::ServiceStatus::Invalid);

  pokemon::PokemonState state{};
  ASSERT_TRUE(store.loadState(state));
  state.itemCounts[0] = 3;  // Moon Stone
  state.bagCounts[0] = 2;   // Poke Ball (item id 7)
  ASSERT_TRUE(store.commit(state));

  ASSERT_EQ(service.consumeBagItem(1), pokemon::ServiceStatus::Ok);
  ASSERT_EQ(service.consumeBagItem(7), pokemon::ServiceStatus::Ok);
  ASSERT_TRUE(store.loadState(state));
  EXPECT_EQ(state.itemCounts[0], 2U);
  EXPECT_EQ(state.bagCounts[0], 1U);
}

TEST(PokemonService, MarkGymDefeatedEnforcesLinearUnlockAndIsIdempotent) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  seedStarter(store);
  pokemon::PokemonService service(store, battleStore, {nullptr, zeroRandom});

  EXPECT_EQ(service.markGymDefeated(2), pokemon::ServiceStatus::NotApplicable);  // gym 1 not yet defeated
  EXPECT_EQ(service.markGymDefeated(9), pokemon::ServiceStatus::NotApplicable);  // no gyms defeated yet
  EXPECT_EQ(service.markGymDefeated(0), pokemon::ServiceStatus::Invalid);
  EXPECT_EQ(service.markGymDefeated(13), pokemon::ServiceStatus::Invalid);

  for (uint8_t gym = 1; gym <= 8; ++gym) {
    ASSERT_EQ(service.markGymDefeated(gym), pokemon::ServiceStatus::Ok);
  }
  EXPECT_EQ(service.markGymDefeated(1), pokemon::ServiceStatus::Ok);  // re-marking an already-defeated gym is a no-op

  pokemon::PokemonState state{};
  ASSERT_TRUE(store.loadState(state));
  EXPECT_EQ(state.battleProgress, 0x00FFU);

  ASSERT_EQ(service.markGymDefeated(9), pokemon::ServiceStatus::Ok);  // all 8 gyms down, Elite Four #1 unlocks
  ASSERT_TRUE(store.loadState(state));
  EXPECT_EQ(state.battleProgress, 0x01FFU);
}

TEST(PokemonService, ReadingCreditHealsAnExistingBattleEntryAndClearsStatusOnceFull) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  seedStarter(store);
  pokemon::PokemonService service(store, battleStore, {nullptr, zeroRandom});

  pokemon::BattleRecordEntry entry{};
  ASSERT_EQ(service.loadBattleEntry(1, entry), pokemon::ServiceStatus::Ok);
  ASSERT_EQ(entry.currentHp, 18U);
  entry.currentHp = 10;
  entry.status = pokemon::Ailment::Poison;
  ASSERT_EQ(service.saveBattleEntry(entry), pokemon::ServiceStatus::Ok);

  ASSERT_TRUE(service.creditMinutes(10, 10));  // +1 HP/minute heals 10->20, capped at maxHp 18

  const pokemon::BattleRecordEntry* healed = battleStore.findEntry(1);
  ASSERT_NE(healed, nullptr);
  EXPECT_EQ(healed->currentHp, 18U);
  EXPECT_EQ(healed->status, pokemon::Ailment::None);
}

TEST(PokemonService, ReadingCreditLeavesAPartyMemberWithNoBattleEntryAlone) {
  // A Pokemon that has never fought has no battle-store entry yet; crediting
  // reading minutes must not create or touch one on its behalf - it will be
  // synthesized fresh (full HP/PP) whenever it is first needed instead.
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  seedStarter(store);
  pokemon::PokemonService service(store, battleStore, {nullptr, zeroRandom});

  ASSERT_TRUE(service.creditMinutes(5, 10));
  EXPECT_EQ(battleStore.findEntry(1), nullptr);
}

TEST(PokemonService, ResolveBattleTurnDelegatesToTheEngineWithItsOwnRandomSource) {
  // A thin-wrapper test: the engine itself (stepBattle) is covered
  // extensively in PokemonBattleTest; this only confirms the service plumbs
  // its own RandomSource through instead of, say, a null one.
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  seedStarter(store);
  pokemon::PokemonService service(store, battleStore, {nullptr, zeroRandom});

  pokemon::BattleCombatant player{};
  player.speciesId = 25;  // Pikachu
  player.level = 20;
  player.currentHp = player.maxHp = 100;
  player.moves[0] = pokemon::BattleMoveSlot{33, 35};  // Tackle

  pokemon::BattleCombatant opponent{};
  opponent.speciesId = 4;  // Charmander
  opponent.level = 5;
  opponent.currentHp = opponent.maxHp = 100;
  opponent.moves[0] = pokemon::BattleMoveSlot{33, 35};

  const pokemon::BattleTurnResult result = service.resolveBattleTurn(player, opponent, 0);
  EXPECT_NE(result.outcome, pokemon::BattleOutcome::OpponentWon);
  // Pikachu (level 20) is faster and far stronger than a level-5 Charmander,
  // so with a deterministic zero-roll RNG the turn must land and do damage.
  EXPECT_LT(opponent.currentHp, opponent.maxHp);
}

TEST(PokemonService, ResolveOpponentOnlyTurnDelegatesToTheEngineWithItsOwnRandomSource) {
  // Same thin-wrapper shape as ResolveBattleTurn above (engine coverage
  // lives in PokemonBattleTest) - this just confirms the service plumbs its
  // own RandomSource through to stepOpponentOnlyTurn(), not a null one.
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  seedStarter(store);
  pokemon::PokemonService service(store, battleStore, {nullptr, zeroRandom});

  pokemon::BattleCombatant player{};
  player.speciesId = 25;  // Pikachu
  player.level = 20;
  player.currentHp = player.maxHp = 100;
  player.moves[0] = pokemon::BattleMoveSlot{33, 35};  // Tackle

  pokemon::BattleCombatant opponent{};
  opponent.speciesId = 4;  // Charmander
  opponent.level = 5;
  opponent.currentHp = opponent.maxHp = 100;
  opponent.moves[0] = pokemon::BattleMoveSlot{33, 35};

  const pokemon::BattleTurnResult result = service.resolveOpponentOnlyTurn(player, opponent);
  EXPECT_FALSE(result.player.acted);   // the player already spent this turn switching/using an item
  EXPECT_TRUE(result.opponent.acted);  // only the opponent's own action runs
  EXPECT_LT(player.currentHp, player.maxHp);
  EXPECT_EQ(opponent.currentHp, opponent.maxHp);  // the opponent's own side is never attacked here
}

TEST(PokemonService, AttemptBattleCatchDelegatesToTheEngineWithItsOwnRandomSource) {
  // Bulbasaur (SpeciesData::captureRate == 45) at full HP with a Poke Ball
  // has catchValue == 15/255 (see PokemonBattleTest for the exact math);
  // zeroRandom rolls 0, which is < 15, so this must succeed.
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  seedStarter(store);
  pokemon::PokemonService service(store, battleStore, {nullptr, zeroRandom});

  pokemon::BattleCombatant wild{};
  wild.speciesId = 1;
  wild.level = 20;
  wild.currentHp = wild.maxHp = pokemon::battleMaxHp(45, 20);

  EXPECT_TRUE(service.attemptBattleCatch(wild, pokemon::BallKind::Poke));
}

// Fills Pikachu's (recordId 1) battle entry with all 4 level-1..16 moves and
// parks its XP one reading-minute short of level 26 (bypassing creditMinutes
// for the climb itself, so none of this setup can trigger a MoveLearn queue
// or an unrelated encounter/item event) - shared by every test below that
// then credits exactly 1 minute to cross into level 26, where Pikachu's
// learnset has its next move and its moveset is already full. Crediting
// only 1 minute (instead of the whole level 16->26 gap) also keeps
// state.readingMinuteRemainder far under the 15/60-minute encounter/item
// check windows, so the MoveLearn event is guaranteed to be the only thing
// landing in the (capacity-3) pending-event queue.
void seedPikachuWithAFullMovesetOneMinuteBeforeLevel26(pokemon::PokemonStore& store,
                                                       pokemon::PokemonBattleStore& battleStore) {
  pokemon::BattleRecordEntry entry{};
  entry.recordId = 1;
  entry.moves = {84, 45, 86, 98};
  entry.pp = {30, 40, 20, 20};
  ASSERT_TRUE(battleStore.upsertEntry(entry));

  pokemon::PokemonRecord leader{};
  ASSERT_TRUE(store.readRecord(1, leader));
  leader.totalXp = pokemon::xpRequired(26) - 1U;
  pokemon::PokemonState state{};
  ASSERT_TRUE(store.loadState(state));
  const pokemon::RecordMutation mutation{1, leader, pokemon::RecordMutationKind::Replace};
  ASSERT_TRUE(store.commit(state, mutation));
}

TEST(PokemonService, CreditingMinutesAutoLearnsIntoAFreeSlotWithoutQueuingAnEvent) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  seedStarter(store);  // Pikachu (species 25), level 5
  pokemon::PokemonService service(store, battleStore, {nullptr, zeroRandom});

  pokemon::BattleRecordEntry entry{};
  ASSERT_EQ(service.loadBattleEntry(1, entry), pokemon::ServiceStatus::Ok);
  ASSERT_EQ(entry.moves[2], 0U);  // only 2 moves known yet - room to grow

  pokemon::PokemonRecord leader{};
  ASSERT_TRUE(store.readRecord(1, leader));
  leader.totalXp = pokemon::xpRequired(9) - 1U;  // one reading-minute short of level 9
  pokemon::PokemonState state{};
  ASSERT_TRUE(store.loadState(state));
  ASSERT_TRUE(store.commit(state, pokemon::RecordMutation{1, leader, pokemon::RecordMutationKind::Replace}));

  ASSERT_TRUE(service.creditMinutes(1, 10));

  ASSERT_TRUE(store.loadState(state));
  EXPECT_EQ(pokemon::pendingEventFront(state), nullptr);  // there was room, so no MoveLearn prompt

  const pokemon::BattleRecordEntry* updated = battleStore.findEntry(1);
  ASSERT_NE(updated, nullptr);
  EXPECT_EQ(updated->moves[2], 86U);  // move Pikachu learns at level 9, auto-filled into the free slot
  EXPECT_GT(updated->pp[2], 0U);
}

TEST(PokemonService, CreditingMinutesQueuesAMoveLearnEventWhenTheMovesetIsFull) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  seedStarter(store);
  seedPikachuWithAFullMovesetOneMinuteBeforeLevel26(store, battleStore);
  pokemon::PokemonService service(store, battleStore, {nullptr, zeroRandom});

  ASSERT_TRUE(service.creditMinutes(1, 10));

  pokemon::PokemonState state{};
  ASSERT_TRUE(store.loadState(state));
  const pokemon::PendingEvent* pending = pokemon::pendingEventFront(state);
  ASSERT_NE(pending, nullptr);
  EXPECT_EQ(pending->kind, pokemon::PendingEventKind::MoveLearn);
  EXPECT_EQ(pending->recordId, 1U);
  EXPECT_EQ(pending->speciesId, 129U);  // move id Pikachu learns at level 26 (reused field - see PendingEventKind)
  EXPECT_EQ(pending->level, 26U);

  const pokemon::BattleRecordEntry* unchanged = battleStore.findEntry(1);
  ASSERT_NE(unchanged, nullptr);
  EXPECT_EQ(unchanged->moves[0], 84U);  // moveset untouched until the UI resolves the choice
}

TEST(PokemonService, ResolveMoveLearnReplacesTheChosenSlotAndDequeuesTheEvent) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  seedStarter(store);
  seedPikachuWithAFullMovesetOneMinuteBeforeLevel26(store, battleStore);
  pokemon::PokemonService service(store, battleStore, {nullptr, zeroRandom});
  ASSERT_TRUE(service.creditMinutes(1, 10));

  ASSERT_EQ(service.resolveMoveLearn(1), pokemon::ServiceStatus::Ok);  // replace slot 1

  pokemon::PokemonState state{};
  ASSERT_TRUE(store.loadState(state));
  EXPECT_EQ(pokemon::pendingEventFront(state), nullptr);

  const pokemon::BattleRecordEntry* updated = battleStore.findEntry(1);
  ASSERT_NE(updated, nullptr);
  EXPECT_EQ(updated->moves[0], 84U);   // other slots untouched
  EXPECT_EQ(updated->moves[1], 129U);  // the newly-learned move
  EXPECT_EQ(updated->moves[2], 86U);
}

TEST(PokemonService, ResolveMoveLearnWithANegativeSlotSkipsLearningButStillDequeues) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  seedStarter(store);
  seedPikachuWithAFullMovesetOneMinuteBeforeLevel26(store, battleStore);
  pokemon::PokemonService service(store, battleStore, {nullptr, zeroRandom});
  ASSERT_TRUE(service.creditMinutes(1, 10));

  ASSERT_EQ(service.resolveMoveLearn(-1), pokemon::ServiceStatus::Ok);

  pokemon::PokemonState state{};
  ASSERT_TRUE(store.loadState(state));
  EXPECT_EQ(pokemon::pendingEventFront(state), nullptr);

  const pokemon::BattleRecordEntry* unchanged = battleStore.findEntry(1);
  ASSERT_NE(unchanged, nullptr);
  const std::array<uint8_t, pokemon::BATTLE_MOVE_SLOTS> expectedMoves{84, 45, 86, 98};
  EXPECT_EQ(unchanged->moves, expectedMoves);
}

TEST(PokemonService, ResolveMoveLearnIsNotApplicableWithoutAPendingMoveLearnEvent) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  seedStarter(store);
  pokemon::PokemonService service(store, battleStore, {nullptr, zeroRandom});

  EXPECT_EQ(service.resolveMoveLearn(0), pokemon::ServiceStatus::NotApplicable);
}

TEST(PokemonService, TeachMoveChecksCompatibilityThenFreeSlotThenAllowsAReplaceSlotRetry) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  seedStarter(store);  // synthesizes to moves [84, 45, 0, 0] at level 5
  pokemon::PokemonService service(store, battleStore, {nullptr, zeroRandom});

  EXPECT_EQ(service.teachMove(1, 84), pokemon::TeachMoveOutcome::AlreadyKnown);
  // Pound (1) is not in Pikachu's real TM/HM compatibility list (GĐ12).
  EXPECT_EQ(service.teachMove(1, 1), pokemon::TeachMoveOutcome::Incompatible);

  // 5 (Mega Punch/TM01), 6 (Pay Day/TM06), 25 (Mega Kick/TM09) are all in
  // Pikachu's real compatibility list (scripts/data/pokemon-tmhm.csv).
  ASSERT_EQ(service.teachMove(1, 5), pokemon::TeachMoveOutcome::Learned);  // fills slot 2
  const pokemon::BattleRecordEntry* afterFirst = battleStore.findEntry(1);
  ASSERT_NE(afterFirst, nullptr);
  EXPECT_EQ(afterFirst->moves[2], 5U);
  EXPECT_GT(afterFirst->pp[2], 0U);

  ASSERT_EQ(service.teachMove(1, 6), pokemon::TeachMoveOutcome::Learned);       // fills the last slot
  EXPECT_EQ(service.teachMove(1, 25), pokemon::TeachMoveOutcome::MovesetFull);  // no room left, no slot picked
  ASSERT_EQ(service.teachMove(1, 25, 1), pokemon::TeachMoveOutcome::Learned);   // retry, replacing slot 1
  const pokemon::BattleRecordEntry* afterReplace = battleStore.findEntry(1);
  ASSERT_NE(afterReplace, nullptr);
  EXPECT_EQ(afterReplace->moves[1], 25U);
}

TEST(PokemonService, PeekBattleMovesNeverPersistsWhenNoEntryExistsYet) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  seedStarter(store);
  pokemon::PokemonService service(store, battleStore, {nullptr, zeroRandom});

  pokemon::PokemonRecord record{};
  ASSERT_TRUE(store.readRecord(1, record));
  const pokemon::BattleRecordEntry peeked = service.peekBattleMoves(record);
  EXPECT_EQ(peeked.moves[0], 84U);
  EXPECT_EQ(peeked.moves[1], 45U);
  EXPECT_EQ(battleStore.findEntry(1), nullptr);  // read-only: peeking never writes to the battle store

  pokemon::BattleRecordEntry modified = peeked;
  modified.currentHp = 1;
  ASSERT_EQ(service.saveBattleEntry(modified), pokemon::ServiceStatus::Ok);

  const pokemon::BattleRecordEntry fromPeek = service.peekBattleMoves(record);
  EXPECT_EQ(fromPeek.currentHp, 1U);  // once a real entry exists, peek returns it verbatim
}

TEST(PokemonService, UseConsumableHealsWithMedicineAndRejectsAtFullHp) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  seedStarter(store);  // Pikachu, currentHp starts at maxHp (18) once synthesized
  pokemon::PokemonService service(store, battleStore, {nullptr, zeroRandom});

  pokemon::BattleRecordEntry entry{};
  ASSERT_EQ(service.loadBattleEntry(1, entry), pokemon::ServiceStatus::Ok);
  entry.currentHp = 5;
  ASSERT_EQ(service.saveBattleEntry(entry), pokemon::ServiceStatus::Ok);

  EXPECT_EQ(service.useConsumable(1, 11), pokemon::UseConsumableOutcome::Applied);  // Potion, +20 HP
  const pokemon::BattleRecordEntry* healed = battleStore.findEntry(1);
  ASSERT_NE(healed, nullptr);
  EXPECT_EQ(healed->currentHp, 18U);  // capped at maxHp, not 25

  EXPECT_EQ(service.useConsumable(1, 11), pokemon::UseConsumableOutcome::NotApplicable);  // already full HP
}

TEST(PokemonService, UseConsumableCuresOnlyTheMatchingStatusWithStatusCureItems) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  seedStarter(store);
  pokemon::PokemonService service(store, battleStore, {nullptr, zeroRandom});

  pokemon::BattleRecordEntry entry{};
  ASSERT_EQ(service.loadBattleEntry(1, entry), pokemon::ServiceStatus::Ok);
  entry.status = pokemon::Ailment::Paralysis;
  ASSERT_EQ(service.saveBattleEntry(entry), pokemon::ServiceStatus::Ok);

  EXPECT_EQ(service.useConsumable(1, 18), pokemon::UseConsumableOutcome::NotApplicable);  // Antidote cures Poison only
  EXPECT_EQ(service.useConsumable(1, 22), pokemon::UseConsumableOutcome::Applied);        // Paralyze Heal
  const pokemon::BattleRecordEntry* cured = battleStore.findEntry(1);
  ASSERT_NE(cured, nullptr);
  EXPECT_EQ(cured->status, pokemon::Ailment::None);
}

TEST(PokemonService, UseConsumableFullRestoreHealsAndCuresAnyStatus) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  seedStarter(store);
  pokemon::PokemonService service(store, battleStore, {nullptr, zeroRandom});

  pokemon::BattleRecordEntry entry{};
  ASSERT_EQ(service.loadBattleEntry(1, entry), pokemon::ServiceStatus::Ok);
  entry.currentHp = 3;
  entry.status = pokemon::Ailment::Burn;
  ASSERT_EQ(service.saveBattleEntry(entry), pokemon::ServiceStatus::Ok);

  EXPECT_EQ(service.useConsumable(1, 17), pokemon::UseConsumableOutcome::Applied);  // Full Restore
  const pokemon::BattleRecordEntry* restored = battleStore.findEntry(1);
  ASSERT_NE(restored, nullptr);
  EXPECT_EQ(restored->currentHp, 18U);
  EXPECT_EQ(restored->status, pokemon::Ailment::None);
}

TEST(PokemonService, UseConsumablePPRestoreTopsUpEveryKnownMoveSlot) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  seedStarter(store);  // synthesizes moves [84, 45, 0, 0] at full PP [30, 40]
  pokemon::PokemonService service(store, battleStore, {nullptr, zeroRandom});

  pokemon::BattleRecordEntry entry{};
  ASSERT_EQ(service.loadBattleEntry(1, entry), pokemon::ServiceStatus::Ok);
  entry.pp[0] = 5;
  entry.pp[1] = 40;  // already full - Ether shouldn't touch it
  ASSERT_EQ(service.saveBattleEntry(entry), pokemon::ServiceStatus::Ok);

  EXPECT_EQ(service.useConsumable(1, 25), pokemon::UseConsumableOutcome::Applied);  // Ether, +10 PP
  const pokemon::BattleRecordEntry* restored = battleStore.findEntry(1);
  ASSERT_NE(restored, nullptr);
  EXPECT_EQ(restored->pp[0], 15U);
  EXPECT_EQ(restored->pp[1], 40U);

  EXPECT_EQ(service.useConsumable(1, 26), pokemon::UseConsumableOutcome::Applied);        // Max Ether tops slot 0 off
  EXPECT_EQ(service.useConsumable(1, 26), pokemon::UseConsumableOutcome::NotApplicable);  // both slots now full
}

TEST(PokemonService, UseConsumableRareCandyAddsOneLevelAndRejectsAtLevel100) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  seedStarter(store);  // Pikachu, level 5
  pokemon::PokemonService service(store, battleStore, {nullptr, zeroRandom});

  EXPECT_EQ(service.useConsumable(1, 24), pokemon::UseConsumableOutcome::Applied);  // Rare Candy
  pokemon::PokemonRecord leader{};
  ASSERT_TRUE(store.readRecord(1, leader));
  EXPECT_EQ(pokemon::levelForXp(leader.totalXp), 6U);

  leader.totalXp = pokemon::xpRequired(100);
  pokemon::PokemonState state{};
  ASSERT_TRUE(store.loadState(state));
  ASSERT_TRUE(store.commit(state, pokemon::RecordMutation{1, leader, pokemon::RecordMutationKind::Replace}));
  EXPECT_EQ(service.useConsumable(1, 24), pokemon::UseConsumableOutcome::NotApplicable);  // already level 100
}

TEST(PokemonService, LearnMoveIntoSlotOverwritesUnconditionallyAtFullPp) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  seedStarter(store);  // synthesizes to moves [84, 45, 0, 0] at level 5
  pokemon::PokemonService service(store, battleStore, {nullptr, zeroRandom});

  ASSERT_EQ(service.learnMoveIntoSlot(1, 1, 98), pokemon::ServiceStatus::Ok);  // overwrite slot 1 with Quick Attack
  const pokemon::BattleRecordEntry* updated = battleStore.findEntry(1);
  ASSERT_NE(updated, nullptr);
  EXPECT_EQ(updated->moves[0], 84U);  // other slots untouched
  EXPECT_EQ(updated->moves[1], 98U);
  EXPECT_GT(updated->pp[1], 0U);

  EXPECT_EQ(service.learnMoveIntoSlot(1, pokemon::BATTLE_MOVE_SLOTS, 5), pokemon::ServiceStatus::Invalid);
  EXPECT_EQ(service.learnMoveIntoSlot(1, 2, 0), pokemon::ServiceStatus::Invalid);
}

TEST(PokemonService, ForgetMoveRepacksTheRemainingMovesInsteadOfLeavingAGap) {
  // Regression test: forgetting anything but the LAST known slot used to
  // leave moves[slot] == 0 with a non-zero move still sitting after it,
  // which validateBattleRecordEntry rejects (moves must stay packed at the
  // front, like PokemonState::partyRecordIds) - upsertEntry then silently
  // failed on every attempt except forgetting the last slot.
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  seedStarter(store);
  pokemon::PokemonService service(store, battleStore, {nullptr, zeroRandom});

  pokemon::BattleRecordEntry entry{};
  ASSERT_EQ(service.loadBattleEntry(1, entry), pokemon::ServiceStatus::Ok);
  entry.moves = {84, 45, 98, 5};
  entry.pp = {30, 40, 20, 20};
  ASSERT_EQ(service.saveBattleEntry(entry), pokemon::ServiceStatus::Ok);

  ASSERT_EQ(service.forgetMove(1, 1), pokemon::ServiceStatus::Ok);  // forget the 2nd of 4 - a middle slot
  const pokemon::BattleRecordEntry* afterForget = battleStore.findEntry(1);
  ASSERT_NE(afterForget, nullptr);
  const std::array<uint8_t, pokemon::BATTLE_MOVE_SLOTS> expectedMoves{84, 98, 5, 0};
  EXPECT_EQ(afterForget->moves, expectedMoves);
  EXPECT_EQ(afterForget->pp[3], 0U);

  EXPECT_EQ(service.forgetMove(1, 3), pokemon::ServiceStatus::NotApplicable);  // slot 3 is already empty now
}

TEST(PokemonService, ForgetMoveRefusesToClearAPokemonsLastRemainingMove) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  seedStarter(store);
  pokemon::PokemonService service(store, battleStore, {nullptr, zeroRandom});

  pokemon::BattleRecordEntry entry{};
  ASSERT_EQ(service.loadBattleEntry(1, entry), pokemon::ServiceStatus::Ok);
  entry.moves = {84, 0, 0, 0};
  entry.pp = {30, 0, 0, 0};
  ASSERT_EQ(service.saveBattleEntry(entry), pokemon::ServiceStatus::Ok);

  EXPECT_EQ(service.forgetMove(1, 0), pokemon::ServiceStatus::NotApplicable);
  const pokemon::BattleRecordEntry* unchanged = battleStore.findEntry(1);
  ASSERT_NE(unchanged, nullptr);
  EXPECT_EQ(unchanged->moves[0], 84U);
}

TEST(PokemonService, HourlyItemDropPrefersAnOwnedPokemonsEvolutionNeed) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  seedStarter(store);
  pokemon::PokemonService service(store, battleStore, {nullptr, itemEventRandom});
  ASSERT_TRUE(service.beginReadingSession());

  ASSERT_TRUE(service.creditMinutes(60, 64));

  pokemon::PokemonState state{};
  ASSERT_TRUE(store.loadState(state));
  EXPECT_EQ(state.pendingEvents[0].kind, pokemon::PendingEventKind::Item);
  EXPECT_EQ(state.pendingEvents[0].item, pokemon::EvolutionItem::ThunderStone);
  EXPECT_EQ(state.itemCounts[2], 1U);
}

}  // namespace
