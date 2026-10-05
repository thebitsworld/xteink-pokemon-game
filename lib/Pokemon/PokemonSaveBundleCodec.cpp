#include "PokemonSaveBundleCodec.h"

#include <algorithm>
#include <cstring>

#include "PokemonBattleStoreCodec.h"
#include "PokemonCrc32.h"
#include "PokemonHallOfFameCodec.h"
#include "PokemonIvEvStoreCodec.h"
#include "PokemonMovesetStoreCodec.h"
#include "PokemonStoreCodec.h"

namespace pokemon {
namespace {

constexpr std::array<uint8_t, 4> BUNDLE_MAGIC = {'P', 'K', 'S', 'B'};
constexpr uint32_t CRC_INITIAL = 0xFFFFFFFFU;

constexpr const char* FILE_NAMES[SAVE_BUNDLE_FILE_COUNT] = {
    "pokemon-a.bin",       "pokemon-b.bin",       "pokemon-v2-a.bin",    "pokemon-v2-b.bin",   "pokemon-battle-a.bin",
    "pokemon-battle-b.bin", "pokemon-battle.bin", "pokemon-moves-a.bin", "pokemon-moves-b.bin", "pokemon-ivev-a.bin",
    "pokemon-ivev-b.bin",  "pokemon-hof-a.bin",   "pokemon-hof-b.bin",
};

void writeU16(uint8_t* out, const uint16_t value) {
  out[0] = static_cast<uint8_t>(value);
  out[1] = static_cast<uint8_t>(value >> 8);
}

void writeU32(uint8_t* out, const uint32_t value) {
  for (int i = 0; i < 4; ++i) out[i] = static_cast<uint8_t>(value >> (8 * i));
}

uint16_t readU16(const uint8_t* in) { return static_cast<uint16_t>(in[0] | (in[1] << 8)); }

uint32_t readU32(const uint8_t* in) {
  return static_cast<uint32_t>(in[0]) | (static_cast<uint32_t>(in[1]) << 8) | (static_cast<uint32_t>(in[2]) << 16) |
         (static_cast<uint32_t>(in[3]) << 24);
}

uint32_t crcOf(const uint8_t* data, const size_t size) {
  return updatePokemonCrc32(CRC_INITIAL, data, size) ^ CRC_INITIAL;
}

void writeVersions(uint8_t* out, const SaveFormatVersions& versions) {
  writeU16(out, versions.snapshot);
  out[2] = versions.battle;
  out[3] = versions.ivEv;
  out[4] = versions.moveset;
  out[5] = versions.hallOfFame;
}

SaveFormatVersions readVersions(const uint8_t* in) {
  SaveFormatVersions versions;
  versions.snapshot = readU16(in);
  versions.battle = in[2];
  versions.ivEv = in[3];
  versions.moveset = in[4];
  versions.hallOfFame = in[5];
  return versions;
}

bool validEntries(const SaveBundleHeader& header) {
  if (header.entryCount == 0 || header.entryCount > SAVE_BUNDLE_FILE_COUNT) return false;
  uint32_t seen = 0;
  uint64_t total = 0;
  for (size_t i = 0; i < header.entryCount; ++i) {
    const SaveBundleEntry& entry = header.entries[i];
    if (entry.fileId >= SAVE_BUNDLE_FILE_COUNT || (seen & (1U << entry.fileId)) != 0) return false;
    if (entry.size == 0 || entry.size > SAVE_BUNDLE_MAX_FILE_BYTES) return false;
    seen |= 1U << entry.fileId;
    total += entry.size;
  }
  // A bundle without either main snapshot slot is not a save at all.
  const uint32_t mainSlots = (1U << static_cast<uint8_t>(SaveBundleFile::MainA)) |
                             (1U << static_cast<uint8_t>(SaveBundleFile::MainB));
  return (seen & mainSlots) != 0 && total + header.tableBytes() <= SAVE_BUNDLE_MAX_TOTAL_BYTES;
}

// Copies a C string into a fixed buffer, always NUL-terminated.
template <size_t N>
void copyName(std::array<char, N>& out, const char* in, const size_t length) {
  out.fill('\0');
  std::memcpy(out.data(), in, std::min(length, N - 1));
}

}  // namespace

const char* saveBundleFileName(const uint8_t fileId) {
  return fileId < SAVE_BUNDLE_FILE_COUNT ? FILE_NAMES[fileId] : nullptr;
}

SaveFormatVersions currentSaveFormatVersions() {
  SaveFormatVersions versions;
  versions.snapshot = POKEMON_SNAPSHOT_VERSION;
  versions.battle = POKEMON_BATTLE_STORE_VERSION;
  versions.ivEv = POKEMON_IVEV_STORE_VERSION;
  versions.moveset = POKEMON_MOVESET_STORE_VERSION;
  versions.hallOfFame = POKEMON_HOF_STORE_VERSION;
  return versions;
}

bool saveFormatCompatible(const SaveFormatVersions& incoming, const SaveFormatVersions& local) {
  return incoming.snapshot <= local.snapshot && incoming.battle <= local.battle && incoming.ivEv <= local.ivEv &&
         incoming.moveset <= local.moveset && incoming.hallOfFame <= local.hallOfFame;
}

uint64_t SaveBundleHeader::totalBytes() const {
  uint64_t total = tableBytes();
  for (size_t i = 0; i < entryCount && i < entries.size(); ++i) total += entries[i].size;
  return total;
}

bool encodeSaveBundleTable(const SaveBundleHeader& header, uint8_t* output, const size_t capacity, size_t& written) {
  written = 0;
  if (output == nullptr || !validEntries(header) || capacity < header.tableBytes()) return false;
  std::memset(output, 0, header.tableBytes());
  std::memcpy(output, BUNDLE_MAGIC.data(), BUNDLE_MAGIC.size());
  output[4] = SAVE_BUNDLE_VERSION;
  output[5] = header.entryCount;
  writeVersions(output + 8, header.versions);
  uint8_t* cursor = output + SAVE_BUNDLE_HEADER_BYTES;
  for (size_t i = 0; i < header.entryCount; ++i) {
    cursor[0] = header.entries[i].fileId;
    writeU32(cursor + 4, header.entries[i].size);
    writeU32(cursor + 8, header.entries[i].crc32);
    cursor += SAVE_BUNDLE_ENTRY_BYTES;
  }
  const size_t crcOffset = static_cast<size_t>(cursor - output);
  writeU32(cursor, crcOf(output, crcOffset));
  written = header.tableBytes();
  return true;
}

SaveBundleParse decodeSaveBundleTable(const uint8_t* data, const size_t available, SaveBundleHeader& output) {
  output = SaveBundleHeader{};
  if (data == nullptr || available < SAVE_BUNDLE_HEADER_BYTES) return SaveBundleParse::NeedMoreBytes;
  if (std::memcmp(data, BUNDLE_MAGIC.data(), BUNDLE_MAGIC.size()) != 0) return SaveBundleParse::Invalid;
  if (data[4] != SAVE_BUNDLE_VERSION) return SaveBundleParse::UnsupportedVersion;
  const uint8_t entryCount = data[5];
  if (entryCount == 0 || entryCount > SAVE_BUNDLE_FILE_COUNT) return SaveBundleParse::Invalid;
  const size_t tableBytes =
      SAVE_BUNDLE_HEADER_BYTES + entryCount * SAVE_BUNDLE_ENTRY_BYTES + SAVE_BUNDLE_TABLE_CRC_BYTES;
  if (available < tableBytes) return SaveBundleParse::NeedMoreBytes;
  const size_t crcOffset = tableBytes - SAVE_BUNDLE_TABLE_CRC_BYTES;
  if (readU32(data + crcOffset) != crcOf(data, crcOffset)) return SaveBundleParse::Invalid;

  SaveBundleHeader header;
  header.entryCount = entryCount;
  header.versions = readVersions(data + 8);
  const uint8_t* cursor = data + SAVE_BUNDLE_HEADER_BYTES;
  for (size_t i = 0; i < entryCount; ++i) {
    header.entries[i].fileId = cursor[0];
    header.entries[i].size = readU32(cursor + 4);
    header.entries[i].crc32 = readU32(cursor + 8);
    cursor += SAVE_BUNDLE_ENTRY_BYTES;
  }
  if (!validEntries(header)) return SaveBundleParse::Invalid;
  output = header;
  return SaveBundleParse::Ok;
}

bool hasSaveBundleTag(const uint8_t* data, const size_t length) {
  return data != nullptr && length >= SAVE_BUNDLE_TAG.size() &&
         std::memcmp(data, SAVE_BUNDLE_TAG.data(), SAVE_BUNDLE_TAG.size()) == 0;
}

// Offer layout: tag(4) totalBytes(4) chunkBytes(2) flags(1) bundleVersion(1)
// versions(6) leaderSpecies(2) leaderLevel(1) partyCount(1) badgeCount(1)
// caughtCount(2) reserved(2) leaderNameLen(1) leaderName senderNameLen(1) senderName
bool encodeSaveBundleOffer(const SaveBundleOffer& offer, uint8_t* output, const size_t capacity, size_t& written) {
  written = 0;
  const size_t leaderLength = strnlen(offer.summary.leaderName.data(), offer.summary.leaderName.size() - 1);
  const size_t senderLength = strnlen(offer.senderName.data(), offer.senderName.size() - 1);
  const size_t length = 4 + 4 + 2 + 1 + 1 + 6 + 9 + 1 + leaderLength + 1 + senderLength;
  if (output == nullptr || capacity < length) return false;
  uint8_t* cursor = output;
  std::memcpy(cursor, SAVE_BUNDLE_TAG.data(), SAVE_BUNDLE_TAG.size());
  cursor += 4;
  writeU32(cursor, offer.totalBytes);
  writeU16(cursor + 4, offer.chunkBytes);
  cursor[6] = offer.moveSave ? 1 : 0;
  cursor[7] = SAVE_BUNDLE_VERSION;
  writeVersions(cursor + 8, offer.versions);
  cursor += 14;
  writeU16(cursor, offer.summary.leaderSpeciesId);
  cursor[2] = offer.summary.leaderLevel;
  cursor[3] = offer.summary.partyCount;
  cursor[4] = offer.summary.badgeCount;
  writeU16(cursor + 5, offer.summary.caughtCount);
  cursor[7] = 0;
  cursor[8] = 0;
  cursor += 9;
  *cursor++ = static_cast<uint8_t>(leaderLength);
  std::memcpy(cursor, offer.summary.leaderName.data(), leaderLength);
  cursor += leaderLength;
  *cursor++ = static_cast<uint8_t>(senderLength);
  std::memcpy(cursor, offer.senderName.data(), senderLength);
  written = length;
  return true;
}

bool decodeSaveBundleOffer(const uint8_t* data, const size_t length, SaveBundleOffer& output) {
  output = SaveBundleOffer{};
  constexpr size_t fixedBytes = 4 + 4 + 2 + 1 + 1 + 6 + 9;
  if (!hasSaveBundleTag(data, length) || length < fixedBytes + 2) return false;
  const uint8_t* cursor = data + 4;
  SaveBundleOffer offer;
  offer.totalBytes = readU32(cursor);
  offer.chunkBytes = readU16(cursor + 4);
  if ((cursor[6] & ~1U) != 0 || cursor[7] != SAVE_BUNDLE_VERSION) return false;
  offer.moveSave = cursor[6] != 0;
  offer.versions = readVersions(cursor + 8);
  cursor += 14;
  offer.summary.leaderSpeciesId = readU16(cursor);
  offer.summary.leaderLevel = cursor[2];
  offer.summary.partyCount = cursor[3];
  offer.summary.badgeCount = cursor[4];
  offer.summary.caughtCount = readU16(cursor + 5);
  cursor += 9;
  const size_t leaderLength = *cursor++;
  if (leaderLength >= offer.summary.leaderName.size() || fixedBytes + 1 + leaderLength + 1 > length) return false;
  copyName(offer.summary.leaderName, reinterpret_cast<const char*>(cursor), leaderLength);
  cursor += leaderLength;
  const size_t senderLength = *cursor++;
  if (senderLength == 0 || senderLength >= offer.senderName.size() ||
      fixedBytes + 1 + leaderLength + 1 + senderLength != length) {
    return false;
  }
  copyName(offer.senderName, reinterpret_cast<const char*>(cursor), senderLength);
  if (offer.totalBytes == 0 || offer.totalBytes > SAVE_BUNDLE_MAX_TOTAL_BYTES ||
      offer.summary.partyCount > PARTY_SIZE) {
    return false;
  }
  output = offer;
  return true;
}

}  // namespace pokemon
