#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Wire format for "Sync with nearby reader": one ESP-NOW transfer that carries
// everything worth moving between two readers in a single container.
//
//   header   magic "NSYC"(4) + version(1) + sectionCount(1) + reserved(2)
//            + sender device MAC(6) + reserved(2)                       = 16 bytes
//   table    sectionCount x { type(1) + reserved(3) + size(4) + crc32(4) } = 12 bytes each
//   tableCrc crc32 of header + table                                     = 4 bytes
//   data     every section's bytes, in table order
//
// Sections are independent and optional; a receiver applies the ones it
// understands. Pure code (no storage, no radio) so the host tests cover it.
namespace nearby_sync {

constexpr uint8_t CONTAINER_VERSION = 1;
constexpr size_t HEADER_BYTES = 16;
constexpr size_t SECTION_ENTRY_BYTES = 12;
constexpr size_t TABLE_CRC_BYTES = 4;
constexpr size_t MAX_SECTIONS = 4;
constexpr size_t MAX_TABLE_BYTES = HEADER_BYTES + MAX_SECTIONS * SECTION_ENTRY_BYTES + TABLE_CRC_BYTES;
constexpr size_t MAC_BYTES = 6;
constexpr uint32_t MAX_SECTION_BYTES = 2U * 1024U * 1024U;
// The book file itself may be much bigger than any other section.
constexpr uint32_t MAX_BOOK_FILE_BYTES = 64U * 1024U * 1024U;
constexpr uint32_t MAX_CONTAINER_BYTES = 3U * MAX_SECTION_BYTES + MAX_BOOK_FILE_BYTES + 256U;

enum class SectionType : uint8_t {
  PokemonSave = 1,   // a Pokemon save bundle (PokemonSaveBundleCodec.h), byte for byte
  ReadingStats = 2,  // the sender's /.crosspoint/global_stats.bin
  BookPosition = 3,  // BookPositionRecord, see below
  BookFile = 4,      // the EPUB itself, only when the receiver asked for it (it had no copy)
};

// Largest size a section of `type` may declare.
uint32_t maxSectionBytes(SectionType type);

struct Section {
  SectionType type = SectionType::ReadingStats;
  uint32_t size = 0;
  uint32_t crc32 = 0;
};

struct ContainerHeader {
  std::array<uint8_t, MAC_BYTES> senderMac{};
  uint8_t sectionCount = 0;
  std::array<Section, MAX_SECTIONS> sections{};

  size_t tableBytes() const { return HEADER_BYTES + sectionCount * SECTION_ENTRY_BYTES + TABLE_CRC_BYTES; }
  uint64_t totalBytes() const;
  // Index of the section of `type`, or -1.
  int find(SectionType type) const;
  // Byte offset of section `index`'s data from the start of the container.
  uint64_t sectionOffset(size_t index) const;
};

uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t size);
constexpr uint32_t CRC_INITIAL = 0xFFFFFFFFU;
inline uint32_t crc32Of(const uint8_t* data, const size_t size) {
  return crc32Update(CRC_INITIAL, data, size) ^ CRC_INITIAL;
}

bool encodeContainerTable(const ContainerHeader& header, uint8_t* output, size_t capacity, size_t& written);

enum class ParseResult : uint8_t { Ok, NeedMoreBytes, Invalid, UnsupportedVersion };
ParseResult decodeContainerTable(const uint8_t* data, size_t available, ContainerHeader& output);

// ---- Book position section ------------------------------------------------
//
// pathLength(2) path  titleLength(1) title  percentBasisPoints(2)
// progressLength(1) progress
//
// `progress` is the EPUB's progress.bin exactly as the reader stores it
// (spine, page, page count and - in the 10-byte form - the visible-text offset
// the reader uses to land on the same text under a different layout, e.g. an
// X3 position opened on an X4 Pro).

constexpr size_t MAX_BOOK_PATH_BYTES = 255;
constexpr size_t MAX_BOOK_TITLE_BYTES = 64;
constexpr size_t MAX_PROGRESS_BYTES = 10;
constexpr uint16_t UNKNOWN_PERCENT = 0xFFFF;
constexpr size_t MAX_BOOK_RECORD_BYTES = 2 + MAX_BOOK_PATH_BYTES + 1 + MAX_BOOK_TITLE_BYTES + 2 + 1 + MAX_PROGRESS_BYTES;

struct BookPositionRecord {
  std::array<char, MAX_BOOK_PATH_BYTES + 1> path{};
  std::array<char, MAX_BOOK_TITLE_BYTES + 1> title{};
  uint16_t percentBasisPoints = UNKNOWN_PERCENT;  // 0..10000
  uint8_t progressLength = 0;                     // 4, 6 or 10
  std::array<uint8_t, MAX_PROGRESS_BYTES> progress{};

  bool operator==(const BookPositionRecord&) const = default;
};

