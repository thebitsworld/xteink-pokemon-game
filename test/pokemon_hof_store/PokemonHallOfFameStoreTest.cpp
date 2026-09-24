#include <HalStorage.h>

#include <cstdio>

#include "PokemonHallOfFameCodec.h"
#include "pokemon/PokemonHallOfFameStore.h"

namespace {

int failures = 0;

#define CHECK(condition)                                                                \
  do {                                                                                  \
    if (!(condition)) {                                                                 \
      std::fprintf(stderr, "%s:%d check failed: %s\n", __FILE__, __LINE__, #condition); \
      ++failures;                                                                       \
    }                                                                                   \
  } while (false)

constexpr const char* STORE_PATH_A = "/.crosspoint/pokemon-hof-a.bin";
constexpr const char* STORE_PATH_B = "/.crosspoint/pokemon-hof-b.bin";

pokemon::HallOfFameState makeState(const uint32_t minutes, const uint16_t firstSpecies) {
  pokemon::HallOfFameState state{};
  state.lifetimeMinutesAtClear = minutes;
  state.members[0].speciesId = firstSpecies;
  state.members[0].level = 55;
  state.members[0].gender = pokemon::Gender::Male;
  return state;
}

void missingFilesLeaveNoEntry() {
  Storage.clear();
  pokemon::PokemonHallOfFameStore store;
  CHECK(!store.hasEntry());
  CHECK(store.entry() == nullptr);
  CHECK(!Storage.exists(STORE_PATH_A));
  CHECK(!Storage.exists(STORE_PATH_B));
}

void captureOncePersistsAndAFreshInstanceReadsItBack() {
  Storage.clear();
  {
    pokemon::PokemonHallOfFameStore writer;
    CHECK(writer.captureOnce(makeState(600, 6)));
  }
  CHECK(Storage.exists(STORE_PATH_A));  // first write with no prior valid slot always lands on A

  pokemon::PokemonHallOfFameStore reader;
  CHECK(reader.hasEntry());
  const pokemon::HallOfFameState* entry = reader.entry();
  CHECK(entry != nullptr);
  if (entry != nullptr) {
    CHECK(entry->cleared);
    CHECK(entry->lifetimeMinutesAtClear == 600);
    CHECK(entry->members[0].speciesId == 6);
  }
}

void captureOnceRefusesToOverwriteAnExistingEntry() {
  Storage.clear();
  pokemon::PokemonHallOfFameStore store;
  CHECK(store.captureOnce(makeState(600, 6)));
  CHECK(!store.captureOnce(makeState(1200, 150)));  // must not overwrite - the Champion can't be re-fought

  const pokemon::HallOfFameState* entry = store.entry();
  CHECK(entry != nullptr);
  if (entry != nullptr) CHECK(entry->members[0].speciesId == 6);  // unchanged
}

void resetThenCaptureOnceAlternatesFilesAndPersistsTheNewSnapshot() {
  Storage.clear();
  pokemon::PokemonHallOfFameStore store;
  CHECK(store.captureOnce(makeState(600, 6)));  // -> A
  CHECK(store.hasEntry());                      // sanity: has an entry right now
  CHECK(store.reset());                         // -> B, cleared back to false
  CHECK(!store.hasEntry());

  CHECK(store.captureOnce(makeState(900, 25)));  // -> A again, a genuinely new snapshot
  pokemon::PokemonHallOfFameStore reader;
  CHECK(reader.hasEntry());
  const pokemon::HallOfFameState* entry = reader.entry();
  CHECK(entry != nullptr);
  if (entry != nullptr) {
    CHECK(entry->lifetimeMinutesAtClear == 900);
    CHECK(entry->members[0].speciesId == 25);
  }
}

void bothFilesCorruptStillLeavesNoEntryNeverAnError() {
  Storage.clear();
  {
    pokemon::PokemonHallOfFameStore writer;
    CHECK(writer.captureOnce(makeState(600, 6)));
  }
  Storage.setByte(STORE_PATH_A, 4, 0xFFU);  // corrupt the version byte

  pokemon::PokemonHallOfFameStore reader;
  CHECK(!reader.hasEntry());  // discarded, not crashed and not silently wrong
}

void aWriteFailureLeavesTheStoreUnchanged() {
  Storage.clear();
  pokemon::PokemonHallOfFameStore store;
  CHECK(store.captureOnce(makeState(600, 6)));  // -> A, active

  Storage.setFailWritableOpen(true);  // the next write to the inactive file (B) fails to open
  CHECK(!store.reset());
  Storage.setFailWritableOpen(false);

  CHECK(store.hasEntry());  // still the original snapshot, never cleared by the failed reset
  const pokemon::HallOfFameState* entry = store.entry();
  CHECK(entry != nullptr);
  if (entry != nullptr) CHECK(entry->members[0].speciesId == 6);
}

}  // namespace

// A failed read must not look like "no Hall of Fame yet", or captureOnce()'s
// never-overwrite guarantee is void.
void aTransientReadFailureNeverOverwritesTheRecordedHallOfFame() {
  Storage.clear();
  {
    pokemon::PokemonHallOfFameStore writer;
    CHECK(writer.captureOnce(makeState(600, 6)));
  }
  pokemon::PokemonHallOfFameStore store;
  Storage.setFailRead(true);
  CHECK(!store.hasEntry());
  CHECK(!store.captureOnce(makeState(999, 25)));
  Storage.setFailRead(false);

  CHECK(store.hasEntry());
  const pokemon::HallOfFameState* entry = store.entry();
  CHECK(entry != nullptr);
  if (entry != nullptr) CHECK(entry->lifetimeMinutesAtClear == 600);
  pokemon::PokemonHallOfFameStore reader;
  CHECK(reader.hasEntry());
}

// Reset is a deliberate wipe even when the old Hall of Fame cannot be read.
void resetStillWipesAStoreThatCannotBeRead() {
  Storage.clear();
  {
    pokemon::PokemonHallOfFameStore writer;
    CHECK(writer.captureOnce(makeState(600, 6)));
  }
  pokemon::PokemonHallOfFameStore store;
  Storage.setFailRead(true);
  CHECK(store.reset());
  Storage.setFailRead(false);

  pokemon::PokemonHallOfFameStore reader;
  CHECK(!reader.hasEntry());
  CHECK(store.captureOnce(makeState(700, 25)));  // a new run can be recorded again
}

int main() {
  missingFilesLeaveNoEntry();
  captureOncePersistsAndAFreshInstanceReadsItBack();
  captureOnceRefusesToOverwriteAnExistingEntry();
  resetThenCaptureOnceAlternatesFilesAndPersistsTheNewSnapshot();
  bothFilesCorruptStillLeavesNoEntryNeverAnError();
  aWriteFailureLeavesTheStoreUnchanged();
  aTransientReadFailureNeverOverwritesTheRecordedHallOfFame();
  resetStillWipesAStoreThatCannotBeRead();
  return failures == 0 ? 0 : 1;
}
