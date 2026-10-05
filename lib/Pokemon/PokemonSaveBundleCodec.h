#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "PokemonTypes.h"

namespace pokemon {

// Wire format for moving a whole Pokemon save between two devices (nearby
// ESP-NOW transfer, see src/pokemon/PokemonSaveTransfer.h). A save is not one
// file: it is every double-buffered side store's slot files together, so the
// bundle carries all of them, byte for byte, behind one header:
//
//   header   magic "PKSB"(4) + bundleVersion(1) + entryCount(1) + reserved(2)
//            + SaveFormatVersions(6) + reserved(2)                     = 16 bytes
//   table    entryCount x { fileId(1) + reserved(3) + size(4) + crc32(4) } = 12 bytes each
//   tableCrc crc32 of header + table                                    = 4 bytes
//   data     every entry's raw file bytes, in table order
//
// Files are named only by a fileId into a fixed allowlist
// (saveBundleFileName()), never by a path from the wire, so a malformed or
// hostile bundle can never make the receiver write anywhere else. Slot files
// are copied as-is (both slots, sequence numbers and all): each store's own
// load-time inspection then picks the right slot on the receiving device
// exactly as it did on the sending one, with no re-encoding step that could
// drift from the stores' own codecs.

constexpr uint8_t SAVE_BUNDLE_VERSION = 1;
constexpr size_t SAVE_BUNDLE_HEADER_BYTES = 16;
constexpr size_t SAVE_BUNDLE_ENTRY_BYTES = 12;
constexpr size_t SAVE_BUNDLE_TABLE_CRC_BYTES = 4;
// Generous ceilings, far above any real store file (the largest, the main
// snapshot at its 1024-record cap, is ~50 KB) - only there so garbage sizes in
// a corrupt header are rejected before anything is allocated or written.
constexpr uint32_t SAVE_BUNDLE_MAX_FILE_BYTES = 512U * 1024U;
constexpr uint32_t SAVE_BUNDLE_MAX_TOTAL_BYTES = 2U * 1024U * 1024U;

// Every file a Pokemon save can consist of. Legacy names are included so a
// receiving device never keeps an old, pre-migration copy around that a store
// could later "migrate" over the transferred save.
enum class SaveBundleFile : uint8_t {
  MainA,
  MainB,
  MainLegacyA,
  MainLegacyB,
  BattleA,
  BattleB,
  BattleLegacy,
  MovesA,
  MovesB,
  IvEvA,
  IvEvB,
  HallOfFameA,
  HallOfFameB,
  Count,
};
constexpr size_t SAVE_BUNDLE_FILE_COUNT = static_cast<size_t>(SaveBundleFile::Count);
constexpr size_t SAVE_BUNDLE_MAX_TABLE_BYTES =
    SAVE_BUNDLE_HEADER_BYTES + SAVE_BUNDLE_FILE_COUNT * SAVE_BUNDLE_ENTRY_BYTES + SAVE_BUNDLE_TABLE_CRC_BYTES;

// Bare file name (no directory) for a file id, or nullptr if out of range.
const char* saveBundleFileName(uint8_t fileId);

// The save-format version each store writes on the device that built the
// bundle. A receiver refuses a bundle with any version newer than its own
// (older is fine - every store's decoder already migrates older layouts).
struct SaveFormatVersions {
  uint16_t snapshot = 0;
  uint8_t battle = 0;
  uint8_t ivEv = 0;
  uint8_t moveset = 0;
  uint8_t hallOfFame = 0;

  bool operator==(const SaveFormatVersions&) const = default;
};

// This firmware's own versions (the *_VERSION constants of every store codec).
SaveFormatVersions currentSaveFormatVersions();
bool saveFormatCompatible(const SaveFormatVersions& incoming, const SaveFormatVersions& local);

struct SaveBundleEntry {
  uint8_t fileId = 0;
  uint32_t size = 0;
  uint32_t crc32 = 0;
};

struct SaveBundleHeader {
  SaveFormatVersions versions{};
  uint8_t entryCount = 0;
  std::array<SaveBundleEntry, SAVE_BUNDLE_FILE_COUNT> entries{};

  // Bytes of header + table + table CRC, i.e. where the first file's data starts.
  size_t tableBytes() const {
    return SAVE_BUNDLE_HEADER_BYTES + entryCount * SAVE_BUNDLE_ENTRY_BYTES + SAVE_BUNDLE_TABLE_CRC_BYTES;
  }
  uint64_t totalBytes() const;
};

// Encodes header + table + table CRC into `output` (at least
// SAVE_BUNDLE_MAX_TABLE_BYTES). False on an invalid header (duplicate or
// unknown file id, oversized entry).
bool encodeSaveBundleTable(const SaveBundleHeader& header, uint8_t* output, size_t capacity, size_t& written);

enum class SaveBundleParse : uint8_t {
  Ok,
  NeedMoreBytes,  // `available` is shorter than the table it announces
  Invalid,        // bad magic, CRC, file id, size...
  UnsupportedVersion,
};

// Parses header + table from the start of a bundle. `available` may be the
// whole bundle or just its first SAVE_BUNDLE_MAX_TABLE_BYTES.
SaveBundleParse decodeSaveBundleTable(const uint8_t* data, size_t available, SaveBundleHeader& output);

// Small summary of the save, sent in the transfer offer so the receiver can
// see what it is about to accept before any data moves.
struct SaveBundleSummary {
  uint16_t leaderSpeciesId = 0;
  uint8_t leaderLevel = 0;
  uint8_t partyCount = 0;
  uint8_t badgeCount = 0;
  uint16_t caughtCount = 0;
  std::array<char, POKEMON_NICKNAME_BYTES> leaderName{};

  bool operator==(const SaveBundleSummary&) const = default;
};

struct SaveBundleOffer {
  uint32_t totalBytes = 0;
  uint16_t chunkBytes = 0;
  bool moveSave = false;  // sender will clear its own copy once the receiver confirms
  SaveFormatVersions versions{};
  SaveBundleSummary summary{};
  std::array<char, 33> senderName{};

  bool operator==(const SaveBundleOffer&) const = default;
};

constexpr size_t SAVE_BUNDLE_OFFER_MAX_BYTES = 4 + 4 + 2 + 1 + 1 + 6 + 9 + 1 + POKEMON_NICKNAME_BYTES + 1 + 33;

// Tag carried by Discover/Advertise/Offer payloads so a Pokemon save transfer
// never pairs with the book/file transfer or stats sync screens (which share
// the same ESP-NOW packet types).
constexpr std::array<uint8_t, 4> SAVE_BUNDLE_TAG = {'P', 'K', 'S', 'B'};
bool hasSaveBundleTag(const uint8_t* data, size_t length);

bool encodeSaveBundleOffer(const SaveBundleOffer& offer, uint8_t* output, size_t capacity, size_t& written);
bool decodeSaveBundleOffer(const uint8_t* data, size_t length, SaveBundleOffer& output);

}  // namespace pokemon
