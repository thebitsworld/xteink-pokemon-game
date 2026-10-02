#include "NearbySyncStore.h"

#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace nearby_sync {
namespace {

[[maybe_unused]] constexpr const char* LOG_TAG = "NSYNC-STORE";
constexpr size_t COPY_CHUNK_BYTES = 512;

// Shared copy buffer: one sync runs at a time, and 512 bytes are better off
// the small loop-task stack.
uint8_t* copyBuffer() {
  static uint8_t buffer[COPY_CHUNK_BYTES];
  return buffer;
}

bool removeIfPresent(const char* path) { return !Storage.exists(path) || Storage.remove(path); }

bool openReceived(const ContainerHeader& header, const size_t index, HalFile& file) {
  if (index >= header.sectionCount) return false;
  file = Storage.open(RECEIVE_PATH, O_RDONLY);
  return static_cast<bool>(file) && file.seek(static_cast<size_t>(header.sectionOffset(index)));
}

}  // namespace

bool ContainerSource::addMemorySection(const SectionType type, const uint8_t* data, const size_t size) {
  if (pendingCount_ >= MAX_SECTIONS || data == nullptr || size == 0 || memoryUsed_ + size > memory_.size()) {
    return false;
  }
  std::memcpy(memory_.data() + memoryUsed_, data, size);
  pending_[pendingCount_++] = Pending{type, nullptr, memoryUsed_, size};
  memoryUsed_ += size;
  return true;
}

bool ContainerSource::addStreamSection(const SectionType type, SectionStream& stream) {
  if (pendingCount_ >= MAX_SECTIONS) return false;
  pending_[pendingCount_++] = Pending{type, &stream, 0, 0};
  return true;
}

bool ContainerSource::rewindSection(const size_t index) {
  const Pending& section = pending_[index];
  sectionRemaining_ = header_.sections[index].size;
  return section.stream == nullptr || section.stream->restart();
}

bool ContainerSource::open(const std::array<uint8_t, MAC_BYTES>& senderMac) {
  open_ = false;
  header_ = ContainerHeader{};
  header_.senderMac = senderMac;
  header_.sectionCount = static_cast<uint8_t>(pendingCount_);
  for (size_t i = 0; i < pendingCount_; ++i) {
    Section& section = header_.sections[i];
    const Pending& source = pending_[i];
    section.type = source.type;
    if (source.stream == nullptr) {
      section.size = static_cast<uint32_t>(source.memorySize);
      section.crc32 = crc32Of(memory_.data() + source.memoryOffset, source.memorySize);
      continue;
    }
    if (!source.stream->restart()) return false;
    uint32_t crc = CRC_INITIAL;
    uint64_t size = 0;
    for (;;) {
      const int count = source.stream->read(copyBuffer(), COPY_CHUNK_BYTES);
      if (count < 0) return false;
      if (count == 0) break;
      crc = crc32Update(crc, copyBuffer(), static_cast<size_t>(count));
      size += static_cast<size_t>(count);
      if (size > MAX_SECTION_BYTES) return false;
    }
    section.size = static_cast<uint32_t>(size);
    section.crc32 = crc ^ CRC_INITIAL;
  }
  if (!encodeContainerTable(header_, table_.data(), table_.size(), tableBytes_)) return false;
  tablePosition_ = 0;
  sectionIndex_ = 0;
  if (pendingCount_ > 0 && !rewindSection(0)) return false;
  open_ = true;
  return true;
}

int ContainerSource::read(uint8_t* output, const size_t capacity) {
  if (!open_ || output == nullptr || capacity == 0) return -1;
  if (tablePosition_ < tableBytes_) {
    const size_t count = std::min(capacity, tableBytes_ - tablePosition_);
    std::memcpy(output, table_.data() + tablePosition_, count);
    tablePosition_ += count;
    return static_cast<int>(count);
  }
  while (sectionRemaining_ == 0) {
    if (++sectionIndex_ >= pendingCount_) return 0;
    if (!rewindSection(sectionIndex_)) return -1;
  }
  const Pending& section = pending_[sectionIndex_];
  size_t count = std::min<size_t>(capacity, sectionRemaining_);
  if (section.stream == nullptr) {
    const size_t done = header_.sections[sectionIndex_].size - sectionRemaining_;
    std::memcpy(output, memory_.data() + section.memoryOffset + done, count);
  } else {
    // A stream may hand back fewer bytes than asked; ending early means it
    // changed size since open(), so refuse to send a mismatching section.
    const int read = section.stream->read(output, count);
    if (read <= 0) return -1;
    count = static_cast<size_t>(read);
  }
  sectionRemaining_ -= static_cast<uint32_t>(count);
  return static_cast<int>(count);
}

