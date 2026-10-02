#include "NearbySyncCodec.h"

#include <algorithm>
#include <cstring>

namespace nearby_sync {
namespace {

constexpr std::array<uint8_t, 4> CONTAINER_MAGIC = {'N', 'S', 'Y', 'C'};

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

bool knownSection(const uint8_t type) {
  return type >= static_cast<uint8_t>(SectionType::PokemonSave) && type <= static_cast<uint8_t>(SectionType::BookPosition);
}

bool validSections(const ContainerHeader& header) {
  if (header.sectionCount == 0 || header.sectionCount > MAX_SECTIONS) return false;
  uint32_t seen = 0;
  for (size_t i = 0; i < header.sectionCount; ++i) {
    const auto type = static_cast<uint8_t>(header.sections[i].type);
    if (!knownSection(type) || (seen & (1U << type)) != 0) return false;
    if (header.sections[i].size == 0 || header.sections[i].size > MAX_SECTION_BYTES) return false;
    seen |= 1U << type;
  }
  return true;
}

// Appends a length-prefixed (1 byte) string, truncated to `maxLength`.
uint8_t* putShortString(uint8_t* cursor, const char* text, const size_t maxLength) {
  const size_t length = strnlen(text, maxLength);
  *cursor++ = static_cast<uint8_t>(length);
  std::memcpy(cursor, text, length);
  return cursor + length;
}

template <size_t N>
bool takeShortString(const uint8_t*& cursor, const uint8_t* end, std::array<char, N>& out) {
  if (cursor >= end) return false;
  const size_t length = *cursor++;
  if (length >= N || static_cast<size_t>(end - cursor) < length) return false;
  out.fill('\0');
  std::memcpy(out.data(), cursor, length);
  cursor += length;
  return true;
}

}  // namespace

uint32_t crc32Update(uint32_t crc, const uint8_t* data, const size_t size) {
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
  }
  return crc;
}

uint64_t ContainerHeader::totalBytes() const {
  uint64_t total = tableBytes();
  for (size_t i = 0; i < sectionCount && i < MAX_SECTIONS; ++i) total += sections[i].size;
  return total;
}

int ContainerHeader::find(const SectionType type) const {
  for (size_t i = 0; i < sectionCount && i < MAX_SECTIONS; ++i) {
    if (sections[i].type == type) return static_cast<int>(i);
  }
  return -1;
}

uint64_t ContainerHeader::sectionOffset(const size_t index) const {
  uint64_t offset = tableBytes();
  for (size_t i = 0; i < index && i < MAX_SECTIONS; ++i) offset += sections[i].size;
  return offset;
}

bool encodeContainerTable(const ContainerHeader& header, uint8_t* output, const size_t capacity, size_t& written) {
  written = 0;
  if (output == nullptr || !validSections(header) || capacity < header.tableBytes()) return false;
  std::memset(output, 0, header.tableBytes());
  std::memcpy(output, CONTAINER_MAGIC.data(), CONTAINER_MAGIC.size());
  output[4] = CONTAINER_VERSION;
  output[5] = header.sectionCount;
  std::memcpy(output + 8, header.senderMac.data(), MAC_BYTES);
  uint8_t* cursor = output + HEADER_BYTES;
  for (size_t i = 0; i < header.sectionCount; ++i) {
    cursor[0] = static_cast<uint8_t>(header.sections[i].type);
    writeU32(cursor + 4, header.sections[i].size);
    writeU32(cursor + 8, header.sections[i].crc32);
    cursor += SECTION_ENTRY_BYTES;
  }
  const size_t crcOffset = static_cast<size_t>(cursor - output);
  writeU32(cursor, crc32Of(output, crcOffset));
  written = header.tableBytes();
  return true;
}

ParseResult decodeContainerTable(const uint8_t* data, const size_t available, ContainerHeader& output) {
  output = ContainerHeader{};
  if (data == nullptr || available < HEADER_BYTES) return ParseResult::NeedMoreBytes;
  if (std::memcmp(data, CONTAINER_MAGIC.data(), CONTAINER_MAGIC.size()) != 0) return ParseResult::Invalid;
  if (data[4] != CONTAINER_VERSION) return ParseResult::UnsupportedVersion;
  const uint8_t count = data[5];
  if (count == 0 || count > MAX_SECTIONS) return ParseResult::Invalid;
  const size_t tableBytes = HEADER_BYTES + count * SECTION_ENTRY_BYTES + TABLE_CRC_BYTES;
  if (available < tableBytes) return ParseResult::NeedMoreBytes;
  const size_t crcOffset = tableBytes - TABLE_CRC_BYTES;
  if (readU32(data + crcOffset) != crc32Of(data, crcOffset)) return ParseResult::Invalid;

  ContainerHeader header;
  header.sectionCount = count;
  std::memcpy(header.senderMac.data(), data + 8, MAC_BYTES);
  const uint8_t* cursor = data + HEADER_BYTES;
  for (size_t i = 0; i < count; ++i) {
    if (!knownSection(cursor[0])) return ParseResult::Invalid;
    header.sections[i].type = static_cast<SectionType>(cursor[0]);
    header.sections[i].size = readU32(cursor + 4);
    header.sections[i].crc32 = readU32(cursor + 8);
    cursor += SECTION_ENTRY_BYTES;
  }
  if (!validSections(header)) return ParseResult::Invalid;
  output = header;
  return ParseResult::Ok;
}

