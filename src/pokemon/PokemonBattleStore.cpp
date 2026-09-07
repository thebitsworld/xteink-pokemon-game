#if defined(CROSSINK_ENABLE_POKEMON)

#include "PokemonBattleStore.h"

#include <HalStorage.h>
#include <Logging.h>

namespace pokemon {
namespace {

constexpr const char* BATTLE_STORE_PATH = "/.crosspoint/pokemon-battle.bin";
constexpr const char* STORE_DIRECTORY = "/.crosspoint";

bool readExact(FsFile& file, void* output, const size_t size) {
  return file.read(output, size) == static_cast<int>(size);
}

bool writeExact(FsFile& file, const void* input, const size_t size) { return file.write(input, size) == size; }

}  // namespace

void PokemonBattleStore::load() const {
  state_ = BattleStoreState{};
  loaded_ = true;  // even an absent/corrupt file leaves us in a valid, usable (empty) state

  if (!Storage.exists(BATTLE_STORE_PATH)) return;
  FsFile file = Storage.open(BATTLE_STORE_PATH, O_RDONLY);
  if (!file) {
    LOG_ERR("PokemonBattleStore", "Failed to open %s", BATTLE_STORE_PATH);
    return;
  }
  const uint64_t fileSize = file.fileSize64();
  if (fileSize > POKEMON_BATTLE_FILE_MAX_BYTES) {
    LOG_ERR("PokemonBattleStore", "Battle store larger than any valid layout, discarding");
    file.close();
    return;
  }
  std::array<uint8_t, POKEMON_BATTLE_FILE_MAX_BYTES> bytes{};
  const bool readOk = readExact(file, bytes.data(), static_cast<size_t>(fileSize));
  file.close();
  if (!readOk) {
    LOG_ERR("PokemonBattleStore", "Short read on %s, discarding", BATTLE_STORE_PATH);
    return;
  }

  BattleStoreState decoded{};
  if (!decodeBattleStoreFile(bytes.data(), static_cast<size_t>(fileSize), decoded)) {
    LOG_ERR("PokemonBattleStore", "Battle store failed validation, discarding");
    return;
  }
  state_ = decoded;
}

const BattleRecordEntry* PokemonBattleStore::findEntry(const uint32_t recordId) const {
  if (!loaded_) load();
  return pokemon::findBattleEntry(state_, recordId);
}

bool PokemonBattleStore::writeFile() const {
  if (!Storage.ensureDirectoryExists(STORE_DIRECTORY)) {
    LOG_ERR("PokemonBattleStore", "Failed to prepare data directory");
    return false;
  }
  BattleStoreFileBytes bytes{};
  size_t size = 0;
  if (!encodeBattleStoreFile(state_, bytes, size)) return false;

  FsFile file = Storage.open(BATTLE_STORE_PATH, O_WRONLY | O_CREAT | O_TRUNC);
  if (!file) {
    LOG_ERR("PokemonBattleStore", "Failed to open %s for write", BATTLE_STORE_PATH);
    return false;
  }
  const bool writeOk = writeExact(file, bytes.data(), size) && file.sync();
  const bool closeOk = file.close();
  if (!writeOk || !closeOk) {
    LOG_ERR("PokemonBattleStore", "Failed to write %s", BATTLE_STORE_PATH);
    return false;
  }
  return true;
}

bool PokemonBattleStore::upsertEntry(const BattleRecordEntry& entry) {
  if (!loaded_) load();
  const BattleStoreState previous = state_;
  if (!pokemon::upsertBattleEntry(state_, entry)) return false;
  if (!writeFile()) {
    state_ = previous;
    return false;
  }
  return true;
}

bool PokemonBattleStore::removeEntry(const uint32_t recordId) {
  if (!loaded_) load();
  const BattleStoreState previous = state_;
  if (!pokemon::removeBattleEntry(state_, recordId)) return false;
  if (!writeFile()) {
    state_ = previous;
    return false;
  }
  return true;
}

PokemonBattleStore& devicePokemonBattleStore() {
  static PokemonBattleStore store;
  return store;
}

}  // namespace pokemon

#endif
