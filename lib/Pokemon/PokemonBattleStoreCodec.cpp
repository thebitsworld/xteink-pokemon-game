#if defined(CROSSINK_ENABLE_POKEMON)

#include "PokemonBattleStoreCodec.h"

#include <algorithm>
#include <cstring>

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

}  // namespace

uint32_t updateBattleStoreCrc32(uint32_t crc, const uint8_t* data, const size_t size) {
  for (size_t index = 0; index < size; ++index) {
    crc ^= data[index];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1U) ^ (0xEDB88320U & static_cast<uint32_t>(0U - (crc & 1U)));
    }
  }
  return crc;
}

bool validateBattleRecordEntry(const BattleRecordEntry& entry) {
  if (entry.recordId == 0) return false;
  bool sawEmptyMoveSlot = false;
  for (size_t index = 0; index < BATTLE_MOVE_SLOTS; ++index) {
    const uint8_t moveId = entry.moves[index];
    if (moveId == 0) {
      sawEmptyMoveSlot = true;
      if (entry.pp[index] != 0 || entry.ppUp[index] != 0) return false;
      continue;
    }
    if (sawEmptyMoveSlot) return false;  // moves must be packed at the front, like PokemonState::partyRecordIds
    if (moveId > POKEMON_MOVE_ID_MAX) return false;
    if (entry.ppUp[index] > 3) return false;  // real Gen 1 cap: 3 PP Ups per move slot
  }
  switch (entry.status) {
    case Ailment::None:
    case Ailment::Paralysis:
    case Ailment::Sleep:
    case Ailment::Freeze:
    case Ailment::Burn:
    case Ailment::Poison:
    case Ailment::Confusion:
      break;
    case Ailment::All:
      return false;  // All is an item-targeting sentinel (Full Heal/Restore), never a live combatant status
  }
  if (entry.status != Ailment::Sleep && entry.status != Ailment::Confusion && entry.statusTurns != 0) return false;
  return true;
}

bool encodeBattleRecordEntry(const BattleRecordEntry& entry, BattleEntryBytes& output) {
  if (!validateBattleRecordEntry(entry)) return false;
  BattleEntryBytes candidate{};
  write32(candidate.data(), 0, entry.recordId);
  for (size_t index = 0; index < BATTLE_MOVE_SLOTS; ++index) candidate[4 + index] = entry.moves[index];
  for (size_t index = 0; index < BATTLE_MOVE_SLOTS; ++index) candidate[8 + index] = entry.pp[index];
  write16(candidate.data(), 12, entry.currentHp);
  candidate[14] = static_cast<uint8_t>(entry.status);
  candidate[15] = entry.statusTurns;
  for (size_t index = 0; index < BATTLE_MOVE_SLOTS; ++index) candidate[16 + index] = entry.ppUp[index];
  output = candidate;
  return true;
}

bool decodeBattleRecordEntry(const BattleEntryBytes& bytes, BattleRecordEntry& output) {
  BattleRecordEntry candidate{};
  candidate.recordId = read32(bytes.data(), 0);
  for (size_t index = 0; index < BATTLE_MOVE_SLOTS; ++index) candidate.moves[index] = bytes[4 + index];
  for (size_t index = 0; index < BATTLE_MOVE_SLOTS; ++index) candidate.pp[index] = bytes[8 + index];
  candidate.currentHp = read16(bytes.data(), 12);
  if (bytes[14] > static_cast<uint8_t>(Ailment::All)) return false;
  candidate.status = static_cast<Ailment>(bytes[14]);
  candidate.statusTurns = bytes[15];
  for (size_t index = 0; index < BATTLE_MOVE_SLOTS; ++index) candidate.ppUp[index] = bytes[16 + index];
  if (!validateBattleRecordEntry(candidate)) return false;
  output = candidate;
  return true;
}

