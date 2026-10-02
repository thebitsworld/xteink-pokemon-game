#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "PokemonSaveBundleCodec.h"

namespace {

int failures = 0;

#define CHECK(condition)                                                                \
  do {                                                                                  \
    if (!(condition)) {                                                                 \
      std::fprintf(stderr, "%s:%d check failed: %s\n", __FILE__, __LINE__, #condition); \
      ++failures;                                                                       \
    }                                                                                   \
  } while (false)

using pokemon::SaveBundleFile;
using pokemon::SaveBundleHeader;
using pokemon::SaveBundleParse;

uint8_t id(const SaveBundleFile file) { return static_cast<uint8_t>(file); }

SaveBundleHeader sampleHeader() {
  SaveBundleHeader header;
  header.versions = pokemon::currentSaveFormatVersions();
  header.entries[0] = {id(SaveBundleFile::MainA), 4000, 0x11111111U};
  header.entries[1] = {id(SaveBundleFile::BattleB), 300, 0x22222222U};
  header.entries[2] = {id(SaveBundleFile::HallOfFameA), 250, 0x33333333U};
  header.entryCount = 3;
  return header;
}

void tableRoundTrips() {
  const SaveBundleHeader header = sampleHeader();
  std::array<uint8_t, pokemon::SAVE_BUNDLE_MAX_TABLE_BYTES> bytes{};
  size_t written = 0;
  CHECK(pokemon::encodeSaveBundleTable(header, bytes.data(), bytes.size(), written));
  CHECK(written == header.tableBytes());
  CHECK(header.totalBytes() == header.tableBytes() + 4000 + 300 + 250);

  SaveBundleHeader decoded;
  CHECK(pokemon::decodeSaveBundleTable(bytes.data(), written, decoded) == SaveBundleParse::Ok);
  CHECK(decoded.entryCount == 3);
  CHECK(decoded.versions == header.versions);
  for (size_t i = 0; i < 3; ++i) {
    CHECK(decoded.entries[i].fileId == header.entries[i].fileId);
    CHECK(decoded.entries[i].size == header.entries[i].size);
    CHECK(decoded.entries[i].crc32 == header.entries[i].crc32);
  }
  // A short prefix asks for more instead of failing.
  CHECK(pokemon::decodeSaveBundleTable(bytes.data(), written - 1, decoded) == SaveBundleParse::NeedMoreBytes);
}

void tableRejectsCorruptionAndBadEntries() {
  const SaveBundleHeader header = sampleHeader();
  std::array<uint8_t, pokemon::SAVE_BUNDLE_MAX_TABLE_BYTES> bytes{};
  size_t written = 0;
  CHECK(pokemon::encodeSaveBundleTable(header, bytes.data(), bytes.size(), written));
  SaveBundleHeader decoded;

  auto flipped = bytes;
  flipped[pokemon::SAVE_BUNDLE_HEADER_BYTES + 5] ^= 0x01U;  // an entry size byte: table CRC must catch it
  CHECK(pokemon::decodeSaveBundleTable(flipped.data(), written, decoded) == SaveBundleParse::Invalid);

  auto badMagic = bytes;
  badMagic[0] = 'X';
  CHECK(pokemon::decodeSaveBundleTable(badMagic.data(), written, decoded) == SaveBundleParse::Invalid);

  auto newerBundle = bytes;
  newerBundle[4] = pokemon::SAVE_BUNDLE_VERSION + 1;
  CHECK(pokemon::decodeSaveBundleTable(newerBundle.data(), written, decoded) == SaveBundleParse::UnsupportedVersion);

  SaveBundleHeader duplicate = header;
  duplicate.entries[1].fileId = header.entries[0].fileId;
  CHECK(!pokemon::encodeSaveBundleTable(duplicate, bytes.data(), bytes.size(), written));

  SaveBundleHeader unknown = header;
  unknown.entries[1].fileId = static_cast<uint8_t>(pokemon::SAVE_BUNDLE_FILE_COUNT);
  CHECK(!pokemon::encodeSaveBundleTable(unknown, bytes.data(), bytes.size(), written));

  SaveBundleHeader noMainSnapshot;
  noMainSnapshot.entries[0] = {id(SaveBundleFile::BattleA), 10, 0};
  noMainSnapshot.entryCount = 1;
  CHECK(!pokemon::encodeSaveBundleTable(noMainSnapshot, bytes.data(), bytes.size(), written));

  SaveBundleHeader oversized = header;
  oversized.entries[0].size = pokemon::SAVE_BUNDLE_MAX_FILE_BYTES + 1;
  CHECK(!pokemon::encodeSaveBundleTable(oversized, bytes.data(), bytes.size(), written));
}

void fileNamesAreAFixedAllowlist() {
  CHECK(std::strcmp(pokemon::saveBundleFileName(id(SaveBundleFile::MainA)), "pokemon-a.bin") == 0);
  CHECK(std::strcmp(pokemon::saveBundleFileName(id(SaveBundleFile::HallOfFameB)), "pokemon-hof-b.bin") == 0);
  CHECK(pokemon::saveBundleFileName(static_cast<uint8_t>(pokemon::SAVE_BUNDLE_FILE_COUNT)) == nullptr);
  for (uint8_t i = 0; i < pokemon::SAVE_BUNDLE_FILE_COUNT; ++i) {
    const char* name = pokemon::saveBundleFileName(i);
    CHECK(name != nullptr && std::strchr(name, '/') == nullptr);
  }
}

void olderFormatsAreCompatibleNewerAreNot() {
  const pokemon::SaveFormatVersions local = pokemon::currentSaveFormatVersions();
  CHECK(pokemon::saveFormatCompatible(local, local));
  pokemon::SaveFormatVersions older = local;
  older.snapshot = 1;
  older.battle = 1;
  CHECK(pokemon::saveFormatCompatible(older, local));
  pokemon::SaveFormatVersions newer = local;
  newer.ivEv = static_cast<uint8_t>(local.ivEv + 1);
  CHECK(!pokemon::saveFormatCompatible(newer, local));
}

void offerRoundTripsAndValidates() {
  pokemon::SaveBundleOffer offer;
  offer.totalBytes = 123456;
  offer.chunkBytes = 1024;
  offer.moveSave = true;
  offer.versions = pokemon::currentSaveFormatVersions();
  offer.summary.leaderSpeciesId = 25;
  offer.summary.leaderLevel = 42;
  offer.summary.partyCount = 6;
  offer.summary.badgeCount = 5;
  offer.summary.caughtCount = 87;
  std::memcpy(offer.summary.leaderName.data(), "Sparky", 6);
  std::memcpy(offer.senderName.data(), "Davide X3", 9);

  std::array<uint8_t, pokemon::SAVE_BUNDLE_OFFER_MAX_BYTES> bytes{};
  size_t written = 0;
  CHECK(pokemon::encodeSaveBundleOffer(offer, bytes.data(), bytes.size(), written));
  CHECK(pokemon::hasSaveBundleTag(bytes.data(), written));
  pokemon::SaveBundleOffer decoded;
  CHECK(pokemon::decodeSaveBundleOffer(bytes.data(), written, decoded));
  CHECK(decoded == offer);

  // Truncated, trailing garbage, or missing tag: all rejected.
  CHECK(!pokemon::decodeSaveBundleOffer(bytes.data(), written - 1, decoded));
  CHECK(!pokemon::decodeSaveBundleOffer(bytes.data(), written + 1, decoded));
  auto untagged = bytes;
  untagged[0] = 'B';
  CHECK(!pokemon::decodeSaveBundleOffer(untagged.data(), written, decoded));
  // A plain book-transfer device name is not a tagged payload.
  CHECK(!pokemon::hasSaveBundleTag(reinterpret_cast<const uint8_t*>("Reader"), 6));

  // Empty leader nickname is fine (the UI falls back to the species name).
  pokemon::SaveBundleOffer unnamed = offer;
  unnamed.summary.leaderName.fill('\0');
  CHECK(pokemon::encodeSaveBundleOffer(unnamed, bytes.data(), bytes.size(), written));
  CHECK(pokemon::decodeSaveBundleOffer(bytes.data(), written, decoded));
  CHECK(decoded == unnamed);
}

}  // namespace

int main() {
  tableRoundTrips();
  tableRejectsCorruptionAndBadEntries();
  fileNamesAreAFixedAllowlist();
  olderFormatsAreCompatibleNewerAreNot();
  offerRoundTripsAndValidates();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  return 0;
}
