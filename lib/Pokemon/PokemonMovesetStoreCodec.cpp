#if defined(CROSSINK_ENABLE_POKEMON)

#include "PokemonMovesetStoreCodec.h"

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

}  // namespace

uint32_t updateMovesetStoreCrc32(const uint32_t crc, const uint8_t* data, const size_t size) {
  return updatePokemonCrc32(crc, data, size);
}

bool validateMovesetEntry(const MovesetEntry& entry) {
  if (entry.recordId == 0) return false;
  bool sawEmptySlot = false;
  for (size_t index = 0; index < BATTLE_MOVE_SLOTS; ++index) {
    const uint8_t moveId = entry.moves[index];
    if (moveId == 0) {
      sawEmptySlot = true;
      if (entry.ppUp[index] != 0) return false;
      continue;
    }
    if (sawEmptySlot) return false;  // packed at the front, like BattleRecordEntry::moves
    if (moveId > POKEMON_MOVE_ID_MAX) return false;
    if (entry.ppUp[index] > 3) return false;  // real Gen 1 cap: 3 PP Ups per move slot
  }
  return true;
}

bool encodeMovesetEntry(const MovesetEntry& entry, MovesetEntryBytes& output) {
  if (!validateMovesetEntry(entry)) return false;
  MovesetEntryBytes candidate{};
  write32(candidate.data(), 0, entry.recordId);
  for (size_t index = 0; index < BATTLE_MOVE_SLOTS; ++index) candidate[4 + index] = entry.moves[index];
  for (size_t index = 0; index < BATTLE_MOVE_SLOTS; ++index) candidate[4 + BATTLE_MOVE_SLOTS + index] = entry.ppUp[index];
  output = candidate;
  return true;
}

bool decodeMovesetEntry(const MovesetEntryBytes& bytes, MovesetEntry& output) {
  MovesetEntry candidate{};
  candidate.recordId = read32(bytes.data(), 0);
  for (size_t index = 0; index < BATTLE_MOVE_SLOTS; ++index) candidate.moves[index] = bytes[4 + index];
  for (size_t index = 0; index < BATTLE_MOVE_SLOTS; ++index) candidate.ppUp[index] = bytes[4 + BATTLE_MOVE_SLOTS + index];
  if (!validateMovesetEntry(candidate)) return false;
  output = candidate;
  return true;
}

size_t movesetEntryCount(const MovesetStoreState& state) {
  size_t count = 0;
  while (count < state.entries.size() && state.entries[count].recordId != 0) ++count;
  return count;
}

const MovesetEntry* findMovesetEntry(const MovesetStoreState& state, const uint32_t recordId) {
  if (recordId == 0) return nullptr;
  const size_t count = movesetEntryCount(state);
  for (size_t index = 0; index < count; ++index) {
    if (state.entries[index].recordId == recordId) return &state.entries[index];
  }
  return nullptr;
}

