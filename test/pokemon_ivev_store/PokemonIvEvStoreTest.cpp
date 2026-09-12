#include <HalStorage.h>

#include <cstdio>

#include "PokemonIvEvStoreCodec.h"
#include "pokemon/PokemonIvEvStore.h"

namespace {

int failures = 0;

#define CHECK(condition)                                                                \
  do {                                                                                  \
    if (!(condition)) {                                                                 \
      std::fprintf(stderr, "%s:%d check failed: %s\n", __FILE__, __LINE__, #condition); \
      ++failures;                                                                       \
    }                                                                                   \
  } while (false)

constexpr const char* STORE_PATH_A = "/.crosspoint/pokemon-ivev-a.bin";
constexpr const char* STORE_PATH_B = "/.crosspoint/pokemon-ivev-b.bin";

pokemon::IvEvEntry makeEntry(const uint32_t recordId, const uint8_t hpEv) {
  pokemon::IvEvEntry entry{};
  entry.recordId = recordId;
  entry.iv = {12, 3, 15, 0, 8};
  entry.ev = {hpEv, 0, 0, 0, 0};
  return entry;
}

void missingFilesLeaveAnEmptyStore() {
  Storage.clear();
  pokemon::PokemonIvEvStore store;
  CHECK(store.findEntry(1) == nullptr);
  CHECK(!Storage.exists(STORE_PATH_A));
  CHECK(!Storage.exists(STORE_PATH_B));
}

void upsertPersistsAndAFreshInstanceReadsItBack() {
  Storage.clear();
  {
    pokemon::PokemonIvEvStore writer;
    CHECK(writer.upsertEntry(makeEntry(7, 20)));
    CHECK(writer.upsertEntry(makeEntry(3, 15)));
  }
  // First write with no prior valid slot always lands on A.
  CHECK(Storage.exists(STORE_PATH_A));

  pokemon::PokemonIvEvStore reader;
  const pokemon::IvEvEntry* seven = reader.findEntry(7);
  CHECK(seven != nullptr);
  if (seven != nullptr) CHECK(seven->ev[0] == 20);
  const pokemon::IvEvEntry* three = reader.findEntry(3);
  CHECK(three != nullptr);
  if (three != nullptr) CHECK(three->ev[0] == 15);
  CHECK(reader.findEntry(999) == nullptr);
}

void upsertReplacesAnExistingEntryInPlace() {
  Storage.clear();
  pokemon::PokemonIvEvStore store;
  CHECK(store.upsertEntry(makeEntry(7, 20)));
  CHECK(store.upsertEntry(makeEntry(7, 5)));  // same recordId, different EV

  pokemon::PokemonIvEvStore reader;
  const pokemon::IvEvEntry* entry = reader.findEntry(7);
  CHECK(entry != nullptr);
  if (entry != nullptr) CHECK(entry->ev[0] == 5);
}

void successiveWritesAlternateBetweenTheTwoFiles() {
  Storage.clear();
  pokemon::PokemonIvEvStore store;
  CHECK(store.upsertEntry(makeEntry(7, 20)));  // -> A, sequence 1
  CHECK(Storage.exists(STORE_PATH_A));
  CHECK(!Storage.exists(STORE_PATH_B));

  CHECK(store.upsertEntry(makeEntry(7, 19)));  // -> B, sequence 2
  CHECK(Storage.exists(STORE_PATH_B));

  CHECK(store.upsertEntry(makeEntry(7, 18)));  // -> A again, sequence 3
  pokemon::PokemonIvEvStore reader;
  const pokemon::IvEvEntry* entry = reader.findEntry(7);
  CHECK(entry != nullptr);
  if (entry != nullptr) CHECK(entry->ev[0] == 18);
}

void corruptingOneFileFallsBackToTheOtherWithoutDataLoss() {
  Storage.clear();
  pokemon::PokemonIvEvStore store;
  CHECK(store.upsertEntry(makeEntry(7, 20)));  // -> A, sequence 1
  CHECK(store.upsertEntry(makeEntry(7, 5)));   // -> B, sequence 2 (now newest)
  Storage.setByte(STORE_PATH_B, 5, 0xFFU);     // corrupt the newer file's entryCount low byte

  // The reader must fall back to A (still valid, one write behind) instead
  // of losing the IV/EV data entirely.
  pokemon::PokemonIvEvStore reader;
  const pokemon::IvEvEntry* entry = reader.findEntry(7);
  CHECK(entry != nullptr);
  if (entry != nullptr) CHECK(entry->ev[0] == 20);
}

void bothFilesCorruptStillLeavesAnEmptyStoreNeverAnError() {
  Storage.clear();
  {
    pokemon::PokemonIvEvStore writer;
    CHECK(writer.upsertEntry(makeEntry(7, 20)));
    CHECK(writer.upsertEntry(makeEntry(7, 5)));
  }
  Storage.setByte(STORE_PATH_A, 5, 0xFFU);
  Storage.setByte(STORE_PATH_B, 5, 0xFFU);

  pokemon::PokemonIvEvStore reader;
  CHECK(reader.findEntry(7) == nullptr);  // discarded, not crashed and not silently wrong
}

void invalidUpsertLeavesThePreviouslyWrittenFilesUntouched() {
  Storage.clear();
  pokemon::PokemonIvEvStore store;
  CHECK(store.upsertEntry(makeEntry(7, 20)));

  pokemon::IvEvEntry invalid = makeEntry(0, 5);  // recordId 0 is never valid
  CHECK(!store.upsertEntry(invalid));

  pokemon::PokemonIvEvStore reader;
  const pokemon::IvEvEntry* seven = reader.findEntry(7);
  CHECK(seven != nullptr);
  if (seven != nullptr) CHECK(seven->ev[0] == 20);
}

void aWriteFailureLeavesTheActiveFileAndInMemoryStateUnchanged() {
  Storage.clear();
  pokemon::PokemonIvEvStore store;
  CHECK(store.upsertEntry(makeEntry(7, 20)));  // -> A, sequence 1, active
  CHECK(store.findEntry(7)->ev[0] == 20);

  Storage.setFailWritableOpen(true);  // the next write to the inactive file (B) fails to open
  CHECK(!store.upsertEntry(makeEntry(7, 1)));
  Storage.setFailWritableOpen(false);

  CHECK(store.findEntry(7)->ev[0] == 20);  // in-memory state rolled back to what A still holds
  pokemon::PokemonIvEvStore reader;
  CHECK(reader.findEntry(7)->ev[0] == 20);  // A (the active file) was never touched
  CHECK(!Storage.exists(STORE_PATH_B));
}

void resetClearsBothTheStoreAndAFreshReader() {
  Storage.clear();
  pokemon::PokemonIvEvStore store;
  CHECK(store.upsertEntry(makeEntry(7, 20)));
  CHECK(store.reset());
  CHECK(store.findEntry(7) == nullptr);

  pokemon::PokemonIvEvStore reader;
  CHECK(reader.findEntry(7) == nullptr);
}

}  // namespace

int main() {
  missingFilesLeaveAnEmptyStore();
  upsertPersistsAndAFreshInstanceReadsItBack();
  upsertReplacesAnExistingEntryInPlace();
  successiveWritesAlternateBetweenTheTwoFiles();
  corruptingOneFileFallsBackToTheOtherWithoutDataLoss();
  bothFilesCorruptStillLeavesAnEmptyStoreNeverAnError();
  invalidUpsertLeavesThePreviouslyWrittenFilesUntouched();
  aWriteFailureLeavesTheActiveFileAndInMemoryStateUnchanged();
  resetClearsBothTheStoreAndAFreshReader();
  return failures == 0 ? 0 : 1;
}