bool decodeBattleRecordEntryV1(const BattleEntryBytesV1& bytes, BattleRecordEntry& output) {
  BattleRecordEntry candidate{};
  candidate.recordId = read32(bytes.data(), 0);
  for (size_t index = 0; index < BATTLE_MOVE_SLOTS; ++index) candidate.moves[index] = bytes[4 + index];
  for (size_t index = 0; index < BATTLE_MOVE_SLOTS; ++index) candidate.pp[index] = bytes[8 + index];
  candidate.currentHp = read16(bytes.data(), 12);
  if (bytes[14] > static_cast<uint8_t>(Ailment::All)) return false;
  candidate.status = static_cast<Ailment>(bytes[14]);
  candidate.statusTurns = bytes[15];
  // ppUp stays all-zero - v1 predates PP Up entirely.
  if (!validateBattleRecordEntry(candidate)) return false;
  output = candidate;
  return true;
}

size_t battleEntryCount(const BattleStoreState& state) {
  size_t count = 0;
  while (count < state.entries.size() && state.entries[count].recordId != 0) ++count;
  return count;
}

const BattleRecordEntry* findBattleEntry(const BattleStoreState& state, const uint32_t recordId) {
  if (recordId == 0) return nullptr;
  const size_t count = battleEntryCount(state);
  for (size_t index = 0; index < count; ++index) {
    if (state.entries[index].recordId == recordId) return &state.entries[index];
  }
  return nullptr;
}

bool upsertBattleEntry(BattleStoreState& state, const BattleRecordEntry& entry) {
  if (!validateBattleRecordEntry(entry)) return false;
  const size_t count = battleEntryCount(state);
  for (size_t index = 0; index < count; ++index) {
    if (state.entries[index].recordId == entry.recordId) {
      state.entries[index] = entry;
      return true;
    }
  }
  if (count == state.entries.size()) return false;
  size_t insertAt = count;
  while (insertAt > 0 && state.entries[insertAt - 1].recordId > entry.recordId) {
    state.entries[insertAt] = state.entries[insertAt - 1];
    --insertAt;
  }
  state.entries[insertAt] = entry;
  return true;
}

bool removeBattleEntry(BattleStoreState& state, const uint32_t recordId) {
  const size_t count = battleEntryCount(state);
  for (size_t index = 0; index < count; ++index) {
    if (state.entries[index].recordId != recordId) continue;
    for (size_t shift = index; shift + 1 < count; ++shift) state.entries[shift] = state.entries[shift + 1];
    state.entries[count - 1] = BattleRecordEntry{};
    return true;
  }
  return false;
}

bool validateBattleStoreState(const BattleStoreState& state) {
  uint32_t previousRecordId = 0;
  bool sawEmptySlot = false;
  for (const BattleRecordEntry& entry : state.entries) {
    if (entry.recordId == 0) {
      sawEmptySlot = true;
      continue;
    }
    if (sawEmptySlot) return false;
    if (entry.recordId <= previousRecordId) return false;
    if (!validateBattleRecordEntry(entry)) return false;
    previousRecordId = entry.recordId;
  }
  return true;
}

bool encodeBattleStoreFile(const BattleStoreState& state, const uint32_t sequence, BattleStoreFileBytes& output,
                           size_t& outputSize) {
  if (sequence == 0 || !validateBattleStoreState(state)) return false;
  const size_t count = battleEntryCount(state);
  BattleStoreFileBytes candidate{};
  candidate[0] = 'P';
  candidate[1] = 'K';
  candidate[2] = 'B';
  candidate[3] = 'T';
  candidate[4] = POKEMON_BATTLE_STORE_VERSION;
  candidate[5] = static_cast<uint8_t>(count);
  write32(candidate.data(), 6, sequence);

  uint32_t crc = updateBattleStoreCrc32(BATTLE_STORE_CRC32_INITIAL, candidate.data(), POKEMON_BATTLE_HEADER_BYTES);
  size_t offset = POKEMON_BATTLE_HEADER_BYTES;
  for (size_t index = 0; index < count; ++index) {
    BattleEntryBytes entryBytes{};
    if (!encodeBattleRecordEntry(state.entries[index], entryBytes)) return false;
    std::memcpy(candidate.data() + offset, entryBytes.data(), entryBytes.size());
    crc = updateBattleStoreCrc32(crc, entryBytes.data(), entryBytes.size());
    offset += POKEMON_BATTLE_ENTRY_BYTES;
  }
  write32(candidate.data(), offset, finishBattleStoreCrc32(crc));
  output = candidate;
  outputSize = offset + POKEMON_BATTLE_FILE_CRC_BYTES;
  return true;
}

