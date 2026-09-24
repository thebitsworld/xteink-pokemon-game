#include <HalStorage.h>
#include <gtest/gtest.h>

#include <array>
#include <map>
#include <set>
#include <vector>

#include "pokemon/PokemonMovesetStore.h"
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

// Property test over the whole save layer: random sequences of catches, releases,
// Party/Box swaps, moveset edits, PP Ups, XP awards, reading credit, evolutions
// and restarts, with random SD read/write failures thrown in. After every step
// the save must re-open from disk, every store must agree with the main save, and
// nothing the player set up (learned moves, PP Ups) may quietly disappear.
namespace svcfuzz {

uint64_t rngState = 0x9E3779B97F4A7C15ULL;
uint32_t rnd() {
  rngState ^= rngState << 13;
  rngState ^= rngState >> 7;
  rngState ^= rngState << 17;
  return static_cast<uint32_t>(rngState >> 11);
}
uint32_t below(void*, const uint32_t n) { return n == 0 ? 0 : rnd() % n; }

struct World {
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battle;
  pokemon::PokemonIvEvStore ivev;
  pokemon::PokemonHallOfFameStore hof;
  pokemon::PokemonService service;
  World() : service(store, battle, ivev, hof, {nullptr, below}) {}
};

std::map<uint32_t, std::array<uint8_t, 4>> expectedMoves;
std::map<uint32_t, int> expectedPpUpSum;
long opNo = 0;
const char* lastOp = "";
int reported = 0;

#define SVC_INV(cond, msg)                                                                                     \
  do {                                                                                                         \
    if (!(cond)) {                                                                                             \
      if (reported++ < 10) ADD_FAILURE() << "op " << opNo << " (" << lastOp << "): " << (msg);                 \
    }                                                                                                          \
  } while (false)

std::vector<pokemon::PokemonRecord> allRecords() {
  std::vector<pokemon::PokemonRecord> out;
  pokemon::PokemonStore fresh;
  if (fresh.begin() != pokemon::StoreBeginResult::Ready) return out;
  for (uint32_t id = 1; id < fresh.nextRecordId(); ++id) {
    pokemon::PokemonRecord record{};
    if (fresh.readRecord(id, record)) out.push_back(record);
  }
  return out;
}

void checkInvariants(World& w, const bool cleanOp) {
  Storage.setFailRead(false);
  Storage.setFailWritableOpen(false);
  pokemon::PokemonStore fresh;
  SVC_INV(fresh.begin() == pokemon::StoreBeginResult::Ready, "main save cannot be re-opened");
  pokemon::PokemonState state{};
  if (!fresh.loadState(state)) {
    SVC_INV(false, "state cannot be loaded");
    return;
  }
  SVC_INV(pokemon::validateState(state), "state is invalid");
  SVC_INV(state.partyRecordIds[0] != 0, "party is empty");
  const auto records = allRecords();
  std::set<uint32_t> ids;
  for (const auto& r : records) ids.insert(r.recordId);
  SVC_INV(records.size() == fresh.recordCount(), "record count does not match the records readable");
  for (const uint32_t partyId : state.partyRecordIds) {
    if (partyId != 0) SVC_INV(ids.count(partyId) == 1, "party member without a record");
  }
  for (const auto& e : state.pendingEvents) {
    if (e.kind == pokemon::PendingEventKind::Evolution || e.kind == pokemon::PendingEventKind::MoveLearn) {
      SVC_INV(ids.count(e.recordId) == 1, "pending event for a missing record");
    }
  }
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonMovesetStore movesetStore;
  pokemon::PokemonIvEvStore ivevStore;
  SVC_INV(battleStore.entries().size() <= pokemon::POKEMON_BATTLE_MAX_ENTRIES, "battle store over capacity");
  for (const auto& e : battleStore.entries()) {
    if (cleanOp) SVC_INV(ids.count(e.recordId) == 1, "battle entry for a missing record");
    SVC_INV(pokemon::validateBattleRecordEntry(e), "invalid battle entry");
    SVC_INV(e.moves[0] != 0, "battle entry without a move");
    if (const pokemon::MovesetEntry* saved = movesetStore.findEntry(e.recordId); saved != nullptr) {
      SVC_INV(saved->moves == e.moves && saved->ppUp == e.ppUp, "moveset store disagrees with the battle entry");
    }
    for (const auto& r : records) {
      if (r.recordId != e.recordId) continue;
      const pokemon::BaseStats* stats = pokemon::baseStatsFor(r.speciesId);
      const pokemon::IvEvEntry* ivEv = ivevStore.findEntry(e.recordId);
      if (ivEv != nullptr && stats != nullptr) {
        const uint16_t maxHp =
            pokemon::battleMaxHp(stats->hp, pokemon::levelForXp(r.totalXp), ivEv->iv[0], ivEv->ev[0]);
        SVC_INV(e.currentHp <= maxHp, "battle HP above max HP");
      }
      for (size_t i = 0; i < pokemon::BATTLE_MOVE_SLOTS; ++i) {
        if (e.moves[i] == 0) continue;
        const pokemon::MoveData* move = pokemon::moveData(e.moves[i]);
        SVC_INV(move != nullptr && e.pp[i] <= pokemon::maxPpFor(move->pp, e.ppUp[i]), "PP above its maximum");
      }
    }
  }
  if (cleanOp) {
    for (uint32_t id = 1; id < 400; ++id) {
      if (movesetStore.findEntry(id) != nullptr) SVC_INV(ids.count(id) == 1, "moveset entry for a missing record");
      if (ivevStore.findEntry(id) != nullptr) SVC_INV(ids.count(id) == 1, "IV/EV entry for a missing record");
    }
  }
  for (const auto& r : records) {
    const pokemon::BattleRecordEntry derived = w.service.peekBattleMoves(r);
    SVC_INV(pokemon::validateBattleRecordEntry(derived), "derived entry is invalid");
    SVC_INV(derived.moves[0] != 0, "derived entry has no move");
  }
}

// Moves and PP Ups a Pokemon has been given must survive deposits, withdrawals,
// battle-store evictions and restarts. Only Pokemon with their own battle/moveset
// entry are tracked: one without derives a default moveset from its level.
void oracle(World& w, const bool assertKept) {
  const auto records = allRecords();
  std::set<uint32_t> live;
  for (const auto& r : records) live.insert(r.recordId);
  for (auto it = expectedMoves.begin(); it != expectedMoves.end();) {
    if (live.count(it->first) == 0) {
      expectedPpUpSum.erase(it->first);
      it = expectedMoves.erase(it);
    } else {
      ++it;
    }
  }
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonMovesetStore movesetStore;
  for (const auto& r : records) {
    if (battleStore.findEntry(r.recordId) == nullptr && movesetStore.findEntry(r.recordId) == nullptr) {
      expectedMoves.erase(r.recordId);
      expectedPpUpSum.erase(r.recordId);
      continue;
    }
    const pokemon::BattleRecordEntry entry = w.service.peekBattleMoves(r);
    if (const auto found = expectedMoves.find(r.recordId); assertKept && found != expectedMoves.end()) {
      for (const uint8_t move : found->second) {
        if (move == 0) continue;
        bool has = false;
        for (const uint8_t known : entry.moves) has = has || known == move;
        SVC_INV(has, "a learned move was lost");
      }
      int sum = 0;
      for (const uint8_t up : entry.ppUp) sum += up;
      SVC_INV(sum >= expectedPpUpSum[r.recordId], "PP Ups were lost");
    }
    expectedMoves[r.recordId] = entry.moves;
    int sum = 0;
    for (const uint8_t up : entry.ppUp) sum += up;
    expectedPpUpSum[r.recordId] = sum;
  }
}

pokemon::PokemonRecord pickRecord(const bool partyOnly, const bool boxOnly) {
  const auto records = allRecords();
  pokemon::PokemonStore fresh;
  pokemon::PokemonState state{};
  if (fresh.begin() != pokemon::StoreBeginResult::Ready || !fresh.loadState(state)) return {};
  std::vector<pokemon::PokemonRecord> candidates;
  for (const auto& r : records) {
    bool inParty = false;
    for (const uint32_t p : state.partyRecordIds) inParty = inParty || p == r.recordId;
    if ((partyOnly && !inParty) || (boxOnly && inParty)) continue;
    candidates.push_back(r);
  }
  return candidates.empty() ? pokemon::PokemonRecord{} : candidates[rnd() % candidates.size()];
}

void queueEncounter(World& w) {
  pokemon::PokemonState state{};
  if (!w.store.loadState(state) || pokemon::pendingEventCount(state) > 0) return;
  const uint16_t species = static_cast<uint16_t>(1 + rnd() % 150);
  const pokemon::SpeciesData* data = pokemon::speciesData(species);
  const pokemon::Gender gender = data->genderRate == 255 ? pokemon::Gender::Genderless
                                 : data->genderRate == 0 ? pokemon::Gender::Male
                                 : data->genderRate == 8 ? pokemon::Gender::Female
                                 : (rnd() % 2 != 0 ? pokemon::Gender::Male : pokemon::Gender::Female);
  state.pendingEvents[0] = pokemon::PendingEvent{0, species, static_cast<uint8_t>(2 + rnd() % 30), gender,
                                                 pokemon::EvolutionItem::None, pokemon::PendingEventKind::Encounter};
  pokemon::markSpecies(state.seenSpecies, species);
  w.store.commit(state);
}

void runOneOp(World*& world, std::unique_ptr<World>& owner, const uint32_t which) {
  World& w = *world;
  pokemon::PokemonRecord r{};
  uint32_t id = 0;
  switch (which) {
    case 0:
    case 1:
    case 2:
      lastOp = "catch";
      queueEncounter(w);
      w.service.resolveEncounter(rnd() % 4 != 0 ? pokemon::EncounterChoice::Catch : pokemon::EncounterChoice::Pass, id);
      break;
    case 3:
      lastOp = "deposit";
      w.service.depositPokemon(pickRecord(true, false).recordId);
      break;
    case 4:
      lastOp = "withdraw";
      w.service.withdrawPokemon(pickRecord(false, true).recordId);
      break;
    case 5:
      lastOp = "release";
      if (rnd() % 4 == 0) w.service.releasePokemon(pickRecord(false, true).recordId);
      break;
    case 6:
      lastOp = "teach";
      w.service.teachMove(pickRecord(false, false).recordId, static_cast<uint8_t>(1 + rnd() % 165),
                          static_cast<int>(rnd() % 5) - 1);
      break;
    case 7:
      lastOp = "learn into slot";
      w.service.learnMoveIntoSlot(pickRecord(false, false).recordId, static_cast<uint8_t>(rnd() % 4),
                                  static_cast<uint8_t>(1 + rnd() % 165));
      break;
    case 8:
      lastOp = "forget";
      w.service.forgetMove(pickRecord(false, false).recordId, static_cast<uint8_t>(rnd() % 4));
      break;
    case 9:
      lastOp = "PP Up";
      w.service.applyPpUp(pickRecord(false, false).recordId, static_cast<uint8_t>(rnd() % 4));
      break;
    case 10:
    case 11:
      lastOp = "award battle XP";
      w.service.awardBattleXp(pickRecord(true, false).recordId, static_cast<uint8_t>(2 + rnd() % 60), rnd() % 2 != 0,
                              static_cast<uint16_t>(1 + rnd() % 150));
      break;
    case 12: {
      lastOp = "save battle entry";
      pokemon::BattleRecordEntry entry{};
      if (w.service.loadBattleEntry(pickRecord(true, false).recordId, entry) == pokemon::ServiceStatus::Ok) {
        entry.currentHp = static_cast<uint16_t>(rnd() % (entry.currentHp + 1U));
        for (size_t i = 0; i < 4; ++i) {
          if (entry.moves[i] != 0) entry.pp[i] = static_cast<uint8_t>(rnd() % (entry.pp[i] + 1U));
        }
        w.service.saveBattleEntry(entry);
      }
      break;
    }
    case 13:
    case 14:
      lastOp = "reading credit";
      w.service.creditMinutes(static_cast<uint16_t>(1 + rnd() % 200), static_cast<uint8_t>(rnd() % 101));
      break;
    case 15: {
      lastOp = "resolve pending event";
      pokemon::PokemonState state{};
      if (!w.store.loadState(state)) break;
      const pokemon::PendingEvent* pending = pokemon::pendingEventFront(state);
      if (pending == nullptr) break;
      switch (pending->kind) {
        case pokemon::PendingEventKind::Encounter:
          w.service.resolveEncounter(rnd() % 2 != 0 ? pokemon::EncounterChoice::Catch : pokemon::EncounterChoice::Pass, id);
          break;
        case pokemon::PendingEventKind::Item:
          w.service.acknowledgeItem();
          break;
        case pokemon::PendingEventKind::Evolution:
          w.service.resolveEvolution(rnd() % 2 != 0 ? pokemon::EvolutionChoice::Evolve : pokemon::EvolutionChoice::Cancel);
          break;
        case pokemon::PendingEventKind::MoveLearn:
          w.service.resolveMoveLearn(static_cast<int>(rnd() % 5) - 1);
          break;
        default:
          break;
      }
      break;
    }
    case 16: {
      lastOp = "Rare Candy";
      pokemon::PokemonState state{};
      if (w.store.loadState(state)) {
        state.bagCounts[24 - 7] = 3;
        w.store.commit(state);
      }
      w.service.useConsumableAndConsumeItem(pickRecord(false, false).recordId, 24);
      break;
    }
    case 17: {
      lastOp = "medicine";
      pokemon::PokemonState state{};
      if (w.store.loadState(state)) {
        for (int i = 0; i < 4; ++i) state.bagCounts[11 - 7 + i] = 2;
        w.store.commit(state);
      }
      w.service.useConsumableAndConsumeItem(pickRecord(false, false).recordId, static_cast<uint8_t>(11 + rnd() % 6));
      break;
    }
    case 18:
      lastOp = "evolve now";
      w.service.evolveNow(pickRecord(false, false).recordId);
      break;
    case 19: {
      lastOp = "vitamin";
      pokemon::PokemonState state{};
      if (w.store.loadState(state)) {
        state.vitaminCounts.fill(3);
        w.store.commit(state);
      }
      w.service.useVitamin(pickRecord(false, false).recordId,
                           static_cast<uint8_t>(pokemon::VITAMIN_ITEM_ID_FIRST + rnd() % 5));
      break;
    }
    case 20:
      lastOp = "restart";
      owner = std::make_unique<World>();
      world = owner.get();
      world->store.begin();
      break;
    case 21:
      lastOp = "reorder party";
      w.service.movePartyMember(static_cast<uint8_t>(rnd() % 6), static_cast<uint8_t>(rnd() % 6));
      break;
    default:
      break;
  }
}

}  // namespace svcfuzz

