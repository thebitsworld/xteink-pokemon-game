#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "PokemonBattle.h"
#include "PokemonBattleTypes.h"

namespace pokemon {

constexpr size_t POKEMON_BATTLE_ENTRY_BYTES = 16;
constexpr size_t POKEMON_BATTLE_MAX_ENTRIES = 6;  // one per Party slot; PC-boxed Pokemon carry no live battle state
constexpr size_t POKEMON_BATTLE_FILE_CRC_BYTES = 4;
constexpr size_t POKEMON_BATTLE_FILE_MAX_BYTES =
    POKEMON_BATTLE_MAX_ENTRIES * POKEMON_BATTLE_ENTRY_BYTES + POKEMON_BATTLE_FILE_CRC_BYTES;

using BattleEntryBytes = std::array<uint8_t, POKEMON_BATTLE_ENTRY_BYTES>;
using BattleStoreFileBytes = std::array<uint8_t, POKEMON_BATTLE_FILE_MAX_BYTES>;

// A Party member's live battle state: which of its 4 moves it currently
// knows and how much PP each has left, its current HP, and any ailment.
// This is intentionally the *only* place PP/HP/status persist - unlike
// PokemonRecord (the 48-byte record in the main save), this data is fully
// reconstructible from the record's level and species learnset, so it lives
// in a small side file that never needs the main store's double-buffering.
struct BattleRecordEntry {
  uint32_t recordId = 0;  // 0 = unused slot, same "no gaps, zero at the end" convention as PokemonState::partyRecordIds
  std::array<uint8_t, BATTLE_MOVE_SLOTS> moves{};
  std::array<uint8_t, BATTLE_MOVE_SLOTS> pp{};
  uint16_t currentHp = 0;
  Ailment status = Ailment::None;
  uint8_t statusTurns = 0;

  bool operator==(const BattleRecordEntry&) const = default;
};

// Fixed-capacity mirror of PokemonState::partyRecordIds: non-zero-recordId
// entries packed at the front, ascending by recordId, zero-filled entries
// (if any) trailing. No separate count field - `pokemon::battleEntryCount`
// derives it the same way pendingEventCount() does for pending events.
struct BattleStoreState {
  std::array<BattleRecordEntry, POKEMON_BATTLE_MAX_ENTRIES> entries{};

  bool operator==(const BattleStoreState&) const = default;
};

size_t battleEntryCount(const BattleStoreState& state);
const BattleRecordEntry* findBattleEntry(const BattleStoreState& state, uint32_t recordId);
// Inserts (keeping ascending recordId order) or replaces an existing entry
// with the same recordId. Fails if recordId == 0, the entry fails
// validateBattleRecordEntry, or the state is already at capacity and
// recordId is not already present.
bool upsertBattleEntry(BattleStoreState& state, const BattleRecordEntry& entry);
// Removes an entry (e.g. a Party member was deposited to the PC) without
// leaving a gap. Returns false if recordId was not present.
bool removeBattleEntry(BattleStoreState& state, uint32_t recordId);

bool validateBattleRecordEntry(const BattleRecordEntry& entry);
bool validateBattleStoreState(const BattleStoreState& state);
bool encodeBattleRecordEntry(const BattleRecordEntry& entry, BattleEntryBytes& output);
bool decodeBattleRecordEntry(const BattleEntryBytes& bytes, BattleRecordEntry& output);

uint32_t updateBattleStoreCrc32(uint32_t crc, const uint8_t* data, size_t size);
constexpr uint32_t finishBattleStoreCrc32(const uint32_t crc) { return crc ^ 0xFFFFFFFFU; }
constexpr uint32_t BATTLE_STORE_CRC32_INITIAL = 0xFFFFFFFFU;

// Encodes `state` into `output`/`outputSize`: entries back to back (only the
// non-zero-recordId ones, i.e. battleEntryCount(state) of them), then a
// trailing CRC32 over everything written before it. Fails if the state does
// not pass validateBattleStoreState.
bool encodeBattleStoreFile(const BattleStoreState& state, BattleStoreFileBytes& output, size_t& outputSize);

// Decodes and fully verifies a battle-store file of exactly `size` bytes:
// size must be entryCount*16+4 for some 0 <= entryCount <=
// POKEMON_BATTLE_MAX_ENTRIES, the CRC32 must match, and the decoded state
// must pass validateBattleStoreState (ascending, no duplicate recordId,
// every entry individually valid). Returns false for anything else,
// including a plain garbled/truncated file - the caller's response is to
// treat every entry as "unknown, rebuild from the learnset," never to
// surface an error to the player.
bool decodeBattleStoreFile(const uint8_t* data, size_t size, BattleStoreState& output);

}  // namespace pokemon
