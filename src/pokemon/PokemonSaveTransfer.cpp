#if defined(CROSSINK_ENABLE_POKEMON)

#include "PokemonSaveTransfer.h"

#include <Logging.h>
#include <PokemonCrc32.h>

#include <algorithm>
#include <string>

namespace pokemon::save_transfer {
namespace {

[[maybe_unused]] constexpr const char* LOG_TAG = "PokemonSaveTransfer";
constexpr const char* SAVE_DIRECTORY = "/.crosspoint";
constexpr uint32_t CRC_INITIAL = 0xFFFFFFFFU;
constexpr size_t COPY_CHUNK_BYTES = 512;

std::string livePath(const uint8_t fileId) { return std::string(SAVE_DIRECTORY) + "/" + saveBundleFileName(fileId); }

std::string backupPath(const uint8_t fileId) {
  return std::string(BACKUP_DIRECTORY) + "/" + saveBundleFileName(fileId);
}

bool removeIfPresent(const std::string& path) { return !Storage.exists(path.c_str()) || Storage.remove(path.c_str()); }

// rename() refuses to overwrite, so the destination is cleared first.
bool replaceWith(const std::string& from, const std::string& to) {
  return removeIfPresent(to) && Storage.rename(from.c_str(), to.c_str());
}

// Shared copy buffer: transfers run one at a time, and keeping 512 bytes off
// the (small) loop-task stack matters more than reentrancy here.
uint8_t* copyBuffer() {
  static uint8_t buffer[COPY_CHUNK_BYTES];
  return buffer;
}

bool fileCrc(const std::string& path, uint32_t& size, uint32_t& crc) {
  HalFile file = Storage.open(path.c_str(), O_RDONLY);
  if (!file) return false;
  const uint64_t fileSize = file.fileSize64();
  if (fileSize == 0 || fileSize > SAVE_BUNDLE_MAX_FILE_BYTES) {
    file.close();
    return false;
  }
  uint32_t running = CRC_INITIAL;
  uint64_t remaining = fileSize;
  while (remaining > 0) {
    const size_t want = static_cast<size_t>(std::min<uint64_t>(remaining, COPY_CHUNK_BYTES));
    if (file.read(copyBuffer(), want) != static_cast<int>(want)) {
      file.close();
      return false;
    }
    running = updatePokemonCrc32(running, copyBuffer(), want);
    remaining -= want;
  }
  file.close();
  size = static_cast<uint32_t>(fileSize);
  crc = running ^ CRC_INITIAL;
  return true;
}

// Reads exactly `size` bytes of `in` into `out` (or just checks them when
// `out` is null), verifying their CRC on the way.
bool copyChecked(HalFile& in, HalFile* out, const uint32_t size, const uint32_t expectedCrc) {
  uint32_t running = CRC_INITIAL;
  uint32_t remaining = size;
  while (remaining > 0) {
    const size_t want = std::min<size_t>(remaining, COPY_CHUNK_BYTES);
    if (in.read(copyBuffer(), want) != static_cast<int>(want)) return false;
    running = updatePokemonCrc32(running, copyBuffer(), want);
    if (out != nullptr && out->write(copyBuffer(), want) != want) return false;
    remaining -= static_cast<uint32_t>(want);
  }
  return (running ^ CRC_INITIAL) == expectedCrc;
}

bool readTable(HalFile& file, SaveBundleHeader& header, SaveBundleParse& parse) {
  std::array<uint8_t, SAVE_BUNDLE_MAX_TABLE_BYTES> table{};
  const uint64_t fileSize = file.fileSize64();
  const size_t want = static_cast<size_t>(std::min<uint64_t>(fileSize, table.size()));
  if (file.read(table.data(), want) != static_cast<int>(want)) return false;
  parse = decodeSaveBundleTable(table.data(), want, header);
  return parse != SaveBundleParse::Ok || file.seek(header.tableBytes());
}

bool clearBackup() {
  Storage.ensureDirectoryExists(BACKUP_DIRECTORY);
  bool ok = true;
  for (uint8_t id = 0; id < SAVE_BUNDLE_FILE_COUNT; ++id) ok = removeIfPresent(backupPath(id)) && ok;
  return ok;
}

// Moves every live save file into the backup folder. Idempotent: a file
// already moved by an interrupted earlier run is simply no longer live.
bool moveLiveFilesToBackup() {
  Storage.ensureDirectoryExists(BACKUP_DIRECTORY);
  for (uint8_t id = 0; id < SAVE_BUNDLE_FILE_COUNT; ++id) {
    const std::string live = livePath(id);
    if (!Storage.exists(live.c_str())) continue;
    if (!replaceWith(live, backupPath(id))) {
      LOG_ERR(LOG_TAG, "Failed to back up %s", live.c_str());
      return false;
    }
  }
  return true;
}

// Puts the backup back in place - only used if the installing journal itself
// turns out to be unreadable, so the device is never left without a save.
void restoreBackup() {
  for (uint8_t id = 0; id < SAVE_BUNDLE_FILE_COUNT; ++id) {
    const std::string backup = backupPath(id);
    const std::string live = livePath(id);
    if (Storage.exists(backup.c_str())) {
      replaceWith(backup, live);
    } else {
      removeIfPresent(live);
    }
  }
}

bool installFromJournal() {
  HalFile journal = Storage.open(INSTALLING_PATH, O_RDONLY);
  if (!journal) {
    LOG_ERR(LOG_TAG, "Failed to open install journal");
    return false;  // transient: retried on the next boot
  }
  SaveBundleHeader header;
  SaveBundleParse parse = SaveBundleParse::Invalid;
  if (!readTable(journal, header, parse)) {
    journal.close();
    return false;
  }
  if (parse != SaveBundleParse::Ok) {
    // Verified before commit, so this is SD corruption: keep the old save.
    journal.close();
    LOG_ERR(LOG_TAG, "Install journal unreadable, restoring previous save");
    restoreBackup();
    Storage.remove(INSTALLING_PATH);
    return false;
  }
  // A live file the bundle does not carry (e.g. a slot the sender never
  // wrote) would otherwise survive from an interrupted earlier attempt.
  uint32_t bundled = 0;
  for (size_t i = 0; i < header.entryCount; ++i) bundled |= 1U << header.entries[i].fileId;
  for (uint8_t id = 0; id < SAVE_BUNDLE_FILE_COUNT; ++id) {
    if ((bundled & (1U << id)) == 0 && !removeIfPresent(livePath(id))) {
      journal.close();
      return false;
    }
  }

  for (size_t i = 0; i < header.entryCount; ++i) {
    const SaveBundleEntry& entry = header.entries[i];
    const std::string live = livePath(entry.fileId);
    const std::string temp = live + ".tmp";
    removeIfPresent(temp);
    HalFile out = Storage.open(temp.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
    if (!out) {
      journal.close();
      LOG_ERR(LOG_TAG, "Failed to create %s", temp.c_str());
      return false;
    }
    const bool copied = copyChecked(journal, &out, entry.size, entry.crc32) && out.sync();
    const bool closed = out.close();
    if (!copied || !closed || !replaceWith(temp, live)) {
      journal.close();
      removeIfPresent(temp);
      LOG_ERR(LOG_TAG, "Failed to install %s", live.c_str());
      return false;
    }
  }
  journal.close();
  if (!Storage.remove(INSTALLING_PATH)) {
    LOG_ERR(LOG_TAG, "Failed to remove install journal");
    return false;
  }
  LOG_INF(LOG_TAG, "Installed transferred save (%u files)", static_cast<unsigned>(header.entryCount));
  return true;
}

}  // namespace

bool hasLocalSave() {
  return Storage.exists(livePath(static_cast<uint8_t>(SaveBundleFile::MainA)).c_str()) ||
         Storage.exists(livePath(static_cast<uint8_t>(SaveBundleFile::MainB)).c_str());
}

bool BundleSource::open() {
  close();
  header_ = SaveBundleHeader{};
  header_.versions = currentSaveFormatVersions();
  for (uint8_t id = 0; id < SAVE_BUNDLE_FILE_COUNT; ++id) {
    const std::string path = livePath(id);
    if (!Storage.exists(path.c_str())) continue;
    SaveBundleEntry entry;
    entry.fileId = id;
    if (!fileCrc(path, entry.size, entry.crc32)) {
      LOG_ERR(LOG_TAG, "Failed to read %s", path.c_str());
      return false;
    }
    header_.entries[header_.entryCount++] = entry;
  }
  if (!encodeSaveBundleTable(header_, table_.data(), table_.size(), tableBytes_)) return false;
  tablePosition_ = 0;
  entryIndex_ = 0;
  entryRemaining_ = 0;
  open_ = true;
  return true;
}

void BundleSource::close() {
  file_.close();
  open_ = false;
}

int BundleSource::read(uint8_t* output, const size_t capacity) {
  if (!open_ || output == nullptr || capacity == 0) return -1;
  if (tablePosition_ < tableBytes_) {
    const size_t count = std::min(capacity, tableBytes_ - tablePosition_);
    std::copy_n(table_.data() + tablePosition_, count, output);
    tablePosition_ += count;
    return static_cast<int>(count);
  }
  while (entryRemaining_ == 0) {
    file_.close();
    if (entryIndex_ >= header_.entryCount) return 0;
    const SaveBundleEntry& entry = header_.entries[entryIndex_++];
    file_ = Storage.open(livePath(entry.fileId).c_str(), O_RDONLY);
    if (!file_ || file_.fileSize64() != entry.size) return -1;
    entryRemaining_ = entry.size;
  }
  const size_t want = std::min<size_t>(capacity, entryRemaining_);
  if (file_.read(output, want) != static_cast<int>(want)) return -1;
  entryRemaining_ -= static_cast<uint32_t>(want);
  return static_cast<int>(want);
}

VerifyResult verifyStagedBundle(SaveBundleHeader& header) {
  HalFile file = Storage.open(STAGING_PATH, O_RDONLY);
  if (!file) return VerifyResult::ReadError;
  SaveBundleParse parse = SaveBundleParse::Invalid;
  if (!readTable(file, header, parse)) {
    file.close();
    return VerifyResult::ReadError;
  }
  if (parse != SaveBundleParse::Ok) {
    file.close();
    return parse == SaveBundleParse::UnsupportedVersion ? VerifyResult::UnsupportedVersion : VerifyResult::Corrupt;
  }
  if (!saveFormatCompatible(header.versions, currentSaveFormatVersions())) {
    file.close();
    return VerifyResult::UnsupportedVersion;
  }
  if (file.fileSize64() != header.totalBytes()) {
    file.close();
    return VerifyResult::Corrupt;
  }
  for (size_t i = 0; i < header.entryCount; ++i) {
    if (!copyChecked(file, nullptr, header.entries[i].size, header.entries[i].crc32)) {
      file.close();
      return VerifyResult::Corrupt;
    }
  }
  file.close();
  return VerifyResult::Ok;
}

bool commitStagedBundle() {
  // Clearing the old backup must happen before the commit point: once
  // READY exists, step 4a may already be filling the backup folder.
  // An unfinished install means the live files are a mix of two saves;
  // that has to complete (applyPendingSaveTransfer()) before a new one.
  if (Storage.exists(INSTALLING_PATH) || !clearBackup()) return false;
  return replaceWith(STAGING_PATH, READY_PATH);
}

void discardStagedBundle() { removeIfPresent(STAGING_PATH); }

bool applyPendingSaveTransfer() {
  if (Storage.exists(READY_PATH)) {
    if (!moveLiveFilesToBackup()) return false;
    if (!Storage.rename(READY_PATH, INSTALLING_PATH)) {
      LOG_ERR(LOG_TAG, "Failed to start install journal");
      return false;
    }
  }
  if (!Storage.exists(INSTALLING_PATH)) return true;
  return installFromJournal();
}

bool moveLocalSaveToBackup() { return clearBackup() && moveLiveFilesToBackup(); }

}  // namespace pokemon::save_transfer

#endif