TEST(PokemonService, RandomOperationSequencesKeepEveryStoreConsistent) {
  using namespace svcfuzz;
  rngState = 0x9E3779B97F4A7C15ULL;
  reported = 0;
  for (int game = 0; game < 12; ++game) {
    Storage.clear();
    expectedMoves.clear();
    expectedPpUpSum.clear();
    std::unique_ptr<World> owner = std::make_unique<World>();
    World* world = owner.get();
    const std::array<uint16_t, 4> starters{1, 4, 7, 25};
    ASSERT_EQ(world->service.createStarter(starters[rnd() % 4], pokemon::Gender::Male, "S"), pokemon::ServiceStatus::Ok);
    for (int step = 0; step < 300; ++step) {
      ++opNo;
      const bool inject = rnd() % 12 == 0;
      if (inject) {
        if (rnd() % 2 != 0) {
          Storage.setFailRead(true);
        } else {
          Storage.setFailWritableOpen(true);
        }
      }
      const uint32_t which = rnd() % 22;
      runOneOp(world, owner, which);
      Storage.setFailRead(false);
      Storage.setFailWritableOpen(false);
      checkInvariants(*world, !inject);
      const bool editsMoves = which == 5 || which == 6 || which == 7 || which == 8 || which == 9 || which == 15;
      oracle(*world, !inject && !editsMoves);
      if (reported >= 10) return;
    }
  }
  Storage.clear();
}

TEST(PokemonService, VerifiedCheckpointDurablyCreditsStateAndLeader) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

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
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});
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
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});
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
  pokemon::PokemonIvEvStore ivEvStore;
  ASSERT_EQ(store.begin(), pokemon::StoreBeginResult::Empty);
  ASSERT_TRUE(store.commit({}));
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  EXPECT_FALSE(service.beginReadingSession());
}

TEST(PokemonService, CreatesOneDurableStarterWithChosenIdentity) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

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
  EXPECT_EQ(snapshot.state.bagCounts[0], 10U);  // starting gift: 10 Poke Balls (item id 7)
  EXPECT_EQ(snapshot.state.bagCounts[4], 1U);   // starting gift: 1 Potion (item id 11)
  EXPECT_EQ(snapshot.state.starterSpeciesId, 25U);  // recorded once, for the Champion's counter slot

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
  pokemon::PokemonIvEvStore ivEvStore;
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});
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
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

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
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

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
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  appendOwnedPokemon(store, caughtPokemon(2, 4), true);
  appendOwnedPokemon(store, caughtPokemon(3, 7), true);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

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
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});
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

TEST(PokemonService, DepositingAPokemonKeepsItsBattleStoreEntry) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  appendOwnedPokemon(store, caughtPokemon(2, 4), true);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  // Teach recordId 1 a move and spend a PP Up on it, matching a Pokemon
  // that's actually been played with, not just fought once.
  ASSERT_EQ(service.teachMove(1, 85, 0), pokemon::TeachMoveOutcome::Learned);  // Thunderbolt into slot 0
  pokemon::PokemonState state{};
  ASSERT_TRUE(store.loadState(state));
  state.ppUpCount = 1;
  ASSERT_TRUE(store.commit(state));
  ASSERT_EQ(service.applyPpUp(1, 0), pokemon::ServiceStatus::Ok);
  const pokemon::BattleRecordEntry* before = battleStore.findEntry(1);
  ASSERT_NE(before, nullptr);
  const pokemon::BattleRecordEntry beforeCopy = *before;

  // Deposit-then-withdraw must not reset the moveset, PP Up, or heal it for
  // free (round 10 audit bug 1) - the entry survives exactly as it was.
  ASSERT_EQ(service.depositPokemon(1), pokemon::ServiceStatus::Ok);
  EXPECT_EQ(*battleStore.findEntry(1), beforeCopy);
  ASSERT_EQ(service.withdrawPokemon(1), pokemon::ServiceStatus::Ok);
  EXPECT_EQ(*battleStore.findEntry(1), beforeCopy);
}

TEST(PokemonService, LoadBattleEntryEvictsANonPartyEntryWhenTheStoreIsFull) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  // recordId 1 is the party's only member. Fill every one of the store's
  // POKEMON_BATTLE_MAX_ENTRIES (6) slots with entries for OTHER (non-party)
  // record ids, simulating Pokemon that fought long ago, were deposited or
  // released, and now no longer need a live entry.
  for (uint32_t id = 100; id < 100 + pokemon::POKEMON_BATTLE_MAX_ENTRIES; ++id) {
    pokemon::BattleRecordEntry stale{};
    stale.recordId = id;
    ASSERT_TRUE(battleStore.upsertEntry(stale));
  }
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  // recordId 1 has no entry yet and the store is completely full of
  // non-party entries - loadBattleEntry() must still succeed for a real
  // party member by evicting one of them, not fail with StorageError.
  pokemon::BattleRecordEntry entry{};
  ASSERT_EQ(service.loadBattleEntry(1, entry), pokemon::ServiceStatus::Ok);
  EXPECT_EQ(entry.recordId, 1U);
  EXPECT_NE(battleStore.findEntry(1), nullptr);
}

TEST(PokemonService, ReleasePokemonRemovesTheRecordAndFreesItsSideStoreSlots) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  appendOwnedPokemon(store, caughtPokemon(2, 4), false);  // Box record, not party
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  // Give recordId 2 real battle-store and IV/EV entries, matching a Pokemon
  // that's actually fought and had its IV/EV rolled at least once.
  pokemon::BattleRecordEntry entry{};
  ASSERT_EQ(service.loadBattleEntry(2, entry), pokemon::ServiceStatus::Ok);
  ASSERT_NE(battleStore.findEntry(2), nullptr);
  service.ensureIvEv(2);
  ASSERT_NE(ivEvStore.findEntry(2), nullptr);

  ASSERT_EQ(service.releasePokemon(2), pokemon::ServiceStatus::Ok);
  pokemon::PokemonRecord loaded{};
  EXPECT_FALSE(store.readRecord(2, loaded));
  EXPECT_EQ(battleStore.findEntry(2), nullptr);
  EXPECT_EQ(ivEvStore.findEntry(2), nullptr);

  // Stays in the Pokedex forever, matching every mainline game.
  pokemon::PokemonSnapshot snapshot{};
  ASSERT_EQ(service.loadSnapshot(snapshot), pokemon::ServiceStatus::Ok);
  EXPECT_TRUE(pokemon::isSpeciesMarked(snapshot.state.seenSpecies, 4));
  EXPECT_TRUE(pokemon::isSpeciesMarked(snapshot.state.caughtSpecies, 4));
}

TEST(PokemonService, ReleasePokemonRejectsAPartyMember) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  EXPECT_EQ(service.releasePokemon(1), pokemon::ServiceStatus::NotApplicable);
  pokemon::PokemonRecord stillThere{};
  EXPECT_TRUE(store.readRecord(1, stillThere));
}

TEST(PokemonService, ReleasePokemonRejectsAnUnknownRecordId) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  EXPECT_EQ(service.releasePokemon(999), pokemon::ServiceStatus::NotFound);
}

TEST(PokemonService, ReleasingAMiddleRecordDoesNotBreakSubsequentCatches) {
  // Regression test for a critical bug: releasing anything but the most-
  // recently-caught record used to make every later catch fail forever,
  // because the next record id was computed as recordCount() + 1 - only
  // safe while ids stayed dense, which Release broke (it removes a record
  // from the middle of the file without renumbering anything, so
  // recordCount() drops while the surviving ids do not shift down).
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);                                     // record 1
  appendOwnedPokemon(store, caughtPokemon(2, 4), false);   // Box record
  appendOwnedPokemon(store, caughtPokemon(3, 7), false);   // Box record
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  ASSERT_EQ(service.releasePokemon(2), pokemon::ServiceStatus::Ok);
  ASSERT_EQ(store.recordCount(), 2U);  // records {1, 3} remain

  pokemon::PokemonState state{};
  ASSERT_TRUE(store.loadState(state));
  state.pendingEvents[0].kind = pokemon::PendingEventKind::Encounter;
  state.pendingEvents[0].speciesId = 133;
  state.pendingEvents[0].level = 12;
  state.pendingEvents[0].gender = pokemon::Gender::Female;
  ASSERT_TRUE(store.commit(state));

  uint32_t caughtRecordId = 0;
  ASSERT_EQ(service.resolveEncounter(pokemon::EncounterChoice::Catch, caughtRecordId), pokemon::ServiceStatus::Ok);
  EXPECT_GT(caughtRecordId, 3U);  // must not collide with the surviving record 3
}

TEST(PokemonService, ReleasingTheHighestIdRecordAllowsTheNextCatchToReuseThatIdSafely) {
  // Releasing the record that happened to hold the highest id lowers the
  // ceiling (nextRecordId() tracks the highest currently-LIVE id, not a
  // monotonic "highest ever assigned" counter) - so the next catch CAN get
  // an id that was previously used and released. That is safe (not a
  // collision with anything - the old record and its battle-store/IV-EV
  // entries are all genuinely gone, see
  // ReleasePokemonRemovesTheRecordAndFreesItsSideStoreSlots), so this test
  // only asserts the one real invariant: the new id must not collide with
  // any record that is still actually alive.
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);                                     // record 1
  appendOwnedPokemon(store, caughtPokemon(2, 4), false);
  appendOwnedPokemon(store, caughtPokemon(3, 7), false);   // highest id

  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});
  ASSERT_EQ(service.releasePokemon(3), pokemon::ServiceStatus::Ok);
  ASSERT_EQ(store.recordCount(), 2U);  // records {1, 2} remain

  pokemon::PokemonState state{};
  ASSERT_TRUE(store.loadState(state));
  state.pendingEvents[0].kind = pokemon::PendingEventKind::Encounter;
  state.pendingEvents[0].speciesId = 133;
  state.pendingEvents[0].level = 12;
  state.pendingEvents[0].gender = pokemon::Gender::Female;
  ASSERT_TRUE(store.commit(state));

  uint32_t caughtRecordId = 0;
  ASSERT_EQ(service.resolveEncounter(pokemon::EncounterChoice::Catch, caughtRecordId), pokemon::ServiceStatus::Ok);
  EXPECT_NE(caughtRecordId, 1U);  // must not collide with the surviving starter
  EXPECT_NE(caughtRecordId, 2U);  // must not collide with the other surviving record
}

TEST(PokemonService, RejectsWithdrawalWhenThePartyIsFull) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  for (uint32_t id = 2; id <= 7; ++id) {
    appendOwnedPokemon(store, caughtPokemon(id, static_cast<uint16_t>(id + 3)), id <= 6);
  }
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  EXPECT_EQ(service.withdrawPokemon(7), pokemon::ServiceStatus::PartyFull);

  pokemon::PokemonSnapshot snapshot{};
  ASSERT_EQ(service.loadSnapshot(snapshot), pokemon::ServiceStatus::Ok);
  EXPECT_EQ(snapshot.partyCount, pokemon::PARTY_SIZE);
}

