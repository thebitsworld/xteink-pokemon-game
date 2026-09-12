#include <cstdint>
#include <cstdio>

#include "Pokemon/PokemonStoreCodec.h"

namespace {

int failures = 0;

#define CHECK(condition)                                                                \
  do {                                                                                  \
    if (!(condition)) {                                                                 \
      std::fprintf(stderr, "%s:%d check failed: %s\n", __FILE__, __LINE__, #condition); \
      ++failures;                                                                       \
    }                                                                                   \
  } while (false)

uint32_t read32(const uint8_t* bytes, const size_t offset) {
  return static_cast<uint32_t>(bytes[offset]) | (static_cast<uint32_t>(bytes[offset + 1]) << 8U) |
         (static_cast<uint32_t>(bytes[offset + 2]) << 16U) | (static_cast<uint32_t>(bytes[offset + 3]) << 24U);
}

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

uint16_t readHeader16(const pokemon::HeaderBytes& bytes, const size_t offset) {
  return static_cast<uint16_t>(bytes[offset]) | static_cast<uint16_t>(static_cast<uint16_t>(bytes[offset + 1]) << 8U);
}

uint32_t readHeader32(const pokemon::HeaderBytes& bytes, const size_t offset) {
  return static_cast<uint32_t>(bytes[offset]) | (static_cast<uint32_t>(bytes[offset + 1]) << 8U) |
         (static_cast<uint32_t>(bytes[offset + 2]) << 16U) | (static_cast<uint32_t>(bytes[offset + 3]) << 24U);
}

void writeHeader32(pokemon::HeaderBytes& bytes, const size_t offset, const uint32_t value) {
  bytes[offset] = static_cast<uint8_t>(value);
  bytes[offset + 1] = static_cast<uint8_t>(value >> 8U);
  bytes[offset + 2] = static_cast<uint8_t>(value >> 16U);
  bytes[offset + 3] = static_cast<uint8_t>(value >> 24U);
}

void stateCodecUsesTheCanonicalV6Layout() {
  static_assert(pokemon::POKEMON_STATE_V2_BYTES == 116);
  static_assert(pokemon::POKEMON_STATE_V3_BYTES == 116 + pokemon::POKEMON_BAG_SLOT_COUNT + 2);
  static_assert(pokemon::POKEMON_STATE_V4_BYTES == pokemon::POKEMON_STATE_V3_BYTES + 3);
  static_assert(pokemon::POKEMON_STATE_V5_BYTES == pokemon::POKEMON_STATE_V4_BYTES + 1);
  static_assert(pokemon::POKEMON_STATE_BYTES ==
                pokemon::POKEMON_STATE_V5_BYTES + pokemon::POKEMON_BATTLE_BOOST_ITEM_COUNT);
  static_assert(pokemon::POKEMON_STATE_V1_BYTES == 96);
  pokemon::PokemonState state{};
  state.partyRecordIds[0] = 7;
  state.partyRecordIds[1] = 9;
  state.pendingEvents[0] = {
      0, 25, 5, pokemon::Gender::Female, pokemon::EvolutionItem::None, pokemon::PendingEventKind::Encounter};
  state.pendingEvents[1] = {
      0, 0, 0, pokemon::Gender::Unknown, pokemon::EvolutionItem::LinkCable, pokemon::PendingEventKind::Item};
  state.pendingEvents[2] = {
      7, 26, 0, pokemon::Gender::Unknown, pokemon::EvolutionItem::None, pokemon::PendingEventKind::Evolution};
  state.itemCounts = {1, 2, 3, 4, 5, 6};
  state.seenSpecies[0] = 0x05;
  state.caughtSpecies[0] = 0x01;
  state.lifetimeMinutes = 0x11223344U;
  state.sequence = 0x55667788U;
  state.readingMinuteRemainder = 59;
  state.encounterMisses = 5;
  state.itemMisses = 19;
  state.dashboardNotice = pokemon::DashboardNotice::ItemFound;
  state.bagCounts[0] = 42;
  state.bagCounts[state.bagCounts.size() - 1] = 7;
  state.battleProgress = pokemon::POKEMON_GYM_PROGRESS_MASK;  // all bits set is the widest legal value
  state.ballMisses = 3;
  state.medicineMisses = 2;
  state.machineMisses = 1;
  state.ppUpCount = 4;
  state.battleBoostCounts = {1, 2, 3, 4, 5, 6};

  pokemon::StateBytes bytes{};
  CHECK(pokemon::encodeState(state, bytes));
  CHECK(read32(bytes.data(), 0) == 7);
  CHECK(read32(bytes.data(), 4) == 9);
  CHECK(bytes[33] == static_cast<uint8_t>(pokemon::PendingEventKind::Encounter));
  CHECK(bytes[42] == static_cast<uint8_t>(pokemon::EvolutionItem::LinkCable));
  CHECK(bytes[43] == static_cast<uint8_t>(pokemon::PendingEventKind::Item));
  CHECK(bytes[53] == static_cast<uint8_t>(pokemon::PendingEventKind::Evolution));
  CHECK(bytes[54] == 1 && bytes[55] == 0);
  CHECK(bytes[66] == 0x05);
  CHECK(bytes[85] == 0x01);
  CHECK(read32(bytes.data(), 104) == 0x11223344U);
  CHECK(read32(bytes.data(), 108) == 0x55667788U);
  CHECK(bytes[112] == 59);
  CHECK(bytes[113] == 5);
  CHECK(bytes[114] == 19);
  CHECK(bytes[115] == static_cast<uint8_t>(pokemon::DashboardNotice::ItemFound));
  CHECK(bytes[116] == 42);
  CHECK(bytes[116 + pokemon::POKEMON_BAG_SLOT_COUNT - 1] == 7);
  CHECK((bytes[116 + pokemon::POKEMON_BAG_SLOT_COUNT] | (bytes[116 + pokemon::POKEMON_BAG_SLOT_COUNT + 1] << 8)) ==
        pokemon::POKEMON_GYM_PROGRESS_MASK);
  CHECK(bytes[pokemon::POKEMON_STATE_V3_BYTES] == 3);
  CHECK(bytes[pokemon::POKEMON_STATE_V3_BYTES + 1] == 2);
  CHECK(bytes[pokemon::POKEMON_STATE_V3_BYTES + 2] == 1);
  CHECK(bytes[pokemon::POKEMON_STATE_V4_BYTES] == 4);
  for (size_t index = 0; index < state.battleBoostCounts.size(); ++index) {
    CHECK(bytes[pokemon::POKEMON_STATE_V5_BYTES + index] == state.battleBoostCounts[index]);
  }

  pokemon::PokemonState decoded{};
  CHECK(pokemon::decodeState(bytes.data(), bytes.size(), pokemon::POKEMON_SNAPSHOT_VERSION, decoded));
  CHECK(decoded == state);
}

void v2StateDecodesWithZeroedBagAndBattleProgress() {
  pokemon::PokemonState v2State{};
  v2State.partyRecordIds[0] = 3;
  v2State.lifetimeMinutes = 500;

  std::array<uint8_t, pokemon::POKEMON_STATE_V2_BYTES> bytes{};
  write32(bytes.data(), 0, v2State.partyRecordIds[0]);
  write32(bytes.data(), 104, v2State.lifetimeMinutes);
  write32(bytes.data(), 108, 1);  // sequence, must be non-zero-ish and match what validateState allows

  pokemon::PokemonState decoded{};
  decoded.bagCounts.fill(0xAA);  // prove the decoder actually zeroes these, not just leaves them alone
  decoded.battleProgress = 0xAAAA;
  CHECK(pokemon::decodeState(bytes.data(), bytes.size(), pokemon::POKEMON_SNAPSHOT_VERSION_V2, decoded));
  CHECK(decoded.partyRecordIds[0] == 3);
  CHECK(decoded.lifetimeMinutes == 500);
  for (const uint8_t slot : decoded.bagCounts) CHECK(slot == 0);
  CHECK(decoded.battleProgress == 0);
}

void v3StateDecodesWithZeroedMissCounters() {
  pokemon::PokemonState v3State{};
  v3State.partyRecordIds[0] = 3;
  v3State.lifetimeMinutes = 500;
  v3State.bagCounts[0] = 9;

  std::array<uint8_t, pokemon::POKEMON_STATE_V3_BYTES> bytes{};
  write32(bytes.data(), 0, v3State.partyRecordIds[0]);
  write32(bytes.data(), 104, v3State.lifetimeMinutes);
  write32(bytes.data(), 108, 1);  // sequence, must be non-zero-ish and match what validateState allows
  bytes[116] = v3State.bagCounts[0];

  pokemon::PokemonState decoded{};
  decoded.ballMisses = 0xAA;  // prove the decoder actually zeroes these, not just leaves them alone
  decoded.medicineMisses = 0xAA;
  decoded.machineMisses = 0xAA;
  CHECK(pokemon::decodeState(bytes.data(), bytes.size(), pokemon::POKEMON_SNAPSHOT_VERSION_V3, decoded));
  CHECK(decoded.partyRecordIds[0] == 3);
  CHECK(decoded.lifetimeMinutes == 500);
  CHECK(decoded.bagCounts[0] == 9);  // v3 fields still decode correctly
  CHECK(decoded.ballMisses == 0);
  CHECK(decoded.medicineMisses == 0);
  CHECK(decoded.machineMisses == 0);
}

void v4StateDecodesWithZeroedPpUpCount() {
  pokemon::PokemonState v4State{};
  v4State.partyRecordIds[0] = 3;
  v4State.lifetimeMinutes = 500;
  v4State.ballMisses = 2;

  std::array<uint8_t, pokemon::POKEMON_STATE_V4_BYTES> bytes{};
  write32(bytes.data(), 0, v4State.partyRecordIds[0]);
  write32(bytes.data(), 104, v4State.lifetimeMinutes);
  write32(bytes.data(), 108, 1);  // sequence, must be non-zero-ish and match what validateState allows
  bytes[pokemon::POKEMON_STATE_V3_BYTES] = v4State.ballMisses;

  pokemon::PokemonState decoded{};
  decoded.ppUpCount = 0xAA;  // prove the decoder actually zeroes this, not just leaves it alone
  CHECK(pokemon::decodeState(bytes.data(), bytes.size(), pokemon::POKEMON_SNAPSHOT_VERSION_V4, decoded));
  CHECK(decoded.partyRecordIds[0] == 3);
  CHECK(decoded.lifetimeMinutes == 500);
  CHECK(decoded.ballMisses == 2);  // v4 fields still decode correctly
  CHECK(decoded.ppUpCount == 0);
}

void v5StateDecodesWithZeroedBattleBoostCounts() {
  pokemon::PokemonState v5State{};
  v5State.partyRecordIds[0] = 3;
  v5State.lifetimeMinutes = 500;
  v5State.ppUpCount = 4;

  std::array<uint8_t, pokemon::POKEMON_STATE_V5_BYTES> bytes{};
  write32(bytes.data(), 0, v5State.partyRecordIds[0]);
  write32(bytes.data(), 104, v5State.lifetimeMinutes);
  write32(bytes.data(), 108, 1);  // sequence, must be non-zero-ish and match what validateState allows
  bytes[pokemon::POKEMON_STATE_V4_BYTES] = v5State.ppUpCount;

  pokemon::PokemonState decoded{};
  decoded.battleBoostCounts.fill(0xAA);  // prove the decoder actually zeroes these, not just leaves them alone
  CHECK(pokemon::decodeState(bytes.data(), bytes.size(), pokemon::POKEMON_SNAPSHOT_VERSION_V5, decoded));
  CHECK(decoded.partyRecordIds[0] == 3);
  CHECK(decoded.lifetimeMinutes == 500);
  CHECK(decoded.ppUpCount == 4);  // v5 fields still decode correctly
  for (const uint8_t slot : decoded.battleBoostCounts) CHECK(slot == 0);
}

void battleProgressReservedBitsAreRejected() {
  pokemon::PokemonState state{};
  state.battleProgress = static_cast<uint16_t>(pokemon::POKEMON_GYM_PROGRESS_MASK + 1U);  // first reserved bit set
  pokemon::StateBytes output{};
  CHECK(!pokemon::encodeState(state, output));
}

void legacyStateDecodesItsPendingEventIntoTheQueue() {
  std::array<uint8_t, pokemon::POKEMON_STATE_V1_BYTES> bytes{};
  write32(bytes.data(), 0, 7);
  write16(bytes.data(), 28, 133);
  bytes[30] = 12;
  bytes[31] = static_cast<uint8_t>(pokemon::Gender::Female);
  bytes[33] = static_cast<uint8_t>(pokemon::PendingEventKind::Encounter);
  bytes[46] = 0x01;
  bytes[65] = 0x01;
  write32(bytes.data(), 84, 60);
  write32(bytes.data(), 88, 9);
  bytes[95] = static_cast<uint8_t>(pokemon::DashboardNotice::NewPokemon);

  pokemon::PokemonState decoded{};
  CHECK(pokemon::decodeState(bytes.data(), bytes.size(), pokemon::POKEMON_SNAPSHOT_VERSION_V1, decoded));
  CHECK(decoded.partyRecordIds[0] == 7);
  CHECK(decoded.pendingEvents[0].kind == pokemon::PendingEventKind::Encounter);
  CHECK(decoded.pendingEvents[0].speciesId == 133);
  CHECK(decoded.pendingEvents[1].kind == pokemon::PendingEventKind::None);
  CHECK(decoded.pendingEvents[2].kind == pokemon::PendingEventKind::None);
  CHECK(decoded.sequence == 9);
}

void invalidStateDoesNotMutateEncodedOutput() {
  pokemon::PokemonState state{};
  state.readingMinuteRemainder = 60;
  pokemon::StateBytes output{};
  output.fill(0xA5);
  const pokemon::StateBytes before = output;

  CHECK(!pokemon::encodeState(state, output));
  CHECK(output == before);
}

void invalidStateBytesDoNotMutateDecodedOutput() {
  pokemon::PokemonState valid{};
  pokemon::StateBytes bytes{};
  CHECK(pokemon::encodeState(valid, bytes));
  bytes[112] = 60;
  pokemon::PokemonState output{};
  output.lifetimeMinutes = 77;
  const pokemon::PokemonState before = output;

  CHECK(!pokemon::decodeState(bytes.data(), bytes.size(), pokemon::POKEMON_SNAPSHOT_VERSION, output));
  CHECK(output == before);
}

void snapshotHeaderUsesCanonical24ByteLayout() {
  static_assert(pokemon::POKEMON_SNAPSHOT_HEADER_BYTES == 24);
  const pokemon::SnapshotHeader header{pokemon::POKEMON_SNAPSHOT_VERSION, 0x01020304U, 3};
  pokemon::HeaderBytes bytes{};

  CHECK(pokemon::encodeSnapshotHeader(header, bytes));
  CHECK(bytes[0] == 'P' && bytes[1] == 'K' && bytes[2] == 'V' && bytes[3] == '2');
  CHECK(readHeader16(bytes, 4) == pokemon::POKEMON_SNAPSHOT_VERSION);
  CHECK(readHeader16(bytes, 6) == 24);
  CHECK(readHeader32(bytes, 8) == 0x01020304U);
  CHECK(readHeader16(bytes, 12) == pokemon::POKEMON_STATE_BYTES);
  CHECK(readHeader16(bytes, 14) == 48);
  CHECK(readHeader32(bytes, 16) == 3);
  CHECK(readHeader32(bytes, 20) == pokemon::POKEMON_STATE_BYTES + 3U * 48U);
  CHECK(pokemon::snapshotFileBytes(header) == 24U + pokemon::POKEMON_STATE_BYTES + 3U * 48U + 4U);
  CHECK(pokemon::snapshotStateBytes(pokemon::POKEMON_SNAPSHOT_VERSION_V1) == 96);
  CHECK(pokemon::snapshotStateBytes(pokemon::POKEMON_SNAPSHOT_VERSION_V2) == 116);
  CHECK(pokemon::snapshotStateBytes(pokemon::POKEMON_SNAPSHOT_VERSION_V5) == pokemon::POKEMON_STATE_V5_BYTES);
  CHECK(pokemon::snapshotStateBytes(pokemon::POKEMON_SNAPSHOT_VERSION) == pokemon::POKEMON_STATE_BYTES);
  CHECK(pokemon::snapshotStateBytes(pokemon::POKEMON_SNAPSHOT_VERSION + 1U) == 0);

  pokemon::SnapshotHeader decoded{};
  CHECK(pokemon::decodeSnapshotHeader(bytes, decoded) == pokemon::HeaderDecodeResult::Ready);
  CHECK(decoded == header);
}

void unknownSnapshotVersionIsUnsupportedWithoutMutation() {
  pokemon::HeaderBytes bytes{};
  CHECK(pokemon::encodeSnapshotHeader({pokemon::POKEMON_SNAPSHOT_VERSION, 1, 0}, bytes));
  bytes[4] = static_cast<uint8_t>(pokemon::POKEMON_SNAPSHOT_VERSION + 1U);  // a version nothing has ever shipped
  pokemon::SnapshotHeader output{pokemon::POKEMON_SNAPSHOT_VERSION, 77, 88};
  const pokemon::SnapshotHeader before = output;

  CHECK(pokemon::decodeSnapshotHeader(bytes, output) == pokemon::HeaderDecodeResult::Unsupported);
  CHECK(output == before);
}

void malformedSupportedHeaderIsCorruptWithoutMutation() {
  pokemon::HeaderBytes canonical{};
  CHECK(pokemon::encodeSnapshotHeader({pokemon::POKEMON_SNAPSHOT_VERSION, 1, 3}, canonical));
  for (uint8_t variant = 0; variant < 6; ++variant) {
    pokemon::HeaderBytes bytes = canonical;
    if (variant == 0) bytes[0] = 'X';
    if (variant == 1) writeHeader32(bytes, 8, 0);
    if (variant == 2) bytes[6] = 23;
    if (variant == 3) bytes[12] = static_cast<uint8_t>(pokemon::POKEMON_STATE_BYTES - 1U);
    if (variant == 4) bytes[14] = 47;
    if (variant == 5) writeHeader32(bytes, 20, pokemon::POKEMON_STATE_BYTES + 3U * 48U - 1U);
    pokemon::SnapshotHeader output{pokemon::POKEMON_SNAPSHOT_VERSION, 77, 88};
    const pokemon::SnapshotHeader before = output;

    CHECK(pokemon::decodeSnapshotHeader(bytes, output) == pokemon::HeaderDecodeResult::Corrupt);
    CHECK(output == before);
  }
}

void unencodableHeaderDoesNotMutateOutput() {
  pokemon::HeaderBytes output{};
  output.fill(0xA5);
  const pokemon::HeaderBytes before = output;

  CHECK(!pokemon::encodeSnapshotHeader({pokemon::POKEMON_SNAPSHOT_VERSION, 0, 0}, output));
  CHECK(output == before);
  CHECK(!pokemon::encodeSnapshotHeader({pokemon::POKEMON_SNAPSHOT_VERSION, 1, UINT32_MAX}, output));
  CHECK(output == before);
}

void crc32MatchesTheStandardVectorAcrossChunks() {
  constexpr uint8_t input[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  const uint32_t oneShot = pokemon::finishSnapshotCrc32(
      pokemon::updateSnapshotCrc32(pokemon::POKEMON_SNAPSHOT_CRC32_INITIAL, input, sizeof(input)));
  uint32_t chunked = pokemon::updateSnapshotCrc32(pokemon::POKEMON_SNAPSHOT_CRC32_INITIAL, input, 4);
  chunked = pokemon::updateSnapshotCrc32(chunked, input + 4, 5);
  chunked = pokemon::finishSnapshotCrc32(chunked);

  CHECK(oneShot == 0xCBF43926U);
  CHECK(chunked == oneShot);
}

}  // namespace

int main() {
  stateCodecUsesTheCanonicalV6Layout();
  v3StateDecodesWithZeroedMissCounters();
  v4StateDecodesWithZeroedPpUpCount();
  v5StateDecodesWithZeroedBattleBoostCounts();
  v2StateDecodesWithZeroedBagAndBattleProgress();
  battleProgressReservedBitsAreRejected();
  legacyStateDecodesItsPendingEventIntoTheQueue();
  invalidStateDoesNotMutateEncodedOutput();
  invalidStateBytesDoNotMutateDecodedOutput();
  snapshotHeaderUsesCanonical24ByteLayout();
  unknownSnapshotVersionIsUnsupportedWithoutMutation();
  malformedSupportedHeaderIsCorruptWithoutMutation();
  unencodableHeaderDoesNotMutateOutput();
  crc32MatchesTheStandardVectorAcrossChunks();
  return failures == 0 ? 0 : 1;
}