bool encodeBookPosition(const BookPositionRecord& record, uint8_t* output, const size_t capacity, size_t& written) {
  written = 0;
  const size_t pathLength = strnlen(record.path.data(), MAX_BOOK_PATH_BYTES);
  const size_t titleLength = strnlen(record.title.data(), MAX_BOOK_TITLE_BYTES);
  const uint8_t progressLength = record.progressLength;
  if (pathLength == 0 || record.path[0] != '/' ||
      (progressLength != 4 && progressLength != 6 && progressLength != 10) ||
      (record.percentBasisPoints > 10000 && record.percentBasisPoints != UNKNOWN_PERCENT)) {
    return false;
  }
  const size_t length = 2 + pathLength + 1 + titleLength + 2 + 1 + progressLength;
  if (output == nullptr || capacity < length) return false;
  uint8_t* cursor = output;
  writeU16(cursor, static_cast<uint16_t>(pathLength));
  cursor += 2;
  std::memcpy(cursor, record.path.data(), pathLength);
  cursor += pathLength;
  cursor = putShortString(cursor, record.title.data(), MAX_BOOK_TITLE_BYTES);
  writeU16(cursor, record.percentBasisPoints);
  cursor += 2;
  *cursor++ = progressLength;
  std::memcpy(cursor, record.progress.data(), progressLength);
  written = length;
  return true;
}

bool decodeBookPosition(const uint8_t* data, const size_t length, BookPositionRecord& output) {
  output = BookPositionRecord{};
  if (data == nullptr || length < 2) return false;
  const uint8_t* cursor = data;
  const uint8_t* end = data + length;
  BookPositionRecord record;
  const size_t pathLength = readU16(cursor);
  cursor += 2;
  if (pathLength == 0 || pathLength > MAX_BOOK_PATH_BYTES || static_cast<size_t>(end - cursor) < pathLength) {
    return false;
  }
  std::memcpy(record.path.data(), cursor, pathLength);
  cursor += pathLength;
  // A path from the wire is only ever used to look a book up, but still refuse
  // anything that is not a plain absolute path.
  if (record.path[0] != '/' || std::memchr(record.path.data(), '\0', pathLength) != nullptr) return false;
  if (!takeShortString(cursor, end, record.title) || end - cursor < 3) return false;
  record.percentBasisPoints = readU16(cursor);
  cursor += 2;
  record.progressLength = *cursor++;
  if ((record.progressLength != 4 && record.progressLength != 6 && record.progressLength != 10) ||
      (record.percentBasisPoints > 10000 && record.percentBasisPoints != UNKNOWN_PERCENT) ||
      static_cast<size_t>(end - cursor) != record.progressLength) {
    return false;
  }
  std::memcpy(record.progress.data(), cursor, record.progressLength);
  output = record;
  return true;
}

const char* baseName(const char* path) {
  if (path == nullptr) return "";
  const char* slash = std::strrchr(path, '/');
  return slash == nullptr ? path : slash + 1;
}

bool isValidStatsPayload(const uint8_t* data, const size_t size) {
  if (data == nullptr) return false;
  return (size == 13 && data[0] == 1) || (size == 17 && data[0] == 2) || (size == 159 && data[0] == 3);
}

bool hasOfferTag(const uint8_t* data, const size_t length) {
  return data != nullptr && length >= OFFER_TAG.size() && std::memcmp(data, OFFER_TAG.data(), OFFER_TAG.size()) == 0;
}