bool decodeBattleStoreFile(const uint8_t* data, const size_t size, BattleStoreState& output, uint32_t& sequence) {
  if (data == nullptr || size < POKEMON_BATTLE_HEADER_BYTES + POKEMON_BATTLE_FILE_CRC_BYTES) return false;
  if (data[0] != 'P' || data[1] != 'K' || data[2] != 'B' || data[3] != 'T') return false;
  const uint8_t version = data[4];
  if (version != POKEMON_BATTLE_STORE_VERSION && version != POKEMON_BATTLE_STORE_VERSION_V1) return false;
  const size_t entryBytes = version == POKEMON_BATTLE_STORE_VERSION_V1 ? POKEMON_BATTLE_ENTRY_BYTES_V1
                                                                       : POKEMON_BATTLE_ENTRY_BYTES;
  const uint8_t count = data[5];
  if (count > POKEMON_BATTLE_MAX_ENTRIES) return false;
  const uint32_t candidateSequence = read32(data, 6);
  if (candidateSequence == 0) return false;  // 0 is reserved for "no valid slot written yet"

  const size_t payloadSize = POKEMON_BATTLE_HEADER_BYTES + static_cast<size_t>(count) * entryBytes;
  if (size != payloadSize + POKEMON_BATTLE_FILE_CRC_BYTES) return false;

  const uint32_t expectedCrc = read32(data, payloadSize);
  const uint32_t actualCrc =
      finishBattleStoreCrc32(updateBattleStoreCrc32(BATTLE_STORE_CRC32_INITIAL, data, payloadSize));
  if (expectedCrc != actualCrc) return false;

  BattleStoreState candidate{};
  for (size_t index = 0; index < count; ++index) {
    const uint8_t* entryData = data + POKEMON_BATTLE_HEADER_BYTES + index * entryBytes;
    bool entryOk = false;
    if (version == POKEMON_BATTLE_STORE_VERSION_V1) {
      BattleEntryBytesV1 v1Bytes{};
      std::memcpy(v1Bytes.data(), entryData, v1Bytes.size());
      entryOk = decodeBattleRecordEntryV1(v1Bytes, candidate.entries[index]);
    } else {
      BattleEntryBytes v2Bytes{};
      std::memcpy(v2Bytes.data(), entryData, v2Bytes.size());
      entryOk = decodeBattleRecordEntry(v2Bytes, candidate.entries[index]);
    }
    if (!entryOk) return false;
  }
  if (!validateBattleStoreState(candidate)) return false;
  output = candidate;
  sequence = candidateSequence;
  return true;
}

bool decodeLegacyBattleStoreFile(const uint8_t* data, const size_t size, BattleStoreState& output) {
  if (data == nullptr || size < POKEMON_BATTLE_FILE_CRC_BYTES) return false;
  const size_t payloadSize = size - POKEMON_BATTLE_FILE_CRC_BYTES;
  if (payloadSize % POKEMON_BATTLE_ENTRY_BYTES_V1 != 0) return false;
  const size_t count = payloadSize / POKEMON_BATTLE_ENTRY_BYTES_V1;
  if (count > POKEMON_BATTLE_MAX_ENTRIES) return false;

  const uint32_t expectedCrc = read32(data, payloadSize);
  const uint32_t actualCrc =
      finishBattleStoreCrc32(updateBattleStoreCrc32(BATTLE_STORE_CRC32_INITIAL, data, payloadSize));
  if (expectedCrc != actualCrc) return false;

  BattleStoreState candidate{};
  for (size_t index = 0; index < count; ++index) {
    BattleEntryBytesV1 entryBytes{};
    std::memcpy(entryBytes.data(), data + index * POKEMON_BATTLE_ENTRY_BYTES_V1, entryBytes.size());
    if (!decodeBattleRecordEntryV1(entryBytes, candidate.entries[index])) return false;
  }
  if (!validateBattleStoreState(candidate)) return false;
  output = candidate;
  return true;
}

}  // namespace pokemon

#endif
