#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "PokemonBattle.h"

namespace pokemon {

// recordId(4) + moves[BATTLE_MOVE_SLOTS](4) + ppUp[BATTLE_MOVE_SLOTS](4), no packing tricks.
constexpr size_t POKEMON_MOVESET_ENTRY_BYTES = 12;
// The most simultaneously-live records is PC_BOX_MAX_RECORDS (512) + PARTY_SIZE
// (6) = 518 (see PokemonService.h and PokemonIvEvStoreCodec.h's identical
// arithmetic), so every live record always has room for an entry here.
// Nothing outside PokemonService needs the individual constants, so they are
// repeated rather than pulling PokemonService.h into this pure codec.
constexpr size_t POKEMON_MOVESET_MAX_ENTRIES = 518;
constexpr size_t POKEMON_MOVESET_FILE_CRC_BYTES = 4;
// Header: magic "PKMV" (4) + version (1) + entryCount (2, u16) + sequence (4).
constexpr size_t POKEMON_MOVESET_HEADER_BYTES = 11;
constexpr uint8_t POKEMON_MOVESET_STORE_VERSION = 1;
constexpr size_t POKEMON_MOVESET_FILE_MAX_BYTES = POKEMON_MOVESET_HEADER_BYTES +
                                                  POKEMON_MOVESET_MAX_ENTRIES * POKEMON_MOVESET_ENTRY_BYTES +
                                                  POKEMON_MOVESET_FILE_CRC_BYTES;

using MovesetEntryBytes = std::array<uint8_t, POKEMON_MOVESET_ENTRY_BYTES>;
using MovesetStoreFileBytes = std::array<uint8_t, POKEMON_MOVESET_FILE_MAX_BYTES>;

// The part of a Pokemon's battle data that CANNOT be recomputed and must
// survive for as long as the Pokemon exists, wherever it is (Party or PC
// Box): which moves it knows (TM/HM teaching, learn/forget choices) and how
// many PP Ups each slot has had. Deliberately separate from the small,
// party-sized, frequently-rewritten pokemon-battle-{a,b}.bin, which holds
// the transient rest (HP/PP/status) and is only ever big enough for the
// current Party - see PokemonService::persistBattleEntry().
//
// A Pokemon that never customised its moves simply has no entry: its moveset
// is then derived from species and level exactly as before.
struct MovesetEntry {
  uint32_t recordId = 0;  // 0 = unused slot, same convention as BattleRecordEntry
  std::array<uint8_t, BATTLE_MOVE_SLOTS> moves{};
  std::array<uint8_t, BATTLE_MOVE_SLOTS> ppUp{};

  bool operator==(const MovesetEntry&) const = default;
};

// Non-zero-recordId entries packed at the front in ascending recordId order,
// zero-filled entries (if any) trailing - same shape as IvEvStoreState.
struct MovesetStoreState {
  std::array<MovesetEntry, POKEMON_MOVESET_MAX_ENTRIES> entries{};

  bool operator==(const MovesetStoreState&) const = default;
};

size_t movesetEntryCount(const MovesetStoreState& state);
const MovesetEntry* findMovesetEntry(const MovesetStoreState& state, uint32_t recordId);
// Inserts (keeping ascending recordId order) or replaces an entry with the
// same recordId. Fails if the entry fails validateMovesetEntry, or the state
// is at capacity and recordId is not already present.
bool upsertMovesetEntry(MovesetStoreState& state, const MovesetEntry& entry);
// Removes one entry, shifting later ones down. False (no change) if absent.
bool removeMovesetEntry(MovesetStoreState& state, uint32_t recordId);

// Same rules validateBattleRecordEntry applies to the moves/ppUp it shares:
// moves packed at the front, ids within POKEMON_MOVE_ID_MAX, ppUp <= 3 and 0
// on an empty slot.
bool validateMovesetEntry(const MovesetEntry& entry);
bool validateMovesetStoreState(const MovesetStoreState& state);
bool encodeMovesetEntry(const MovesetEntry& entry, MovesetEntryBytes& output);
bool decodeMovesetEntry(const MovesetEntryBytes& bytes, MovesetEntry& output);

uint32_t updateMovesetStoreCrc32(uint32_t crc, const uint8_t* data, size_t size);
constexpr uint32_t finishMovesetStoreCrc32(const uint32_t crc) { return crc ^ 0xFFFFFFFFU; }
constexpr uint32_t MOVESET_STORE_CRC32_INITIAL = 0xFFFFFFFFU;

// Encodes `state` (header, the non-empty entries back to back, trailing
// CRC32) straight into `output`. Fails for sequence 0 (reserved for "no valid
// slot yet"), an invalid state, or a buffer too small for the actual entry
// count. Writes directly into the caller's buffer rather than a second local
// one - see PokemonIvEvStoreCodec.cpp's encodeIvEvStoreFile() for the field
// crashes that pattern came from.
bool encodeMovesetStoreFile(const MovesetStoreState& state, uint32_t sequence, uint8_t* output, size_t outputCapacity,
                            size_t& outputSize);

template <size_t N>
bool encodeMovesetStoreFile(const MovesetStoreState& state, const uint32_t sequence, std::array<uint8_t, N>& output,
                            size_t& outputSize) {
  return encodeMovesetStoreFile(state, sequence, output.data(), output.size(), outputSize);
}

// Decodes and fully verifies a store file of exactly `size` bytes (magic,
// version, entry count <= POKEMON_MOVESET_MAX_ENTRIES, exact length, CRC32,
// every entry valid and strictly ascending by recordId). `output` is only
// written once the whole file is known good; `sequence` lets the caller pick
// the newer of the two alternating files.
bool decodeMovesetStoreFile(const uint8_t* data, size_t size, MovesetStoreState& output, uint32_t& sequence);

}  // namespace pokemon
