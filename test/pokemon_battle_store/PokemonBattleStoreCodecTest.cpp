#include <cstdio>
#include <cstring>
#include <vector>

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

void fileRoundTripsCarriesSequenceAndDetectsCorruption() {
  BattleStoreState state{};
  CHECK(pokemon::upsertBattleEntry(state, makeEntry(5)));
  CHECK(pokemon::upsertBattleEntry(state, makeEntry(12)));

  pokemon::BattleStoreFileBytes bytes{};
  size_t size = 0;
  CHECK(pokemon::encodeBattleStoreFile(state, 7, bytes, size));
  CHECK(size == pokemon::POKEMON_BATTLE_HEADER_BYTES + 2 * pokemon::POKEMON_BATTLE_ENTRY_BYTES +
                    pokemon::POKEMON_BATTLE_FILE_CRC_BYTES);
  CHECK(bytes[0] == 'P' && bytes[1] == 'K' && bytes[2] == 'B' && bytes[3] == 'T');
  CHECK(bytes[4] == pokemon::POKEMON_BATTLE_STORE_VERSION);
  CHECK(bytes[5] == 2);  // entryCount

  BattleStoreState decoded{};
  uint32_t sequence = 0;
  CHECK(pokemon::decodeBattleStoreFile(bytes.data(), size, decoded, sequence));
  CHECK(decoded == state);
  CHECK(sequence == 7);

  // Flip a byte inside the header: CRC must catch it.
  pokemon::BattleStoreFileBytes corrupted = bytes;
  corrupted[5] ^= 0xFFU;
  BattleStoreState corruptOutput{};
  uint32_t corruptSequence = 0;
  CHECK(!pokemon::decodeBattleStoreFile(corrupted.data(), size, corruptOutput, corruptSequence));

  // A sequence of 0 is reserved ("no valid slot written yet") and rejected.
  pokemon::BattleStoreFileBytes zeroSequence{};
  size_t zeroSequenceSize = 0;
  CHECK(!pokemon::encodeBattleStoreFile(state, 0, zeroSequence, zeroSequenceSize));

  // A size that doesn't match header+entryCount*16+4 is rejected outright.
  CHECK(!pokemon::decodeBattleStoreFile(bytes.data(), size - 1, decoded, sequence));
  CHECK(!pokemon::decodeBattleStoreFile(bytes.data(), 0, decoded, sequence));

  // A different, unrecognized version byte is rejected too.
  pokemon::BattleStoreFileBytes badVersion = bytes;
  badVersion[4] = pokemon::POKEMON_BATTLE_STORE_VERSION + 1;
  CHECK(!pokemon::decodeBattleStoreFile(badVersion.data(), size, decoded, sequence));
}

void emptyStateEncodesToJustTheHeaderAndCrc() {
  BattleStoreState state{};
  pokemon::BattleStoreFileBytes bytes{};
  size_t size = 0;
  CHECK(pokemon::encodeBattleStoreFile(state, 1, bytes, size));
  CHECK(size == pokemon::POKEMON_BATTLE_HEADER_BYTES + pokemon::POKEMON_BATTLE_FILE_CRC_BYTES);

  BattleStoreState decoded{};
  uint32_t sequence = 0;
  decoded.entries[0] = makeEntry(1);  // prove decode actually clears this, not just leaves it
  CHECK(pokemon::decodeBattleStoreFile(bytes.data(), size, decoded, sequence));
  CHECK(pokemon::battleEntryCount(decoded) == 0);
  CHECK(sequence == 1);
}

// Hand-rolls a genuine v1 (pre-PP-Up, 16-byte) entry - encodeBattleRecordEntry
// only ever writes the current (v2, 20-byte) format now, so a real v1 byte
// layout has to be built by hand here, matching exactly what the old
// encodeBattleRecordEntry used to produce before PP Up existed.
pokemon::BattleEntryBytesV1 makeV1EntryBytes(const BattleRecordEntry& entry) {
  pokemon::BattleEntryBytesV1 bytes{};
  bytes[0] = static_cast<uint8_t>(entry.recordId);
  bytes[1] = static_cast<uint8_t>(entry.recordId >> 8);
  bytes[2] = static_cast<uint8_t>(entry.recordId >> 16);
  bytes[3] = static_cast<uint8_t>(entry.recordId >> 24);
  for (size_t i = 0; i < 4; ++i) bytes[4 + i] = entry.moves[i];
  for (size_t i = 0; i < 4; ++i) bytes[8 + i] = entry.pp[i];
  bytes[12] = static_cast<uint8_t>(entry.currentHp);
  bytes[13] = static_cast<uint8_t>(entry.currentHp >> 8);
  bytes[14] = static_cast<uint8_t>(entry.status);
  bytes[15] = entry.statusTurns;
  return bytes;
}