// Until an IV entry exists every screen derives a Pokemon's stats with IV 0, so
// the numbers used to jump the first time it fought. New Pokemon now get their
// roll the moment they are created.
TEST(PokemonService, NewStarterAndNewCatchHaveTheirIvsRolledImmediately) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});
  ASSERT_EQ(service.createStarter(25, pokemon::Gender::Male, "Pika"), pokemon::ServiceStatus::Ok);
  EXPECT_NE(ivEvStore.findEntry(1), nullptr);

  pokemon::PokemonState state{};
  ASSERT_TRUE(store.loadState(state));
  state.pendingEvents[0].kind = pokemon::PendingEventKind::Encounter;
  state.pendingEvents[0].speciesId = 133;
  state.pendingEvents[0].level = 12;
  state.pendingEvents[0].gender = pokemon::Gender::Female;
  ASSERT_TRUE(store.commit(state));

  uint32_t caughtRecordId = 0;
  ASSERT_EQ(service.resolveEncounter(pokemon::EncounterChoice::Catch, caughtRecordId), pokemon::ServiceStatus::Ok);
  ASSERT_NE(caughtRecordId, 0U);
  EXPECT_NE(ivEvStore.findEntry(caughtRecordId), nullptr);
}

TEST(PokemonService, ResolvesEncounterCatchThenAllowsNickname) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  pokemon::PokemonState state{};
  ASSERT_TRUE(store.loadState(state));
  state.pendingEvents[0].kind = pokemon::PendingEventKind::Encounter;
  state.pendingEvents[0].speciesId = 133;
  state.pendingEvents[0].level = 12;
  state.pendingEvents[0].gender = pokemon::Gender::Female;
  state.dashboardNotice = pokemon::DashboardNotice::NewPokemon;
  ASSERT_TRUE(store.commit(state));
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

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

TEST(PokemonService, ResolveEncounterBlockedOnlyWhenPartyAndBoxAreBothFull) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);  // recordId 1, party slot 0 (1/6)
  uint32_t nextId = 2;
  for (uint8_t i = 1; i < pokemon::PARTY_SIZE - 1U; ++i) {  // fill the party to 5/6
    appendOwnedPokemon(store, caughtPokemon(nextId++, 4), true);
  }
  for (uint32_t i = 0; i < pokemon::PC_BOX_MAX_RECORDS; ++i) {  // fill the Box to its own cap
    appendOwnedPokemon(store, caughtPokemon(nextId++, 4), false);
  }
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  // Party still has one empty slot (5/6) - a full Box must not block this
  // catch, since it goes into the party, not the Box.
  pokemon::PokemonState state{};
  ASSERT_TRUE(store.loadState(state));
  state.pendingEvents[0].kind = pokemon::PendingEventKind::Encounter;
  state.pendingEvents[0].speciesId = 133;
  state.pendingEvents[0].level = 12;
  state.pendingEvents[0].gender = pokemon::Gender::Female;
  ASSERT_TRUE(store.commit(state));
  uint32_t caughtRecordId = 0;
  ASSERT_EQ(service.resolveEncounter(pokemon::EncounterChoice::Catch, caughtRecordId), pokemon::ServiceStatus::Ok);
  EXPECT_NE(caughtRecordId, 0U);

  // Now the party is full too (6/6) - the next catch has nowhere to go.
  ASSERT_TRUE(store.loadState(state));
  state.pendingEvents[0].kind = pokemon::PendingEventKind::Encounter;
  state.pendingEvents[0].speciesId = 133;
  state.pendingEvents[0].level = 12;
  state.pendingEvents[0].gender = pokemon::Gender::Female;
  ASSERT_TRUE(store.commit(state));
  caughtRecordId = 0;
  EXPECT_EQ(service.resolveEncounter(pokemon::EncounterChoice::Catch, caughtRecordId),
            pokemon::ServiceStatus::BoxFull);
  EXPECT_EQ(caughtRecordId, 0U);
}

TEST(PokemonService, ResolvesEncounterPassWithoutCreatingARecord) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  pokemon::PokemonState state{};
  ASSERT_TRUE(store.loadState(state));
  state.pendingEvents[0].kind = pokemon::PendingEventKind::Encounter;
  state.pendingEvents[0].speciesId = 4;
  state.pendingEvents[0].level = 9;
  state.pendingEvents[0].gender = pokemon::Gender::Male;
  state.dashboardNotice = pokemon::DashboardNotice::NewPokemon;
  ASSERT_TRUE(store.commit(state));
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

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
  pokemon::PokemonIvEvStore ivEvStore;
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
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

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
  pokemon::PokemonIvEvStore ivEvStore;
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
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  ASSERT_EQ(service.resolveEvolution(pokemon::EvolutionChoice::Evolve), pokemon::ServiceStatus::Ok);
  pokemon::PokemonRecord evolved{};
  ASSERT_EQ(service.readRecord(1, evolved), pokemon::ServiceStatus::Ok);
  EXPECT_EQ(evolved.speciesId, 2U);
  ASSERT_EQ(service.setEvolutionPrompts(1, false), pokemon::ServiceStatus::Ok);
  ASSERT_EQ(service.readRecord(1, evolved), pokemon::ServiceStatus::Ok);
  EXPECT_NE(evolved.flags & pokemon::recordFlag(pokemon::RecordFlag::EvolutionPromptsDisabled), 0U);
}

TEST(PokemonService, EvolvingByLevelBackfillsTheNewSpeciesLevelAppropriateMoves) {
  // Metapod's entire real Gen 1 learnset is a single level-1 move, Harden
  // (id 106) - not in Caterpie's own set (Tackle/String Shot) - so
  // evolving at the real level-7 trigger should silently fill one of
  // Caterpie's 2 empty move slots with it, matching the real games (a
  // freshly-evolved Metapod already knows Harden, not "no moves at all").
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  ASSERT_EQ(store.begin(), pokemon::StoreBeginResult::Empty);
  pokemon::PokemonRecord caterpie = caughtPokemon(1, 10);
  caterpie.totalXp = pokemon::xpRequired(7);
  pokemon::PokemonState state{};
  state.partyRecordIds[0] = 1;
  state.pendingEvents[0].kind = pokemon::PendingEventKind::Evolution;
  state.pendingEvents[0].recordId = 1;
  state.pendingEvents[0].speciesId = 11;
  ASSERT_TRUE(pokemon::markSpecies(state.seenSpecies, 10));
  ASSERT_TRUE(pokemon::markSpecies(state.caughtSpecies, 10));
  ASSERT_TRUE(store.commit(state, {1, caterpie, pokemon::RecordMutationKind::Append}));
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  // defaultMovesetForLevel() walks the learnset backwards to fill slots
  // (most-recently-learned first), so among Caterpie's two same-level
  // moves, String Shot (81) lands in slot 0 and Tackle (33) in slot 1.
  pokemon::BattleRecordEntry beforeEntry{};
  ASSERT_EQ(service.loadBattleEntry(1, beforeEntry), pokemon::ServiceStatus::Ok);
  EXPECT_EQ(beforeEntry.moves[0], 81U);
  EXPECT_EQ(beforeEntry.moves[1], 33U);
  EXPECT_EQ(beforeEntry.moves[2], 0U);

  ASSERT_EQ(service.resolveEvolution(pokemon::EvolutionChoice::Evolve), pokemon::ServiceStatus::Ok);
  pokemon::PokemonRecord evolved{};
  ASSERT_EQ(service.readRecord(1, evolved), pokemon::ServiceStatus::Ok);
  EXPECT_EQ(evolved.speciesId, 11U);

  const pokemon::BattleRecordEntry* afterEntry = battleStore.findEntry(1);
  ASSERT_NE(afterEntry, nullptr);
  EXPECT_EQ(afterEntry->moves[2], 106U);  // Harden, backfilled into the first empty slot
}

TEST(PokemonService, EvolvingViaStoneBackfillsTheNewSpeciesLevelAppropriateMoves) {
  // Vaporeon's own level-1 moves (Sand Attack/Tackle/Water Gun/Quick Attack)
  // include 2 Eevee doesn't already know at level 5 (Water Gun/Quick
  // Attack) - both should backfill into Eevee's 2 empty move slots the
  // instant the Water Stone evolution happens, not stay unlearned.
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  appendOwnedPokemon(store, caughtPokemon(2, 133), true);  // Eevee, level 5: Sand Attack(28)/Tackle(33) only
  pokemon::PokemonState state{};
  ASSERT_TRUE(store.loadState(state));
  state.itemCounts[3] = 1;  // Water Stone (EvolutionItem::WaterStone == 4, itemCounts index 3)
  ASSERT_TRUE(store.commit(state));
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  // Same backwards-fill ordering as above: Tackle (33) lands in slot 0,
  // Sand Attack (28) in slot 1.
  pokemon::BattleRecordEntry beforeEntry{};
  ASSERT_EQ(service.loadBattleEntry(2, beforeEntry), pokemon::ServiceStatus::Ok);
  EXPECT_EQ(beforeEntry.moves[0], 33U);
  EXPECT_EQ(beforeEntry.moves[1], 28U);
  EXPECT_EQ(beforeEntry.moves[2], 0U);

  ASSERT_EQ(service.useEvolutionItem(2, pokemon::EvolutionItem::WaterStone), pokemon::ServiceStatus::Ok);
  pokemon::PokemonRecord evolved{};
  ASSERT_EQ(service.readRecord(2, evolved), pokemon::ServiceStatus::Ok);
  EXPECT_EQ(evolved.speciesId, 134U);  // Vaporeon

  const pokemon::BattleRecordEntry* afterEntry = battleStore.findEntry(2);
  ASSERT_NE(afterEntry, nullptr);
  EXPECT_EQ(afterEntry->moves[2], 55U);  // Water Gun
  EXPECT_EQ(afterEntry->moves[3], 98U);  // Quick Attack
}

TEST(PokemonService, MarkSpeciesSeenMarksOnceAndIsANoOpIfAlreadySeen) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  pokemon::PokemonSnapshot before{};
  ASSERT_EQ(service.loadSnapshot(before), pokemon::ServiceStatus::Ok);
  EXPECT_FALSE(pokemon::isSpeciesMarked(before.state.seenSpecies, 4));

  ASSERT_EQ(service.markSpeciesSeen(4), pokemon::ServiceStatus::Ok);
  pokemon::PokemonSnapshot after{};
  ASSERT_EQ(service.loadSnapshot(after), pokemon::ServiceStatus::Ok);
  EXPECT_TRUE(pokemon::isSpeciesMarked(after.state.seenSpecies, 4));

  // Already seen - a no-op (still Ok), not a redundant write.
  EXPECT_EQ(service.markSpeciesSeen(4), pokemon::ServiceStatus::Ok);
}

TEST(PokemonService, ConsumesStoneOnlyForApplicableEvolution) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  pokemon::PokemonState state{};
  ASSERT_TRUE(store.loadState(state));
  state.itemCounts[2] = 1;
  ASSERT_TRUE(store.commit(state));
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

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
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  appendOwnedPokemon(store, caughtPokemon(2, 7), false);
  appendOwnedPokemon(store, caughtPokemon(3, 4), false);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

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
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});
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
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);  // Pikachu (species 25), level 5
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

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
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

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

TEST(PokemonService, ResetAlsoClearsTheBattleStoreSoTheNextStarterDoesNotInheritLeftoverBattleData) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);  // Pikachu (species 25), level 5, recordId 1
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  pokemon::BattleRecordEntry entry{};
  ASSERT_EQ(service.loadBattleEntry(1, entry), pokemon::ServiceStatus::Ok);
  entry.currentHp = 1;
  entry.status = pokemon::Ailment::Poison;
  ASSERT_EQ(service.saveBattleEntry(entry), pokemon::ServiceStatus::Ok);
  ASSERT_NE(battleStore.findEntry(1), nullptr);

  ASSERT_EQ(service.reset(), pokemon::ServiceStatus::Ok);
  EXPECT_EQ(battleStore.findEntry(1), nullptr);

  // A fresh game reassigns record ids from 1 again - a different species
  // this time, so leftover Pikachu battle data landing on it would be
  // obviously wrong rather than coincidentally looking right.
  ASSERT_EQ(service.createStarter(1, pokemon::Gender::Male, ""), pokemon::ServiceStatus::Ok);
  pokemon::BattleRecordEntry freshEntry{};
  ASSERT_EQ(service.loadBattleEntry(1, freshEntry), pokemon::ServiceStatus::Ok);
  EXPECT_EQ(freshEntry.status, pokemon::Ailment::None);
  EXPECT_EQ(freshEntry.currentHp, pokemon::battleMaxHp(pokemon::baseStatsFor(1)->hp, 5));  // full HP, not the old 1
}

TEST(PokemonService, ConsumeBagItemDecrementsStonesAndNonStoneItemsInTheirOwnArrays) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

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
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  EXPECT_EQ(service.markGymDefeated(2), pokemon::ServiceStatus::NotApplicable);  // gym 1 not yet defeated
  EXPECT_EQ(service.markGymDefeated(9), pokemon::ServiceStatus::NotApplicable);  // no gyms defeated yet
  EXPECT_EQ(service.markGymDefeated(0), pokemon::ServiceStatus::Invalid);
  // GYM_COUNT is 13 since the Champion was added (8 gyms + 4 Elite Four + the
  // Champion) - index 13 is a real, valid gym index now, so the first
  // genuinely out-of-range one is 14.
  EXPECT_EQ(service.markGymDefeated(14), pokemon::ServiceStatus::Invalid);

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
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

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

