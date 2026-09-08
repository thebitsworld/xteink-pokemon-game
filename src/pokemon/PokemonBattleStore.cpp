#if defined(CROSSINK_ENABLE_POKEMON)

#include "PokemonBattleStore.h"

#include <HalStorage.h>
#include <Logging.h>

namespace pokemon {
namespace {

constexpr const char* STORE_DIRECTORY = "/.crosspoint";
constexpr const char* STORE_PATH_A = "/.crosspoint/pokemon-battle-a.bin";
constexpr const char* STORE_PATH_B = "/.crosspoint/pokemon-battle-b.bin";
constexpr const char* LEGACY_STORE_PATH = "/.crosspoint/pokemon-battle.bin";

bool readExact(FsFile& file, void* output, const size_t size) {
  return file.read(output, size) == static_cast<int>(size);
}

bool writeExact(FsFile& file, const void* input, const size_t size) { return file.write(input, size) == size; }

// Wraparound-safe "is candidate newer than current", identical in spirit to
// PokemonStore's own sequenceIsNewer - sequence only ever increments by 1
// per successful write, so this only matters after ~2^31 writes to one file.
bool sequenceIsNewer(const uint32_t candidate, const uint32_t current) {
  return candidate != current && candidate - current < 0x80000000U;
}

// Reads and decodes one alternating-format slot. Returns false for a
// missing, oversized, short, or failed-validation file - all treated
// identically by the caller (this slot just isn't a candidate).
bool inspectSlot(const char* path, BattleStoreState& outputState, uint32_t& outputSequence) {
  if (!Storage.exists(path)) return false;
  FsFile file = Storage.open(path, O_RDONLY);
  if (!file) {
    LOG_ERR("PokemonBattleStore", "Failed to open %s", path);
    return false;
  }
  const uint64_t fileSize = file.fileSize64();
  if (fileSize > POKEMON_BATTLE_FILE_MAX_BYTES) {
    LOG_ERR("PokemonBattleStore", "Battle store slot larger than any valid layout, discarding %s", path);
    file.close();
    return false;
  }
  BattleStoreFileBytes bytes{};
  const bool readOk = readExact(file, bytes.data(), static_cast<size_t>(fileSize));
  file.close();
  if (!readOk) {
    LOG_ERR("PokemonBattleStore", "Short read on %s, discarding", path);
    return false;
  }
  return decodeBattleStoreFile(bytes.data(), static_cast<size_t>(fileSize), outputState, outputSequence);
}

// Reads and decodes the pre-double-buffering single-file format, for a
// one-time migration into the new alternating files.
bool inspectLegacySlot(BattleStoreState& outputState) {
  if (!Storage.exists(LEGACY_STORE_PATH)) return false;
  FsFile file = Storage.open(LEGACY_STORE_PATH, O_RDONLY);
  if (!file) {
    LOG_ERR("PokemonBattleStore", "Failed to open legacy %s", LEGACY_STORE_PATH);
    return false;
  }
  const uint64_t fileSize = file.fileSize64();
  if (fileSize > POKEMON_BATTLE_LEGACY_FILE_MAX_BYTES) {
    LOG_ERR("PokemonBattleStore", "Legacy battle store larger than any valid layout, discarding");
    file.close();
    return false;
  }
  BattleStoreLegacyFileBytes bytes{};
  const bool readOk = readExact(file, bytes.data(), static_cast<size_t>(fileSize));
  file.close();
  if (!readOk) {
    LOG_ERR("PokemonBattleStore", "Short read on legacy battle store, discarding");
    return false;
  }
  return decodeLegacyBattleStoreFile(bytes.data(), static_cast<size_t>(fileSize), outputState);
}

}  // namespace

void PokemonBattleStore::load() const {
  state_ = BattleStoreState{};
  ready_ = false;
  activeIsA_ = false;
  sequence_ = 0;
  loaded_ = true;  // even a fully-empty result leaves us in a valid, usable state

  BattleStoreState stateA{};
  BattleStoreState stateB{};
  uint32_t sequenceA = 0;
  uint32_t sequenceB = 0;
  const bool readyA = inspectSlot(STORE_PATH_A, stateA, sequenceA);
  const bool readyB = inspectSlot(STORE_PATH_B, stateB, sequenceB);
  if (readyA || readyB) {
    activeIsA_ = readyA && (!readyB || !sequenceIsNewer(sequenceB, sequenceA));
    state_ = activeIsA_ ? stateA : stateB;
    sequence_ = activeIsA_ ? sequenceA : sequenceB;
    ready_ = true;
    return;
  }

  // Neither alternating slot is valid yet - either a fresh install, or an
  // upgrade from before this file was double-buffered. Migrate the legacy
  // single file if it decodes; a fresh install has neither and stays empty.
  BattleStoreState legacyState{};
  if (!inspectLegacySlot(legacyState)) return;
  state_ = legacyState;
  // Write it out under the new format immediately rather than waiting for
  // the next upsert, so a crash right after migration doesn't strand the
  // player back on the legacy file with no alternating copy protecting it.
  // Leave the legacy file itself untouched, as a recovery copy, matching
  // PokemonStore's handling of its own legacy filename migration.
  if (writeState(state_)) return;
  // Migration write failed (e.g. read-only card): keep using the decoded
  // legacy state in memory for this session, but leave ready_ false so the
  // next successful write still starts a fresh sequence at slot A.
  LOG_ERR("PokemonBattleStore", "Failed to migrate legacy battle store to the double-buffered format");
}

const BattleRecordEntry* PokemonBattleStore::findEntry(const uint32_t recordId) const {
  if (!loaded_) load();
  return pokemon::findBattleEntry(state_, recordId);
}

bool PokemonBattleStore::writeState(const BattleStoreState& state) const {
  if (!Storage.ensureDirectoryExists(STORE_DIRECTORY)) {
    LOG_ERR("PokemonBattleStore", "Failed to prepare data directory");
    return false;
  }
  const uint32_t nextSequence = !ready_ ? 1U : (sequence_ == UINT32_MAX ? 1U : sequence_ + 1U);
  BattleStoreFileBytes bytes{};
  size_t size = 0;
  if (!encodeBattleStoreFile(state, nextSequence, bytes, size)) return false;

  const bool destinationIsA = !ready_ || !activeIsA_;
  const char* destinationPath = destinationIsA ? STORE_PATH_A : STORE_PATH_B;
  FsFile file = Storage.open(destinationPath, O_WRONLY | O_CREAT | O_TRUNC);
  if (!file) {
    LOG_ERR("PokemonBattleStore", "Failed to open %s for write", destinationPath);
    return false;
  }
  const bool writeOk = writeExact(file, bytes.data(), size) && file.sync();
  const bool closeOk = file.close();
  if (!writeOk || !closeOk) {
    LOG_ERR("PokemonBattleStore", "Failed to write %s", destinationPath);
    return false;
  }

  BattleStoreState verified{};
  uint32_t verifiedSequence = 0;
  if (!inspectSlot(destinationPath, verified, verifiedSequence) || verifiedSequence != nextSequence ||
      !(verified == state)) {
    LOG_ERR("PokemonBattleStore", "Inactive battle store slot verification failed");
    return false;
  }

  state_ = state;
  sequence_ = nextSequence;
  activeIsA_ = destinationIsA;
  ready_ = true;
  return true;
}

bool PokemonBattleStore::upsertEntry(const BattleRecordEntry& entry) {
  if (!loaded_) load();
  BattleStoreState candidate = state_;
  if (!pokemon::upsertBattleEntry(candidate, entry)) return false;
  return writeState(candidate);
}

bool PokemonBattleStore::removeEntry(const uint32_t recordId) {
  if (!loaded_) load();
  BattleStoreState candidate = state_;
  if (!pokemon::removeBattleEntry(candidate, recordId)) return false;
  return writeState(candidate);
}

PokemonBattleStore& devicePokemonBattleStore() {
  static PokemonBattleStore store;
  return store;
}

}  // namespace pokemon

#endif
