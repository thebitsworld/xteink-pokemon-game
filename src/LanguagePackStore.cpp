#include "LanguagePackStore.h"

#include <Epub/hyphenation/LanguageRegistry.h>
#include <HalStorage.h>
#include <LanguagePack.h>
#include <Logging.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <new>

#ifndef SIMULATOR
#include <esp_partition.h>
#endif

namespace language_pack_store {
namespace {

constexpr const char* TAG = "LANGPACK";
constexpr size_t COPY_CHUNK_BYTES = 4096;
// What the boot did with the pack, for readers without a serial port (X3).
constexpr const char* LOG_PATH = "/.crosspoint/language-pack-log.txt";
constexpr size_t LOG_MAX_BYTES = 16 * 1024;

void packLog(const char* format, ...) {
  char line[160];
  va_list args;
  va_start(args, format);
  const int len = std::vsnprintf(line, sizeof(line) - 1, format, args);
  va_end(args);
  if (len <= 0) return;
  const size_t length = std::min(static_cast<size_t>(len), sizeof(line) - 2);
  LOG_INF(TAG, "%s", line);
  line[length] = '\n';
  HalFile file = Storage.open(LOG_PATH, O_WRONLY | O_CREAT | O_APPEND);
  if (!file) return;
  if (file.size() < LOG_MAX_BYTES) file.write(line, length + 1);
  file.close();
}

bool readHeaderFromFile(const char* path, language_pack::Header& header) {
  HalFile file;
  if (!Storage.exists(path) || !Storage.openFileForRead(TAG, path, file) || !file) return false;
  uint8_t bytes[language_pack::HEADER_BYTES];
  const bool read = file.read(bytes, sizeof(bytes)) == static_cast<int>(sizeof(bytes));
  const uint64_t size = file.fileSize64();
  file.close();
  return read && language_pack::parseHeader(bytes, sizeof(bytes), header) && header.totalSize == size;
}

// Hands the attached pack's patterns to the hyphenation registry (or clears them).
void applyHyphenation() {
  clearPackedHyphenationPatterns();
  if (!language_pack::attached()) return;
  size_t count = 0;
  const char* const* tags = packedHyphenationTags(count);
  for (size_t i = 0; i < count; ++i) {
    language_pack::Hyphenation patterns{};
    if (language_pack::findHyphenation(tags[i], patterns)) {
      setPackedHyphenationPatterns(tags[i], patterns.data, patterns.size, patterns.rootOffset);
    }
  }
}

void detach() {
  language_pack::attach(nullptr, 0);
  clearPackedHyphenationPatterns();
}

#ifdef SIMULATOR

std::unique_ptr<uint8_t[]> gPackBytes;

// The simulator has no partition: the SD copy is the installed pack.
bool mount() {
  detach();
  gPackBytes.reset();
  language_pack::Header header{};
  if (!readHeaderFromFile(PACK_PATH, header)) return false;
  std::unique_ptr<uint8_t[]> bytes(new (std::nothrow) uint8_t[header.totalSize]);
  HalFile file;
  if (!bytes || !Storage.openFileForRead(TAG, PACK_PATH, file) || !file) return false;
  const bool read = file.read(bytes.get(), header.totalSize) == static_cast<int>(header.totalSize);
  file.close();
  if (!read || !language_pack::crcMatches(bytes.get(), header) ||
      !language_pack::attach(bytes.get(), header.totalSize)) {
    LOG_ERR(TAG, "Invalid language pack on the SD card");
    return false;
  }
  gPackBytes = std::move(bytes);
  applyHyphenation();
  return true;
}

bool installIfNewer(const std::function<void(int)>&) { return true; }

#else

const esp_partition_t* gPartition = nullptr;
esp_partition_mmap_handle_t gMapHandle = 0;
bool gMapped = false;

const esp_partition_t* partition() {
  if (gPartition != nullptr) return gPartition;
  gPartition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "spiffs");
  // A reader first flashed with another firmware may name it differently.
  if (gPartition == nullptr) {
    gPartition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
  }
  if (gPartition != nullptr) {
    packLog("partition %s at 0x%06x, %u KB", gPartition->label, static_cast<unsigned>(gPartition->address),
            static_cast<unsigned>(gPartition->size / 1024));
    return gPartition;
  }
  packLog("no spiffs partition; data partitions:");
  esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, nullptr);
  for (; it != nullptr; it = esp_partition_next(it)) {
    const esp_partition_t* part = esp_partition_get(it);
    packLog("  %s subtype 0x%02x at 0x%06x, %u KB", part->label, static_cast<unsigned>(part->subtype),
            static_cast<unsigned>(part->address), static_cast<unsigned>(part->size / 1024));
  }
  esp_partition_iterator_release(it);
  return nullptr;
}

