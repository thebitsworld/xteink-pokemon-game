#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "PokemonTypes.h"

namespace pokemon {

// speciesId(2) + nickname(POKEMON_NICKNAME_BYTES) + level(1) + gender(1) + shiny(1).
constexpr size_t POKEMON_HOF_MEMBER_BYTES = 2 + POKEMON_NICKNAME_BYTES + 1 + 1 + 1;
constexpr size_t POKEMON_HOF_FILE_CRC_BYTES = 4;
// Header: magic "PKHF" (4) + version (1) + sequence (4). No entryCount field
// like the battle/IV-EV stores have - a Hall of Fame snapshot is always
// exactly PARTY_SIZE member slots (empty ones marked by speciesId 0), never a
// variable-length collection, so there is nothing to count.
constexpr size_t POKEMON_HOF_HEADER_BYTES = 9;
constexpr uint8_t POKEMON_HOF_STORE_VERSION = 1;
// cleared(1) + lifetimeMinutesAtClear(4) + PARTY_SIZE members.
constexpr size_t POKEMON_HOF_PAYLOAD_BYTES = 1 + 4 + PARTY_SIZE * POKEMON_HOF_MEMBER_BYTES;
constexpr size_t POKEMON_HOF_FILE_BYTES = POKEMON_HOF_HEADER_BYTES + POKEMON_HOF_PAYLOAD_BYTES + POKEMON_HOF_FILE_CRC_BYTES;

using HallOfFameFileBytes = std::array<uint8_t, POKEMON_HOF_FILE_BYTES>;

// One party slot as it stood the moment the Champion was defeated. speciesId
// 0 means the party had fewer than PARTY_SIZE members at that moment - every
// other field is left at its default for that slot (see
// validateHallOfFameMember()). recordId is deliberately NOT captured here:
// the whole point of this snapshot is that it must never "drift" if the
// player later releases or evolves that Pokemon, so it is a self-contained
// copy of exactly what mattered at the time, not a live reference back into
// PokemonRecord/the battle store.
struct HallOfFameMember {
  uint16_t speciesId = 0;
  std::array<char, POKEMON_NICKNAME_BYTES> nickname{};
  uint8_t level = 0;
  Gender gender = Gender::Unknown;
  bool shiny = false;

  bool operator==(const HallOfFameMember&) const = default;
};

// The one-time snapshot taken by PokemonService::captureHallOfFame() the
// moment the Champion is first defeated (see docs - gyms/the Champion can
// never be re-fought, so this is written at most once per playthrough).
// Lives in its own side file (pokemon-hof-{a,b}.bin), double-buffered the
// same way as pokemon-battle-{a,b}.bin/pokemon-ivev-{a,b}.bin, rather than in
// PokemonState - it is written once and read forever after, nothing like
// PokemonState's every-few-minutes churn.
struct HallOfFameState {
  // false for both "no file yet" (fresh install) and "reset, captured
  // nothing since" - PokemonHallOfFameStore::reset() writes a fresh
  // HallOfFameState{} (cleared still false) rather than deleting the file,
  // so a valid, decodable file existing is NOT by itself "has an entry";
  // only this flag is. Kept as an explicit field rather than inferring
  // "captured" from file existence so a reset can go through the exact same
  // write-then-verify-then-flip commit as everything else instead of a
  // separate delete-file path.
  bool cleared = false;
  uint32_t lifetimeMinutesAtClear = 0;
  std::array<HallOfFameMember, PARTY_SIZE> members{};

  bool operator==(const HallOfFameState&) const = default;
};

bool validateHallOfFameMember(const HallOfFameMember& member);
// Members must be packed at the front (non-empty slots first, in original
// party order), matching every other packed-array convention in this project
// (PokemonState::partyRecordIds, BattleStoreState::entries, ...).
bool validateHallOfFameState(const HallOfFameState& state);

uint32_t updateHallOfFameCrc32(uint32_t crc, const uint8_t* data, size_t size);
constexpr uint32_t finishHallOfFameCrc32(const uint32_t crc) { return crc ^ 0xFFFFFFFFU; }
constexpr uint32_t HALL_OF_FAME_CRC32_INITIAL = 0xFFFFFFFFU;

// Encodes `state` into `output`/`outputSize`: header (magic, version,
// `sequence`), lifetimeMinutesAtClear, then exactly PARTY_SIZE member slots,
// then a trailing CRC32 over everything written before it. Fails if
// `sequence` is 0 (reserved for "no valid slot yet") or the state does not
// pass validateHallOfFameState.
bool encodeHallOfFameFile(const HallOfFameState& state, uint32_t sequence, HallOfFameFileBytes& output,
                          size_t& outputSize);

// Decodes and fully verifies a Hall of Fame file of exactly `size` bytes: the
// magic/version must match, the CRC32 must match, and the decoded state must
// pass validateHallOfFameState. `sequence` (output) lets the caller pick the
// newer of the two alternating files, same as every other double-buffered
// store in this project.
bool decodeHallOfFameFile(const uint8_t* data, size_t size, HallOfFameState& output, uint32_t& sequence);

}  // namespace pokemon