bool encodeBookPosition(const BookPositionRecord& record, uint8_t* output, size_t capacity, size_t& written);
bool decodeBookPosition(const uint8_t* data, size_t length, BookPositionRecord& output);

// Base name of a '/'-separated path (the part after the last '/').
const char* baseName(const char* path);

// ---- Reading stats section -------------------------------------------------

// Mirrors NearbyStatsSyncActivity's own acceptance rule for a global_stats.bin
// payload (v1: 13 bytes, v2: 17 bytes, v3: 159 bytes).
constexpr size_t MAX_STATS_BYTES = 159;
bool isValidStatsPayload(const uint8_t* data, size_t size);

// ---- Offer -------------------------------------------------------------------
//
// Sent before any data so the receiver can see what is coming and accept or
// decline. Every Discover/Advertise/Offer payload starts with OFFER_TAG so this
// never pairs with the book transfer, stats sync or position sync screens.

constexpr std::array<uint8_t, 4> OFFER_TAG = {'N', 'S', 'Y', 'N'};
bool hasOfferTag(const uint8_t* data, size_t length);

constexpr uint8_t OFFER_MOVE_POKEMON = 1U << 0;
constexpr uint8_t OFFER_HAS_POKEMON = 1U << 1;
constexpr uint8_t OFFER_HAS_STATS = 1U << 2;
constexpr uint8_t OFFER_HAS_BOOK = 1U << 3;

constexpr size_t NAME_BYTES = 33;

struct PokemonOfferSummary {
  // Raw SaveFormatVersions (snapshot u16, battle, ivEv, moveset, hallOfFame) -
  // kept as plain numbers so this codec does not depend on lib/Pokemon.
  uint16_t snapshotVersion = 0;
  std::array<uint8_t, 4> storeVersions{};
  uint16_t leaderSpeciesId = 0;
  uint8_t leaderLevel = 0;
  uint8_t partyCount = 0;
  uint8_t badgeCount = 0;
  uint16_t caughtCount = 0;
  std::array<char, NAME_BYTES> leaderName{};

  bool operator==(const PokemonOfferSummary&) const = default;
};

struct Offer {
  uint32_t totalBytes = 0;
  uint16_t chunkBytes = 0;
  uint8_t flags = 0;
  PokemonOfferSummary pokemon{};
  std::array<char, MAX_BOOK_TITLE_BYTES + 1> bookTitle{};
  uint16_t bookPercentBasisPoints = UNKNOWN_PERCENT;
  // The sender's path to the book, so the receiver can tell whether it already
  // has it, and the file's size (0 = the sender cannot send the file itself).
  std::array<char, MAX_BOOK_PATH_BYTES + 1> bookPath{};
  uint32_t bookFileBytes = 0;
  std::array<char, NAME_BYTES> senderName{};

  bool has(const uint8_t flag) const { return (flags & flag) != 0; }
  bool operator==(const Offer&) const = default;
};

constexpr size_t OFFER_MAX_BYTES = 4 + 4 + 2 + 1 + 1 + 2 + 4 + 2 + 1 + 1 + 1 + 2 + 1 + (NAME_BYTES - 1) + 1 +
                                   MAX_BOOK_TITLE_BYTES + 2 + 1 + MAX_BOOK_PATH_BYTES + 4 + 1 + (NAME_BYTES - 1);

bool encodeOffer(const Offer& offer, uint8_t* output, size_t capacity, size_t& written);
// Container size once a BookFile section of `bookFileBytes` is added to an
// offer's container (one more table entry plus the file).
inline uint64_t totalWithBookFile(const Offer& offer) {
  return static_cast<uint64_t>(offer.totalBytes) + SECTION_ENTRY_BYTES + offer.bookFileBytes;
}

// The receiver's Accept: chunkBytes(2) + flags(1). ACCEPT_WANTS_BOOK_FILE asks
// the sender to add the book file because this reader has no copy of it.
constexpr uint8_t ACCEPT_WANTS_BOOK_FILE = 1U << 0;
bool decodeOffer(const uint8_t* data, size_t length, Offer& output);

// ---- Result ------------------------------------------------------------------
//
// The receiver's final answer: status(1) + its device MAC(6) + statsLength(1)
// + its own global_stats.bin, so one sync updates the stats of BOTH readers.

constexpr uint8_t RESULT_OK = 0;
constexpr uint8_t RESULT_FAILED = 1;
constexpr size_t RESULT_MAX_BYTES = 1 + MAC_BYTES + 1 + MAX_STATS_BYTES;

struct Result {
  uint8_t status = RESULT_FAILED;
  std::array<uint8_t, MAC_BYTES> deviceMac{};
  uint8_t statsLength = 0;
  std::array<uint8_t, MAX_STATS_BYTES> stats{};
};

bool encodeResult(const Result& result, uint8_t* output, size_t capacity, size_t& written);
// Also accepts the bare 1-byte status a failure path may send.
bool decodeResult(const uint8_t* data, size_t length, Result& output);

}  // namespace nearby_sync