TEST(PokemonService, ReadingCreditHealsEveryDamagedPartyMemberInOneBatchedWrite) {
  // Regression test for healPartyOnRead()'s batched write (PokemonBattleStore
  // ::upsertEntries()) - with 2+ damaged party members in the same credit
  // call, every one of them must come out healed, not just the first (a bug
  // that a naive "build one candidate state, upsert per member" refactor
  // could introduce if a later member's upsert started from a stale copy of
  // the state instead of one that already reflects an earlier member's
  // heal).
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  appendOwnedPokemon(store, caughtPokemon(2, 1), true);  // Bulbasaur, party slot 1
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  pokemon::BattleRecordEntry first{};
  ASSERT_EQ(service.loadBattleEntry(1, first), pokemon::ServiceStatus::Ok);
  first.currentHp = 1;
  ASSERT_EQ(service.saveBattleEntry(first), pokemon::ServiceStatus::Ok);

  pokemon::BattleRecordEntry second{};
  ASSERT_EQ(service.loadBattleEntry(2, second), pokemon::ServiceStatus::Ok);
  second.currentHp = 1;
  ASSERT_EQ(service.saveBattleEntry(second), pokemon::ServiceStatus::Ok);

  ASSERT_TRUE(service.creditMinutes(20, 10));  // +1 HP/minute for 20 minutes - plenty to heal both fully

  const pokemon::BattleRecordEntry* healedFirst = battleStore.findEntry(1);
  const pokemon::BattleRecordEntry* healedSecond = battleStore.findEntry(2);
  ASSERT_NE(healedFirst, nullptr);
  ASSERT_NE(healedSecond, nullptr);
  EXPECT_GT(healedFirst->currentHp, 1U);
  EXPECT_GT(healedSecond->currentHp, 1U);
}

TEST(PokemonService, ReadingCreditRestoresPpAcrossFiveMinuteCheckpoints) {
  // Regression: reading credits arrive as ~5-minute checkpoints, and PP used
  // to be restored with `minutes / 10` per call - always 0 for a 5-minute
  // credit - so PP never recovered from reading. The 10-minute cadence must
  // accumulate across calls.
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  pokemon::BattleRecordEntry entry{};
  ASSERT_EQ(service.loadBattleEntry(1, entry), pokemon::ServiceStatus::Ok);
  ASSERT_NE(entry.moves[0], 0U);
  const uint8_t fullPp = entry.pp[0];
  ASSERT_GE(fullPp, 3U);
  entry.pp[0] = static_cast<uint8_t>(fullPp - 3);
  ASSERT_EQ(service.saveBattleEntry(entry), pokemon::ServiceStatus::Ok);

  ASSERT_TRUE(service.creditMinutes(5, 10));
  EXPECT_EQ(battleStore.findEntry(1)->pp[0], fullPp - 3);  // 5 minutes: no tick yet
  ASSERT_TRUE(service.creditMinutes(5, 10));
  EXPECT_EQ(battleStore.findEntry(1)->pp[0], fullPp - 2);  // 10 minutes total: +1
  ASSERT_TRUE(service.creditMinutes(5, 10));
  ASSERT_TRUE(service.creditMinutes(5, 10));
  EXPECT_EQ(battleStore.findEntry(1)->pp[0], fullPp - 1);  // 20 minutes total: +2
}

TEST(PokemonService, ReadingCreditLeavesAPartyMemberWithNoBattleEntryAlone) {
  // A Pokemon that has never fought has no battle-store entry yet; crediting
  // reading minutes must not create or touch one on its behalf - it will be
  // synthesized fresh (full HP/PP) whenever it is first needed instead.
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

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
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

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
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

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
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

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
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);  // Pikachu (species 25), level 5
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

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
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  seedPikachuWithAFullMovesetOneMinuteBeforeLevel26(store, battleStore);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  ASSERT_TRUE(service.creditMinutes(1, 10));

  pokemon::PokemonState state{};
  ASSERT_TRUE(store.loadState(state));
  const pokemon::PendingEvent* pending = pokemon::pendingEventFront(state);
  ASSERT_NE(pending, nullptr);
  EXPECT_EQ(pending->kind, pokemon::PendingEventKind::MoveLearn);
  EXPECT_EQ(pending->recordId, 1U);
  EXPECT_EQ(pending->speciesId, 129U);  // move id Pikachu learns at level 26 (reused field - see PendingEventKind)
  EXPECT_EQ(pending->level, 26U);
  // The Home accessory reads only state.dashboardNotice, so queueing the prompt must set it.
  EXPECT_EQ(state.dashboardNotice, pokemon::DashboardNotice::WhatsThis);

  const pokemon::BattleRecordEntry* unchanged = battleStore.findEntry(1);
  ASSERT_NE(unchanged, nullptr);
  EXPECT_EQ(unchanged->moves[0], 84U);  // moveset untouched until the UI resolves the choice
}

TEST(PokemonService, ResolveMoveLearnReplacesTheChosenSlotAndDequeuesTheEvent) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  seedPikachuWithAFullMovesetOneMinuteBeforeLevel26(store, battleStore);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});
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
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  seedPikachuWithAFullMovesetOneMinuteBeforeLevel26(store, battleStore);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});
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
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  EXPECT_EQ(service.resolveMoveLearn(0), pokemon::ServiceStatus::NotApplicable);
}

TEST(PokemonService, TeachMoveChecksCompatibilityThenFreeSlotThenAllowsAReplaceSlotRetry) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);  // synthesizes to moves [84, 45, 0, 0] at level 5
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  EXPECT_EQ(service.teachMove(1, 84), pokemon::TeachMoveOutcome::AlreadyKnown);
  // Pound (1) is not in Pikachu's real TM/HM compatibility list (Stage 12).
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

// A customised moveset (TM/HM, learn/forget, PP Up) lives in its own
// per-record moveset store, so it survives the party-sized battle store
// dropping that Pokemon's entry - the entry only carries transient HP/PP/
// status on top of it.
TEST(PokemonService, CustomisedMovesetSurvivesLosingItsBattleEntry) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);  // Pikachu, level 5, moves [84, 45, 0, 0]
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  ASSERT_EQ(service.teachMove(1, 5), pokemon::TeachMoveOutcome::Learned);  // TM Mega Punch into slot 2
  ASSERT_TRUE(battleStore.removeEntry(1));                                   // e.g. evicted to make room

  pokemon::PokemonRecord record{};
  ASSERT_TRUE(store.readRecord(1, record));
  const pokemon::BattleRecordEntry peeked = service.peekBattleMoves(record);
  EXPECT_EQ(peeked.moves[2], 5U);  // not reverted to the level-derived default
  EXPECT_GT(peeked.pp[2], 0U);

  pokemon::BattleRecordEntry reloaded{};
  ASSERT_EQ(service.loadBattleEntry(1, reloaded), pokemon::ServiceStatus::Ok);
  EXPECT_EQ(reloaded.moves[2], 5U);
  EXPECT_NE(battleStore.findEntry(1), nullptr);  // and a fresh entry was recreated from it
}

// The exact reported problem: boxed Pokemon hold battle-store slots, so a
// party member needing one makes the store evict a boxed Pokemon's entry.
// That used to throw away its TM move and PP Ups; now only its HP/PP/status
// (which reset to full) go.
TEST(PokemonService, EvictingABoxedPokemonsBattleEntryKeepsItsCustomMoveset) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);  // record 1: the party's only member, no battle entry yet
  for (uint32_t id = 2; id <= 7; ++id) appendOwnedPokemon(store, caughtPokemon(id, 25), false);  // six boxed Pikachu
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  ASSERT_EQ(service.teachMove(2, 5), pokemon::TeachMoveOutcome::Learned);  // the lowest boxed record id: evicted first
  pokemon::BattleRecordEntry scratch{};
  for (uint32_t id = 3; id <= 7; ++id) ASSERT_EQ(service.loadBattleEntry(id, scratch), pokemon::ServiceStatus::Ok);
  ASSERT_TRUE(battleStore.isFull());

  ASSERT_EQ(service.loadBattleEntry(1, scratch), pokemon::ServiceStatus::Ok);  // the party member needs a slot
  EXPECT_EQ(battleStore.findEntry(2), nullptr);                                // record 2's entry was evicted
  EXPECT_NE(battleStore.findEntry(1), nullptr);

  pokemon::PokemonRecord boxed{};
  ASSERT_TRUE(store.readRecord(2, boxed));
  EXPECT_EQ(service.peekBattleMoves(boxed).moves[2], 5U);  // ...but its TM move is still there
}

// Battle-store entries written before the moveset store existed have no copy
// there; the ones about to be evicted must be backed up first or a legacy
// customised moveset would still be lost exactly as before.
TEST(PokemonService, EvictionBacksUpALegacyEntrysMovesetFirst) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  for (uint32_t id = 2; id <= 7; ++id) appendOwnedPokemon(store, caughtPokemon(id, 25), false);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  pokemon::BattleRecordEntry legacy{};
  legacy.recordId = 2;
  legacy.moves = {84, 45, 5, 0};
  legacy.pp = {30, 40, 20, 0};
  legacy.ppUp = {3, 0, 0, 0};
  legacy.currentHp = 18;
  ASSERT_TRUE(battleStore.upsertEntry(legacy));  // straight into the battle store, as an older build would have
  pokemon::BattleRecordEntry scratch{};
  for (uint32_t id = 3; id <= 7; ++id) ASSERT_EQ(service.loadBattleEntry(id, scratch), pokemon::ServiceStatus::Ok);
  ASSERT_TRUE(battleStore.isFull());

  ASSERT_EQ(service.loadBattleEntry(1, scratch), pokemon::ServiceStatus::Ok);
  ASSERT_EQ(battleStore.findEntry(2), nullptr);

  pokemon::PokemonRecord boxed{};
  ASSERT_TRUE(store.readRecord(2, boxed));
  const pokemon::BattleRecordEntry restored = service.peekBattleMoves(boxed);
  EXPECT_EQ(restored.moves[2], 5U);
  EXPECT_EQ(restored.ppUp[0], 3U);  // the spent PP Up too
}

// A released record's moveset must not be inherited by whichever Pokemon is
// later handed the same record id.
TEST(PokemonService, ReleasingAPokemonDropsItsSavedMoveset) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  appendOwnedPokemon(store, caughtPokemon(2, 25), false);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  ASSERT_EQ(service.teachMove(2, 5), pokemon::TeachMoveOutcome::Learned);
  ASSERT_EQ(service.releasePokemon(2), pokemon::ServiceStatus::Ok);
  appendOwnedPokemon(store, caughtPokemon(2, 25), false);  // the id is handed out again

  pokemon::PokemonRecord reused{};
  ASSERT_TRUE(store.readRecord(2, reused));
  const pokemon::BattleRecordEntry fresh = service.peekBattleMoves(reused);
  EXPECT_EQ(fresh.moves[2], 0U);  // default level-5 Pikachu: [84, 45, 0, 0]
}

TEST(PokemonService, ResetClearsSavedMovesets) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});
  ASSERT_EQ(service.createStarter(25, pokemon::Gender::Male, "Pika"), pokemon::ServiceStatus::Ok);
  ASSERT_EQ(service.teachMove(1, 5), pokemon::TeachMoveOutcome::Learned);

  ASSERT_EQ(service.reset(), pokemon::ServiceStatus::Ok);
  ASSERT_EQ(service.createStarter(25, pokemon::Gender::Male, "Pika"), pokemon::ServiceStatus::Ok);  // record id 1 again

  pokemon::PokemonRecord record{};
  ASSERT_TRUE(store.readRecord(1, record));
  EXPECT_EQ(service.peekBattleMoves(record).moves[2], 0U);
}

// Leveling up while only the moveset (not the battle entry) is saved still
// picks up the new learnset move instead of silently skipping it.
TEST(PokemonService, RareCandyLevelUpLearnsIntoASavedMovesetWithoutABattleEntry) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  uint8_t learnLevel = 0;
  uint8_t learnMove = 0;
  for (const pokemon::LearnsetEntry& learn : pokemon::learnsetFor(25)) {
    if (learn.level > 5 && learn.moveId != 84 && learn.moveId != 45) {
      learnLevel = learn.level;
      learnMove = learn.moveId;
      break;
    }
  }
  ASSERT_GT(learnLevel, 5U);
  pokemon::PokemonRecord leader{};
  ASSERT_TRUE(store.readRecord(1, leader));
  leader.totalXp = pokemon::xpRequired(static_cast<uint8_t>(learnLevel - 1));
  pokemon::PokemonState state{};
  ASSERT_TRUE(store.loadState(state));
  state.bagCounts[24 - 7] = 1;  // one Rare Candy
  ASSERT_TRUE(store.commit(state, pokemon::RecordMutation{1, leader, pokemon::RecordMutationKind::Replace}));

  ASSERT_EQ(service.teachMove(1, 5), pokemon::TeachMoveOutcome::Learned);  // customise -> moveset store entry
  ASSERT_TRUE(battleStore.removeEntry(1));                                   // battle entry gone, moveset saved

  ASSERT_EQ(service.useConsumableAndConsumeItem(1, 24), pokemon::UseConsumableOutcome::Applied);
  EXPECT_EQ(battleStore.findEntry(1), nullptr);  // no battle entry was invented for it

  ASSERT_TRUE(store.readRecord(1, leader));
  const pokemon::BattleRecordEntry moves = service.peekBattleMoves(leader);
  bool hasTm = false;
  bool hasLearned = false;
  for (const uint8_t move : moves.moves) {
    hasTm = hasTm || move == 5;
    hasLearned = hasLearned || move == learnMove;
  }
  EXPECT_TRUE(hasTm);
  EXPECT_TRUE(hasLearned);
}