VerifyResult verifyReceived(ContainerHeader& header) {
  HalFile file = Storage.open(RECEIVE_PATH, O_RDONLY);
  if (!file) return VerifyResult::ReadError;
  std::array<uint8_t, MAX_TABLE_BYTES> table{};
  const uint64_t fileSize = file.fileSize64();
  const size_t want = static_cast<size_t>(std::min<uint64_t>(fileSize, table.size()));
  if (file.read(table.data(), want) != static_cast<int>(want)) {
    file.close();
    return VerifyResult::ReadError;
  }
  const ParseResult parse = decodeContainerTable(table.data(), want, header);
  if (parse != ParseResult::Ok) {
    file.close();
    return parse == ParseResult::UnsupportedVersion ? VerifyResult::UnsupportedVersion : VerifyResult::Corrupt;
  }
  if (fileSize != header.totalBytes() || !file.seek(header.tableBytes())) {
    file.close();
    return VerifyResult::Corrupt;
  }
  for (size_t i = 0; i < header.sectionCount; ++i) {
    uint32_t crc = CRC_INITIAL;
    uint32_t remaining = header.sections[i].size;
    while (remaining > 0) {
      const size_t chunk = std::min<size_t>(remaining, COPY_CHUNK_BYTES);
      if (file.read(copyBuffer(), chunk) != static_cast<int>(chunk)) {
        file.close();
        return VerifyResult::Corrupt;
      }
      crc = crc32Update(crc, copyBuffer(), chunk);
      remaining -= static_cast<uint32_t>(chunk);
    }
    if ((crc ^ CRC_INITIAL) != header.sections[i].crc32) {
      file.close();
      return VerifyResult::Corrupt;
    }
  }
  file.close();
  return VerifyResult::Ok;
}

bool extractSection(const ContainerHeader& header, const size_t index, const char* destination) {
  HalFile in;
  if (!openReceived(header, index, in)) return false;
  removeIfPresent(destination);
  HalFile out = Storage.open(destination, O_WRONLY | O_CREAT | O_TRUNC);
  if (!out) {
    in.close();
    return false;
  }
  uint32_t remaining = header.sections[index].size;
  bool ok = true;
  while (ok && remaining > 0) {
    const size_t chunk = std::min<size_t>(remaining, COPY_CHUNK_BYTES);
    ok = in.read(copyBuffer(), chunk) == static_cast<int>(chunk) && out.write(copyBuffer(), chunk) == chunk;
    remaining -= static_cast<uint32_t>(chunk);
  }
  in.close();
  ok = ok && out.sync();
  ok = out.close() && ok;
  if (!ok) {
    LOG_ERR(LOG_TAG, "Failed to extract section to %s", destination);
    removeIfPresent(destination);
  }
  return ok;
}

bool readSection(const ContainerHeader& header, const size_t index, uint8_t* output, const size_t capacity,
                 size_t& length) {
  length = 0;
  if (index >= header.sectionCount || output == nullptr || header.sections[index].size > capacity) return false;
  HalFile in;
  if (!openReceived(header, index, in)) return false;
  const size_t size = header.sections[index].size;
  const bool ok = in.read(output, size) == static_cast<int>(size);
  in.close();
  if (ok) length = size;
  return ok;
}

void discardReceived() { removeIfPresent(RECEIVE_PATH); }

bool writeFileAtomic(const char* path, const uint8_t* data, const size_t size, const char* tempPath) {
  const std::string temp = tempPath != nullptr ? std::string(tempPath) : std::string(path) + ".tmp";
  removeIfPresent(temp.c_str());
  HalFile out = Storage.open(temp.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
  if (!out) return false;
  bool ok = out.write(data, size) == size && out.sync();
  ok = out.close() && ok;
  if (!ok || !removeIfPresent(path) || !Storage.rename(temp.c_str(), path)) {
    removeIfPresent(temp.c_str());
    LOG_ERR(LOG_TAG, "Failed to write %s", path);
    return false;
  }
  return true;
}

bool storePeerStats(const std::array<uint8_t, MAC_BYTES>& peerMac, const uint8_t* data, const size_t size) {
  if (!isValidStatsPayload(data, size) || peerMac == std::array<uint8_t, MAC_BYTES>{}) return false;
  if (!Storage.ensureDirectoryExists("/.crosspoint") || !Storage.ensureDirectoryExists(SYNCED_STATS_DIR)) return false;
  char path[64];
  snprintf(path, sizeof(path), "%s/device_%02x%02x%02x%02x%02x%02x.bin", SYNCED_STATS_DIR, peerMac[0], peerMac[1],
           peerMac[2], peerMac[3], peerMac[4], peerMac[5]);
  // The temp file stays outside synced_stats/: the Reading Stats screen sums
  // every valid file in that folder, so a stray complete temp copy there would
  // count the same reader twice.
  return writeFileAtomic(path, data, size, "/.crosspoint/nearby-sync-stats.tmp");
}

}  // namespace nearby_sync
