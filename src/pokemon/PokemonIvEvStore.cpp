#if defined(CROSSINK_ENABLE_POKEMON)

#include "PokemonIvEvStore.h"

#include <HalStorage.h>
#include <Logging.h>

#include <memory>
#include <new>

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
  // Gated against the LEGACY (larger) bound, not the current
  // POKEMON_IVEV_MAX_ENTRIES-sized one - a file written under a higher cap
  // by a previous build must still be readable here so decodeIvEvStoreFile()
  // gets the chance to clamp it (keep the lowest-recordId entries, drop the
  // rest) instead of the whole slot being discarded outright as "too big."
  if (fileSize > POKEMON_IVEV_LEGACY_FILE_MAX_BYTES) {
    LOG_ERR("PokemonIvEvStore", "IV/EV store slot larger than any valid layout, discarding %s", path);
    file.close();
    return false;
  }
  // Heap-allocated, not a stack local - at POKEMON_IVEV_LEGACY_FILE_MAX_BYTES
  // (~14KB) this, combined with the same-sized buffers this function's own
  // caller/callee both need at the same time (PokemonIvEvStore::load()'s
  // stateA/stateB, decodeIvEvStoreFile()'s own candidate), was large enough
  // to overflow a real device's task stack - confirmed via a field crash
  // report symbolized back to this exact call chain.
  // Non-throwing new, not std::make_unique - a plain `new` failing to find
  // ~14KB throws std::bad_alloc, and nothing in this codebase catches
  // exceptions, so it would propagate to std::terminate()/abort() and
  // crash the device instead of just failing this one read (confirmed via
  // a second field crash report, once heap pressure during rendering made
  // an allocation in this call chain fail).
  std::unique_ptr<IvEvStoreFileBytes> bytes(new (std::nothrow) IvEvStoreFileBytes());
  if (!bytes) {
    LOG_ERR("PokemonIvEvStore", "Out of memory reading %s", path);
    file.close();
    return false;
  }
  const bool readOk = readExact(file, bytes->data(), static_cast<size_t>(fileSize));
  file.close();
  if (!readOk) {
    LOG_ERR("PokemonIvEvStore", "Short read on %s, discarding", path);
    return false;
  }
  return decodeIvEvStoreFile(bytes->data(), static_cast<size_t>(fileSize), outputState, outputSequence);
}

}  // namespace