// Layout: tag(4) totalBytes(4) chunkBytes(2) flags(1) version(1)
// [pokemon: snapshotVersion(2) storeVersions(4) species(2) level(1) party(1)
//  badges(1) caught(2) leaderName(1+n)] [book: title(1+n) percent(2)] senderName(1+n)
bool encodeOffer(const Offer& offer, uint8_t* output, const size_t capacity, size_t& written) {
  written = 0;
  if (output == nullptr || capacity < OFFER_MAX_BYTES || offer.senderName[0] == '\0') return false;
  if (!offer.has(OFFER_HAS_POKEMON) && !offer.has(OFFER_HAS_STATS) && !offer.has(OFFER_HAS_BOOK)) return false;
  uint8_t* cursor = output;
  std::memcpy(cursor, OFFER_TAG.data(), OFFER_TAG.size());
  cursor += 4;
  writeU32(cursor, offer.totalBytes);
  writeU16(cursor + 4, offer.chunkBytes);
  cursor[6] = offer.flags;
  cursor[7] = CONTAINER_VERSION;
  cursor += 8;
  if (offer.has(OFFER_HAS_POKEMON)) {
    writeU16(cursor, offer.pokemon.snapshotVersion);
    std::memcpy(cursor + 2, offer.pokemon.storeVersions.data(), 4);
    writeU16(cursor + 6, offer.pokemon.leaderSpeciesId);
    cursor[8] = offer.pokemon.leaderLevel;
    cursor[9] = offer.pokemon.partyCount;
    cursor[10] = offer.pokemon.badgeCount;
    writeU16(cursor + 11, offer.pokemon.caughtCount);
    cursor += 13;
    cursor = putShortString(cursor, offer.pokemon.leaderName.data(), NAME_BYTES - 1);
  }
  if (offer.has(OFFER_HAS_BOOK)) {
    cursor = putShortString(cursor, offer.bookTitle.data(), MAX_BOOK_TITLE_BYTES);
    writeU16(cursor, offer.bookPercentBasisPoints);
    cursor += 2;
  }
  cursor = putShortString(cursor, offer.senderName.data(), NAME_BYTES - 1);
  written = static_cast<size_t>(cursor - output);
  return true;
}

bool decodeOffer(const uint8_t* data, const size_t length, Offer& output) {
  output = Offer{};
  if (!hasOfferTag(data, length) || length < 12) return false;
  const uint8_t* cursor = data + 4;
  const uint8_t* end = data + length;
  Offer offer;
  offer.totalBytes = readU32(cursor);
  offer.chunkBytes = readU16(cursor + 4);
  offer.flags = cursor[6];
  if (cursor[7] != CONTAINER_VERSION || (offer.flags & ~0x0FU) != 0) return false;
  if (!offer.has(OFFER_HAS_POKEMON) && !offer.has(OFFER_HAS_STATS) && !offer.has(OFFER_HAS_BOOK)) return false;
  if (offer.has(OFFER_MOVE_POKEMON) && !offer.has(OFFER_HAS_POKEMON)) return false;
  cursor += 8;
  if (offer.has(OFFER_HAS_POKEMON)) {
    if (end - cursor < 13) return false;
    offer.pokemon.snapshotVersion = readU16(cursor);
    std::memcpy(offer.pokemon.storeVersions.data(), cursor + 2, 4);
    offer.pokemon.leaderSpeciesId = readU16(cursor + 6);
    offer.pokemon.leaderLevel = cursor[8];
    offer.pokemon.partyCount = cursor[9];
    offer.pokemon.badgeCount = cursor[10];
    offer.pokemon.caughtCount = readU16(cursor + 11);
    cursor += 13;
    if (!takeShortString(cursor, end, offer.pokemon.leaderName)) return false;
  }
  if (offer.has(OFFER_HAS_BOOK)) {
    if (!takeShortString(cursor, end, offer.bookTitle) || end - cursor < 2) return false;
    offer.bookPercentBasisPoints = readU16(cursor);
    cursor += 2;
    if (offer.bookPercentBasisPoints > 10000 && offer.bookPercentBasisPoints != UNKNOWN_PERCENT) return false;
  }
  if (!takeShortString(cursor, end, offer.senderName) || cursor != end || offer.senderName[0] == '\0') return false;
  if (offer.totalBytes == 0 || offer.totalBytes > MAX_SECTION_BYTES * MAX_SECTIONS) return false;
  output = offer;
  return true;
}

bool encodeResult(const Result& result, uint8_t* output, const size_t capacity, size_t& written) {
  written = 0;
  if (result.statsLength != 0 && !isValidStatsPayload(result.stats.data(), result.statsLength)) return false;
  const size_t length = 1 + MAC_BYTES + 1 + result.statsLength;
  if (output == nullptr || capacity < length) return false;
  output[0] = result.status;
  std::memcpy(output + 1, result.deviceMac.data(), MAC_BYTES);
  output[1 + MAC_BYTES] = result.statsLength;
  std::memcpy(output + 2 + MAC_BYTES, result.stats.data(), result.statsLength);
  written = length;
  return true;
}

bool decodeResult(const uint8_t* data, const size_t length, Result& output) {
  output = Result{};
  if (data == nullptr || length == 0) return false;
  output.status = data[0];
  if (length == 1) return true;
  if (length < 2 + MAC_BYTES) return false;
  std::memcpy(output.deviceMac.data(), data + 1, MAC_BYTES);
  const size_t statsLength = data[1 + MAC_BYTES];
  if (length != 2 + MAC_BYTES + statsLength) return false;
  if (statsLength != 0 && !isValidStatsPayload(data + 2 + MAC_BYTES, statsLength)) return false;
  output.statsLength = static_cast<uint8_t>(statsLength);
  std::memcpy(output.stats.data(), data + 2 + MAC_BYTES, statsLength);
  return true;
}

}  // namespace nearby_sync
