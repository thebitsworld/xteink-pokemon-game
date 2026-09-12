#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "PokemonBattleTypes.h"

namespace pokemon {

constexpr size_t POKEMON_IVEV_ENTRY_BYTES = 14;  // recordId(4) + iv(5) + ev(5), no bit-packing - see .cpp
// Matches PokemonService::resolveEncounter()'s own hard cap on total records
// (`store_.recordCount() >= 1024U`) - unlike pokemon-battle-{a,b}.bin (which
// only tracks the 6 active Party members), this file covers every record a
// save can ever hold, party and PC box both, so it needs the same ceiling
// the main store already enforces rather than inventing a smaller one of its
// own that could bind first.
constexpr size_t POKEMON_IVEV_MAX_ENTRIES = 1024;
constexpr size_t POKEMON_IVEV_FILE_CRC_BYTES = 4;
// Header: magic "PKIV" (4) + version (1) + entryCount (2, u16 - unlike the
// battle store's u8, this file's count can plausibly exceed 255) + sequence
// (4).
constexpr size_t POKEMON_IVEV_HEADER_BYTES = 11;
constexpr uint8_t POKEMON_IVEV_STORE_VERSION = 1;
constexpr size_t POKEMON_IVEV_FILE_MAX_BYTES = POKEMON_IVEV_HEADER_BYTES +
                                               POKEMON_IVEV_MAX_ENTRIES * POKEMON_IVEV_ENTRY_BYTES +
                                               POKEMON_IVEV_FILE_CRC_BYTES;

using IvEvEntryBytes = std::array<uint8_t, POKEMON_IVEV_ENTRY_BYTES>;
using IvEvStoreFileBytes = std::array<uint8_t, POKEMON_IVEV_FILE_MAX_BYTES>;

// A Pokemon's permanent individual variance: IVs (0-15 per stat, rolled once
// at creation - catch, starter pick, gift/event - and never changed again)
// and EVs (0-255 per stat, accumulated from battle wins - see
// PokemonService::awardBattleXp()). Lives in its own side file rather than
// growing PokemonRecord (which has only 1 spare byte, nowhere near enough -
// see docs/development/pokemon-iv-ev-plan.md) or reusing
// pokemon-battle-{a,b}.bin (which only covers the 6 active Party members and
// is designed to be lossy/reconstructible - wrong fit for a permanent,
// PC-box-inclusive attribute).
//
// EVs here are a deliberate simplification of real Gen 1 (which ran
// 0-65,535 per stat with a floor(sqrt(EV)/4) bonus curve): capped at 0-255
// with a flat EV/4 bonus instead. The two formulas reach the *same* maximum
// bonus (63), so the ceiling is authentic even though the climb curve and
// the on-disk cost (1 byte/stat instead of 2) are both simpler.
struct IvEvEntry {
  uint32_t recordId = 0;  // 0 = unused slot, same convention as BattleRecordEntry
  std::array<uint8_t, STAT_COUNT> iv{};  // each 0-15
  std::array<uint8_t, STAT_COUNT> ev{};  // each 0-255

  bool operator==(const IvEvEntry&) const = default;
};

// Fixed-capacity mirror of BattleStoreState's own shape: non-zero-recordId
// entries packed at the front in ascending recordId order, zero-filled
// entries (if any) trailing.
struct IvEvStoreState {
  std::array<IvEvEntry, POKEMON_IVEV_MAX_ENTRIES> entries{};

  bool operator==(const IvEvStoreState&) const = default;
};

size_t ivEvEntryCount(const IvEvStoreState& state);
const IvEvEntry* findIvEvEntry(const IvEvStoreState& state, uint32_t recordId);
// Inserts (keeping ascending recordId order) or replaces an existing entry
// with the same recordId. Fails if recordId == 0, the entry fails
// validateIvEvEntry, or the state is already at capacity and recordId is not
// already present.
bool upsertIvEvEntry(IvEvStoreState& state, const IvEvEntry& entry);

bool validateIvEvEntry(const IvEvEntry& entry);
bool validateIvEvStoreState(const IvEvStoreState& state);
bool encodeIvEvEntry(const IvEvEntry& entry, IvEvEntryBytes& output);
bool decodeIvEvEntry(const IvEvEntryBytes& bytes, IvEvEntry& output);

uint32_t updateIvEvStoreCrc32(uint32_t crc, const uint8_t* data, size_t size);
constexpr uint32_t finishIvEvStoreCrc32(const uint32_t crc) { return crc ^ 0xFFFFFFFFU; }
constexpr uint32_t IVEV_STORE_CRC32_INITIAL = 0xFFFFFFFFU;

// Encodes `state` into `output`/`outputSize`: a header (magic, version,
// entryCount, `sequence`), then entries back to back (only the non-zero-
// recordId ones), then a trailing CRC32. Fails if `sequence` is 0 (reserved
// for "no valid slot yet") or the state does not pass validateIvEvStoreState.
bool encodeIvEvStoreFile(const IvEvStoreState& state, uint32_t sequence, IvEvStoreFileBytes& output,
                         size_t& outputSize);

// Decodes and fully verifies an IV/EV store file of exactly `size` bytes:
// magic/version must match, entryCount must be <= POKEMON_IVEV_MAX_ENTRIES
// with `size` matching it exactly, the CRC32 must match, and the decoded
// state must pass validateIvEvStoreState. `sequence` (output) lets the
// caller pick the newer of the two alternating files, same as every other
// double-buffered store in this project.
bool decodeIvEvStoreFile(const uint8_t* data, size_t size, IvEvStoreState& output, uint32_t& sequence);

}  // namespace pokemon