// The Moveset action is offered for boxed Pokemon too, but the battle store
// only has 6 slots. With all 6 taken by party members there was nothing to
// evict, so editing a boxed Pokemon's moveset failed with a save error even
// though the moveset store could take the write directly.
TEST(PokemonService, BoxedPokemonMovesetCanBeEditedWhileTheBattleStoreIsFullOfPartyEntries) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  for (uint32_t id = 2; id <= 6; ++id) appendOwnedPokemon(store, caughtPokemon(id, 25), true);  // party of 6
  appendOwnedPokemon(store, caughtPokemon(7, 25), false);                                      // one boxed Pikachu
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  pokemon::BattleRecordEntry scratch{};
  for (uint32_t id = 1; id <= 6; ++id) ASSERT_EQ(service.loadBattleEntry(id, scratch), pokemon::ServiceStatus::Ok);
  ASSERT_TRUE(battleStore.isFull());

  EXPECT_EQ(service.teachMove(7, 5), pokemon::TeachMoveOutcome::Learned);  // used to fail: nothing to evict
  for (uint32_t id = 1; id <= 6; ++id) EXPECT_NE(battleStore.findEntry(id), nullptr);  // no party entry was touched

  pokemon::PokemonRecord boxed{};
  ASSERT_TRUE(store.readRecord(7, boxed));
  const pokemon::BattleRecordEntry moves = service.peekBattleMoves(boxed);
  EXPECT_EQ(moves.moves[2], 5U);  // durable in the moveset store even without a battle entry
  EXPECT_GT(moves.pp[2], 0U);

  // ...and forgetting works the same way.
  EXPECT_EQ(service.forgetMove(7, 0), pokemon::ServiceStatus::Ok);
  EXPECT_EQ(service.peekBattleMoves(boxed).moves[0], 45U);
}

// A MoveLearn prompt can sit in the queue while the player empties slots on
// the Moves screen; choosing an empty row past the first gap wrote a move into
// a hole in the packed moveset, which the codec rejects (a bogus save error).
TEST(PokemonService, ResolveMoveLearnIntoAnEmptyRowPastAGapLandsInTheFirstEmptySlot) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);  // synthesizes to moves [84, 45, 0, 0] at level 5
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});
  pokemon::BattleRecordEntry scratch{};
  ASSERT_EQ(service.loadBattleEntry(1, scratch), pokemon::ServiceStatus::Ok);

  pokemon::PokemonState state{};
  ASSERT_TRUE(store.loadState(state));
  state.pendingEvents[0] = {1, 129, 26, pokemon::Gender::Unknown, pokemon::EvolutionItem::None,
                            pokemon::PendingEventKind::MoveLearn};
  ASSERT_TRUE(store.commit(state));

  ASSERT_EQ(service.resolveMoveLearn(3), pokemon::ServiceStatus::Ok);  // row 4 is empty; slot 2 is the first empty one
  const pokemon::BattleRecordEntry* updated = battleStore.findEntry(1);
  ASSERT_NE(updated, nullptr);
  EXPECT_EQ(updated->moves[2], 129U);
  EXPECT_EQ(updated->moves[3], 0U);
}

TEST(PokemonService, DepositIsBlockedOnceTheBoxIsAtItsCap) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  appendOwnedPokemon(store, caughtPokemon(2, 25), true);  // a second party member to deposit
  for (uint32_t id = 3; id < 3 + pokemon::PC_BOX_MAX_RECORDS; ++id) appendOwnedPokemon(store, caughtPokemon(id, 25), false);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  EXPECT_EQ(service.depositPokemon(2), pokemon::ServiceStatus::BoxFull);  // would make a 513th boxed record
  pokemon::PokemonState state{};
  ASSERT_TRUE(store.loadState(state));
  EXPECT_EQ(state.partyRecordIds[1], 2U);  // still in the party

  ASSERT_EQ(service.depositPokemon(1), pokemon::ServiceStatus::BoxFull);  // any party member, not just this one
  // (releaseRecord needs the record out of the party; free a slot in the Box instead)
  ASSERT_EQ(service.releasePokemon(3), pokemon::ServiceStatus::Ok);
  EXPECT_EQ(service.depositPokemon(2), pokemon::ServiceStatus::Ok);
}

// Cancelling an evolution leaves the species alone, so it must not run the
// "learn everything the new species already knows" catch-up - that only made
// sense for a real evolution, and on Cancel it refilled slots the player had
// emptied on purpose.
TEST(PokemonService, CancellingAnEvolutionDoesNotRefillEmptiedMoveSlots) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  // Charmander -> Charmeleon is a level evolution (Pikachu's is a stone), at
  // level 16; 30 leaves plenty of learnset behind it.
  pokemon::PokemonRecord leader{};
  ASSERT_TRUE(store.readRecord(1, leader));
  leader.speciesId = 4;
  leader.totalXp = pokemon::xpRequired(30);
  pokemon::PokemonState state{};
  ASSERT_TRUE(store.loadState(state));
  ASSERT_TRUE(pokemon::markSpecies(state.seenSpecies, 4));
  ASSERT_TRUE(pokemon::markSpecies(state.caughtSpecies, 4));
  state.pendingEvents[0] = {1, 5, 0, pokemon::Gender::Unknown, pokemon::EvolutionItem::None,
                            pokemon::PendingEventKind::Evolution};
  ASSERT_TRUE(store.commit(state, pokemon::RecordMutation{1, leader, pokemon::RecordMutationKind::Replace}));

  pokemon::BattleRecordEntry entry{};
  entry.recordId = 1;
  entry.moves = {10, 45, 0, 0};  // the player deliberately kept just two moves
  entry.pp = {35, 40, 0, 0};
  entry.currentHp = 30;
  ASSERT_TRUE(battleStore.upsertEntry(entry));

  ASSERT_EQ(service.resolveEvolution(pokemon::EvolutionChoice::Cancel), pokemon::ServiceStatus::Ok);
  const pokemon::BattleRecordEntry* after = battleStore.findEntry(1);
  ASSERT_NE(after, nullptr);
  EXPECT_EQ(after->moves[2], 0U);
  EXPECT_EQ(after->moves[3], 0U);
}

// A new game always reuses record id 1. If clearing the side stores at reset
// time failed (or the main save was removed on its own), the new starter used
// to inherit the old id-1 moves.
TEST(PokemonService, NewStarterDoesNotInheritStaleSideStoreEntriesForRecordIdOne) {
  Storage.clear();
  {
    pokemon::PokemonStore store;
    pokemon::PokemonBattleStore battleStore;
    pokemon::PokemonIvEvStore ivEvStore;
    pokemon::PokemonHallOfFameStore hallOfFameStore;
    pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});
    ASSERT_EQ(service.createStarter(25, pokemon::Gender::Male, "Pika"), pokemon::ServiceStatus::Ok);
    ASSERT_EQ(service.teachMove(1, 5), pokemon::TeachMoveOutcome::Learned);  // battle + moveset entries for id 1
  }
  // Only the main save goes away; every side store is left exactly as it was.
  Storage.remove("/.crosspoint/pokemon-a.bin");
  Storage.remove("/.crosspoint/pokemon-b.bin");

  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});
  ASSERT_EQ(service.createStarter(25, pokemon::Gender::Male, "Pika2"), pokemon::ServiceStatus::Ok);

  pokemon::PokemonRecord record{};
  ASSERT_TRUE(store.readRecord(1, record));
  const pokemon::BattleRecordEntry fresh = service.peekBattleMoves(record);
  EXPECT_EQ(fresh.moves[2], 0U);  // default level-5 Pikachu, not the previous game's TM move
  EXPECT_EQ(battleStore.findEntry(1), nullptr);
}

// Round 8 audit item C: teachMove() then a separate consumeBagItem() call
// (the old PokemonActivity.cpp flow) could learn the move and then, if the
// second write failed, keep it without ever charging the TM/HM.
// teachMoveAndConsumeItem() fixes the order - verify it only ever spends the
// item once a move is actually about to be learned.
TEST(PokemonService, TeachMoveAndConsumeItemSpendsExactlyOneItemOnlyWhenLearned) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);  // synthesizes to moves [84, 45, 0, 0] at level 5
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  pokemon::PokemonState state{};
  ASSERT_TRUE(store.loadState(state));
  state.bagCounts[22] = 2;  // TM01 (item id 29), teaches Mega Punch (move id 5)
  ASSERT_TRUE(store.commit(state));

  // AlreadyKnown/Incompatible leave the bag untouched, exactly like teachMove().
  EXPECT_EQ(service.teachMoveAndConsumeItem(1, 84, 29), pokemon::TeachMoveOutcome::AlreadyKnown);
  EXPECT_EQ(service.teachMoveAndConsumeItem(1, 1, 29), pokemon::TeachMoveOutcome::Incompatible);
  ASSERT_TRUE(store.loadState(state));
  EXPECT_EQ(state.bagCounts[22], 2U);  // nothing spent yet

  ASSERT_EQ(service.teachMoveAndConsumeItem(1, 5, 29), pokemon::TeachMoveOutcome::Learned);  // fills slot 2
  ASSERT_TRUE(store.loadState(state));
  EXPECT_EQ(state.bagCounts[22], 1U);  // exactly one TM spent
  const pokemon::BattleRecordEntry* afterFirst = battleStore.findEntry(1);
  ASSERT_NE(afterFirst, nullptr);
  EXPECT_EQ(afterFirst->moves[2], 5U);

  // A full moveset must not spend the item either - the caller still has to
  // ask which slot to overwrite before anything is consumed.
  ASSERT_EQ(service.teachMove(1, 6), pokemon::TeachMoveOutcome::Learned);  // fills the last slot directly
  EXPECT_EQ(service.teachMoveAndConsumeItem(1, 25, 33), pokemon::TeachMoveOutcome::MovesetFull);
}

TEST(PokemonService, PeekBattleMovesNeverPersistsWhenNoEntryExistsYet) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

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
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);  // Pikachu, currentHp starts at maxHp (18) once synthesized
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

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

TEST(PokemonService, UseConsumableRejectsPlainMedicineOnAFaintedPokemonAndRequiresRevive) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);  // Pikachu, maxHp 18 once synthesized
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  pokemon::BattleRecordEntry entry{};
  ASSERT_EQ(service.loadBattleEntry(1, entry), pokemon::ServiceStatus::Ok);
  entry.currentHp = 0;
  entry.status = pokemon::Ailment::Poison;
  ASSERT_EQ(service.saveBattleEntry(entry), pokemon::ServiceStatus::Ok);

  // A fainted Pokemon cannot be healed, cured, or have its PP restored by
  // anything but a revival item - Potion/Full Restore/status cures/PP
  // restores all no-op instead of quietly reviving it as a side effect.
  EXPECT_EQ(service.useConsumable(1, 11), pokemon::UseConsumableOutcome::NotApplicable);  // Potion
  EXPECT_EQ(service.useConsumable(1, 17), pokemon::UseConsumableOutcome::NotApplicable);  // Full Restore
  EXPECT_EQ(service.useConsumable(1, 22), pokemon::UseConsumableOutcome::NotApplicable);  // Paralyze Heal
  EXPECT_EQ(service.useConsumable(1, 25), pokemon::UseConsumableOutcome::NotApplicable);  // Ether
  const pokemon::BattleRecordEntry* untouched = battleStore.findEntry(1);
  ASSERT_NE(untouched, nullptr);
  EXPECT_EQ(untouched->currentHp, 0U);
  EXPECT_EQ(untouched->status, pokemon::Ailment::Poison);

  // Revive (id 15) restores half max HP; a second faint then requires Max
  // Revive (id 16) to come back at full HP. This test injects a fainted-
  // but-still-poisoned entry directly (bypassing the battle engine, which
  // now clears status the instant HP hits 0 via faintCombatant() -
  // matching the real games, where a fainted Pokemon has no status left to
  // cure) to confirm useConsumable() itself doesn't need to touch status
  // either way - it's purely a defensive/synthetic scenario, not something
  // real gameplay produces anymore.
  EXPECT_EQ(service.useConsumable(1, 15), pokemon::UseConsumableOutcome::Applied);
  const pokemon::BattleRecordEntry* revived = battleStore.findEntry(1);
  ASSERT_NE(revived, nullptr);
  EXPECT_EQ(revived->currentHp, 9U);  // 50% of 18, rounded down
  EXPECT_EQ(revived->status, pokemon::Ailment::Poison);

  pokemon::BattleRecordEntry fainted = *revived;
  fainted.currentHp = 0;
  ASSERT_EQ(service.saveBattleEntry(fainted), pokemon::ServiceStatus::Ok);
  EXPECT_EQ(service.useConsumable(1, 16), pokemon::UseConsumableOutcome::Applied);  // Max Revive
  const pokemon::BattleRecordEntry* maxRevived = battleStore.findEntry(1);
  ASSERT_NE(maxRevived, nullptr);
  EXPECT_EQ(maxRevived->currentHp, 18U);  // 100% of max

  // Revive on a Pokemon that isn't fainted has no effect either.
  EXPECT_EQ(service.useConsumable(1, 15), pokemon::UseConsumableOutcome::NotApplicable);
}

