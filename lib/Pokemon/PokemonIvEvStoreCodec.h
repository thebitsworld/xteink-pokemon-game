#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "PokemonBattleTypes.h"

namespace pokemon {

constexpr size_t POKEMON_IVEV_ENTRY_BYTES = 14;  // recordId(4) + iv(5) + ev(5), no bit-packing - see .cpp
// The resident cap: sizes IvEvStoreState::entries (the array actually kept
// in RAM, both as PokemonIvEvStore's own member and as decode/verify
// scratch), and the write-side cap upsertIvEvEntry() enforces. Deliberately
// smaller than PokemonService::resolveEncounter()'s own 1024-record hard
// cap on the *main* save - a realistic PC Box now has its own explicit
// 512-entry cap (see PC_BOX_MAX_RECORDS / the Release feature in
// PokemonService), and 512 + the party's 6 covers every record that can
// exist once that cap is enforced. A record that somehow still can't get a
// slot (e.g. a save written before this cap existed) doesn't crash or
// corrupt anything - PokemonService::ensureIvEv() already tolerates a
// "not found" entry by rolling a fresh, unpersisted one.
constexpr size_t POKEMON_IVEV_MAX_ENTRIES = 512;
// The largest entry count any previously-shipped build could have written
// (when this cap was 1024) - decodeIvEvStoreFile() accepts an input file up
// to THIS size (so an old, larger file isn't discarded as corrupt/oversized
// outright) even though it only ever keeps the first POKEMON_IVEV_MAX_ENTRIES
// of it; the caller reads no more file bytes than
// POKEMON_IVEV_LEGACY_FILE_MAX_BYTES for exactly this reason. Should never
// need to grow again - not intended as a general "future cap increase"
// knob, just a one-time backward-compatibility ceiling for the 1024->512
// change itself.
constexpr size_t POKEMON_IVEV_LEGACY_MAX_ENTRIES = 1024;
constexpr size_t POKEMON_IVEV_FILE_CRC_BYTES = 4;
// Header: magic "PKIV" (4) + version (1) + entryCount (2, u16 - unlike the
// battle store's u8, this file's count can plausibly exceed 255) + sequence
// (4).
constexpr size_t POKEMON_IVEV_HEADER_BYTES = 11;
constexpr uint8_t POKEMON_IVEV_STORE_VERSION = 1;
constexpr size_t POKEMON_IVEV_FILE_MAX_BYTES = POKEMON_IVEV_HEADER_BYTES +
                                               POKEMON_IVEV_MAX_ENTRIES * POKEMON_IVEV_ENTRY_BYTES +
                                               POKEMON_IVEV_FILE_CRC_BYTES;
// The buffer size decodeIvEvStoreFile() actually accepts input into - sized
// for POKEMON_IVEV_LEGACY_MAX_ENTRIES, not POKEMON_IVEV_MAX_ENTRIES, so a
// file written under the old, larger cap can still be read (its tail is
// dropped, not the whole file rejected). This is only ever a transient
// heap-allocated read buffer (PokemonIvEvStore.cpp's inspectSlot()), never
// resident, so keeping it at the legacy size costs nothing ongoing.
constexpr size_t POKEMON_IVEV_LEGACY_FILE_MAX_BYTES = POKEMON_IVEV_HEADER_BYTES +
                                                      POKEMON_IVEV_LEGACY_MAX_ENTRIES * POKEMON_IVEV_ENTRY_BYTES +
                                                      POKEMON_IVEV_FILE_CRC_BYTES;

using IvEvEntryBytes = std::array<uint8_t, POKEMON_IVEV_ENTRY_BYTES>;
using IvEvStoreFileBytes = std::array<uint8_t, POKEMON_IVEV_LEGACY_FILE_MAX_BYTES>;

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
// Removes the entry with the given recordId, shifting later entries down to
// keep the array packed at the front. Returns false (no change) if no entry
// with that recordId exists. Mirrors removeBattleEntry()'s exact shape.
bool removeIvEvEntry(IvEvStoreState& state, uint32_t recordId);

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
// magic/version must match, entryCount must be <= POKEMON_IVEV_LEGACY_MAX_ENTRIES
// (entries past POKEMON_IVEV_MAX_ENTRIES are dropped, not rejected - see
// that constant's own doc comment)
// with `size` matching it exactly, the CRC32 must match, and the decoded
// state must pass validateIvEvStoreState. `sequence` (output) lets the
// caller pick the newer of the two alternating files, same as every other
// double-buffered store in this project.
bool decodeIvEvStoreFile(const uint8_t* data, size_t size, IvEvStoreState& output, uint32_t& sequence);

}  // namespace pokemon
