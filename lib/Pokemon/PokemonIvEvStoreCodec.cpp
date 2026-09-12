#if defined(CROSSINK_ENABLE_POKEMON)

#include "PokemonIvEvStoreCodec.h"

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

uint32_t updateIvEvStoreCrc32(uint32_t crc, const uint8_t* data, const size_t size) {
  for (size_t index = 0; index < size; ++index) {
    crc ^= data[index];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1U) ^ (0xEDB88320U & static_cast<uint32_t>(0U - (crc & 1U)));
    }
  }
  return crc;
}

bool validateIvEvEntry(const IvEvEntry& entry) {
  if (entry.recordId == 0) return false;
  for (const uint8_t iv : entry.iv) {
    if (iv > 15) return false;  // real Gen 1 IV range
  }
  return true;
}

bool encodeIvEvEntry(const IvEvEntry& entry, IvEvEntryBytes& output) {
  if (!validateIvEvEntry(entry)) return false;
  IvEvEntryBytes candidate{};
  write32(candidate.data(), 0, entry.recordId);
  for (size_t index = 0; index < STAT_COUNT; ++index) candidate[4 + index] = entry.iv[index];
  for (size_t index = 0; index < STAT_COUNT; ++index) candidate[4 + STAT_COUNT + index] = entry.ev[index];
  output = candidate;
  return true;
}

bool decodeIvEvEntry(const IvEvEntryBytes& bytes, IvEvEntry& output) {
  IvEvEntry candidate{};
  candidate.recordId = read32(bytes.data(), 0);
  for (size_t index = 0; index < STAT_COUNT; ++index) candidate.iv[index] = bytes[4 + index];
  for (size_t index = 0; index < STAT_COUNT; ++index) candidate.ev[index] = bytes[4 + STAT_COUNT + index];
  if (!validateIvEvEntry(candidate)) return false;
  output = candidate;
  return true;
}

size_t ivEvEntryCount(const IvEvStoreState& state) {
  size_t count = 0;
  while (count < state.entries.size() && state.entries[count].recordId != 0) ++count;
  return count;
}

const IvEvEntry* findIvEvEntry(const IvEvStoreState& state, const uint32_t recordId) {
  if (recordId == 0) return nullptr;
  const size_t count = ivEvEntryCount(state);
  for (size_t index = 0; index < count; ++index) {
    if (state.entries[index].recordId == recordId) return &state.entries[index];
  }
  return nullptr;
}

bool upsertIvEvEntry(IvEvStoreState& state, const IvEvEntry& entry) {
  if (!validateIvEvEntry(entry)) return false;
  const size_t count = ivEvEntryCount(state);
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

bool validateIvEvStoreState(const IvEvStoreState& state) {
  uint32_t previousRecordId = 0;
  bool sawEmptySlot = false;
  for (const IvEvEntry& entry : state.entries) {
    if (entry.recordId == 0) {
      sawEmptySlot = true;
      continue;
    }
    if (sawEmptySlot) return false;
    if (entry.recordId <= previousRecordId) return false;
    if (!validateIvEvEntry(entry)) return false;
    previousRecordId = entry.recordId;
  }
  return true;
}

bool encodeIvEvStoreFile(const IvEvStoreState& state, const uint32_t sequence, IvEvStoreFileBytes& output,
                         size_t& outputSize) {
  if (sequence == 0 || !validateIvEvStoreState(state)) return false;
  const size_t count = ivEvEntryCount(state);
  IvEvStoreFileBytes candidate{};
  candidate[0] = 'P';
  candidate[1] = 'K';
  candidate[2] = 'I';
  candidate[3] = 'V';
  candidate[4] = POKEMON_IVEV_STORE_VERSION;
  write16(candidate.data(), 5, static_cast<uint16_t>(count));
  write32(candidate.data(), 7, sequence);

  uint32_t crc = updateIvEvStoreCrc32(IVEV_STORE_CRC32_INITIAL, candidate.data(), POKEMON_IVEV_HEADER_BYTES);
  size_t offset = POKEMON_IVEV_HEADER_BYTES;
  for (size_t index = 0; index < count; ++index) {
    IvEvEntryBytes entryBytes{};
    if (!encodeIvEvEntry(state.entries[index], entryBytes)) return false;
    std::memcpy(candidate.data() + offset, entryBytes.data(), entryBytes.size());
    crc = updateIvEvStoreCrc32(crc, entryBytes.data(), entryBytes.size());
    offset += POKEMON_IVEV_ENTRY_BYTES;
  }
  write32(candidate.data(), offset, finishIvEvStoreCrc32(crc));
  output = candidate;
  outputSize = offset + POKEMON_IVEV_FILE_CRC_BYTES;
  return true;
}

bool decodeIvEvStoreFile(const uint8_t* data, const size_t size, IvEvStoreState& output, uint32_t& sequence) {
  if (data == nullptr || size < POKEMON_IVEV_HEADER_BYTES + POKEMON_IVEV_FILE_CRC_BYTES) return false;
  if (data[0] != 'P' || data[1] != 'K' || data[2] != 'I' || data[3] != 'V') return false;
  if (data[4] != POKEMON_IVEV_STORE_VERSION) return false;
  const uint16_t count = read16(data, 5);
  if (count > POKEMON_IVEV_MAX_ENTRIES) return false;
  const uint32_t candidateSequence = read32(data, 7);
  if (candidateSequence == 0) return false;  // 0 is reserved for "no valid slot written yet"

  const size_t payloadSize = POKEMON_IVEV_HEADER_BYTES + static_cast<size_t>(count) * POKEMON_IVEV_ENTRY_BYTES;
  if (size != payloadSize + POKEMON_IVEV_FILE_CRC_BYTES) return false;

  const uint32_t expectedCrc = read32(data, payloadSize);
  const uint32_t actualCrc = finishIvEvStoreCrc32(updateIvEvStoreCrc32(IVEV_STORE_CRC32_INITIAL, data, payloadSize));
  if (expectedCrc != actualCrc) return false;

  IvEvStoreState candidate{};
  for (size_t index = 0; index < count; ++index) {
    IvEvEntryBytes entryBytes{};
    std::memcpy(entryBytes.data(), data + POKEMON_IVEV_HEADER_BYTES + index * POKEMON_IVEV_ENTRY_BYTES,
                entryBytes.size());
    if (!decodeIvEvEntry(entryBytes, candidate.entries[index])) return false;
  }
  if (!validateIvEvStoreState(candidate)) return false;
  output = candidate;
  sequence = candidateSequence;
  return true;
}

}  // namespace pokemon

#endif
