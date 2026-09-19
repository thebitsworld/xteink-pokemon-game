#if defined(CROSSINK_ENABLE_POKEMON)

#include "PokemonHallOfFameCodec.h"

#include <algorithm>
#include <cstring>

#include "PokemonCrc32.h"

namespace pokemon {
namespace {

void write16(uint8_t* bytes, const size_t offset, const uint16_t value) {
  bytes[offset] = static_cast<uint8_t>(value);
  bytes[offset + 1] = static_cast<uint8_t>(value >> 8U);
}

void write32(uint8_t* bytes, const size_t offset, const uint32_t value) {
  bytes[offset] = static_cast<uint8_t>(value);
  bytes[offset + 1] = static_cast<uint8_t>(value >> 8U);
  bytes[offset + 2] = static_cast<uint8_t>(value >> 16U);
  bytes[offset + 3] = static_cast<uint8_t>(value >> 24U);
}

uint16_t read16(const uint8_t* bytes, const size_t offset) {
  return static_cast<uint16_t>(bytes[offset]) | static_cast<uint16_t>(static_cast<uint16_t>(bytes[offset + 1]) << 8U);
}

uint32_t read32(const uint8_t* bytes, const size_t offset) {
  return static_cast<uint32_t>(bytes[offset]) | (static_cast<uint32_t>(bytes[offset + 1]) << 8U) |
         (static_cast<uint32_t>(bytes[offset + 2]) << 16U) | (static_cast<uint32_t>(bytes[offset + 3]) << 24U);
}

// A lighter check than PokemonTypes.cpp's own file-local canonicalNickname()
// (not exported, and pulling in the real one would drag this codec's tests
// into needing the generated species data table just for an unrelated
// function in the same translation unit) - just NUL-terminated with nothing
// but zero padding after it, so a corrupted on-disk string can never read
// out of bounds downstream. The actual UTF-8/length validation already
// happened once, for real, when this nickname was first set on the live
// PokemonRecord it was copied from (setNickname()/validateNickname()) - this
// is only a decode-time corruption guard, not re-validating that. An empty
// (all-zero) nickname passes trivially, which is exactly what an empty
// member slot wants.
bool canonicalHofNickname(const std::array<char, POKEMON_NICKNAME_BYTES>& nickname) {
  const auto terminator = std::find(nickname.begin(), nickname.end(), '\0');
  if (terminator == nickname.end()) return false;
  return std::all_of(terminator, nickname.end(), [](const char value) { return value == '\0'; });
}

}  // namespace

uint32_t updateHallOfFameCrc32(const uint32_t crc, const uint8_t* data, const size_t size) {
  return updatePokemonCrc32(crc, data, size);
}

bool validateHallOfFameMember(const HallOfFameMember& member) {
  if (!canonicalHofNickname(member.nickname)) return false;
  if (member.speciesId == 0) {
    return member.level == 0 && member.gender == Gender::Unknown && !member.shiny &&
           member.nickname[0] == '\0';  // canonical "empty slot" - nothing else set
  }
  if (member.speciesId > KANTO_SPECIES_COUNT) return false;
  if (member.level == 0 || member.level > 100) return false;
  switch (member.gender) {
    case Gender::Unknown:
    case Gender::Male:
    case Gender::Female:
    case Gender::Genderless:
      return true;
  }
  return false;
}

bool validateHallOfFameState(const HallOfFameState& state) {
  bool sawEmptySlot = false;
  bool anyMember = false;
  for (const HallOfFameMember& member : state.members) {
    if (!validateHallOfFameMember(member)) return false;
    if (member.speciesId == 0) {
      sawEmptySlot = true;
      continue;
    }
    if (sawEmptySlot) return false;  // non-empty slots must be packed at the front, like every other slot array here
    anyMember = true;
  }
  // An uncleared snapshot (fresh install, or right after reset()) is always
  // the canonical all-empty state - there is no other reason for `cleared`
  // to be false with real members already sitting in it.
  if (!state.cleared && (anyMember || state.lifetimeMinutesAtClear != 0)) return false;
  return true;
}

bool encodeHallOfFameFile(const HallOfFameState& state, const uint32_t sequence, HallOfFameFileBytes& output,
                          size_t& outputSize) {
  if (sequence == 0 || !validateHallOfFameState(state)) return false;
  HallOfFameFileBytes candidate{};
  candidate[0] = 'P';
  candidate[1] = 'K';
  candidate[2] = 'H';
  candidate[3] = 'F';
  candidate[4] = POKEMON_HOF_STORE_VERSION;
  write32(candidate.data(), 5, sequence);

  size_t offset = POKEMON_HOF_HEADER_BYTES;
  candidate[offset++] = state.cleared ? 1 : 0;
  write32(candidate.data(), offset, state.lifetimeMinutesAtClear);
  offset += 4;
  for (const HallOfFameMember& member : state.members) {
    write16(candidate.data(), offset, member.speciesId);
    offset += 2;
    std::memcpy(candidate.data() + offset, member.nickname.data(), member.nickname.size());
    offset += member.nickname.size();
    candidate[offset++] = member.level;
    candidate[offset++] = static_cast<uint8_t>(member.gender);
    candidate[offset++] = member.shiny ? 1 : 0;
  }

  const uint32_t crc = finishHallOfFameCrc32(updateHallOfFameCrc32(HALL_OF_FAME_CRC32_INITIAL, candidate.data(), offset));
  write32(candidate.data(), offset, crc);
  output = candidate;
  outputSize = offset + POKEMON_HOF_FILE_CRC_BYTES;
  return true;
}

bool decodeHallOfFameFile(const uint8_t* data, const size_t size, HallOfFameState& output, uint32_t& sequence) {
  if (data == nullptr || size != POKEMON_HOF_FILE_BYTES) return false;
  if (data[0] != 'P' || data[1] != 'K' || data[2] != 'H' || data[3] != 'F') return false;
  if (data[4] != POKEMON_HOF_STORE_VERSION) return false;
  const uint32_t candidateSequence = read32(data, 5);
  if (candidateSequence == 0) return false;  // 0 is reserved for "no valid slot written yet"

  const size_t payloadSize = size - POKEMON_HOF_FILE_CRC_BYTES;
  const uint32_t expectedCrc = read32(data, payloadSize);
  const uint32_t actualCrc = finishHallOfFameCrc32(updateHallOfFameCrc32(HALL_OF_FAME_CRC32_INITIAL, data, payloadSize));
  if (expectedCrc != actualCrc) return false;

  HallOfFameState candidate{};
  size_t offset = POKEMON_HOF_HEADER_BYTES;
  candidate.cleared = data[offset++] != 0;
  candidate.lifetimeMinutesAtClear = read32(data, offset);
  offset += 4;
  for (HallOfFameMember& member : candidate.members) {
    member.speciesId = read16(data, offset);
    offset += 2;
    std::memcpy(member.nickname.data(), data + offset, member.nickname.size());
    offset += member.nickname.size();
    member.level = data[offset++];
    const uint8_t genderByte = data[offset++];
    if (genderByte > static_cast<uint8_t>(Gender::Genderless)) return false;
    member.gender = static_cast<Gender>(genderByte);
    member.shiny = data[offset++] != 0;
  }
  if (!validateHallOfFameState(candidate)) return false;
  output = candidate;
  sequence = candidateSequence;
  return true;
}

}  // namespace pokemon

#endif
