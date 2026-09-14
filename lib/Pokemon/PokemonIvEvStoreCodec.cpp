#if defined(CROSSINK_ENABLE_POKEMON)

#include "PokemonIvEvStoreCodec.h"

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

}  // namespace

uint32_t updateIvEvStoreCrc32(const uint32_t crc, const uint8_t* data, const size_t size) {
  return updatePokemonCrc32(crc, data, size);
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

bool removeIvEvEntry(IvEvStoreState& state, const uint32_t recordId) {
  const size_t count = ivEvEntryCount(state);
  for (size_t index = 0; index < count; ++index) {
    if (state.entries[index].recordId != recordId) continue;
    for (size_t shift = index; shift + 1 < count; ++shift) state.entries[shift] = state.entries[shift + 1];
    state.entries[count - 1] = IvEvEntry{};
    return true;
  }
  return false;
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
  // Writes directly into the caller-provided `output` rather than building
  // a second, same-sized local first: an earlier fix already had this
  // function heap-allocate its own candidate buffer to get it off the
  // stack (a real field crash traced to these buffers' combined stack
  // depth), but that meant `output` (already heap-allocated by the caller,
  // see PokemonIvEvStore.cpp's writeState()) and this function's own
  // candidate were BOTH alive at once - doubling the peak heap this write
  // path needs, which is exactly what later made the allocation itself
  // start failing under real heap pressure (a second field crash, this
  // time an uncaught std::bad_alloc). Writing straight into `output`
  // removes this function's own allocation entirely. Unlike decode (see
  // its own comment below), this can't actually leave `output` partially
  // written on a real failure: validateIvEvStoreState(state) above already
  // validates every entry via the exact same check encodeIvEvEntry() uses,
  // so the loop's own `if (!encodeIvEvEntry(...)) return false;` can never
  // trigger in practice once that top check has passed - it stays only as
  // a cheap, defensive backstop.
  output[0] = 'P';
  output[1] = 'K';
  output[2] = 'I';
  output[3] = 'V';
  output[4] = POKEMON_IVEV_STORE_VERSION;
  write16(output.data(), 5, static_cast<uint16_t>(count));
  write32(output.data(), 7, sequence);

  uint32_t crc = updateIvEvStoreCrc32(IVEV_STORE_CRC32_INITIAL, output.data(), POKEMON_IVEV_HEADER_BYTES);
  size_t offset = POKEMON_IVEV_HEADER_BYTES;
  for (size_t index = 0; index < count; ++index) {
    IvEvEntryBytes entryBytes{};
    if (!encodeIvEvEntry(state.entries[index], entryBytes)) return false;
    std::memcpy(output.data() + offset, entryBytes.data(), entryBytes.size());
    crc = updateIvEvStoreCrc32(crc, entryBytes.data(), entryBytes.size());
    offset += POKEMON_IVEV_ENTRY_BYTES;
  }
  write32(output.data(), offset, finishIvEvStoreCrc32(crc));
  outputSize = offset + POKEMON_IVEV_FILE_CRC_BYTES;
  return true;
}

bool decodeIvEvStoreFile(const uint8_t* data, const size_t size, IvEvStoreState& output, uint32_t& sequence) {
  if (data == nullptr || size < POKEMON_IVEV_HEADER_BYTES + POKEMON_IVEV_FILE_CRC_BYTES) return false;
  if (data[0] != 'P' || data[1] != 'K' || data[2] != 'I' || data[3] != 'V') return false;
  if (data[4] != POKEMON_IVEV_STORE_VERSION) return false;
  const uint16_t count = read16(data, 5);
  if (count > POKEMON_IVEV_LEGACY_MAX_ENTRIES) return false;
  const uint32_t candidateSequence = read32(data, 7);
  if (candidateSequence == 0) return false;  // 0 is reserved for "no valid slot written yet"

  const size_t payloadSize = POKEMON_IVEV_HEADER_BYTES + static_cast<size_t>(count) * POKEMON_IVEV_ENTRY_BYTES;
  if (size != payloadSize + POKEMON_IVEV_FILE_CRC_BYTES) return false;

  const uint32_t expectedCrc = read32(data, payloadSize);
  const uint32_t actualCrc = finishIvEvStoreCrc32(updateIvEvStoreCrc32(IVEV_STORE_CRC32_INITIAL, data, payloadSize));
  if (expectedCrc != actualCrc) return false;

  // Validate every entry - and the whole array's ascending-recordId
  // invariant - using only small per-entry scratch values, BEFORE writing
  // anything into the caller-provided `output`. This matters because
  // `output` is written to directly rather than via a second, same-sized
  // (~16KB) internal copy (see encodeIvEvStoreFile()'s matching comment for
  // why that duplicate was itself a real crash risk): without this
  // validate-first pass, a failure partway through decoding entries would
  // leave `output` part freshly-written/part however it looked before this
  // call, instead of the clean "untouched on failure" guarantee a second
  // internal copy used to provide for free. A valid CRC (which covers the
  // entire payload) makes it effectively certain this pass agrees with
  // validateIvEvStoreState() below, which stays only as a cheap defensive
  // backstop.
  uint32_t previousRecordId = 0;
  for (size_t index = 0; index < count; ++index) {
    IvEvEntryBytes entryBytes{};
    std::memcpy(entryBytes.data(), data + POKEMON_IVEV_HEADER_BYTES + index * POKEMON_IVEV_ENTRY_BYTES,
                entryBytes.size());
    IvEvEntry scratch{};
    if (!decodeIvEvEntry(entryBytes, scratch)) return false;
    if (scratch.recordId <= previousRecordId) return false;  // validateIvEvEntry() already ruled out 0
    previousRecordId = scratch.recordId;
  }

  // Every entry already proved valid above, so this pass cannot fail -
  // `output` is only ever mutated once decoding as a whole is guaranteed to
  // succeed. `output` may hold stale data from a previous decode into the
  // same buffer, so every entry past `keptCount` is explicitly cleared too,
  // rather than relying on it starting zeroed.
  //
  // `keptCount` (not `count`) bounds this pass: a file written under a
  // higher POKEMON_IVEV_MAX_ENTRIES than this build's (see
  // POKEMON_IVEV_LEGACY_MAX_ENTRIES's doc comment) can have more entries
  // than `output.entries` has room for. The CRC/ordering validation above
  // already covered the file's FULL contents (using `count`), so this isn't
  // silently accepting a corrupt file - it's deliberately keeping only the
  // first `keptCount` (lowest recordId, i.e. oldest) entries of an
  // otherwise-valid one and dropping the rest, the same "doesn't crash,
  // just re-rolls next time it's looked up" degradation
  // PokemonService::ensureIvEv() already handles for a store that's simply
  // full.
  const size_t keptCount = std::min<size_t>(count, output.entries.size());
  for (size_t index = 0; index < keptCount; ++index) {
    IvEvEntryBytes entryBytes{};
    std::memcpy(entryBytes.data(), data + POKEMON_IVEV_HEADER_BYTES + index * POKEMON_IVEV_ENTRY_BYTES,
                entryBytes.size());
    decodeIvEvEntry(entryBytes, output.entries[index]);
  }
  for (size_t index = keptCount; index < output.entries.size(); ++index) {
    output.entries[index] = IvEvEntry{};
  }
  if (!validateIvEvStoreState(output)) return false;  // defensive backstop only, see comment above
  sequence = candidateSequence;
  return true;
}

}  // namespace pokemon

#endif