TEST(PokemonService, UseConsumableCuresOnlyTheMatchingStatusWithStatusCureItems) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

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
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

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

// Round 5 audit bug 3.1: toxicCounter is only ever valid while status ==
// Poison (validateBattleRecordEntry) - curing Poison via an item must clear
// it too, or the persisted entry becomes invalid and silently fails to save.
TEST(PokemonService, UseConsumableCuringPoisonAlsoClearsTheToxicCounter) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  pokemon::BattleRecordEntry entry{};
  ASSERT_EQ(service.loadBattleEntry(1, entry), pokemon::ServiceStatus::Ok);
  entry.status = pokemon::Ailment::Poison;
  entry.toxicCounter = 5;
  ASSERT_EQ(service.saveBattleEntry(entry), pokemon::ServiceStatus::Ok);

  EXPECT_EQ(service.useConsumable(1, 18), pokemon::UseConsumableOutcome::Applied);  // Antidote
  const pokemon::BattleRecordEntry* cured = battleStore.findEntry(1);
  ASSERT_NE(cured, nullptr);
  EXPECT_EQ(cured->status, pokemon::Ailment::None);
  EXPECT_EQ(cured->toxicCounter, 0U);
}

TEST(PokemonService, UseConsumablePPRestoreTopsUpEveryKnownMoveSlot) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);  // synthesizes moves [84, 45, 0, 0] at full PP [30, 40]
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

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

TEST(PokemonService, AwardBattleXpAppliesWildOrTrainerMultiplierAndCapsAtLevel100) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);  // Pikachu, level 5, totalXp starts at xpRequired(5)
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  pokemon::PokemonRecord leader{};
  ASSERT_TRUE(store.readRecord(1, leader));
  const uint32_t startingXp = leader.totalXp;

  ASSERT_EQ(service.awardBattleXp(1, 5, false, 4), pokemon::ServiceStatus::Ok);  // wild win: 5*4=20 XP
  ASSERT_TRUE(store.readRecord(1, leader));
  EXPECT_EQ(leader.totalXp, startingXp + 20U);

  ASSERT_EQ(service.awardBattleXp(1, 12, true, 4), pokemon::ServiceStatus::Ok);  // trainer win: 12*6=72 XP
  ASSERT_TRUE(store.readRecord(1, leader));
  EXPECT_EQ(leader.totalXp, startingXp + 20U + 72U);

  // A gain that would overshoot the level-100 ceiling clamps exactly to it,
  // matching how reading credit (applyCreditedMinutes) never exceeds it either.
  leader.totalXp = pokemon::xpRequired(100) - 10U;
  pokemon::PokemonState state{};
  ASSERT_TRUE(store.loadState(state));
  ASSERT_TRUE(store.commit(state, pokemon::RecordMutation{1, leader, pokemon::RecordMutationKind::Replace}));
  ASSERT_EQ(service.awardBattleXp(1, 65, true, 4), pokemon::ServiceStatus::Ok);  // 65*6=390, far past the remaining 10
  ASSERT_TRUE(store.readRecord(1, leader));
  EXPECT_EQ(leader.totalXp, pokemon::xpRequired(100));
  EXPECT_EQ(pokemon::levelForXp(leader.totalXp), 100U);

  // Already at level 100 - a further award is a no-op, not an error.
  ASSERT_EQ(service.awardBattleXp(1, 65, true, 4), pokemon::ServiceStatus::Ok);
  ASSERT_TRUE(store.readRecord(1, leader));
  EXPECT_EQ(leader.totalXp, pokemon::xpRequired(100));
}

TEST(PokemonService, AwardBattleXpQueuesEvolutionPromptOncePastTheLevelRule) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  // Oddish (43) evolves into Gloom (44) at level 21; sit it just below that.
  pokemon::PokemonRecord record{};
  ASSERT_TRUE(store.readRecord(1, record));
  record.speciesId = 43;
  record.totalXp = pokemon::xpRequired(20);
  pokemon::PokemonState state{};
  ASSERT_TRUE(store.loadState(state));
  ASSERT_TRUE(pokemon::markSpecies(state.caughtSpecies, 43));
  ASSERT_TRUE(pokemon::markSpecies(state.seenSpecies, 43));
  ASSERT_TRUE(store.commit(state, pokemon::RecordMutation{1, record, pokemon::RecordMutationKind::Replace}));

  // A trainer win at level 30 is 180 XP, far more than one level: crosses 21.
  ASSERT_EQ(service.awardBattleXp(1, 30, true, 4), pokemon::ServiceStatus::Ok);
  ASSERT_TRUE(store.loadState(state));
  const pokemon::PendingEvent* pending = pokemon::pendingEventFront(state);
  ASSERT_NE(pending, nullptr);
  EXPECT_EQ(pending->kind, pokemon::PendingEventKind::Evolution);
  EXPECT_EQ(pending->recordId, 1U);
  EXPECT_EQ(pending->speciesId, 44U);
}

TEST(PokemonService, AwardBattleXpDoesNotReAskForAnEvolutionThatWasAlreadyCancelled) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  seedStarter(store);
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  // A Level 100 Oddish is long past its evolution level (21) and gains no XP from a win.
  pokemon::PokemonRecord record{};
  ASSERT_TRUE(store.readRecord(1, record));
  record.speciesId = 43;
  record.totalXp = pokemon::xpRequired(100);
  pokemon::PokemonState state{};
  ASSERT_TRUE(store.loadState(state));
  ASSERT_TRUE(pokemon::markSpecies(state.caughtSpecies, 43));
  ASSERT_TRUE(pokemon::markSpecies(state.seenSpecies, 43));
  ASSERT_TRUE(store.commit(state, pokemon::RecordMutation{1, record, pokemon::RecordMutationKind::Replace}));

  ASSERT_EQ(service.awardBattleXp(1, 30, true, 4), pokemon::ServiceStatus::Ok);
  ASSERT_TRUE(store.loadState(state));
  EXPECT_EQ(pokemon::pendingEventFront(state), nullptr);  // no level gained -> no prompt ("Evolve now" covers it)
}

TEST(PokemonService, ReleasingAPokemonWithAQueuedMoveLearnDoesNotLeaveTheQueueStuck) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  appendOwnedPokemon(store, caughtPokemon(2, 4), true);  // second party member
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  pokemon::PokemonState state{};
  ASSERT_TRUE(store.loadState(state));
  state.pendingEvents[0] = {2, 33, 9, pokemon::Gender::Unknown, pokemon::EvolutionItem::None,
                            pokemon::PendingEventKind::MoveLearn};
  ASSERT_TRUE(store.commit(state));

  ASSERT_EQ(service.depositPokemon(2), pokemon::ServiceStatus::Ok);
  ASSERT_EQ(service.releasePokemon(2), pokemon::ServiceStatus::Ok);

  ASSERT_TRUE(store.loadState(state));
  const pokemon::PendingEvent* front = pokemon::pendingEventFront(state);
  EXPECT_TRUE(front == nullptr || front->recordId != 2U);
}

// The Champion's final slot is picked from the starter's species. That must
// survive the starter being deposited and released like any other Pokemon,
// so it is recorded in the save when the starter is created instead of being
// looked up from record 1 later.
TEST(PokemonService, ReleasingTheStarterKeepsTheRecordedStarterSpecies) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});
  ASSERT_EQ(service.createStarter(4, pokemon::Gender::Male, "Char"), pokemon::ServiceStatus::Ok);
  appendOwnedPokemon(store, caughtPokemon(2, 16), true);  // a second party member so the starter can be deposited

  ASSERT_EQ(service.depositPokemon(1), pokemon::ServiceStatus::Ok);
  ASSERT_EQ(service.releasePokemon(1), pokemon::ServiceStatus::Ok);

  pokemon::PokemonRecord gone{};
  EXPECT_FALSE(store.readRecord(1, gone));  // the starter record really is gone
  pokemon::PokemonState state{};
  ASSERT_TRUE(store.loadState(state));
  EXPECT_EQ(state.starterSpeciesId, 4U);  // ...but its species is not
}

TEST(PokemonService, ResolveMoveLearnDropsAPromptWhoseRecordIsGone) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  // A stale prompt (e.g. from a save made before the release fix) for a record that doesn't exist.
  pokemon::PokemonState state{};
  ASSERT_TRUE(store.loadState(state));
  state.pendingEvents[0] = {77, 33, 9, pokemon::Gender::Unknown, pokemon::EvolutionItem::None,
                            pokemon::PendingEventKind::MoveLearn};
  ASSERT_TRUE(store.commit(state));

  EXPECT_EQ(service.resolveMoveLearn(-1), pokemon::ServiceStatus::Ok);
  ASSERT_TRUE(store.loadState(state));
  EXPECT_EQ(pokemon::pendingEventFront(state), nullptr);
}

TEST(PokemonService, EnsureIvEvRollsShinyIvsForAShinyRecordEvenWithoutTheFlag) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  pokemon::PokemonRecord shiny = caughtPokemon(2, 4);
  shiny.flags = static_cast<uint8_t>(shiny.flags | pokemon::recordFlag(pokemon::RecordFlag::Shiny));
  appendOwnedPokemon(store, shiny, false);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  // No `shiny` argument, as the vitamin/medicine/XP call sites pass none.
  const pokemon::IvEvEntry entry = service.ensureIvEv(2);
  std::array<uint8_t, pokemon::STAT_COUNT> expected{};
  pokemon::rollShinyIvSet({nullptr, zeroRandom}, expected);
  EXPECT_EQ(entry.iv, expected);
}

TEST(PokemonService, EnsureIvEvRollsOnceAndPersistsForSubsequentCalls) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  // No entry yet - peek returns a zero default without creating anything.
  const pokemon::IvEvEntry peeked = service.peekIvEv(1);
  EXPECT_EQ(peeked.recordId, 0U);
  for (const uint8_t iv : peeked.iv) EXPECT_EQ(iv, 0U);

  const pokemon::IvEvEntry first = service.ensureIvEv(1);
  EXPECT_EQ(first.recordId, 1U);
  // zeroRandom always rolls 0, so every IV comes back 0 too here -
  // deterministic for this test, not a claim about the real distribution
  // (see PokemonBattleTest's own rollIvSetProducesValuesInTheRealGen1Range).
  for (const uint8_t iv : first.iv) EXPECT_EQ(iv, 0U);

  // Now a peek finds the persisted entry instead of a default.
  const pokemon::IvEvEntry secondPeek = service.peekIvEv(1);
  EXPECT_EQ(secondPeek.recordId, 1U);

  // Calling ensureIvEv again must not re-roll - returns the same entry.
  const pokemon::IvEvEntry second = service.ensureIvEv(1);
  EXPECT_TRUE(first.iv == second.iv);
  EXPECT_TRUE(first.ev == second.ev);
}

// Regression test for a real-device bug: once PokemonIvEvStore's writes could
// genuinely fail (v0.18.1+'s heap-allocation-based OOM safety, see CHANGELOG
// v0.18.x), ensureIvEv() used to re-roll a brand new random IV on every call
// for a record that never managed to persist - since Party/Summary screens
// call ensureIvEv() on every render, this showed up as a Pokemon's displayed
// max HP flickering between values on every redraw. It must instead keep
// returning the SAME rolled value across repeated failed-to-persist calls.
TEST(PokemonService, EnsureIvEvReturnsAStableRollEvenWhilePersistingKeepsFailing) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  Storage.setFailWritableOpen(true);
  const pokemon::IvEvEntry first = service.ensureIvEv(1);
  const pokemon::IvEvEntry second = service.ensureIvEv(1);
  const pokemon::IvEvEntry third = service.ensureIvEv(1);
  EXPECT_TRUE(first.iv == second.iv);
  EXPECT_TRUE(first.iv == third.iv);
  EXPECT_TRUE(first.ev == second.ev);
  EXPECT_TRUE(first.ev == third.ev);
  // Confirms this scenario never actually persisted while the fault was
  // active - the stability above is genuinely from the pending-roll cache,
  // not from the store having quietly succeeded anyway.
  EXPECT_EQ(ivEvStore.findEntry(1), nullptr);

  // Once writes can succeed again, the exact same rolled value (not a new
  // roll) finally makes it to disk.
  Storage.setFailWritableOpen(false);
  const pokemon::IvEvEntry fourth = service.ensureIvEv(1);
  EXPECT_TRUE(first.iv == fourth.iv);
  const pokemon::IvEvEntry* persisted = ivEvStore.findEntry(1);
  ASSERT_NE(persisted, nullptr);
  EXPECT_TRUE(persisted->iv == first.iv);
}

