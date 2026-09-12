#include <cstdio>

#include "PokemonIvEvStoreCodec.h"

namespace {

using pokemon::IvEvEntry;
using pokemon::IvEvStoreState;

int failures = 0;

#define CHECK(condition)                                                                \
  do {                                                                                  \
    if (!(condition)) {                                                                 \
      std::fprintf(stderr, "%s:%d check failed: %s\n", __FILE__, __LINE__, #condition); \
      ++failures;                                                                       \
    }                                                                                   \
  } while (false)

IvEvEntry makeEntry(const uint32_t recordId) {
  IvEvEntry entry{};
  entry.recordId = recordId;
  entry.iv = {15, 10, 5, 0, 8};
  entry.ev = {252, 100, 0, 63, 1};
  return entry;
}

void validateRejectsZeroRecordIdAndOutOfRangeIv() {
  IvEvEntry entry = makeEntry(1);
  CHECK(pokemon::validateIvEvEntry(entry));

  IvEvEntry zeroRecordId = entry;
  zeroRecordId.recordId = 0;
  CHECK(!pokemon::validateIvEvEntry(zeroRecordId));

  IvEvEntry outOfRangeIv = entry;
  outOfRangeIv.iv[2] = 16;  // real Gen 1 IV range is 0-15
  CHECK(!pokemon::validateIvEvEntry(outOfRangeIv));

  // EV has no dedicated range check beyond uint8_t's own 0-255 - any byte
  // value is valid.
  IvEvEntry maxEv = entry;
  maxEv.ev[0] = 255;
  CHECK(pokemon::validateIvEvEntry(maxEv));
}

void entryRoundTripsThroughEncodeDecode() {
  IvEvEntry entry = makeEntry(0x11223344U);

  pokemon::IvEvEntryBytes bytes{};
  CHECK(pokemon::encodeIvEvEntry(entry, bytes));
  CHECK(bytes[0] == 0x44 && bytes[1] == 0x33 && bytes[2] == 0x22 && bytes[3] == 0x11);
  CHECK(bytes[4] == 15 && bytes[5] == 10 && bytes[6] == 5 && bytes[7] == 0 && bytes[8] == 8);
  CHECK(bytes[9] == 252 && bytes[10] == 100 && bytes[11] == 0 && bytes[12] == 63 && bytes[13] == 1);

  IvEvEntry decoded{};
  CHECK(pokemon::decodeIvEvEntry(bytes, decoded));
  CHECK(decoded == entry);
}

void invalidIvByteFailsToDecode() {
  IvEvEntry entry = makeEntry(1);
  pokemon::IvEvEntryBytes bytes{};
  CHECK(pokemon::encodeIvEvEntry(entry, bytes));
  bytes[4] = 16;  // out of range IV

  IvEvEntry output{};
  CHECK(!pokemon::decodeIvEvEntry(bytes, output));
}

void upsertKeepsAscendingOrderAndReplacesInPlace() {
  IvEvStoreState state{};
  CHECK(pokemon::upsertIvEvEntry(state, makeEntry(30)));
  CHECK(pokemon::upsertIvEvEntry(state, makeEntry(10)));
  CHECK(pokemon::upsertIvEvEntry(state, makeEntry(20)));
  CHECK(pokemon::ivEvEntryCount(state) == 3);
  CHECK(state.entries[0].recordId == 10);
  CHECK(state.entries[1].recordId == 20);
  CHECK(state.entries[2].recordId == 30);

  IvEvEntry replacement = makeEntry(20);
  replacement.ev[0] = 200;
  CHECK(pokemon::upsertIvEvEntry(state, replacement));
  CHECK(pokemon::ivEvEntryCount(state) == 3);  // replace, not append
  CHECK(pokemon::findIvEvEntry(state, 20)->ev[0] == 200);
  CHECK(pokemon::findIvEvEntry(state, 999) == nullptr);
}

void upsertFailsPastCapacityForAnUnseenRecordId() {
  IvEvStoreState state{};
  for (uint32_t id = 1; id <= 8; ++id) {
    CHECK(pokemon::upsertIvEvEntry(state, makeEntry(id)));
  }
  // Fill the rest of capacity quickly without 1024 individual CHECKs.
  for (uint32_t id = 9; id <= pokemon::POKEMON_IVEV_MAX_ENTRIES; ++id) {
    if (!pokemon::upsertIvEvEntry(state, makeEntry(id))) {
      std::fprintf(stderr, "upsert unexpectedly failed at id=%u\n", id);
      ++failures;
      return;
    }
  }
  CHECK(pokemon::ivEvEntryCount(state) == pokemon::POKEMON_IVEV_MAX_ENTRIES);
  CHECK(!pokemon::upsertIvEvEntry(state, makeEntry(999999)));
  // Replacing an existing one at full capacity must still work.
  IvEvEntry replacement = makeEntry(3);
  replacement.ev[0] = 9;
  CHECK(pokemon::upsertIvEvEntry(state, replacement));
  CHECK(pokemon::findIvEvEntry(state, 3)->ev[0] == 9);
}

void fileRoundTripsCarriesSequenceAndDetectsCorruption() {
  IvEvStoreState state{};
  CHECK(pokemon::upsertIvEvEntry(state, makeEntry(5)));
  CHECK(pokemon::upsertIvEvEntry(state, makeEntry(12)));

  pokemon::IvEvStoreFileBytes bytes{};
  size_t size = 0;
  CHECK(pokemon::encodeIvEvStoreFile(state, 7, bytes, size));
  CHECK(size == pokemon::POKEMON_IVEV_HEADER_BYTES + 2 * pokemon::POKEMON_IVEV_ENTRY_BYTES +
                    pokemon::POKEMON_IVEV_FILE_CRC_BYTES);
  CHECK(bytes[0] == 'P' && bytes[1] == 'K' && bytes[2] == 'I' && bytes[3] == 'V');
  CHECK(bytes[4] == pokemon::POKEMON_IVEV_STORE_VERSION);
  CHECK(bytes[5] == 2 && bytes[6] == 0);  // entryCount (u16 little-endian)

  IvEvStoreState decoded{};
  uint32_t sequence = 0;
  CHECK(pokemon::decodeIvEvStoreFile(bytes.data(), size, decoded, sequence));
  CHECK(decoded == state);
  CHECK(sequence == 7);

  // Flip a byte inside the header: CRC must catch it.
  pokemon::IvEvStoreFileBytes corrupted = bytes;
  corrupted[5] ^= 0xFFU;
  IvEvStoreState corruptOutput{};
  uint32_t corruptSequence = 0;
  CHECK(!pokemon::decodeIvEvStoreFile(corrupted.data(), size, corruptOutput, corruptSequence));

  // A sequence of 0 is reserved ("no valid slot written yet") and rejected.
  pokemon::IvEvStoreFileBytes zeroSequence{};
  size_t zeroSequenceSize = 0;
  CHECK(!pokemon::encodeIvEvStoreFile(state, 0, zeroSequence, zeroSequenceSize));

  // A size that doesn't match header+entryCount*14+4 is rejected outright.
  CHECK(!pokemon::decodeIvEvStoreFile(bytes.data(), size - 1, decoded, sequence));
  CHECK(!pokemon::decodeIvEvStoreFile(bytes.data(), 0, decoded, sequence));

  // A different, unrecognized version byte is rejected too.
  pokemon::IvEvStoreFileBytes badVersion = bytes;
  badVersion[4] = pokemon::POKEMON_IVEV_STORE_VERSION + 1;
  CHECK(!pokemon::decodeIvEvStoreFile(badVersion.data(), size, decoded, sequence));
}

void emptyStateEncodesToJustTheHeaderAndCrc() {
  IvEvStoreState state{};
  pokemon::IvEvStoreFileBytes bytes{};
  size_t size = 0;
  CHECK(pokemon::encodeIvEvStoreFile(state, 1, bytes, size));
  CHECK(size == pokemon::POKEMON_IVEV_HEADER_BYTES + pokemon::POKEMON_IVEV_FILE_CRC_BYTES);

  IvEvStoreState decoded{};
  uint32_t sequence = 0;
  decoded.entries[0] = makeEntry(1);  // prove decode actually clears this, not just leaves it
  CHECK(pokemon::decodeIvEvStoreFile(bytes.data(), size, decoded, sequence));
  CHECK(pokemon::ivEvEntryCount(decoded) == 0);
  CHECK(sequence == 1);
}

}  // namespace

int main() {
  validateRejectsZeroRecordIdAndOutOfRangeIv();
  entryRoundTripsThroughEncodeDecode();
  invalidIvByteFailsToDecode();
  upsertKeepsAscendingOrderAndReplacesInPlace();
  upsertFailsPastCapacityForAnUnseenRecordId();
  fileRoundTripsCarriesSequenceAndDetectsCorruption();
  emptyStateEncodesToJustTheHeaderAndCrc();
  return failures == 0 ? 0 : 1;
}
