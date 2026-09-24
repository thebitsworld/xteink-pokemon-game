#if defined(CROSSINK_ENABLE_POKEMON)

#include "PokemonHallOfFameStore.h"

#include <HalStorage.h>
#include <Logging.h>

namespace pokemon {
namespace {

constexpr const char* STORE_DIRECTORY = "/.crosspoint";
constexpr const char* STORE_PATH_A = "/.crosspoint/pokemon-hof-a.bin";
constexpr const char* STORE_PATH_B = "/.crosspoint/pokemon-hof-b.bin";

// Deletes the slot files outright. Used only by reset() when the store could not be
// read: a deliberate wipe must not be skipped just because the old contents are
// unreadable, but it also must not write over files it cannot inspect.
bool discardSlotFiles() {
  for (const char* path : {STORE_PATH_A, STORE_PATH_B}) {
    if (Storage.exists(path) && !Storage.remove(path)) return false;
  }
  return true;
}

bool readExact(FsFile& file, void* output, const size_t size) {
  return file.read(output, size) == static_cast<int>(size);
}

bool writeExact(FsFile& file, const void* input, const size_t size) { return file.write(input, size) == size; }

// Wraparound-safe "is candidate newer than current", identical in spirit to
// every other double-buffered store in this project.
bool sequenceIsNewer(const uint32_t candidate, const uint32_t current) {
  return candidate != current && candidate - current < 0x80000000U;
}

// Reads and decodes one alternating-format slot. Returns false for a
// missing, wrong-sized, or failed-validation file - all treated identically
// by the caller (this slot just isn't a candidate). `transientFailure` is set
// when the slot could not be READ at all (open failed, short read): that says
// nothing about its contents, and mistaking it for "no Hall of Fame yet" would
// let captureOnce() overwrite an already-recorded one.
bool inspectSlot(const char* path, HallOfFameState& outputState, uint32_t& outputSequence, bool& transientFailure) {
  if (!Storage.exists(path)) return false;
  FsFile file = Storage.open(path, O_RDONLY);
  if (!file) {
    LOG_ERR("PokemonHallOfFameStore", "Failed to open %s", path);
    transientFailure = true;
    return false;
  }
  const uint64_t fileSize = file.fileSize64();
  if (fileSize != POKEMON_HOF_FILE_BYTES) {
    file.close();
    return false;
  }
  HallOfFameFileBytes bytes{};
  const bool readOk = readExact(file, bytes.data(), bytes.size());
  file.close();
  if (!readOk) {
    LOG_ERR("PokemonHallOfFameStore", "Short read on %s", path);
    transientFailure = true;
    return false;
  }
  return decodeHallOfFameFile(bytes.data(), bytes.size(), outputState, outputSequence);
}

}  // namespace

void PokemonHallOfFameStore::load() const {
  state_ = HallOfFameState{};
  ready_ = false;
  activeIsA_ = false;
  sequence_ = 0;
  loaded_ = true;  // even a fully-empty result leaves us in a valid, usable state

  HallOfFameState stateA{};
  HallOfFameState stateB{};
  uint32_t sequenceA = 0;
  uint32_t sequenceB = 0;
  bool transientFailure = false;
  const bool readyA = inspectSlot(STORE_PATH_A, stateA, sequenceA, transientFailure);
  const bool readyB = inspectSlot(STORE_PATH_B, stateB, sequenceB, transientFailure);
  if (transientFailure) {
    loaded_ = false;  // reads see "no Hall of Fame" for now, writes are refused, the next call retries
    return;
  }
  if (!readyA && !readyB) return;  // fresh install, or both slots lost - stay empty, not an error
  activeIsA_ = readyA && (!readyB || !sequenceIsNewer(sequenceB, sequenceA));
  state_ = activeIsA_ ? stateA : stateB;
  sequence_ = activeIsA_ ? sequenceA : sequenceB;
  ready_ = true;
}

bool PokemonHallOfFameStore::hasEntry() const {
  if (!loaded_) load();
  return ready_ && state_.cleared;
}

const HallOfFameState* PokemonHallOfFameStore::entry() const { return hasEntry() ? &state_ : nullptr; }

bool PokemonHallOfFameStore::writeState(const HallOfFameState& state) const {
  if (!Storage.ensureDirectoryExists(STORE_DIRECTORY)) {
    LOG_ERR("PokemonHallOfFameStore", "Failed to prepare data directory");
    return false;
  }
  const uint32_t nextSequence = !ready_ ? 1U : (sequence_ == UINT32_MAX ? 1U : sequence_ + 1U);
  HallOfFameFileBytes bytes{};
  size_t size = 0;
  if (!encodeHallOfFameFile(state, nextSequence, bytes, size)) return false;

  const bool destinationIsA = !ready_ || !activeIsA_;
  const char* destinationPath = destinationIsA ? STORE_PATH_A : STORE_PATH_B;
  FsFile file = Storage.open(destinationPath, O_WRONLY | O_CREAT | O_TRUNC);
  if (!file) {
    LOG_ERR("PokemonHallOfFameStore", "Failed to open %s for write", destinationPath);
    return false;
  }
  const bool writeOk = writeExact(file, bytes.data(), size) && file.sync();
  const bool closeOk = file.close();
  if (!writeOk || !closeOk) {
    LOG_ERR("PokemonHallOfFameStore", "Failed to write %s", destinationPath);
    return false;
  }

  HallOfFameState verified{};
  uint32_t verifiedSequence = 0;
  bool verifyReadFailed = false;
  if (!inspectSlot(destinationPath, verified, verifiedSequence, verifyReadFailed) || verifiedSequence != nextSequence ||
      !(verified == state)) {
    LOG_ERR("PokemonHallOfFameStore", "Inactive Hall of Fame store slot verification failed");
    return false;
  }

  state_ = state;
  sequence_ = nextSequence;
  activeIsA_ = destinationIsA;
  ready_ = true;
  return true;
}

bool PokemonHallOfFameStore::captureOnce(const HallOfFameState& state) {
  if (!loaded_) load();
  if (!loaded_) return false;  // couldn't read the existing files: refuse to overwrite them
  if (ready_ && state_.cleared) return false;  // already captured - never overwrite
  HallOfFameState candidate = state;
  candidate.cleared = true;
  return writeState(candidate);
}

bool PokemonHallOfFameStore::reset() {
  if (!loaded_) load();
  if (!loaded_) {
    if (!discardSlotFiles()) return false;
    state_ = HallOfFameState{};
    ready_ = false;
    activeIsA_ = false;
    sequence_ = 0;
    loaded_ = true;
    return true;
  }
  return writeState(HallOfFameState{});
}

PokemonHallOfFameStore& devicePokemonHallOfFameStore() {
  static PokemonHallOfFameStore store;
  return store;
}

}  // namespace pokemon

#endif