TEST(PokemonService, AwardBattleXpAccumulatesEvYieldAndSaturatesAt255) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);  // Pikachu, level 5
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  // Charizard (species 6) yields 3 Special EV per real PokeAPI data (see
  // scripts/data/pokemon-stats.csv).
  ASSERT_EQ(service.awardBattleXp(1, 50, true, 6), pokemon::ServiceStatus::Ok);
  pokemon::IvEvEntry ivEv = service.peekIvEv(1);
  constexpr size_t specialIndex = 3;  // HP/Attack/Defense/Special/Speed order
  EXPECT_EQ(ivEv.ev[specialIndex], 3U);

  // Repeated wins keep accumulating, saturating at 255 rather than wrapping
  // - and this keeps happening even once totalXp itself has capped at level
  // 100, since EVs are independent of the XP/level cap.
  for (int i = 0; i < 100; ++i) {
    ASSERT_EQ(service.awardBattleXp(1, 50, true, 6), pokemon::ServiceStatus::Ok);
  }
  ivEv = service.peekIvEv(1);
  EXPECT_EQ(ivEv.ev[specialIndex], 255U);

  // Other stats' EV are untouched by an opponent that only yields Special.
  EXPECT_EQ(ivEv.ev[0], 0U);
  EXPECT_EQ(ivEv.ev[1], 0U);
  EXPECT_EQ(ivEv.ev[2], 0U);
  EXPECT_EQ(ivEv.ev[4], 0U);
}

// Real Gen 1: the max-HP gain of a level-up is added to current HP too, so a
// Pokemon at full health stays at full health and a fainted one stays fainted.
TEST(PokemonService, LevelUpFromRareCandyAndBattleXpAddsTheMaxHpGainToCurrentHp) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);  // Pikachu, level 5
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  pokemon::PokemonState state{};
  ASSERT_TRUE(store.loadState(state));
  state.bagCounts[24 - 7] = 2;  // two Rare Candies
  ASSERT_TRUE(store.commit(state));

  pokemon::BattleRecordEntry entry{};
  ASSERT_EQ(service.loadBattleEntry(1, entry), pokemon::ServiceStatus::Ok);
  const pokemon::BaseStats* stats = pokemon::baseStatsFor(25);
  ASSERT_NE(stats, nullptr);
  const pokemon::IvEvEntry ivEv = service.ensureIvEv(1);
  constexpr size_t hp = static_cast<size_t>(pokemon::StatIndex::Hp);
  const uint16_t maxAt5 = pokemon::battleMaxHp(stats->hp, 5, ivEv.iv[hp], ivEv.ev[hp]);
  const uint16_t maxAt6 = pokemon::battleMaxHp(stats->hp, 6, ivEv.iv[hp], ivEv.ev[hp]);
  ASSERT_GT(maxAt6, maxAt5);
  ASSERT_EQ(entry.currentHp, maxAt5);

  // Wounded, but standing: the gain is added on top instead of leaving it behind.
  entry.currentHp = 3;
  ASSERT_EQ(service.saveBattleEntry(entry), pokemon::ServiceStatus::Ok);
  ASSERT_EQ(service.useConsumableAndConsumeItem(1, 24), pokemon::UseConsumableOutcome::Applied);
  ASSERT_NE(battleStore.findEntry(1), nullptr);
  EXPECT_EQ(battleStore.findEntry(1)->currentHp, 3U + (maxAt6 - maxAt5));

  // The same through a battle win.
  pokemon::PokemonRecord record{};
  ASSERT_TRUE(store.readRecord(1, record));
  const uint16_t hpBeforeWin = battleStore.findEntry(1)->currentHp;
  record.totalXp = pokemon::xpRequired(7) - 1U;
  ASSERT_TRUE(store.loadState(state));
  ASSERT_TRUE(store.commit(state, pokemon::RecordMutation{1, record, pokemon::RecordMutationKind::Replace}));
  const uint16_t maxAt7 = pokemon::battleMaxHp(stats->hp, 7, ivEv.iv[hp], ivEv.ev[hp]);
  ASSERT_EQ(service.awardBattleXp(1, 1, false, 4), pokemon::ServiceStatus::Ok);  // +4 XP crosses level 7 only
  ASSERT_TRUE(store.readRecord(1, record));
  ASSERT_EQ(pokemon::levelForXp(record.totalXp), 7U);
  EXPECT_EQ(battleStore.findEntry(1)->currentHp, hpBeforeWin + (maxAt7 - maxAt6));

  // A fainted Pokemon stays fainted through a level-up.
  entry = *battleStore.findEntry(1);
  entry.currentHp = 0;
  ASSERT_EQ(service.saveBattleEntry(entry), pokemon::ServiceStatus::Ok);
  ASSERT_EQ(service.useConsumable(1, 24), pokemon::UseConsumableOutcome::Applied);
  EXPECT_EQ(battleStore.findEntry(1)->currentHp, 0U);
}

TEST(PokemonService, UseConsumableRareCandyAddsOneLevelAndRejectsAtLevel100) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);  // Pikachu, level 5
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

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

// Round 8 audit item C: useConsumable() then a separate consumeBagItem() call
// (the old PokemonActivity.cpp flow) could apply the effect and then, if the
// second write failed, keep it without ever charging the item.
// useConsumableAndConsumeItem() fixes the order - verify it only ever spends
// the item once the effect is actually about to be applied.
TEST(PokemonService, UseConsumableAndConsumeItemSpendsExactlyOneItemOnlyWhenApplied) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);  // Pikachu, currentHp starts at maxHp (18) once synthesized
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  pokemon::PokemonState state{};
  ASSERT_TRUE(store.loadState(state));
  state.bagCounts[4] = 2;  // Potion (item id 11)
  ASSERT_TRUE(store.commit(state));

  // Already at full HP - NotApplicable, and the bag must stay untouched.
  EXPECT_EQ(service.useConsumableAndConsumeItem(1, 11), pokemon::UseConsumableOutcome::NotApplicable);
  ASSERT_TRUE(store.loadState(state));
  EXPECT_EQ(state.bagCounts[4], 2U);

  pokemon::BattleRecordEntry entry{};
  ASSERT_EQ(service.loadBattleEntry(1, entry), pokemon::ServiceStatus::Ok);
  entry.currentHp = 5;
  ASSERT_EQ(service.saveBattleEntry(entry), pokemon::ServiceStatus::Ok);

  ASSERT_EQ(service.useConsumableAndConsumeItem(1, 11), pokemon::UseConsumableOutcome::Applied);
  ASSERT_TRUE(store.loadState(state));
  EXPECT_EQ(state.bagCounts[4], 1U);  // exactly one Potion spent
  const pokemon::BattleRecordEntry* healed = battleStore.findEntry(1);
  ASSERT_NE(healed, nullptr);
  EXPECT_EQ(healed->currentHp, 18U);
}

// useConsumableAndConsumeItem() dry-runs the Rare Candy first, purely to check
// eligibility before touching the bag. That probe must have no side effects:
// it used to persist the newly-learned move via queueMoveLearnIfNeeded()
// before the dryRun early-return, so an unowned candy (or any failure after
// the probe) still handed the Pokemon a free move.
TEST(PokemonService, RareCandyProbeWithNoCandyOwnedDoesNotTeachAFreeMove) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);  // Pikachu, level 5, moves [84, 45, 0, 0]
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  // Find the first level-up that would drop a brand new move into an empty slot.
  uint8_t learnLevel = 0;
  uint8_t learnMove = 0;
  for (const pokemon::LearnsetEntry& learn : pokemon::learnsetFor(25)) {
    if (learn.level > 5 && learn.moveId != 84 && learn.moveId != 45) {
      learnLevel = learn.level;
      learnMove = learn.moveId;
      break;
    }
  }
  ASSERT_GT(learnLevel, 5U);

  pokemon::PokemonRecord leader{};
  ASSERT_TRUE(store.readRecord(1, leader));
  leader.totalXp = pokemon::xpRequired(static_cast<uint8_t>(learnLevel - 1));
  pokemon::PokemonState state{};
  ASSERT_TRUE(store.loadState(state));
  ASSERT_TRUE(store.commit(state, pokemon::RecordMutation{1, leader, pokemon::RecordMutationKind::Replace}));

  pokemon::BattleRecordEntry entry{};
  ASSERT_EQ(service.loadBattleEntry(1, entry), pokemon::ServiceStatus::Ok);
  for (const uint8_t move : entry.moves) ASSERT_NE(move, learnMove);
  bool hasEmptySlot = false;
  for (const uint8_t move : entry.moves) hasEmptySlot = hasEmptySlot || move == 0;
  ASSERT_TRUE(hasEmptySlot);

  // No Rare Candy in the bag: the probe runs, the spend fails, nothing applies.
  ASSERT_TRUE(store.loadState(state));
  ASSERT_EQ(state.bagCounts[24 - 7], 0U);
  EXPECT_NE(service.useConsumableAndConsumeItem(1, 24), pokemon::UseConsumableOutcome::Applied);

  const pokemon::BattleRecordEntry* after = battleStore.findEntry(1);
  ASSERT_NE(after, nullptr);
  for (const uint8_t move : after->moves) EXPECT_NE(move, learnMove);
}

TEST(PokemonService, LearnMoveIntoSlotOverwritesUnconditionallyAtFullPp) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);  // synthesizes to moves [84, 45, 0, 0] at level 5
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  ASSERT_EQ(service.learnMoveIntoSlot(1, 1, 98), pokemon::ServiceStatus::Ok);  // overwrite slot 1 with Quick Attack
  const pokemon::BattleRecordEntry* updated = battleStore.findEntry(1);
  ASSERT_NE(updated, nullptr);
  EXPECT_EQ(updated->moves[0], 84U);  // other slots untouched
  EXPECT_EQ(updated->moves[1], 98U);
  EXPECT_GT(updated->pp[1], 0U);

  EXPECT_EQ(service.learnMoveIntoSlot(1, pokemon::BATTLE_MOVE_SLOTS, 5), pokemon::ServiceStatus::Invalid);
  EXPECT_EQ(service.learnMoveIntoSlot(1, 2, 0), pokemon::ServiceStatus::Invalid);
}

TEST(PokemonService, LearnMoveIntoSlotRedirectsToTheFirstEmptySlotInsteadOfLeavingAGap) {
  // Regression test: Screen::Moveset always lists all BATTLE_MOVE_SLOTS rows
  // (empty ones shown as "-"), so picking the LAST empty row while an
  // earlier one is also still empty used to write a gap
  // (validateBattleRecordEntry() rejects moves[2]==0 with moves[3]!=0) -
  // upsertEntry() then failed the whole write, surfacing as a misleading
  // "save error" with nothing actually learned.
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);  // synthesizes to moves [84, 45, 0, 0] at level 5
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  ASSERT_EQ(service.learnMoveIntoSlot(1, 3, 98), pokemon::ServiceStatus::Ok);  // Quick Attack into slot 3
  const pokemon::BattleRecordEntry* updated = battleStore.findEntry(1);
  ASSERT_NE(updated, nullptr);
  EXPECT_EQ(updated->moves[0], 84U);
  EXPECT_EQ(updated->moves[1], 45U);
  EXPECT_EQ(updated->moves[2], 98U);  // redirected to the first empty slot, not slot 3
  EXPECT_EQ(updated->moves[3], 0U);
  EXPECT_GT(updated->pp[2], 0U);
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
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

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

TEST(PokemonService, ApplyPpUpRaisesMaxPpAndCapsAtThreeUses) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);  // synthesizes to moves [84, 45, 0, 0] - move 84 (Thunder Shock) has 30 base PP
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  ASSERT_EQ(service.applyPpUp(1, 0), pokemon::ServiceStatus::Ok);
  const pokemon::BattleRecordEntry* entry = battleStore.findEntry(1);
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(entry->ppUp[0], 1U);
  EXPECT_EQ(entry->pp[0], 36U);  // was already full (30/30) - tops up to the new max too

  ASSERT_EQ(service.applyPpUp(1, 0), pokemon::ServiceStatus::Ok);
  ASSERT_EQ(service.applyPpUp(1, 0), pokemon::ServiceStatus::Ok);
  entry = battleStore.findEntry(1);
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(entry->ppUp[0], 3U);
  EXPECT_EQ(entry->pp[0], 48U);

  EXPECT_EQ(service.applyPpUp(1, 0), pokemon::ServiceStatus::NotApplicable);  // already at the real cap of 3 uses
  EXPECT_EQ(service.applyPpUp(1, 3), pokemon::ServiceStatus::NotApplicable);  // slot 3 is empty
  EXPECT_EQ(service.applyPpUp(1, pokemon::BATTLE_MOVE_SLOTS), pokemon::ServiceStatus::Invalid);
}