void unmap() {
  detach();
  if (gMapped) esp_partition_munmap(gMapHandle);
  gMapped = false;
}

bool readInstalledHeader(language_pack::Header& header) {
  const esp_partition_t* part = partition();
  uint8_t bytes[language_pack::HEADER_BYTES];
  return part != nullptr && esp_partition_read(part, 0, bytes, sizeof(bytes)) == ESP_OK &&
         language_pack::parseHeader(bytes, sizeof(bytes), header) && header.totalSize <= part->size;
}

bool mount() {
  unmap();
  language_pack::Header header{};
  if (!readInstalledHeader(header)) return false;
  const void* mapped = nullptr;
  const esp_err_t mapError =
      esp_partition_mmap(partition(), 0, header.totalSize, ESP_PARTITION_MMAP_DATA, &mapped, &gMapHandle);
  if (mapError != ESP_OK) {
    packLog("map failed (%u bytes, err 0x%x)", static_cast<unsigned>(header.totalSize), mapError);
    return false;
  }
  gMapped = true;
  if (!language_pack::attach(static_cast<const uint8_t*>(mapped), header.totalSize)) {
    packLog("installed pack is damaged");
    unmap();
    return false;
  }
  applyHyphenation();
  return true;
}

// Copies the SD pack into the partition: body first, header last, so a power
// cut leaves no valid header over a half-written pack.
bool copyToPartition(const language_pack::Header& header, const std::function<void(int)>& progress) {
  const esp_partition_t* part = partition();
  if (part == nullptr || header.totalSize > part->size) {
    packLog("no partition for a %u byte pack", static_cast<unsigned>(header.totalSize));
    return false;
  }
  std::unique_ptr<uint8_t[]> buffer(new (std::nothrow) uint8_t[COPY_CHUNK_BYTES]);
  HalFile file;
  if (!buffer || !Storage.openFileForRead(TAG, PACK_PATH, file) || !file) return false;
  uint8_t headerBytes[language_pack::HEADER_BYTES];
  bool ok = file.read(headerBytes, sizeof(headerBytes)) == static_cast<int>(sizeof(headerBytes));

  const size_t eraseSize = (header.totalSize + part->erase_size - 1) / part->erase_size * part->erase_size;
  if (ok) {
    const esp_err_t eraseError = esp_partition_erase_range(part, 0, eraseSize);
    if (eraseError != ESP_OK) packLog("erase failed (err 0x%x)", eraseError);
    ok = eraseError == ESP_OK;
  }
  uint32_t crc = 0;
  size_t offset = language_pack::HEADER_BYTES;
  while (ok && offset < header.totalSize) {
    const size_t chunk = std::min(COPY_CHUNK_BYTES, static_cast<size_t>(header.totalSize - offset));
    ok = file.read(buffer.get(), chunk) == static_cast<int>(chunk) &&
         esp_partition_write(part, offset, buffer.get(), chunk) == ESP_OK;
    crc = language_pack::crc32Update(crc, buffer.get(), chunk);
    offset += chunk;
    if (progress) progress(static_cast<int>(offset * 90 / header.totalSize));
  }
  file.close();
  if (!ok || crc != header.crc32) {
    packLog("copy failed at %u (crc %s)", static_cast<unsigned>(offset), crc == header.crc32 ? "ok" : "mismatch");
    return false;
  }
  // Read the flash back before the header makes it valid.
  crc = 0;
  for (offset = language_pack::HEADER_BYTES; ok && offset < header.totalSize;) {
    const size_t chunk = std::min(COPY_CHUNK_BYTES, static_cast<size_t>(header.totalSize - offset));
    ok = esp_partition_read(part, offset, buffer.get(), chunk) == ESP_OK;
    crc = language_pack::crc32Update(crc, buffer.get(), chunk);
    offset += chunk;
  }
  if (!ok || crc != header.crc32) {
    packLog("read-back failed");
    return false;
  }
  ok = esp_partition_write(part, 0, headerBytes, sizeof(headerBytes)) == ESP_OK;
  if (progress) progress(100);
  return ok;
}

