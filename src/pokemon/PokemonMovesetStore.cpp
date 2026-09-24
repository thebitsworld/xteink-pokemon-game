#if defined(CROSSINK_ENABLE_POKEMON)

#include "PokemonMovesetStore.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cstring>
#include <memory>
#include <new>

namespace pokemon {
namespace {

constexpr const char* STORE_DIRECTORY = "/.crosspoint";
constexpr const char* STORE_PATH_A = "/.crosspoint/pokemon-moves-a.bin";
constexpr const char* STORE_PATH_B = "/.crosspoint/pokemon-moves-b.bin";

bool readExact(FsFile& file, void* output, const size_t size) {
  return file.read(output, size) == static_cast<int>(size);
}

bool writeExact(FsFile& file, const void* input, const size_t size) { return file.write(input, size) == size; }

// Wraparound-safe "is candidate newer than current", same as every other
// double-buffered store here.
bool sequenceIsNewer(const uint32_t candidate, const uint32_t current) {
  return candidate != current && candidate - current < 0x80000000U;
}

// Reads and decodes one alternating-format slot; a missing, oversized, short
// or invalid file just isn't a candidate.
bool inspectSlot(const char* path, MovesetStoreState& outputState, uint32_t& outputSequence) {
  if (!Storage.exists(path)) return false;
  FsFile file = Storage.open(path, O_RDONLY);
  if (!file) {
    LOG_ERR("PokemonMovesetStore", "Failed to open %s", path);
    return false;
  }
  const uint64_t fileSize = file.fileSize64();
  if (fileSize > POKEMON_MOVESET_FILE_MAX_BYTES) {
    LOG_ERR("PokemonMovesetStore", "Moveset store slot larger than any valid layout, discarding %s", path);
    file.close();
    return false;
  }
  // Heap, non-throwing new: see PokemonIvEvStore.cpp's inspectSlot() for the
  // field crashes (stack depth, then std::bad_alloc under heap pressure).
  std::unique_ptr<MovesetStoreFileBytes> bytes(new (std::nothrow) MovesetStoreFileBytes());
  if (!bytes) {
    LOG_ERR("PokemonMovesetStore", "Out of memory reading %s", path);
    file.close();
    return false;
  }
  const bool readOk = readExact(file, bytes->data(), static_cast<size_t>(fileSize));
  file.close();
  if (!readOk) {
    LOG_ERR("PokemonMovesetStore", "Short read on %s, discarding", path);
    return false;
  }
  return decodeMovesetStoreFile(bytes->data(), static_cast<size_t>(fileSize), outputState, outputSequence);
}

}  // namespace

void PokemonMovesetStore::load() const {
  state_ = MovesetStoreState{};
  ready_ = false;
  activeIsA_ = false;
  sequence_ = 0;
  loaded_ = true;  // even a fully-empty result leaves us in a valid, usable state

  std::unique_ptr<MovesetStoreState> stateA(new (std::nothrow) MovesetStoreState());
  std::unique_ptr<MovesetStoreState> stateB(new (std::nothrow) MovesetStoreState());
  if (!stateA || !stateB) {
    LOG_ERR("PokemonMovesetStore", "Out of memory loading moveset store, staying empty");
    return;
  }
  uint32_t sequenceA = 0;
  uint32_t sequenceB = 0;
  const bool readyA = inspectSlot(STORE_PATH_A, *stateA, sequenceA);
  const bool readyB = inspectSlot(STORE_PATH_B, *stateB, sequenceB);
  if (!readyA && !readyB) return;  // fresh install, or both slots lost - stay empty, not an error
  activeIsA_ = readyA && (!readyB || !sequenceIsNewer(sequenceB, sequenceA));
  state_ = activeIsA_ ? *stateA : *stateB;
  sequence_ = activeIsA_ ? sequenceA : sequenceB;
  ready_ = true;
}

const MovesetEntry* PokemonMovesetStore::findEntry(const uint32_t recordId) const {
  if (!loaded_) load();
  return pokemon::findMovesetEntry(state_, recordId);
}

bool PokemonMovesetStore::writeState(const MovesetStoreState& state) const {
  if (!Storage.ensureDirectoryExists(STORE_DIRECTORY)) {
    LOG_ERR("PokemonMovesetStore", "Failed to prepare data directory");
    return false;
  }
  const uint32_t nextSequence = !ready_ ? 1U : (sequence_ == UINT32_MAX ? 1U : sequence_ + 1U);
  std::unique_ptr<MovesetStoreFileBytes> bytes(new (std::nothrow) MovesetStoreFileBytes());
  if (!bytes) {
    LOG_ERR("PokemonMovesetStore", "Out of memory encoding moveset store");
    return false;
  }
  size_t size = 0;
  if (!encodeMovesetStoreFile(state, nextSequence, *bytes, size)) return false;

  const bool destinationIsA = !ready_ || !activeIsA_;
  const char* destinationPath = destinationIsA ? STORE_PATH_A : STORE_PATH_B;
  FsFile file = Storage.open(destinationPath, O_WRONLY | O_CREAT | O_TRUNC);
  if (!file) {
    LOG_ERR("PokemonMovesetStore", "Failed to open %s for write", destinationPath);
    return false;
  }
  const bool writeOk = writeExact(file, bytes->data(), size) && file.sync();
  const bool closeOk = file.close();
  if (!writeOk || !closeOk) {
    LOG_ERR("PokemonMovesetStore", "Failed to write %s", destinationPath);
    return false;
  }

  // Verify by reading the just-written bytes straight back and comparing them
  // byte-for-byte with what was intended (header and sequence included),
  // instead of decoding a second full-size state just to == it.
  FsFile verifyFile = Storage.open(destinationPath, O_RDONLY);
  bool verifiedOk = false;
  if (verifyFile && verifyFile.fileSize64() == size) {
    std::unique_ptr<MovesetStoreFileBytes> readBack(new (std::nothrow) MovesetStoreFileBytes());
    verifiedOk = readBack != nullptr && readExact(verifyFile, readBack->data(), size) &&
                 std::memcmp(readBack->data(), bytes->data(), size) == 0;
  }
  const bool verifyCloseOk = !verifyFile || verifyFile.close();
  if (!verifiedOk || !verifyCloseOk) {
    LOG_ERR("PokemonMovesetStore", "Inactive moveset store slot verification failed");
    return false;
  }

  state_ = state;
  sequence_ = nextSequence;
  activeIsA_ = destinationIsA;
  ready_ = true;
  return true;
}

bool PokemonMovesetStore::upsertEntry(const MovesetEntry& entry) {
  if (!loaded_) load();
  std::unique_ptr<MovesetStoreState> candidate(new (std::nothrow) MovesetStoreState(state_));
  if (!candidate) {
    LOG_ERR("PokemonMovesetStore", "Out of memory upserting moveset entry");
    return false;
  }
  if (!pokemon::upsertMovesetEntry(*candidate, entry)) return false;
  return writeState(*candidate);
}

bool PokemonMovesetStore::upsertEntries(const std::span<const MovesetEntry> entries) {
  if (!loaded_) load();
  std::unique_ptr<MovesetStoreState> candidate(new (std::nothrow) MovesetStoreState(state_));
  if (!candidate) {
    LOG_ERR("PokemonMovesetStore", "Out of memory upserting moveset entries");
    return false;
  }
  bool anyApplied = false;
  for (const MovesetEntry& entry : entries) {
    if (pokemon::upsertMovesetEntry(*candidate, entry)) anyApplied = true;
  }
  if (!anyApplied) return false;
  return writeState(*candidate);
}

bool PokemonMovesetStore::removeEntry(const uint32_t recordId) {
  if (!loaded_) load();
  std::unique_ptr<MovesetStoreState> candidate(new (std::nothrow) MovesetStoreState(state_));
  if (!candidate) {
    LOG_ERR("PokemonMovesetStore", "Out of memory removing moveset entry");
    return false;
  }
  if (!pokemon::removeMovesetEntry(*candidate, recordId)) return false;
  return writeState(*candidate);
}

bool PokemonMovesetStore::reset() {
  if (!loaded_) load();
  std::unique_ptr<MovesetStoreState> empty(new (std::nothrow) MovesetStoreState());
  if (!empty) {
    LOG_ERR("PokemonMovesetStore", "Out of memory resetting moveset store");
    return false;
  }
  return writeState(*empty);
}

}  // namespace pokemon

#endif
