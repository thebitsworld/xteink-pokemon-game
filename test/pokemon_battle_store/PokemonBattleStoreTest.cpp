#include <HalStorage.h>

#include <cstdio>

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

constexpr const char* BATTLE_STORE_PATH = "/.crosspoint/pokemon-battle.bin";

pokemon::BattleRecordEntry makeEntry(const uint32_t recordId, const uint16_t hp) {
  pokemon::BattleRecordEntry entry{};
  entry.recordId = recordId;
  entry.moves = {33, 45, 0, 0};
  entry.pp = {35, 40, 0, 0};
  entry.currentHp = hp;
  entry.status = pokemon::Ailment::None;
  return entry;
}

void missingFileLeavesAnEmptyStore() {
  Storage.clear();
  pokemon::PokemonBattleStore store;
  CHECK(store.findEntry(1) == nullptr);
  CHECK(!Storage.exists(BATTLE_STORE_PATH));
}

void upsertPersistsAndAFreshInstanceReadsItBack() {
  Storage.clear();
  {
    pokemon::PokemonBattleStore writer;
    CHECK(writer.upsertEntry(makeEntry(7, 20)));
    CHECK(writer.upsertEntry(makeEntry(3, 15)));
  }
  CHECK(Storage.exists(BATTLE_STORE_PATH));

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

void corruptedFileIsTreatedAsEmptyNeverAsAnError() {
  Storage.clear();
  {
    pokemon::PokemonBattleStore writer;
    CHECK(writer.upsertEntry(makeEntry(7, 20)));
  }
  CHECK(Storage.exists(BATTLE_STORE_PATH));
  Storage.setByte(BATTLE_STORE_PATH, 0, 0xFF);  // flip a byte inside the entry; CRC must catch it

  pokemon::PokemonBattleStore reader;
  CHECK(reader.findEntry(7) == nullptr);  // discarded, not crashed and not silently wrong
}

void invalidUpsertLeavesThePreviouslyWrittenFileUntouched() {
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

}  // namespace

int main() {
  missingFileLeavesAnEmptyStore();
  upsertPersistsAndAFreshInstanceReadsItBack();
  upsertReplacesAnExistingEntryInPlace();
  removeEntryPersistsAndLeavesOthersIntact();
  corruptedFileIsTreatedAsEmptyNeverAsAnError();
  invalidUpsertLeavesThePreviouslyWrittenFileUntouched();
  return failures == 0 ? 0 : 1;
}