void legacyHeaderlessFileStillDecodesForMigration() {
  BattleStoreState state{};
  CHECK(pokemon::upsertBattleEntry(state, makeEntry(9)));

  // The pre-double-buffering format: just entries back to back + CRC32,
  // no magic/version/sequence header, and always v1 (16-byte) entries since
  // it predates PP Up entirely.
  pokemon::BattleEntryBytesV1 entryBytes = makeV1EntryBytes(state.entries[0]);
  pokemon::BattleStoreLegacyFileBytes legacyBytes{};
  std::memcpy(legacyBytes.data(), entryBytes.data(), entryBytes.size());
  const uint32_t crc = pokemon::finishBattleStoreCrc32(
      pokemon::updateBattleStoreCrc32(pokemon::BATTLE_STORE_CRC32_INITIAL, entryBytes.data(), entryBytes.size()));
  for (size_t i = 0; i < 4; ++i) legacyBytes[entryBytes.size() + i] = static_cast<uint8_t>(crc >> (8 * i));
  const size_t legacySize = entryBytes.size() + pokemon::POKEMON_BATTLE_FILE_CRC_BYTES;

  BattleStoreState decoded{};
  CHECK(pokemon::decodeLegacyBattleStoreFile(legacyBytes.data(), legacySize, decoded));
  CHECK(decoded == state);

  // A byte flip is still caught the same way.
  legacyBytes[0] ^= 0xFFU;
  BattleStoreState corruptOutput{};
  CHECK(!pokemon::decodeLegacyBattleStoreFile(legacyBytes.data(), legacySize, corruptOutput));
}

void v1DoubleBufferedFileStillDecodesWithZeroPpUp() {
  BattleStoreState state{};
  CHECK(pokemon::upsertBattleEntry(state, makeEntry(5)));

  // Hand-build a genuine v1 (16-byte entries) double-buffered file, exactly
  // what a real existing user's pokemon-battle-{a,b}.bin already looks like
  // from before PP Up existed - distinct from the older, headerless
  // pre-double-buffering format the test above covers.
  const pokemon::BattleEntryBytesV1 entryBytes = makeV1EntryBytes(state.entries[0]);
  std::vector<uint8_t> bytes = {'P', 'K', 'B', 'T', pokemon::POKEMON_BATTLE_STORE_VERSION_V1, 1};
  constexpr uint32_t sequence = 3;
  for (size_t i = 0; i < 4; ++i) bytes.push_back(static_cast<uint8_t>(sequence >> (8 * i)));
  bytes.insert(bytes.end(), entryBytes.begin(), entryBytes.end());
  const uint32_t crc = pokemon::finishBattleStoreCrc32(
      pokemon::updateBattleStoreCrc32(pokemon::BATTLE_STORE_CRC32_INITIAL, bytes.data(), bytes.size()));
  for (size_t i = 0; i < 4; ++i) bytes.push_back(static_cast<uint8_t>(crc >> (8 * i)));

  BattleStoreState decoded{};
  uint32_t decodedSequence = 0;
  CHECK(pokemon::decodeBattleStoreFile(bytes.data(), bytes.size(), decoded, decodedSequence));
  CHECK(decodedSequence == sequence);
  CHECK(decoded == state);  // ppUp defaults to 0 on both sides, so equality still holds

  // An unrecognized version (neither v1 nor the current v2) is rejected.
  std::vector<uint8_t> badVersion = bytes;
  badVersion[4] = 99;
  BattleStoreState badOutput{};
  uint32_t badSequence = 0;
  CHECK(!pokemon::decodeBattleStoreFile(badVersion.data(), badVersion.size(), badOutput, badSequence));
}

}  // namespace

int main() {
  validateRejectsGapsAndOutOfRangeMoveIds();
  entryRoundTripsThroughEncodeDecode();
  invalidStatusByteFailsToDecode();
  upsertKeepsAscendingOrderAndReplacesInPlace();
  upsertFailsPastCapacityForAnUnseenRecordId();
  fileRoundTripsCarriesSequenceAndDetectsCorruption();
  emptyStateEncodesToJustTheHeaderAndCrc();
  legacyHeaderlessFileStillDecodesForMigration();
  v1DoubleBufferedFileStillDecodesWithZeroPpUp();
  return failures == 0 ? 0 : 1;
}