TEST(PokemonService, ApplyPpUpDoesNotGrantFreePpWhenPartiallyUsed) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  pokemon::BattleRecordEntry entry{};
  ASSERT_EQ(service.loadBattleEntry(1, entry), pokemon::ServiceStatus::Ok);
  entry.pp[0] = 10;  // partially used (out of 30 max)
  ASSERT_EQ(service.saveBattleEntry(entry), pokemon::ServiceStatus::Ok);

  ASSERT_EQ(service.applyPpUp(1, 0), pokemon::ServiceStatus::Ok);
  const pokemon::BattleRecordEntry* updated = battleStore.findEntry(1);
  ASSERT_NE(updated, nullptr);
  EXPECT_EQ(updated->ppUp[0], 1U);
  EXPECT_EQ(updated->pp[0], 10U);  // current PP untouched - matches the real games exactly
}

namespace {
void setPpUpCount(pokemon::PokemonStore& store, const uint8_t count) {
  pokemon::PokemonState state{};
  ASSERT_TRUE(store.loadState(state));
  state.ppUpCount = count;
  ASSERT_TRUE(store.commit(state));
}

uint8_t storedPpUpCount(pokemon::PokemonStore& store) {
  pokemon::PokemonState state{};
  EXPECT_TRUE(store.loadState(state));
  return state.ppUpCount;
}
}  // namespace

TEST(PokemonService, UsePpUpSpendsExactlyOneItemAndRaisesMaxPp) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});
  setPpUpCount(store, 2);

  ASSERT_EQ(service.usePpUp(1, 0), pokemon::ServiceStatus::Ok);
  EXPECT_EQ(storedPpUpCount(store), 1U);
  const pokemon::BattleRecordEntry* entry = battleStore.findEntry(1);
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(entry->ppUp[0], 1U);
}

TEST(PokemonService, UsePpUpDoesNotConsumeAnItemForASlotThatCannotTakeIt) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);  // moves [84, 45, 0, 0] - slot 3 is empty
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});
  setPpUpCount(store, 2);

  EXPECT_EQ(service.usePpUp(1, 3), pokemon::ServiceStatus::NotApplicable);
  EXPECT_EQ(service.usePpUp(1, pokemon::BATTLE_MOVE_SLOTS), pokemon::ServiceStatus::Invalid);
  EXPECT_EQ(storedPpUpCount(store), 2U);
}

TEST(PokemonService, UsePpUpAppliesNothingWhenTheBagHasNoPpUp) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});
  setPpUpCount(store, 0);

  EXPECT_EQ(service.usePpUp(1, 0), pokemon::ServiceStatus::NotApplicable);
  const pokemon::BattleRecordEntry* entry = battleStore.findEntry(1);
  if (entry != nullptr) EXPECT_EQ(entry->ppUp[0], 0U);  // no free boost without an item
}

namespace {
void setVitaminCount(pokemon::PokemonStore& store, const size_t index, const uint8_t count) {
  pokemon::PokemonState state{};
  ASSERT_TRUE(store.loadState(state));
  state.vitaminCounts[index] = count;
  ASSERT_TRUE(store.commit(state));
}

uint8_t storedVitaminCount(pokemon::PokemonStore& store, const size_t index) {
  pokemon::PokemonState state{};
  EXPECT_TRUE(store.loadState(state));
  return state.vitaminCounts[index];
}
}  // namespace

TEST(PokemonService, UseVitaminRaisesOnlyTheMatchingStatByTenAndSpendsOneItem) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});
  setVitaminCount(store, 1, 2);  // Protein -> Attack

  ASSERT_EQ(service.useVitamin(1, pokemon::ITEM_PROTEIN), pokemon::ServiceStatus::Ok);
  const pokemon::IvEvEntry ivEv = service.peekIvEv(1);
  EXPECT_EQ(ivEv.ev[static_cast<size_t>(pokemon::StatIndex::Attack)], 10U);
  EXPECT_EQ(ivEv.ev[static_cast<size_t>(pokemon::StatIndex::Hp)], 0U);
  EXPECT_EQ(ivEv.ev[static_cast<size_t>(pokemon::StatIndex::Speed)], 0U);
  EXPECT_EQ(storedVitaminCount(store, 1), 1U);
}

TEST(PokemonService, EachVitaminMapsToItsOwnStat) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});
  for (size_t i = 0; i < pokemon::VITAMIN_ITEM_COUNT; ++i) setVitaminCount(store, i, 1);

  for (uint8_t id = pokemon::VITAMIN_ITEM_ID_FIRST; id <= pokemon::VITAMIN_ITEM_ID_LAST; ++id) {
    ASSERT_EQ(service.useVitamin(1, id), pokemon::ServiceStatus::Ok);
  }
  const pokemon::IvEvEntry ivEv = service.peekIvEv(1);
  EXPECT_EQ(ivEv.ev[static_cast<size_t>(pokemon::StatIndex::Hp)], 10U);       // HP Up
  EXPECT_EQ(ivEv.ev[static_cast<size_t>(pokemon::StatIndex::Attack)], 10U);   // Protein
  EXPECT_EQ(ivEv.ev[static_cast<size_t>(pokemon::StatIndex::Defense)], 10U);  // Iron
  EXPECT_EQ(ivEv.ev[static_cast<size_t>(pokemon::StatIndex::Special)], 10U);  // Calcium
  EXPECT_EQ(ivEv.ev[static_cast<size_t>(pokemon::StatIndex::Speed)], 10U);    // Carbos
}

TEST(PokemonService, UseVitaminStopsAtTheVitaminCapWithoutConsumingTheItem) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});
  setVitaminCount(store, 0, 11);  // HP Up

  for (int use = 0; use < 10; ++use) {
    ASSERT_EQ(service.useVitamin(1, pokemon::ITEM_HP_UP), pokemon::ServiceStatus::Ok);
  }
  EXPECT_EQ(service.peekIvEv(1).ev[static_cast<size_t>(pokemon::StatIndex::Hp)], pokemon::VITAMIN_EV_CAP);
  EXPECT_EQ(service.useVitamin(1, pokemon::ITEM_HP_UP), pokemon::ServiceStatus::NotApplicable);
  EXPECT_EQ(storedVitaminCount(store, 0), 1U);  // the 11th was not spent
}

TEST(PokemonService, UseVitaminDoesNothingWithoutOneInTheBagOrForABadItemId) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  EXPECT_EQ(service.useVitamin(1, pokemon::ITEM_CARBOS), pokemon::ServiceStatus::NotApplicable);
  EXPECT_EQ(service.peekIvEv(1).ev[static_cast<size_t>(pokemon::StatIndex::Speed)], 0U);
  EXPECT_EQ(service.useVitamin(1, pokemon::ITEM_DIRE_HIT), pokemon::ServiceStatus::Invalid);
  EXPECT_EQ(service.useVitamin(1, pokemon::VITAMIN_ITEM_ID_LAST + 1), pokemon::ServiceStatus::Invalid);
}

TEST(PokemonService, CatchBlockedByFullBoxOnlyWhenPartyAndBoxAreBothFull) {
  constexpr size_t party = pokemon::PARTY_SIZE;
  constexpr size_t box = pokemon::PC_BOX_MAX_RECORDS;
  EXPECT_TRUE(pokemon::catchBlockedByFullBox(party, party + box));
  EXPECT_FALSE(pokemon::catchBlockedByFullBox(party, party + box - 1));    // one Box slot left
  EXPECT_FALSE(pokemon::catchBlockedByFullBox(party - 1, party - 1 + box));  // party has a free slot
  EXPECT_FALSE(pokemon::catchBlockedByFullBox(0, 0));
}

TEST(PokemonService, ForgetMoveShiftsPpUpAlongWithMovesAndPp) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  pokemon::BattleRecordEntry entry{};
  ASSERT_EQ(service.loadBattleEntry(1, entry), pokemon::ServiceStatus::Ok);
  entry.moves = {84, 45, 98, 5};
  entry.pp = {30, 40, 20, 20};
  entry.ppUp = {2, 0, 1, 0};
  ASSERT_EQ(service.saveBattleEntry(entry), pokemon::ServiceStatus::Ok);

  ASSERT_EQ(service.forgetMove(1, 1), pokemon::ServiceStatus::Ok);  // forget Growl (slot 1)
  const pokemon::BattleRecordEntry* after = battleStore.findEntry(1);
  ASSERT_NE(after, nullptr);
  const std::array<uint8_t, pokemon::BATTLE_MOVE_SLOTS> expectedPpUp{2, 1, 0, 0};
  EXPECT_EQ(after->ppUp, expectedPpUp);  // slot 0's own PP Up (2) stays put; slot 2's (1) shifts into slot 1
}

TEST(PokemonService, TeachMoveKeepsTheSlotsExistingPpUpForTheNewMove) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);  // synthesizes to moves [84, 45, 0, 0] at level 5
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  ASSERT_EQ(service.applyPpUp(1, 1), pokemon::ServiceStatus::Ok);  // PP-Up slot 1 (Growl) once

  ASSERT_EQ(service.teachMove(1, 5), pokemon::TeachMoveOutcome::Learned);  // fills slot 2
  ASSERT_EQ(service.teachMove(1, 6), pokemon::TeachMoveOutcome::Learned);  // fills the last slot
  EXPECT_EQ(service.teachMove(1, 25), pokemon::TeachMoveOutcome::MovesetFull);
  ASSERT_EQ(service.teachMove(1, 25, 1), pokemon::TeachMoveOutcome::Learned);  // replace slot 1 (was Growl, ppUp=1)

  const pokemon::BattleRecordEntry* updated = battleStore.findEntry(1);
  ASSERT_NE(updated, nullptr);
  EXPECT_EQ(updated->moves[1], 25U);
  EXPECT_EQ(updated->ppUp[1], 1U);  // the slot's PP Up level carried over to the new move
}

TEST(PokemonService, ForgetMoveRefusesToClearAPokemonsLastRemainingMove) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

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
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, itemEventRandom});
  ASSERT_TRUE(service.beginReadingSession());

  ASSERT_TRUE(service.creditMinutes(60, 64));

  pokemon::PokemonState state{};
  ASSERT_TRUE(store.loadState(state));
  EXPECT_EQ(state.pendingEvents[0].kind, pokemon::PendingEventKind::Item);
  EXPECT_EQ(state.pendingEvents[0].item, pokemon::EvolutionItem::ThunderStone);
  EXPECT_EQ(state.itemCounts[2], 1U);
}

TEST(PokemonService, CaptureHallOfFameSnapshotsThePartyOnceThenRefusesToOverwriteIt) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);  // party[0] = Pikachu, recordId 1, level 5
  appendOwnedPokemon(store, caughtPokemon(2, 6), /*addToParty=*/true);   // party[1] = Charizard
  appendOwnedPokemon(store, caughtPokemon(3, 9), /*addToParty=*/false);  // in the PC Box, not the party
  pokemon::PokemonState seeded{};
  ASSERT_TRUE(store.loadState(seeded));
  seeded.lifetimeMinutes = 754;
  ASSERT_TRUE(store.commit(seeded));
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  ASSERT_EQ(service.captureHallOfFame(), pokemon::ServiceStatus::Ok);
  const pokemon::HallOfFameState snapshot = service.peekHallOfFame();
  EXPECT_TRUE(snapshot.cleared);
  EXPECT_EQ(snapshot.lifetimeMinutesAtClear, 754U);
  EXPECT_EQ(snapshot.members[0].speciesId, 25U);  // Pikachu
  EXPECT_EQ(snapshot.members[0].level, 5U);
  EXPECT_EQ(snapshot.members[0].gender, pokemon::Gender::Female);
  EXPECT_EQ(snapshot.members[1].speciesId, 6U);  // Charizard
  EXPECT_EQ(snapshot.members[1].gender, pokemon::Gender::Male);
  EXPECT_EQ(snapshot.members[2].speciesId, 0U);  // the boxed Pokemon was never in the party
  EXPECT_EQ(snapshot.members[3].speciesId, 0U);

  // The Champion can never be re-fought, so a second capture attempt (should
  // this ever be reached) must leave the frozen snapshot untouched.
  EXPECT_EQ(service.captureHallOfFame(), pokemon::ServiceStatus::NotApplicable);
  const pokemon::HallOfFameState unchanged = service.peekHallOfFame();
  EXPECT_EQ(unchanged, snapshot);
}

TEST(PokemonService, PeekHallOfFameIsUnclearedBeforeAnyCapture) {
  Storage.clear();
  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  seedStarter(store);
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});

  const pokemon::HallOfFameState snapshot = service.peekHallOfFame();
  EXPECT_FALSE(snapshot.cleared);
}

}  // namespace
