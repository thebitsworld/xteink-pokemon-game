#include <cstdio>
#include <cstring>

#include "PokemonHallOfFameCodec.h"

namespace {

using pokemon::HallOfFameMember;
using pokemon::HallOfFameState;

int failures = 0;

#define CHECK(condition)                                                                \
  do {                                                                                  \
    if (!(condition)) {                                                                 \
      std::fprintf(stderr, "%s:%d check failed: %s\n", __FILE__, __LINE__, #condition); \
      ++failures;                                                                       \
    }                                                                                   \
  } while (false)

void setNickname(std::array<char, pokemon::POKEMON_NICKNAME_BYTES>& nickname, const char* text) {
  nickname = {};
  std::strncpy(nickname.data(), text, nickname.size() - 1);
}

HallOfFameMember makeMember(const uint16_t speciesId, const char* nickname, const uint8_t level,
                            const pokemon::Gender gender, const bool shiny) {
  HallOfFameMember member{};
  member.speciesId = speciesId;
  setNickname(member.nickname, nickname);
  member.level = level;
  member.gender = gender;
  member.shiny = shiny;
  return member;
}

HallOfFameState makeClearedState() {
  HallOfFameState state{};
  state.cleared = true;
  state.lifetimeMinutesAtClear = 1234;
  state.members[0] = makeMember(6, "Charizard", 62, pokemon::Gender::Male, true);
  state.members[1] = makeMember(9, "", 58, pokemon::Gender::Female, false);
  state.members[2] = makeMember(3, "Venusaur", 60, pokemon::Gender::Genderless, false);
  // members[3..5] left as default (speciesId 0) - party had only 3 members.
  return state;
}

void memberValidationAcceptsCanonicalEmptyAndRejectsOutOfRange() {
  CHECK(pokemon::validateHallOfFameMember(HallOfFameMember{}));  // canonical empty slot

  HallOfFameMember filled = makeMember(151, "Mew", 100, pokemon::Gender::Unknown, true);
  CHECK(pokemon::validateHallOfFameMember(filled));

  HallOfFameMember badSpecies = filled;
  badSpecies.speciesId = 152;  // past KANTO_SPECIES_COUNT
  CHECK(!pokemon::validateHallOfFameMember(badSpecies));

  HallOfFameMember zeroLevel = filled;
  zeroLevel.level = 0;
  CHECK(!pokemon::validateHallOfFameMember(zeroLevel));

  HallOfFameMember overLevel = filled;
  overLevel.level = 101;
  CHECK(!pokemon::validateHallOfFameMember(overLevel));

  HallOfFameMember nonEmptyLevelZero = HallOfFameMember{};
  nonEmptyLevelZero.speciesId = 1;  // non-empty slot but still level 0 - invalid either way
  CHECK(!pokemon::validateHallOfFameMember(nonEmptyLevelZero));
}

void stateValidationRequiresPackedMembersAndClearedDataInvariant() {
  HallOfFameState state = makeClearedState();
  CHECK(pokemon::validateHallOfFameState(state));

  HallOfFameState gap = state;
  gap.members[0] = HallOfFameMember{};  // empty, but members[2] after it is still non-empty
  CHECK(!pokemon::validateHallOfFameState(gap));

  HallOfFameState unclearedWithData = state;
  unclearedWithData.cleared = false;  // has real members but claims "not yet captured"
  CHECK(!pokemon::validateHallOfFameState(unclearedWithData));

  CHECK(pokemon::validateHallOfFameState(HallOfFameState{}));  // canonical not-yet-captured state
}

void stateRoundTripsThroughEncodeDecode() {
  const HallOfFameState state = makeClearedState();
  pokemon::HallOfFameFileBytes bytes{};
  size_t size = 0;
  CHECK(pokemon::encodeHallOfFameFile(state, 7, bytes, size));
  CHECK(size == pokemon::POKEMON_HOF_FILE_BYTES);
  CHECK(bytes[0] == 'P' && bytes[1] == 'K' && bytes[2] == 'H' && bytes[3] == 'F');
  CHECK(bytes[4] == pokemon::POKEMON_HOF_STORE_VERSION);

  HallOfFameState decoded{};
  uint32_t sequence = 0;
  CHECK(pokemon::decodeHallOfFameFile(bytes.data(), size, decoded, sequence));
  CHECK(sequence == 7);
  CHECK(decoded == state);
}

void emptyStateRoundTrips() {
  const HallOfFameState state{};
  pokemon::HallOfFameFileBytes bytes{};
  size_t size = 0;
  CHECK(pokemon::encodeHallOfFameFile(state, 1, bytes, size));

  HallOfFameState decoded = makeClearedState();  // prove decode actually overwrites this, not merges
  uint32_t sequence = 0;
  CHECK(pokemon::decodeHallOfFameFile(bytes.data(), size, decoded, sequence));
  CHECK(decoded == state);
}

void encodeRejectsZeroSequenceOrInvalidState() {
  const HallOfFameState state = makeClearedState();
  pokemon::HallOfFameFileBytes bytes{};
  size_t size = 0;
  CHECK(!pokemon::encodeHallOfFameFile(state, 0, bytes, size));  // 0 is reserved

  HallOfFameState invalid = state;
  invalid.cleared = false;  // now internally inconsistent
  CHECK(!pokemon::encodeHallOfFameFile(invalid, 1, bytes, size));
}

void corruptedCrcFailsToDecode() {
  const HallOfFameState state = makeClearedState();
  pokemon::HallOfFameFileBytes bytes{};
  size_t size = 0;
  CHECK(pokemon::encodeHallOfFameFile(state, 1, bytes, size));
  bytes[size - 1] ^= 0xFFU;  // flip a byte inside the trailing CRC32

  HallOfFameState decoded{};
  uint32_t sequence = 0;
  CHECK(!pokemon::decodeHallOfFameFile(bytes.data(), size, decoded, sequence));
}

void decodeRejectsWrongSizeOrMagic() {
  const HallOfFameState state = makeClearedState();
  pokemon::HallOfFameFileBytes bytes{};
  size_t size = 0;
  CHECK(pokemon::encodeHallOfFameFile(state, 1, bytes, size));

  HallOfFameState decoded{};
  uint32_t sequence = 0;
  CHECK(!pokemon::decodeHallOfFameFile(bytes.data(), size - 1, decoded, sequence));  // truncated

  pokemon::HallOfFameFileBytes wrongMagic = bytes;
  wrongMagic[0] = 'X';
  CHECK(!pokemon::decodeHallOfFameFile(wrongMagic.data(), size, decoded, sequence));
}

}  // namespace

int main() {
  memberValidationAcceptsCanonicalEmptyAndRejectsOutOfRange();
  stateValidationRequiresPackedMembersAndClearedDataInvariant();
  stateRoundTripsThroughEncodeDecode();
  emptyStateRoundTrips();
  encodeRejectsZeroSequenceOrInvalidState();
  corruptedCrcFailsToDecode();
  decodeRejectsWrongSizeOrMagic();
  return failures == 0 ? 0 : 1;
}