bool installIfNewer(const std::function<void(int)>& progress) {
  language_pack::Header sdHeader{};
  if (!readHeaderFromFile(PACK_PATH, sdHeader)) {
    if (Storage.exists(PACK_PATH)) packLog("%s is not a valid pack", PACK_PATH);
    return true;  // nothing to install
  }
  language_pack::Header installedHeader{};
  if (readInstalledHeader(installedHeader) && installedHeader.crc32 == sdHeader.crc32 &&
      installedHeader.totalSize == sdHeader.totalSize) {
    return true;  // already installed
  }
  packLog("installing %u bytes (crc %08x)", static_cast<unsigned>(sdHeader.totalSize),
          static_cast<unsigned>(sdHeader.crc32));
  unmap();
  const uint32_t startMs = millis();
  const bool ok = copyToPartition(sdHeader, progress);
  if (!ok && partition() != nullptr) {
    // Never leave a header over what may be a partial copy.
    esp_partition_erase_range(partition(), 0, partition()->erase_size);
  }
  packLog("install %s in %lu ms", ok ? "done" : "FAILED", static_cast<unsigned long>(millis() - startMs));
  return ok;
}

#endif

}  // namespace

void begin() {
  if (Storage.exists(USER_PACK_PATH)) {
    if (fileIsValidPack(USER_PACK_PATH)) {
      Storage.ensureDirectoryExists("/.crosspoint");
      Storage.remove(PACK_PATH);
      packLog(Storage.rename(USER_PACK_PATH, PACK_PATH) ? "moved %s into /.crosspoint" : "could not move %s",
              USER_PACK_PATH);
    } else {
      packLog("%s is not a valid language pack", USER_PACK_PATH);
    }
  }
  installIfNewer(nullptr);
  if (mount()) {
    LOG_INF(TAG, "Language pack: %u languages", static_cast<unsigned>(language_pack::header().languageCount));
  } else if (Storage.exists(PACK_PATH)) {
    packLog("no pack mounted although %s exists", PACK_PATH);
  }
}

bool fileIsValidPack(const char* path) {
  language_pack::Header header{};
  if (!readHeaderFromFile(path, header)) return false;
  std::unique_ptr<uint8_t[]> buffer(new (std::nothrow) uint8_t[COPY_CHUNK_BYTES]);
  HalFile file;
  if (!buffer || !Storage.openFileForRead(TAG, path, file) || !file) return false;
  bool ok = file.seekSet(language_pack::HEADER_BYTES);
  uint32_t crc = 0;
  for (size_t offset = language_pack::HEADER_BYTES; ok && offset < header.totalSize;) {
    const size_t chunk = std::min(COPY_CHUNK_BYTES, static_cast<size_t>(header.totalSize - offset));
    ok = file.read(buffer.get(), chunk) == static_cast<int>(chunk);
    crc = language_pack::crc32Update(crc, buffer.get(), chunk);
    offset += chunk;
  }
  file.close();
  return ok && crc == header.crc32;
}

bool installed() { return language_pack::attached(); }

size_t languageCount() { return language_pack::attached() ? language_pack::header().languageCount : 0; }

}  // namespace language_pack_store
