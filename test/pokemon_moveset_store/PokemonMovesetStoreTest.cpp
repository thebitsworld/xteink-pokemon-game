#include <HalStorage.h>

#include <cstdio>
#include <vector>

#include "PokemonMovesetStoreCodec.h"
#include "pokemon/PokemonMovesetStore.h"

namespace {

int failures = 0;

#define CHECK(condition)                                                                \
  do {                                                                                  \
    if (!(condition)) {                                                                 \
      std::fprintf(stderr, "%s:%d check failed: %s\n", __FILE__, __LINE__, #condition); \
      ++failures;                                                                       \
    }                                                                                   \
  } while (false)

constexpr const char* STORE_PATH_A = "/.crosspoint/pokemon-moves-a.bin";
constexpr const char* STORE_PATH_B = "/.crosspoint/pokemon-moves-b.bin";

pokemon::MovesetEntry makeEntry(const uint32_t recordId, const uint8_t firstMove) {
  pokemon::MovesetEntry entry{};
  entry.recordId = recordId;
  entry.moves = {firstMove, 45, 0, 0};
  entry.ppUp = {2, 0, 0, 0};
  return entry;
}

void missingFilesLeaveAnEmptyStore() {
  Storage.clear();
  pokemon::PokemonMovesetStore store;
  CHECK(store.findEntry(1) == nullptr);
  CHECK(!Storage.exists(STORE_PATH_A));
  CHECK(!Storage.exists(STORE_PATH_B));
}

void upsertPersistsAndAFreshInstanceReadsItBack() {
  Storage.clear();
  {
    pokemon::PokemonMovesetStore writer;
    CHECK(writer.upsertEntry(makeEntry(7, 84)));
    CHECK(writer.upsertEntry(makeEntry(3, 33)));
  }
  CHECK(Storage.exists(STORE_PATH_A));  // the first write with no prior valid slot lands on A

  pokemon::PokemonMovesetStore reader;
  const pokemon::MovesetEntry* seven = reader.findEntry(7);
  CHECK(seven != nullptr);
  if (seven != nullptr) CHECK(seven->moves[0] == 84 && seven->ppUp[0] == 2);
  const pokemon::MovesetEntry* three = reader.findEntry(3);
  CHECK(three != nullptr);
  if (three != nullptr) CHECK(three->moves[0] == 33);
  CHECK(reader.findEntry(999) == nullptr);
}

void writesAlternateBetweenTheTwoFilesAndTheNewestWins() {
  Storage.clear();
  pokemon::PokemonMovesetStore store;
  CHECK(store.upsertEntry(makeEntry(1, 10)));  // -> A
  CHECK(store.upsertEntry(makeEntry(1, 11)));  // -> B
  CHECK(Storage.exists(STORE_PATH_A) && Storage.exists(STORE_PATH_B));
  CHECK(store.upsertEntry(makeEntry(1, 12)));  // -> A again

  pokemon::PokemonMovesetStore reader;
  const pokemon::MovesetEntry* entry = reader.findEntry(1);
  CHECK(entry != nullptr);
  if (entry != nullptr) CHECK(entry->moves[0] == 12);
}

void aCorruptNewestSlotFallsBackToTheOlderOne() {
  Storage.clear();
  {
    pokemon::PokemonMovesetStore store;
    CHECK(store.upsertEntry(makeEntry(1, 10)));  // A
    CHECK(store.upsertEntry(makeEntry(1, 11)));  // B (newest)
  }
  Storage.setByte(STORE_PATH_B, 12, 0xFF);  // corrupt B's payload

  pokemon::PokemonMovesetStore reader;
  const pokemon::MovesetEntry* entry = reader.findEntry(1);
  CHECK(entry != nullptr);
  if (entry != nullptr) CHECK(entry->moves[0] == 10);
}

void removeAndResetPersist() {
  Storage.clear();
  pokemon::PokemonMovesetStore store;
  CHECK(store.upsertEntry(makeEntry(1, 10)));
  CHECK(store.upsertEntry(makeEntry(2, 20)));
  CHECK(store.removeEntry(1));
  CHECK(!store.removeEntry(1));  // already gone: no write, reports false

  pokemon::PokemonMovesetStore afterRemove;
  CHECK(afterRemove.findEntry(1) == nullptr);
  CHECK(afterRemove.findEntry(2) != nullptr);

  CHECK(store.reset());
  pokemon::PokemonMovesetStore afterReset;
  CHECK(afterReset.findEntry(2) == nullptr);
}

void upsertEntriesBatchesOneWriteAndSkipsInvalidEntries() {
  Storage.clear();
  pokemon::PokemonMovesetStore store;
  pokemon::MovesetEntry invalid = makeEntry(9, 10);
  invalid.ppUp[0] = 7;  // out of range
  const std::vector<pokemon::MovesetEntry> batch{makeEntry(1, 10), invalid, makeEntry(3, 30)};
  CHECK(store.upsertEntries(batch));

  pokemon::PokemonMovesetStore reader;
  CHECK(reader.findEntry(1) != nullptr);
  CHECK(reader.findEntry(3) != nullptr);
  CHECK(reader.findEntry(9) == nullptr);  // skipped, didn't abort the rest

  const std::vector<pokemon::MovesetEntry> onlyInvalid{invalid};
  CHECK(!store.upsertEntries(onlyInvalid));  // nothing applicable -> nothing written
}

void aFailedValidationLeavesTheStoreUntouched() {
  Storage.clear();
  pokemon::PokemonMovesetStore store;
  CHECK(store.upsertEntry(makeEntry(1, 10)));
  pokemon::MovesetEntry invalid = makeEntry(1, 10);
  invalid.ppUp[0] = 9;
  CHECK(!store.upsertEntry(invalid));
  const pokemon::MovesetEntry* entry = store.findEntry(1);
  CHECK(entry != nullptr && entry->ppUp[0] == 2);
}

}  // namespace

// A read that fails outright must not look like "no saved movesets" - the next
// write would then replace the real file (and every boxed Pokemon's TM moves).
void aTransientReadFailureNeverOverwritesTheRealFile() {
  Storage.clear();
  {
    pokemon::PokemonMovesetStore writer;
    CHECK(writer.upsertEntry(makeEntry(7, 84)));
  }
  pokemon::PokemonMovesetStore store;
  Storage.setFailRead(true);
  CHECK(store.findEntry(7) == nullptr);
  CHECK(!store.upsertEntry(makeEntry(9, 33)));
  CHECK(!store.removeEntry(7));
  Storage.setFailRead(false);

  CHECK(store.findEntry(7) != nullptr);
  CHECK(store.upsertEntry(makeEntry(9, 33)));
  pokemon::PokemonMovesetStore reader;
  CHECK(reader.findEntry(7) != nullptr);
  CHECK(reader.findEntry(9) != nullptr);
}

// Reset is a deliberate wipe: an unreadable store must not leave the old game's
// data behind, and the files are deleted rather than written over blind.
void resetStillWipesAStoreThatCannotBeRead() {
  Storage.clear();
  {
    pokemon::PokemonMovesetStore writer;
    CHECK(writer.upsertEntry(makeEntry(7, 84)));
    CHECK(writer.upsertEntry(makeEntry(9, 33)));  // second write -> both slots exist
  }
  CHECK(Storage.exists("/.crosspoint/pokemon-moves-a.bin"));
  CHECK(Storage.exists("/.crosspoint/pokemon-moves-b.bin"));
  pokemon::PokemonMovesetStore store;
  Storage.setFailRead(true);
  CHECK(store.reset());
  Storage.setFailRead(false);
  CHECK(!Storage.exists("/.crosspoint/pokemon-moves-a.bin"));
  CHECK(!Storage.exists("/.crosspoint/pokemon-moves-b.bin"));

  pokemon::PokemonMovesetStore reader;
  CHECK(reader.findEntry(7) == nullptr);
  CHECK(reader.findEntry(9) == nullptr);
  CHECK(store.upsertEntry(makeEntry(7, 84)));  // usable again straight after the wipe
}

int main() {
  missingFilesLeaveAnEmptyStore();
  upsertPersistsAndAFreshInstanceReadsItBack();
  writesAlternateBetweenTheTwoFilesAndTheNewestWins();
  aCorruptNewestSlotFallsBackToTheOlderOne();
  removeAndResetPersist();
  upsertEntriesBatchesOneWriteAndSkipsInvalidEntries();
  aFailedValidationLeavesTheStoreUntouched();
  aTransientReadFailureNeverOverwritesTheRealFile();
  resetStillWipesAStoreThatCannotBeRead();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  return 0;
}
