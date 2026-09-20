#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "PokemonBattle.h"
#include "PokemonBattleTypes.h"

namespace pokemon {

// v1 (original format) entries were 16 bytes with no PP-Up counters; v2
// added a per-slot ppUp[BATTLE_MOVE_SLOTS] (20 bytes); v3 adds a single
// toxicCounter byte (see BattleRecordEntry below, and its doc comment for
// why), 21 bytes. Both the header's version byte and each entry's actual
// on-disk size follow the format version - decodeBattleStoreFile() branches
// on the header's version to pick the right entry size and decode function,
// exactly the way PokemonStoreCodec's own decodeState() branches on its own
// version to handle old vs. new PokemonState layouts. Encoding always
// writes the current (latest) version.
constexpr size_t POKEMON_BATTLE_ENTRY_BYTES_V1 = 16;
constexpr size_t POKEMON_BATTLE_ENTRY_BYTES_V2 = 20;
constexpr size_t POKEMON_BATTLE_ENTRY_BYTES = 21;
constexpr size_t POKEMON_BATTLE_MAX_ENTRIES = 6;  // one per Party slot; PC-boxed Pokemon carry no live battle state
constexpr size_t POKEMON_BATTLE_FILE_CRC_BYTES = 4;
// Header: magic "PKBT" (4) + version (1) + entryCount (1) + sequence (4). A
// Pokemon's actual moveset can now diverge from what defaultMovesetForLevel()
// would synthesize (TM/HM teaching, the Moveset screen's learn/forget) - it
// is no longer purely a cache of the main save, so this file is protected
// the same way as pokemon-{a,b}.bin: two alternating files plus a sequence
// number, never overwriting the currently-active slot in place.
constexpr size_t POKEMON_BATTLE_HEADER_BYTES = 10;
constexpr uint8_t POKEMON_BATTLE_STORE_VERSION_V1 = 1;  // legacy: no ppUp counters
constexpr uint8_t POKEMON_BATTLE_STORE_VERSION_V2 = 2;  // legacy: adds ppUp[BATTLE_MOVE_SLOTS], no toxicCounter
constexpr uint8_t POKEMON_BATTLE_STORE_VERSION = 3;     // current: adds toxicCounter
constexpr size_t POKEMON_BATTLE_FILE_MAX_BYTES = POKEMON_BATTLE_HEADER_BYTES +
                                                 POKEMON_BATTLE_MAX_ENTRIES * POKEMON_BATTLE_ENTRY_BYTES +
                                                 POKEMON_BATTLE_FILE_CRC_BYTES;
// The legacy single-file format (no header, no double-buffering) this
// replaces: just entries back to back plus a trailing CRC32. Kept only so a
// pre-existing pokemon-battle.bin can be migrated once into the new format.
// Predates PP Up entirely, so it always uses the v1 (16-byte) entry size.
constexpr size_t POKEMON_BATTLE_LEGACY_FILE_MAX_BYTES =
    POKEMON_BATTLE_MAX_ENTRIES * POKEMON_BATTLE_ENTRY_BYTES_V1 + POKEMON_BATTLE_FILE_CRC_BYTES;

using BattleEntryBytes = std::array<uint8_t, POKEMON_BATTLE_ENTRY_BYTES>;
using BattleEntryBytesV1 = std::array<uint8_t, POKEMON_BATTLE_ENTRY_BYTES_V1>;
using BattleEntryBytesV2 = std::array<uint8_t, POKEMON_BATTLE_ENTRY_BYTES_V2>;
using BattleStoreFileBytes = std::array<uint8_t, POKEMON_BATTLE_FILE_MAX_BYTES>;
using BattleStoreLegacyFileBytes = std::array<uint8_t, POKEMON_BATTLE_LEGACY_FILE_MAX_BYTES>;

// A Party member's live battle state: which of its 4 moves it currently
// knows and how much PP each has left, its current HP, and any ailment.
// This is intentionally the *only* place PP/HP/status persist - unlike
// PokemonRecord (the 48-byte record in the main save), it lives in a small
// side file rather than growing the record. HP/PP/status alone would be
// reconstructible from the record's level and species learnset, but the
// moveset is not (see PokemonBattleStore.h), so this file is still
// double-buffered like the main save.
struct BattleRecordEntry {
  uint32_t recordId = 0;  // 0 = unused slot, same "no gaps, zero at the end" convention as PokemonState::partyRecordIds
  std::array<uint8_t, BATTLE_MOVE_SLOTS> moves{};
  std::array<uint8_t, BATTLE_MOVE_SLOTS> pp{};
  uint16_t currentHp = 0;
  Ailment status = Ailment::None;
  uint8_t statusTurns = 0;
  // How many times each move slot has had a PP Up used on it (0-3, real Gen
  // 1's own cap). Teaching a brand-new move into an existing slot (teachMove/
  // learnMoveIntoSlot/resolveMoveLearn) keeps that slot's existing PP Up
  // level for whatever move ends up there - matching how the real games tie
  // PP Up to the slot, not the move identity. Unlike the real games, this
  // engine keeps `moves`/`pp` packed with no gaps (see validateBattleRecordEntry),
  // so forgetMove() shifts `ppUp` left in lockstep with `moves`/`pp` when it
  // closes a gap, keeping each remaining move's own PP Up level attached to
  // it through the shift rather than resetting or scrambling it. See
  // maxPpFor() in PokemonBattle.h. Absent (defaults to all 0) when decoded
  // from a v1 (pre-PP-Up) file.
  std::array<uint8_t, BATTLE_MOVE_SLOTS> ppUp{};
  // Mirrors BattleCombatant::toxicCounter (PokemonBattle.h) across a switch/
  // save-reload boundary - without this, switching a Toxic'd Pokemon out and
  // back in (or simply exiting/reopening the app mid-battle) silently reset
  // the escalating Toxic damage back to flat 1/8 the moment the entry was
  // reloaded, since there was nowhere to persist the in-progress counter
  // (round 5 audit bug 3.1). Meaningful only while status == Ailment::Poison
  // - kept at 0 otherwise (validateBattleRecordEntry enforces this, the same
  // way statusTurns is only allowed nonzero for Sleep/Confusion). Absent
  // (defaults to 0) when decoded from a v1/v2 (pre-Toxic-escalation) file.
  uint8_t toxicCounter = 0;

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
// Removes an entry (e.g. a Pokemon was released) without leaving a gap.
// Returns false if recordId was not present.
bool removeBattleEntry(BattleStoreState& state, uint32_t recordId);
// Frees up one slot by removing the first entry whose recordId is not in
// `keepIds`, without leaving a gap - used when the store is at capacity and
// a current party member needs a fresh entry (see PokemonService::
// loadBattleEntry()'s doc comment: deposited/released Pokemon keep their
// entry instead of it being freed proactively, so this is how a slot is
// reclaimed on actual demand instead). Returns false if every entry's
// recordId is in `keepIds` (nothing safe to evict - never happens in
// practice, since `keepIds` is always PARTY_SIZE long and the store only
// ever has POKEMON_BATTLE_MAX_ENTRIES == PARTY_SIZE slots).
bool evictBattleEntryNotIn(BattleStoreState& state, std::span<const uint32_t> keepIds);

bool validateBattleRecordEntry(const BattleRecordEntry& entry);
bool validateBattleStoreState(const BattleStoreState& state);
bool encodeBattleRecordEntry(const BattleRecordEntry& entry, BattleEntryBytes& output);
bool decodeBattleRecordEntry(const BattleEntryBytes& bytes, BattleRecordEntry& output);
// Decodes a v1 (pre-PP-Up) 16-byte entry - ppUp comes back all zero. Used by
// decodeBattleStoreFile() when the file header says version 1, and by
// decodeLegacyBattleStoreFile() (which predates PP Up entirely).
bool decodeBattleRecordEntryV1(const BattleEntryBytesV1& bytes, BattleRecordEntry& output);
// Decodes a v2 (pre-Toxic-escalation) 20-byte entry - toxicCounter comes back
// 0. Used by decodeBattleStoreFile() when the file header says version 2.
bool decodeBattleRecordEntryV2(const BattleEntryBytesV2& bytes, BattleRecordEntry& output);

uint32_t updateBattleStoreCrc32(uint32_t crc, const uint8_t* data, size_t size);
constexpr uint32_t finishBattleStoreCrc32(const uint32_t crc) { return crc ^ 0xFFFFFFFFU; }
constexpr uint32_t BATTLE_STORE_CRC32_INITIAL = 0xFFFFFFFFU;

// Encodes `state` into `output`/`outputSize`: a header (magic, version,
// entryCount, `sequence`), then entries back to back (only the non-zero-
// recordId ones), then a trailing CRC32 over the header+entries written
// before it. Fails if `sequence` is 0 (reserved for "no valid slot yet") or
// the state does not pass validateBattleStoreState.
bool encodeBattleStoreFile(const BattleStoreState& state, uint32_t sequence, BattleStoreFileBytes& output,
                           size_t& outputSize);

// Decodes and fully verifies a battle-store file of exactly `size` bytes:
// the magic/version must match, entryCount must be <=
// POKEMON_BATTLE_MAX_ENTRIES with `size` matching it exactly, the CRC32 must
// match, and the decoded state must pass validateBattleStoreState
// (ascending, no duplicate recordId, every entry individually valid).
// Returns false for anything else, including a plain garbled/truncated
// file. `sequence` (output) lets the caller pick the newer of the two
// alternating files the same way PokemonStore does for the main save.
bool decodeBattleStoreFile(const uint8_t* data, size_t size, BattleStoreState& output, uint32_t& sequence);

// Legacy pre-double-buffering format: no header, just entries+CRC32. Used
// only to migrate a pre-existing single pokemon-battle.bin into the new
// alternating-file format on first load after an update.
bool decodeLegacyBattleStoreFile(const uint8_t* data, size_t size, BattleStoreState& output);

}  // namespace pokemon