void PokemonIvEvStore::load() const {
  state_ = IvEvStoreState{};
  ready_ = false;
  activeIsA_ = false;
  sequence_ = 0;
  loaded_ = true;  // even a fully-empty result leaves us in a valid, usable state

  // Heap-allocated, not stack locals - two of these (~16KB each) alive at
  // once, on top of the same-sized scratch buffers inspectSlot()/
  // decodeIvEvStoreFile() need at the nested call depths this function
  // reaches, was enough to overflow a real device's task stack (confirmed
  // via a field crash report symbolized back to exactly this function).
  // Non-throwing new, not std::make_unique - see inspectSlot()'s matching
  // comment. On failure we just stay in the already-set fresh-install/empty
  // state above instead of crashing; a future call retries once more heap
  // is free.
  std::unique_ptr<IvEvStoreState> stateA(new (std::nothrow) IvEvStoreState());
  std::unique_ptr<IvEvStoreState> stateB(new (std::nothrow) IvEvStoreState());
  if (!stateA || !stateB) {
    LOG_ERR("PokemonIvEvStore", "Out of memory loading IV/EV store, staying empty");
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
  // Heap-allocated, not stack locals - see load()'s matching comment; this
  // function reaches similarly deep nested call depths (encode, then
  // write, then inspectSlot -> decode again to verify) that overflowed a
  // real device's task stack.
  // Non-throwing new, not std::make_unique - see inspectSlot()'s matching
  // comment; a failed allocation here must fail this write attempt, not
  // crash the device.
  std::unique_ptr<IvEvStoreFileBytes> bytes(new (std::nothrow) IvEvStoreFileBytes());
  if (!bytes) {
    LOG_ERR("PokemonIvEvStore", "Out of memory encoding IV/EV store");
    return false;
  }
  size_t size = 0;
  if (!encodeIvEvStoreFile(state, nextSequence, *bytes, size)) return false;

  const bool destinationIsA = !ready_ || !activeIsA_;
  const char* destinationPath = destinationIsA ? STORE_PATH_A : STORE_PATH_B;
  FsFile file = Storage.open(destinationPath, O_WRONLY | O_CREAT | O_TRUNC);
  if (!file) {
    LOG_ERR("PokemonIvEvStore", "Failed to open %s for write", destinationPath);
    return false;
  }
  const bool writeOk = writeExact(file, bytes->data(), size) && file.sync();
  const bool closeOk = file.close();
  if (!writeOk || !closeOk) {
    LOG_ERR("PokemonIvEvStore", "Failed to write %s", destinationPath);
    return false;
  }

  // Free bytes (~14KB) before allocating verified (~16KB) rather than
  // holding both at once - lowers the peak concurrent heap this function
  // needs, on top of failing gracefully (below) if the heap is tight.
  bytes.reset();
  std::unique_ptr<IvEvStoreState> verified(new (std::nothrow) IvEvStoreState());
  if (!verified) {
    LOG_ERR("PokemonIvEvStore", "Out of memory verifying IV/EV store write");
    return false;
  }
  uint32_t verifiedSequence = 0;
  if (!inspectSlot(destinationPath, *verified, verifiedSequence) || verifiedSequence != nextSequence ||
      !(*verified == state)) {
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
  // Heap-allocated, not a stack local copy of state_ - see load()'s
  // matching comment. This one was missed in the first pass at this fix
  // (found via a second real-device crash report, symbolized back to
  // exactly this line) - a reminder to grep the whole file for every
  // IvEvStoreState/IvEvStoreFileBytes local, not just the ones already
  // known about.
  // Non-throwing new, not std::make_unique - see inspectSlot()'s matching
  // comment. A third field crash report showed even this heap allocation
  // can itself fail (and, un-guarded against throwing, crash the device)
  // when the heap is under pressure elsewhere (e.g. mid-render) - failing
  // this one upsert gracefully is the correct behavior, matching how the
  // caller (PokemonService::ensureIvEv) already treats a `false` return.
  std::unique_ptr<IvEvStoreState> candidate(new (std::nothrow) IvEvStoreState(state_));
  if (!candidate) {
    LOG_ERR("PokemonIvEvStore", "Out of memory upserting IV/EV entry");
    return false;
  }
  if (!pokemon::upsertIvEvEntry(*candidate, entry)) return false;
  return writeState(*candidate);
}

bool PokemonIvEvStore::removeEntry(const uint32_t recordId) {
  if (!loaded_) load();
  // Heap-allocated, not a stack local copy of state_ - see upsertEntry()'s
  // matching comment (and load()'s, for the original field-crash context).
  std::unique_ptr<IvEvStoreState> candidate(new (std::nothrow) IvEvStoreState(state_));
  if (!candidate) {
    LOG_ERR("PokemonIvEvStore", "Out of memory removing IV/EV entry");
    return false;
  }
  if (!pokemon::removeIvEvEntry(*candidate, recordId)) return false;
  return writeState(*candidate);
}

bool PokemonIvEvStore::reset() {
  if (!loaded_) load();
  std::unique_ptr<IvEvStoreState> empty(new (std::nothrow) IvEvStoreState());
  if (!empty) {
    LOG_ERR("PokemonIvEvStore", "Out of memory resetting IV/EV store");
    return false;
  }
  return writeState(*empty);
}

PokemonIvEvStore& devicePokemonIvEvStore() {
  static PokemonIvEvStore store;
  return store;
}

}  // namespace pokemon

#endif
