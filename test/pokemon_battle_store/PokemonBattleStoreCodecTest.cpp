#include <cstdio>

#include "PokemonBattleStoreCodec.h"

namespace {

using pokemon::Ailment;
using pokemon::BattleRecordEntry;
using pokemon::BattleStoreState;

int failures = 0;

#define CHECK(condition)                                                                \
  do {                                                                                  \
    if (!(condition)) {                                                                 \
      std::fprintf(stderr, "%s:%d check failed: %s\n", __FILE__, __LINE__, #condition); \
      ++failures;                                                                       \
    }                                                                                   \
  } while (false)

BattleRecordEntry makeEntry(const uint32_t recordId) {
  BattleRecordEntry entry{};
  entry.recordId = recordId;
  entry.moves = {33, 45, 0, 0};
  entry.pp = {35, 40, 0, 0};
  entry.currentHp = 20;
  entry.status = Ailment::None;
  entry.statusTurns = 0;
  return entry;
}

void validateRejectsGapsAndOutOfRangeMoveIds() {
  BattleRecordEntry entry = makeEntry(1);
  CHECK(pokemon::validateBattleRecordEntry(entry));

  BattleRecordEntry zeroRecordId = entry;
  zeroRecordId.recordId = 0;
  CHECK(!pokemon::validateBattleRecordEntry(zeroRecordId));

  BattleRecordEntry gappedMoves = entry;
  gappedMoves.moves = {33, 0, 45, 0};  // a move after an empty slot
  CHECK(!pokemon::validateBattleRecordEntry(gappedMoves));

  BattleRecordEntry ppOnEmptySlot = entry;
  ppOnEmptySlot.pp[2] = 5;  // moves[2] is 0 (empty) but pp[2] is non-zero
  CHECK(!pokemon::validateBattleRecordEntry(ppOnEmptySlot));

  BattleRecordEntry outOfRangeMove = entry;
  outOfRangeMove.moves[0] = static_cast<uint8_t>(pokemon::POKEMON_MOVE_ID_MAX + 1U);
  CHECK(!pokemon::validateBattleRecordEntry(outOfRangeMove));

  BattleRecordEntry allStatusOnLiveCombatant = entry;
  allStatusOnLiveCombatant.status = Ailment::All;  // item-cure sentinel, never a real status
  CHECK(!pokemon::validateBattleRecordEntry(allStatusOnLiveCombatant));

  BattleRecordEntry paralysisWithTurns = entry;
  paralysisWithTurns.status = Ailment::Paralysis;
  paralysisWithTurns.statusTurns = 2;  // only Sleep/Confusion carry a turn counter
  CHECK(!pokemon::validateBattleRecordEntry(paralysisWithTurns));

  BattleRecordEntry sleepWithTurns = entry;
  sleepWithTurns.status = Ailment::Sleep;
  sleepWithTurns.statusTurns = 2;
  CHECK(pokemon::validateBattleRecordEntry(sleepWithTurns));
}

void entryRoundTripsThroughEncodeDecode() {
  BattleRecordEntry entry = makeEntry(0x11223344U);
  entry.status = Ailment::Confusion;
  entry.statusTurns = 3;

  pokemon::BattleEntryBytes bytes{};
  CHECK(pokemon::encodeBattleRecordEntry(entry, bytes));
  CHECK(bytes[0] == 0x44 && bytes[1] == 0x33 && bytes[2] == 0x22 && bytes[3] == 0x11);
  CHECK(bytes[4] == 33 && bytes[5] == 45 && bytes[6] == 0 && bytes[7] == 0);
  CHECK(bytes[14] == static_cast<uint8_t>(Ailment::Confusion));
  CHECK(bytes[15] == 3);

  BattleRecordEntry decoded{};
  CHECK(pokemon::decodeBattleRecordEntry(bytes, decoded));
  CHECK(decoded == entry);
}

void invalidStatusByteFailsToDecode() {
  BattleRecordEntry entry = makeEntry(1);
  pokemon::BattleEntryBytes bytes{};
  CHECK(pokemon::encodeBattleRecordEntry(entry, bytes));
  bytes[14] = 200;  // far past Ailment::All

  BattleRecordEntry output{};
  CHECK(!pokemon::decodeBattleRecordEntry(bytes, output));
}

void upsertKeepsAscendingOrderAndReplacesInPlace() {
  BattleStoreState state{};
  CHECK(pokemon::upsertBattleEntry(state, makeEntry(30)));
  CHECK(pokemon::upsertBattleEntry(state, makeEntry(10)));
  CHECK(pokemon::upsertBattleEntry(state, makeEntry(20)));
  CHECK(pokemon::battleEntryCount(state) == 3);
  CHECK(state.entries[0].recordId == 10);
  CHECK(state.entries[1].recordId == 20);
  CHECK(state.entries[2].recordId == 30);

  BattleRecordEntry replacement = makeEntry(20);
  replacement.currentHp = 1;
  CHECK(pokemon::upsertBattleEntry(state, replacement));
  CHECK(pokemon::battleEntryCount(state) == 3);  // replace, not append
  CHECK(pokemon::findBattleEntry(state, 20)->currentHp == 1);

  CHECK(pokemon::removeBattleEntry(state, 20));
  CHECK(pokemon::battleEntryCount(state) == 2);
  CHECK(state.entries[0].recordId == 10);
  CHECK(state.entries[1].recordId == 30);
  CHECK(pokemon::findBattleEntry(state, 20) == nullptr);
  CHECK(!pokemon::removeBattleEntry(state, 999));
}

void upsertFailsPastCapacityForAnUnseenRecordId() {
  BattleStoreState state{};
  for (uint32_t id = 1; id <= pokemon::POKEMON_BATTLE_MAX_ENTRIES; ++id) {
    CHECK(pokemon::upsertBattleEntry(state, makeEntry(id)));
  }
  CHECK(pokemon::battleEntryCount(state) == pokemon::POKEMON_BATTLE_MAX_ENTRIES);
  CHECK(!pokemon::upsertBattleEntry(state, makeEntry(999)));
  // Replacing an existing one at full capacity must still work.
  BattleRecordEntry replacement = makeEntry(3);
  replacement.currentHp = 9;
  CHECK(pokemon::upsertBattleEntry(state, replacement));
  CHECK(pokemon::findBattleEntry(state, 3)->currentHp == 9);
}

void fileRoundTripsAndDetectsCorruption() {
  BattleStoreState state{};
  CHECK(pokemon::upsertBattleEntry(state, makeEntry(5)));
  CHECK(pokemon::upsertBattleEntry(state, makeEntry(12)));

  pokemon::BattleStoreFileBytes bytes{};
  size_t size = 0;
  CHECK(pokemon::encodeBattleStoreFile(state, bytes, size));
  CHECK(size == 2 * pokemon::POKEMON_BATTLE_ENTRY_BYTES + pokemon::POKEMON_BATTLE_FILE_CRC_BYTES);

  BattleStoreState decoded{};
  CHECK(pokemon::decodeBattleStoreFile(bytes.data(), size, decoded));
  CHECK(decoded == state);

  // Flip a byte inside the first entry: CRC must catch it.
  pokemon::BattleStoreFileBytes corrupted = bytes;
  corrupted[0] ^= 0xFFU;
  BattleStoreState corruptOutput{};
  CHECK(!pokemon::decodeBattleStoreFile(corrupted.data(), size, corruptOutput));

  // A size that isn't entryCount*16+4 is rejected outright.
  CHECK(!pokemon::decodeBattleStoreFile(bytes.data(), size - 1, decoded));
  CHECK(!pokemon::decodeBattleStoreFile(bytes.data(), 0, decoded));
}

void emptyStateEncodesToJustTheCrc() {
  BattleStoreState state{};
  pokemon::BattleStoreFileBytes bytes{};
  size_t size = 0;
  CHECK(pokemon::encodeBattleStoreFile(state, bytes, size));
  CHECK(size == pokemon::POKEMON_BATTLE_FILE_CRC_BYTES);

  BattleStoreState decoded{};
  decoded.entries[0] = makeEntry(1);  // prove decode actually clears this, not just leaves it
  CHECK(pokemon::decodeBattleStoreFile(bytes.data(), size, decoded));
  CHECK(pokemon::battleEntryCount(decoded) == 0);
}

}  // namespace

int main() {
  validateRejectsGapsAndOutOfRangeMoveIds();
  entryRoundTripsThroughEncodeDecode();
  invalidStatusByteFailsToDecode();
  upsertKeepsAscendingOrderAndReplacesInPlace();
  upsertFailsPastCapacityForAnUnseenRecordId();
  fileRoundTripsAndDetectsCorruption();
  emptyStateEncodesToJustTheCrc();
  return failures == 0 ? 0 : 1;
}
