#include <cstdio>
#include <cstring>
#include <memory>

#include "PokemonMovesetStoreCodec.h"

namespace {

using pokemon::MovesetEntry;
using pokemon::MovesetStoreState;

int failures = 0;

#define CHECK(condition)                                                                \
  do {                                                                                  \
    if (!(condition)) {                                                                 \
      std::fprintf(stderr, "%s:%d check failed: %s\n", __FILE__, __LINE__, #condition); \
      ++failures;                                                                       \
    }                                                                                   \
  } while (false)

MovesetEntry makeEntry(const uint32_t recordId) {
  MovesetEntry entry{};
  entry.recordId = recordId;
  entry.moves = {84, 45, 98, 0};
  entry.ppUp = {3, 0, 1, 0};
  return entry;
}

void layoutConstantsAreConsistent() {
  static_assert(pokemon::POKEMON_MOVESET_ENTRY_BYTES == 4 + 2 * pokemon::BATTLE_MOVE_SLOTS);
  static_assert(pokemon::POKEMON_MOVESET_FILE_MAX_BYTES ==
                pokemon::POKEMON_MOVESET_HEADER_BYTES +
                    pokemon::POKEMON_MOVESET_MAX_ENTRIES * pokemon::POKEMON_MOVESET_ENTRY_BYTES +
                    pokemon::POKEMON_MOVESET_FILE_CRC_BYTES);
  static_assert(sizeof(MovesetEntry) == pokemon::POKEMON_MOVESET_ENTRY_BYTES);  // no padding: kept small in RAM too
}

void validateRejectsBadEntries() {
  CHECK(pokemon::validateMovesetEntry(makeEntry(1)));
  MovesetEntry entry = makeEntry(1);
  entry.recordId = 0;
  CHECK(!pokemon::validateMovesetEntry(entry));

  entry = makeEntry(1);
  entry.moves = {84, 0, 98, 0};  // a gap: moves must be packed at the front
  entry.ppUp = {0, 0, 0, 0};
  CHECK(!pokemon::validateMovesetEntry(entry));

  entry = makeEntry(1);
  entry.moves[0] = static_cast<uint8_t>(pokemon::POKEMON_MOVE_ID_MAX + 1);
  CHECK(!pokemon::validateMovesetEntry(entry));

  entry = makeEntry(1);
  entry.ppUp[0] = 4;  // real Gen 1 cap is 3
  CHECK(!pokemon::validateMovesetEntry(entry));

  entry = makeEntry(1);
  entry.ppUp[3] = 1;  // PP Up on an empty slot
  CHECK(!pokemon::validateMovesetEntry(entry));
}

void entryRoundTripsThroughEncodeDecode() {
  const MovesetEntry entry = makeEntry(0x01020304U);
  pokemon::MovesetEntryBytes bytes{};
  CHECK(pokemon::encodeMovesetEntry(entry, bytes));
  CHECK(bytes[0] == 0x04 && bytes[3] == 0x01);  // little-endian recordId
  CHECK(bytes[4] == 84 && bytes[6] == 98);
  CHECK(bytes[8] == 3 && bytes[10] == 1);
  MovesetEntry decoded{};
  CHECK(pokemon::decodeMovesetEntry(bytes, decoded));
  CHECK(decoded == entry);

  bytes[8] = 9;  // ppUp out of range
  CHECK(!pokemon::decodeMovesetEntry(bytes, decoded));
}

void upsertKeepsAscendingOrderReplacesInPlaceAndRemoves() {
  auto state = std::make_unique<MovesetStoreState>();
  CHECK(pokemon::upsertMovesetEntry(*state, makeEntry(30)));
  CHECK(pokemon::upsertMovesetEntry(*state, makeEntry(10)));
  CHECK(pokemon::upsertMovesetEntry(*state, makeEntry(20)));
  CHECK(pokemon::movesetEntryCount(*state) == 3);
  CHECK(state->entries[0].recordId == 10 && state->entries[1].recordId == 20 && state->entries[2].recordId == 30);

  MovesetEntry changed = makeEntry(20);
  changed.moves = {1, 0, 0, 0};
  changed.ppUp = {0, 0, 0, 0};
  CHECK(pokemon::upsertMovesetEntry(*state, changed));
  CHECK(pokemon::movesetEntryCount(*state) == 3);
  const MovesetEntry* found = pokemon::findMovesetEntry(*state, 20);
  CHECK(found != nullptr && found->moves[0] == 1);

  CHECK(pokemon::removeMovesetEntry(*state, 10));
  CHECK(!pokemon::removeMovesetEntry(*state, 10));
  CHECK(pokemon::movesetEntryCount(*state) == 2);
  CHECK(state->entries[0].recordId == 20 && state->entries[2].recordId == 0);
  CHECK(pokemon::findMovesetEntry(*state, 10) == nullptr);
  CHECK(pokemon::validateMovesetStoreState(*state));
}

void upsertFailsPastCapacityForAnUnseenRecordIdButStillReplaces() {
  auto state = std::make_unique<MovesetStoreState>();
  for (uint32_t id = 1; id <= pokemon::POKEMON_MOVESET_MAX_ENTRIES; ++id) {
    CHECK(pokemon::upsertMovesetEntry(*state, makeEntry(id)));
  }
  CHECK(!pokemon::upsertMovesetEntry(*state, makeEntry(pokemon::POKEMON_MOVESET_MAX_ENTRIES + 1)));
  MovesetEntry changed = makeEntry(5);
  changed.moves = {33, 0, 0, 0};
  changed.ppUp = {0, 0, 0, 0};
  CHECK(pokemon::upsertMovesetEntry(*state, changed));  // an existing record can still be updated when full
}

void fileRoundTripsCarriesSequenceAndDetectsCorruption() {
  auto state = std::make_unique<MovesetStoreState>();
  CHECK(pokemon::upsertMovesetEntry(*state, makeEntry(7)));
  CHECK(pokemon::upsertMovesetEntry(*state, makeEntry(3)));

  auto bytes = std::make_unique<pokemon::MovesetStoreFileBytes>();
  size_t size = 0;
  CHECK(pokemon::encodeMovesetStoreFile(*state, 42, *bytes, size));
  CHECK(size == pokemon::POKEMON_MOVESET_HEADER_BYTES + 2 * pokemon::POKEMON_MOVESET_ENTRY_BYTES +
                    pokemon::POKEMON_MOVESET_FILE_CRC_BYTES);
  CHECK((*bytes)[0] == 'P' && (*bytes)[1] == 'K' && (*bytes)[2] == 'M' && (*bytes)[3] == 'V');

  auto decoded = std::make_unique<MovesetStoreState>();
  uint32_t sequence = 0;
  CHECK(pokemon::decodeMovesetStoreFile(bytes->data(), size, *decoded, sequence));
  CHECK(sequence == 42);
  CHECK(*decoded == *state);

  CHECK(!pokemon::encodeMovesetStoreFile(*state, 0, *bytes, size));  // sequence 0 is reserved

  (*bytes)[pokemon::POKEMON_MOVESET_HEADER_BYTES + 1] ^= 0x01;  // flip a payload bit: CRC must catch it
  auto untouched = std::make_unique<MovesetStoreState>();
  CHECK(!pokemon::decodeMovesetStoreFile(bytes->data(), size, *untouched, sequence));
  CHECK(pokemon::movesetEntryCount(*untouched) == 0);  // output untouched on failure
  (*bytes)[pokemon::POKEMON_MOVESET_HEADER_BYTES + 1] ^= 0x01;

  CHECK(!pokemon::decodeMovesetStoreFile(bytes->data(), size - 1, *untouched, sequence));  // wrong length
  (*bytes)[4] = static_cast<uint8_t>(pokemon::POKEMON_MOVESET_STORE_VERSION + 1);          // unknown version
  CHECK(!pokemon::decodeMovesetStoreFile(bytes->data(), size, *untouched, sequence));
}

void emptyStateEncodesToJustTheHeaderAndCrc() {
  MovesetStoreState state{};
  auto bytes = std::make_unique<pokemon::MovesetStoreFileBytes>();
  size_t size = 0;
  CHECK(pokemon::encodeMovesetStoreFile(state, 1, *bytes, size));
  CHECK(size == pokemon::POKEMON_MOVESET_HEADER_BYTES + pokemon::POKEMON_MOVESET_FILE_CRC_BYTES);
  auto decoded = std::make_unique<MovesetStoreState>();
  decoded->entries[0] = makeEntry(9);  // stale content must be cleared, not kept
  uint32_t sequence = 0;
  CHECK(pokemon::decodeMovesetStoreFile(bytes->data(), size, *decoded, sequence));
  CHECK(pokemon::movesetEntryCount(*decoded) == 0);
}

}  // namespace

int main() {
  layoutConstantsAreConsistent();
  validateRejectsBadEntries();
  entryRoundTripsThroughEncodeDecode();
  upsertKeepsAscendingOrderReplacesInPlaceAndRemoves();
  upsertFailsPastCapacityForAnUnseenRecordIdButStillReplaces();
  fileRoundTripsCarriesSequenceAndDetectsCorruption();
  emptyStateEncodesToJustTheHeaderAndCrc();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  return 0;
}
