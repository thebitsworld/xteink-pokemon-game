#if defined(CROSSINK_ENABLE_POKEMON)

#include "PokemonIvEvStore.h"

#include <HalStorage.h>
#include <Logging.h>

namespace pokemon {
namespace {

constexpr const char* STORE_DIRECTORY = "/.crosspoint";
constexpr const char* STORE_PATH_A = "/.crosspoint/pokemon-ivev-a.bin";
constexpr const char* STORE_PATH_B = "/.crosspoint/pokemon-ivev-b.bin";

bool readExact(FsFile& file, void* output, const size_t size) {
  return file.read(output, size) == static_cast<int>(size);
}

bool writeExact(FsFile& file, const void* input, const size_t size) { return file.write(input, size) == size; }

// Wraparound-safe "is candidate newer than current", identical in spirit to
// PokemonStore's own sequenceIsNewer.
bool sequenceIsNewer(const uint32_t candidate, const uint32_t current) {
  return candidate != current && candidate - current < 0x80000000U;
}

// Reads and decodes one alternating-format slot. Returns false for a
// missing, oversized, short, or failed-validation file - all treated
// identically by the caller (this slot just isn't a candidate).
bool inspectSlot(const char* path, IvEvStoreState& outputState, uint32_t& outputSequence) {
  if (!Storage.exists(path)) return false;
  FsFile file = Storage.open(path, O_RDONLY);
  if (!file) {
    LOG_ERR("PokemonIvEvStore", "Failed to open %s", path);
    return false;
  }
  const uint64_t fileSize = file.fileSize64();
  if (fileSize > POKEMON_IVEV_FILE_MAX_BYTES) {
    LOG_ERR("PokemonIvEvStore", "IV/EV store slot larger than any valid layout, discarding %s", path);
    file.close();
    return false;
  }
  IvEvStoreFileBytes bytes{};
  const bool readOk = readExact(file, bytes.data(), static_cast<size_t>(fileSize));
  file.close();
  if (!readOk) {
    LOG_ERR("PokemonIvEvStore", "Short read on %s, discarding", path);
    return false;
  }
  return decodeIvEvStoreFile(bytes.data(), static_cast<size_t>(fileSize), outputState, outputSequence);
}

}  // namespace

void PokemonIvEvStore::load() const {
  state_ = IvEvStoreState{};
  ready_ = false;
  activeIsA_ = false;
  sequence_ = 0;
  loaded_ = true;  // even a fully-empty result leaves us in a valid, usable state

  IvEvStoreState stateA{};
  IvEvStoreState stateB{};
  uint32_t sequenceA = 0;
  uint32_t sequenceB = 0;
  const bool readyA = inspectSlot(STORE_PATH_A, stateA, sequenceA);
  const bool readyB = inspectSlot(STORE_PATH_B, stateB, sequenceB);
  if (!readyA && !readyB) return;  // fresh install, or both slots lost - stay empty, not an error
  activeIsA_ = readyA && (!readyB || !sequenceIsNewer(sequenceB, sequenceA));
  state_ = activeIsA_ ? stateA : stateB;
  sequence_ = activeIsA_ ? sequenceA : sequenceB;
  ready_ = true;
}

const IvEvEntry* PokemonIvEvStore::findEntry(const uint32_t recordId) const {
  if (!loaded_) load();
  return pokemon::findIvEvEntry(state_, recordId);
}

bool PokemonIvEvStore::writeState(const IvEvStoreState& state) const {
  if (!Storage.ensureDirectoryExists(STORE_DIRECTORY)) {
    LOG_ERR("PokemonIvEvStore", "Failed to prepare data directory");
    return false;
  }
  const uint32_t nextSequence = !ready_ ? 1U : (sequence_ == UINT32_MAX ? 1U : sequence_ + 1U);
  IvEvStoreFileBytes bytes{};
  size_t size = 0;
  if (!encodeIvEvStoreFile(state, nextSequence, bytes, size)) return false;

  const bool destinationIsA = !ready_ || !activeIsA_;
  const char* destinationPath = destinationIsA ? STORE_PATH_A : STORE_PATH_B;
  FsFile file = Storage.open(destinationPath, O_WRONLY | O_CREAT | O_TRUNC);
  if (!file) {
    LOG_ERR("PokemonIvEvStore", "Failed to open %s for write", destinationPath);
    return false;
  }
  const bool writeOk = writeExact(file, bytes.data(), size) && file.sync();
  const bool closeOk = file.close();
  if (!writeOk || !closeOk) {
    LOG_ERR("PokemonIvEvStore", "Failed to write %s", destinationPath);
    return false;
  }

  IvEvStoreState verified{};
  uint32_t verifiedSequence = 0;
  if (!inspectSlot(destinationPath, verified, verifiedSequence) || verifiedSequence != nextSequence ||
      !(verified == state)) {
    LOG_ERR("PokemonIvEvStore", "Inactive IV/EV store slot verification failed");
    return false;
  }

  state_ = state;
  sequence_ = nextSequence;
  activeIsA_ = destinationIsA;
  ready_ = true;
  return true;
}

bool PokemonIvEvStore::upsertEntry(const IvEvEntry& entry) {
  if (!loaded_) load();
  IvEvStoreState candidate = state_;
  if (!pokemon::upsertIvEvEntry(candidate, entry)) return false;
  return writeState(candidate);
}

bool PokemonIvEvStore::reset() {
  if (!loaded_) load();
  return writeState(IvEvStoreState{});
}

PokemonIvEvStore& devicePokemonIvEvStore() {
  static PokemonIvEvStore store;
  return store;
}

}  // namespace pokemon

#endif