bool upsertMovesetEntry(MovesetStoreState& state, const MovesetEntry& entry) {
  if (!validateMovesetEntry(entry)) return false;
  const size_t count = movesetEntryCount(state);
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

bool removeMovesetEntry(MovesetStoreState& state, const uint32_t recordId) {
  const size_t count = movesetEntryCount(state);
  for (size_t index = 0; index < count; ++index) {
    if (state.entries[index].recordId != recordId) continue;
    for (size_t shift = index; shift + 1 < count; ++shift) state.entries[shift] = state.entries[shift + 1];
    state.entries[count - 1] = MovesetEntry{};
    return true;
  }
  return false;
}

bool validateMovesetStoreState(const MovesetStoreState& state) {
  uint32_t previousRecordId = 0;
  bool sawEmptySlot = false;
  for (const MovesetEntry& entry : state.entries) {
    if (entry.recordId == 0) {
      sawEmptySlot = true;
      continue;
    }
    if (sawEmptySlot) return false;
    if (entry.recordId <= previousRecordId) return false;
    if (!validateMovesetEntry(entry)) return false;
    previousRecordId = entry.recordId;
  }
  return true;
}

bool encodeMovesetStoreFile(const MovesetStoreState& state, const uint32_t sequence, uint8_t* output,
                            const size_t outputCapacity, size_t& outputSize) {
  if (sequence == 0 || output == nullptr || !validateMovesetStoreState(state)) return false;
  const size_t count = movesetEntryCount(state);
  const size_t required =
      POKEMON_MOVESET_HEADER_BYTES + count * POKEMON_MOVESET_ENTRY_BYTES + POKEMON_MOVESET_FILE_CRC_BYTES;
  if (outputCapacity < required) return false;

  // validateMovesetStoreState() above already validated every entry with the
  // exact check encodeMovesetEntry() uses, so the loop's own failure branch
  // can't trigger once that has passed - it stays as a cheap backstop.
  output[0] = 'P';
  output[1] = 'K';
  output[2] = 'M';
  output[3] = 'V';
  output[4] = POKEMON_MOVESET_STORE_VERSION;
  write16(output, 5, static_cast<uint16_t>(count));
  write32(output, 7, sequence);

  uint32_t crc = updateMovesetStoreCrc32(MOVESET_STORE_CRC32_INITIAL, output, POKEMON_MOVESET_HEADER_BYTES);
  size_t offset = POKEMON_MOVESET_HEADER_BYTES;
  for (size_t index = 0; index < count; ++index) {
    MovesetEntryBytes entryBytes{};
    if (!encodeMovesetEntry(state.entries[index], entryBytes)) return false;
    std::memcpy(output + offset, entryBytes.data(), entryBytes.size());
    crc = updateMovesetStoreCrc32(crc, entryBytes.data(), entryBytes.size());
    offset += POKEMON_MOVESET_ENTRY_BYTES;
  }
  write32(output, offset, finishMovesetStoreCrc32(crc));
  outputSize = offset + POKEMON_MOVESET_FILE_CRC_BYTES;
  return true;
}

bool decodeMovesetStoreFile(const uint8_t* data, const size_t size, MovesetStoreState& output, uint32_t& sequence) {
  if (data == nullptr || size < POKEMON_MOVESET_HEADER_BYTES + POKEMON_MOVESET_FILE_CRC_BYTES) return false;
  if (data[0] != 'P' || data[1] != 'K' || data[2] != 'M' || data[3] != 'V') return false;
  if (data[4] != POKEMON_MOVESET_STORE_VERSION) return false;
  const uint16_t count = read16(data, 5);
  if (count > POKEMON_MOVESET_MAX_ENTRIES) return false;
  const uint32_t candidateSequence = read32(data, 7);
  if (candidateSequence == 0) return false;  // 0 is reserved for "no valid slot written yet"

  const size_t payloadSize = POKEMON_MOVESET_HEADER_BYTES + static_cast<size_t>(count) * POKEMON_MOVESET_ENTRY_BYTES;
  if (size != payloadSize + POKEMON_MOVESET_FILE_CRC_BYTES) return false;

  const uint32_t expectedCrc = read32(data, payloadSize);
  const uint32_t actualCrc =
      finishMovesetStoreCrc32(updateMovesetStoreCrc32(MOVESET_STORE_CRC32_INITIAL, data, payloadSize));
  if (expectedCrc != actualCrc) return false;

  // Validate every entry and the ascending-recordId invariant with small
  // per-entry scratch values BEFORE touching `output`, so a failure leaves
  // it exactly as the caller passed it (no second full-size copy needed).
  uint32_t previousRecordId = 0;
  for (size_t index = 0; index < count; ++index) {
    MovesetEntryBytes entryBytes{};
    std::memcpy(entryBytes.data(), data + POKEMON_MOVESET_HEADER_BYTES + index * POKEMON_MOVESET_ENTRY_BYTES,
                entryBytes.size());
    MovesetEntry scratch{};
    if (!decodeMovesetEntry(entryBytes, scratch)) return false;
    if (scratch.recordId <= previousRecordId) return false;  // validateMovesetEntry() already ruled out 0
    previousRecordId = scratch.recordId;
  }

  for (size_t index = 0; index < count; ++index) {
    MovesetEntryBytes entryBytes{};
    std::memcpy(entryBytes.data(), data + POKEMON_MOVESET_HEADER_BYTES + index * POKEMON_MOVESET_ENTRY_BYTES,
                entryBytes.size());
    decodeMovesetEntry(entryBytes, output.entries[index]);
  }
  for (size_t index = count; index < output.entries.size(); ++index) output.entries[index] = MovesetEntry{};
  if (!validateMovesetStoreState(output)) return false;  // defensive backstop only
  sequence = candidateSequence;
  return true;
}

}  // namespace pokemon

#endif
