#include <HalStorage.h>

#include <array>
#include <cstdio>
#include <cstring>

#include "PokemonBattleStoreCodec.h"
#include "pokemon/PokemonBattleStore.h"

namespace {

int failures = 0;

#define CHECK(condition)                                                                \
  do {                                                                                  \
    if (!(condition)) {                                                                 \
      std::fprintf(stderr, "%s:%d check failed: %s\n", __FILE__, __LINE__, #condition); \
      ++failures;                                                                       \
    }                                                                                   \
  } while (false)

constexpr const char* STORE_PATH_A = "/.crosspoint/pokemon-battle-a.bin";
constexpr const char* STORE_PATH_B = "/.crosspoint/pokemon-battle-b.bin";
constexpr const char* LEGACY_STORE_PATH = "/.crosspoint/pokemon-battle.bin";

pokemon::BattleRecordEntry makeEntry(const uint32_t recordId, const uint16_t hp) {
  pokemon::BattleRecordEntry entry{};
  entry.recordId = recordId;
  entry.moves = {33, 45, 0, 0};
  entry.pp = {35, 40, 0, 0};
  entry.currentHp = hp;
  entry.status = pokemon::Ailment::None;
  return entry;
}

void missingFilesLeaveAnEmptyStore() {
  Storage.clear();
  pokemon::PokemonBattleStore store;
  CHECK(store.findEntry(1) == nullptr);
  CHECK(!Storage.exists(STORE_PATH_A));
  CHECK(!Storage.exists(STORE_PATH_B));
}

void upsertPersistsAndAFreshInstanceReadsItBack() {
  Storage.clear();
  {
    pokemon::PokemonBattleStore writer;
    CHECK(writer.upsertEntry(makeEntry(7, 20)));
    CHECK(writer.upsertEntry(makeEntry(3, 15)));
  }
  // First write with no prior valid slot always lands on A.
  CHECK(Storage.exists(STORE_PATH_A));

  pokemon::PokemonBattleStore reader;
  const pokemon::BattleRecordEntry* seven = reader.findEntry(7);
  CHECK(seven != nullptr);
  if (seven != nullptr) CHECK(seven->currentHp == 20);
  const pokemon::BattleRecordEntry* three = reader.findEntry(3);
  CHECK(three != nullptr);
  if (three != nullptr) CHECK(three->currentHp == 15);
  CHECK(reader.findEntry(999) == nullptr);
}

void upsertReplacesAnExistingEntryInPlace() {
  Storage.clear();
  pokemon::PokemonBattleStore store;
  CHECK(store.upsertEntry(makeEntry(7, 20)));
  CHECK(store.upsertEntry(makeEntry(7, 5)));  // same recordId, different HP

  pokemon::PokemonBattleStore reader;
  const pokemon::BattleRecordEntry* entry = reader.findEntry(7);
  CHECK(entry != nullptr);
  if (entry != nullptr) CHECK(entry->currentHp == 5);
}

void removeEntryPersistsAndLeavesOthersIntact() {
  Storage.clear();
  pokemon::PokemonBattleStore store;
  CHECK(store.upsertEntry(makeEntry(7, 20)));
  CHECK(store.upsertEntry(makeEntry(3, 15)));
  CHECK(store.removeEntry(7));
  CHECK(!store.removeEntry(7));  // already gone

  pokemon::PokemonBattleStore reader;
  CHECK(reader.findEntry(7) == nullptr);
  const pokemon::BattleRecordEntry* three = reader.findEntry(3);
  CHECK(three != nullptr);
  if (three != nullptr) CHECK(three->currentHp == 15);
}

void successiveWritesAlternateBetweenTheTwoFiles() {
  Storage.clear();
  pokemon::PokemonBattleStore store;
  CHECK(store.upsertEntry(makeEntry(7, 20)));  // -> A, sequence 1
  CHECK(Storage.exists(STORE_PATH_A));
  CHECK(!Storage.exists(STORE_PATH_B));

  CHECK(store.upsertEntry(makeEntry(7, 19)));  // -> B, sequence 2
  CHECK(Storage.exists(STORE_PATH_B));

  CHECK(store.upsertEntry(makeEntry(7, 18)));  // -> A again, sequence 3
  pokemon::PokemonBattleStore reader;
  const pokemon::BattleRecordEntry* entry = reader.findEntry(7);
  CHECK(entry != nullptr);
  if (entry != nullptr) CHECK(entry->currentHp == 18);
}

void corruptingOneFileFallsBackToTheOtherWithoutDataLoss() {
  Storage.clear();
  pokemon::PokemonBattleStore store;
  CHECK(store.upsertEntry(makeEntry(7, 20)));  // -> A, sequence 1
  CHECK(store.upsertEntry(makeEntry(7, 5)));   // -> B, sequence 2 (now newest)
  Storage.setByte(STORE_PATH_B, 5, 0xFFU);     // corrupt the newer file's entryCount byte

  // The reader must fall back to A (still valid, one write behind) instead
  // of losing the moveset/HP entirely - this is exactly the crash-safety
  // double-buffering exists for now that moves are no longer reconstructible.
  pokemon::PokemonBattleStore reader;
  const pokemon::BattleRecordEntry* entry = reader.findEntry(7);
  CHECK(entry != nullptr);
  if (entry != nullptr) CHECK(entry->currentHp == 20);
}

void bothFilesCorruptStillLeavesAnEmptyStoreNeverAnError() {
  Storage.clear();
  {
    pokemon::PokemonBattleStore writer;
    CHECK(writer.upsertEntry(makeEntry(7, 20)));
    CHECK(writer.upsertEntry(makeEntry(7, 5)));
  }
  Storage.setByte(STORE_PATH_A, 5, 0xFFU);
  Storage.setByte(STORE_PATH_B, 5, 0xFFU);

  pokemon::PokemonBattleStore reader;
  CHECK(reader.findEntry(7) == nullptr);  // discarded, not crashed and not silently wrong
}

void invalidUpsertLeavesThePreviouslyWrittenFilesUntouched() {
  Storage.clear();
  pokemon::PokemonBattleStore store;
  CHECK(store.upsertEntry(makeEntry(7, 20)));

  pokemon::BattleRecordEntry invalid = makeEntry(0, 5);  // recordId 0 is never valid
  CHECK(!store.upsertEntry(invalid));

  pokemon::PokemonBattleStore reader;
  const pokemon::BattleRecordEntry* seven = reader.findEntry(7);
  CHECK(seven != nullptr);
  if (seven != nullptr) CHECK(seven->currentHp == 20);
}

void aWriteFailureLeavesTheActiveFileAndInMemoryStateUnchanged() {
  Storage.clear();
  pokemon::PokemonBattleStore store;
  CHECK(store.upsertEntry(makeEntry(7, 20)));  // -> A, sequence 1, active
  CHECK(store.findEntry(7)->currentHp == 20);

  Storage.setFailWritableOpen(true);  // the next write to the inactive file (B) fails to open
  CHECK(!store.upsertEntry(makeEntry(7, 1)));
  Storage.setFailWritableOpen(false);

  CHECK(store.findEntry(7)->currentHp == 20);  // in-memory state rolled back to what A still holds
  pokemon::PokemonBattleStore reader;
  CHECK(reader.findEntry(7)->currentHp == 20);  // A (the active file) was never touched
  CHECK(!Storage.exists(STORE_PATH_B));
}

void preExistingLegacySingleFileIsMigratedOnFirstLoad() {
  Storage.clear();
  // Hand-roll a legacy (pre-double-buffering, headerless) file exactly as
  // the old single-file format wrote it: v1 (16-byte, no PP-Up counters)
  // entries back to back + CRC32. encodeBattleRecordEntry only ever writes
  // the current (v2, 20-byte) format now, so the true legacy layout has to
  // be built by hand here.
  pokemon::BattleRecordEntry entry = makeEntry(11, 30);
  entry.moves = {14, 0, 0, 0};  // teach a move that's *not* level-derivable, to show migration keeps it
  entry.pp = {20, 0, 0, 0};
  pokemon::BattleEntryBytesV1 entryBytes{};
  entryBytes[0] = static_cast<uint8_t>(entry.recordId);
  entryBytes[1] = static_cast<uint8_t>(entry.recordId >> 8);
  entryBytes[2] = static_cast<uint8_t>(entry.recordId >> 16);
  entryBytes[3] = static_cast<uint8_t>(entry.recordId >> 24);
  for (size_t i = 0; i < 4; ++i) entryBytes[4 + i] = entry.moves[i];
  for (size_t i = 0; i < 4; ++i) entryBytes[8 + i] = entry.pp[i];
  entryBytes[12] = static_cast<uint8_t>(entry.currentHp);
  entryBytes[13] = static_cast<uint8_t>(entry.currentHp >> 8);
  entryBytes[14] = static_cast<uint8_t>(entry.status);
  entryBytes[15] = entry.statusTurns;
  std::vector<uint8_t> legacyBytes(entryBytes.begin(), entryBytes.end());
  const uint32_t crc = pokemon::finishBattleStoreCrc32(
      pokemon::updateBattleStoreCrc32(pokemon::BATTLE_STORE_CRC32_INITIAL, entryBytes.data(), entryBytes.size()));
  for (size_t i = 0; i < 4; ++i) legacyBytes.push_back(static_cast<uint8_t>(crc >> (8 * i)));

  FsFile legacyFile = Storage.open(LEGACY_STORE_PATH, O_WRONLY | O_CREAT | O_TRUNC);
  CHECK(legacyFile.write(legacyBytes.data(), legacyBytes.size()) == legacyBytes.size());
  CHECK(legacyFile.close());

  pokemon::PokemonBattleStore store;
  const pokemon::BattleRecordEntry* migrated = store.findEntry(11);
  CHECK(migrated != nullptr);
  if (migrated != nullptr) {
    CHECK(migrated->currentHp == 30);
    CHECK(migrated->moves[0] == 14);
  }
  // Migration writes the new alternating-file format immediately, and
  // leaves the legacy file alone as a recovery copy.
  CHECK(Storage.exists(STORE_PATH_A));
  CHECK(Storage.exists(LEGACY_STORE_PATH));

  // A later instance must prefer the migrated slot, not re-migrate.
  pokemon::PokemonBattleStore reader;
  const pokemon::BattleRecordEntry* rereadEntry = reader.findEntry(11);
  CHECK(rereadEntry != nullptr);
  if (rereadEntry != nullptr) CHECK(rereadEntry->currentHp == 30);
}

// Round 3 audit bug 2.7: one invalid entry in a batch must not discard the
// other, otherwise-good entries.
void upsertEntriesAppliesValidEntriesAndSkipsAnInvalidOne() {
  Storage.clear();
  pokemon::PokemonBattleStore store;

  pokemon::BattleRecordEntry invalid = makeEntry(0, 5);  // recordId 0 is never valid
  std::array<pokemon::BattleRecordEntry, 3> batch = {makeEntry(7, 20), invalid, makeEntry(3, 15)};
  CHECK(store.upsertEntries(batch));

  const pokemon::BattleRecordEntry* seven = store.findEntry(7);
  CHECK(seven != nullptr);
  if (seven != nullptr) CHECK(seven->currentHp == 20);
  const pokemon::BattleRecordEntry* three = store.findEntry(3);
  CHECK(three != nullptr);
  if (three != nullptr) CHECK(three->currentHp == 15);
  CHECK(store.findEntry(0) == nullptr);

  pokemon::PokemonBattleStore reader;
  CHECK(reader.findEntry(7) != nullptr);
  CHECK(reader.findEntry(3) != nullptr);
}

void upsertEntriesFailsWhenEveryEntryIsInvalid() {
  Storage.clear();
  pokemon::PokemonBattleStore store;
  std::array<pokemon::BattleRecordEntry, 2> batch = {makeEntry(0, 5), makeEntry(0, 6)};
  CHECK(!store.upsertEntries(batch));
  CHECK(!Storage.exists(STORE_PATH_A));
}

}  // namespace

// See PokemonIvEvStoreTest.cpp's matching test: a failed read must not be
// mistaken for an empty store that the next write then replaces.
void aTransientReadFailureNeverOverwritesTheRealFile() {
  Storage.clear();
  {
    pokemon::PokemonBattleStore writer;
    CHECK(writer.upsertEntry(makeEntry(7, 20)));
  }
  pokemon::PokemonBattleStore store;
  Storage.setFailRead(true);
  CHECK(store.findEntry(7) == nullptr);
  CHECK(!store.upsertEntry(makeEntry(9, 15)));
  CHECK(!store.removeEntry(7));
  Storage.setFailRead(false);

  const pokemon::BattleRecordEntry* recovered = store.findEntry(7);
  CHECK(recovered != nullptr);
  if (recovered != nullptr) CHECK(recovered->currentHp == 20);
  CHECK(store.upsertEntry(makeEntry(9, 15)));
  pokemon::PokemonBattleStore reader;
  CHECK(reader.findEntry(7) != nullptr);
  CHECK(reader.findEntry(9) != nullptr);
}

// Reset is a deliberate wipe: an unreadable store must not leave the old game's
// data behind, and the files are deleted rather than written over blind.
void resetStillWipesAStoreThatCannotBeRead() {
  Storage.clear();
  {
    pokemon::PokemonBattleStore writer;
    CHECK(writer.upsertEntry(makeEntry(7, 20)));
    CHECK(writer.upsertEntry(makeEntry(9, 15)));  // second write -> both slots exist
  }
  CHECK(Storage.exists("/.crosspoint/pokemon-battle-a.bin"));
  CHECK(Storage.exists("/.crosspoint/pokemon-battle-b.bin"));
  pokemon::PokemonBattleStore store;
  Storage.setFailRead(true);
  CHECK(store.reset());
  Storage.setFailRead(false);
  CHECK(!Storage.exists("/.crosspoint/pokemon-battle-a.bin"));
  CHECK(!Storage.exists("/.crosspoint/pokemon-battle-b.bin"));

  pokemon::PokemonBattleStore reader;
  CHECK(reader.findEntry(7) == nullptr);
  CHECK(reader.findEntry(9) == nullptr);
  CHECK(store.upsertEntry(makeEntry(7, 20)));  // usable again straight after the wipe
}

int main() {
  missingFilesLeaveAnEmptyStore();
  upsertPersistsAndAFreshInstanceReadsItBack();
  upsertReplacesAnExistingEntryInPlace();
  removeEntryPersistsAndLeavesOthersIntact();
  successiveWritesAlternateBetweenTheTwoFiles();
  corruptingOneFileFallsBackToTheOtherWithoutDataLoss();
  bothFilesCorruptStillLeavesAnEmptyStoreNeverAnError();
  invalidUpsertLeavesThePreviouslyWrittenFilesUntouched();
  aWriteFailureLeavesTheActiveFileAndInMemoryStateUnchanged();
  preExistingLegacySingleFileIsMigratedOnFirstLoad();
  upsertEntriesAppliesValidEntriesAndSkipsAnInvalidOne();
  upsertEntriesFailsWhenEveryEntryIsInvalid();
  aTransientReadFailureNeverOverwritesTheRealFile();
  resetStillWipesAStoreThatCannotBeRead();
  return failures == 0 ? 0 : 1;
}
