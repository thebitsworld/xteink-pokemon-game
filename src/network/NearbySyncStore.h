#pragma once

#include <HalStorage.h>
#include <NearbySyncCodec.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

// SD-card side of "Sync with nearby reader" (NearbySyncActivity): streams the
// outgoing container section by section, and on the receiving side verifies
// the whole received container before any section is applied. See
// NearbySyncCodec.h for the layout.
namespace nearby_sync {

constexpr const char* RECEIVE_PATH = "/.crosspoint/nearby-sync.part";
constexpr const char* SYNCED_STATS_DIR = "/.crosspoint/synced_stats";

// A section produced on the fly (the Pokemon save bundle can be ~100 KB, far
// more than should ever sit in RAM at once).
class SectionStream {
 public:
  virtual ~SectionStream() = default;
  // Back to the first byte. False if the source can no longer be read.
  virtual bool restart() = 0;
  // >0 bytes copied, 0 at the end, -1 on error.
  virtual int read(uint8_t* output, size_t capacity) = 0;
  // Size and CRC-32 when already known (a multi-megabyte book is checksummed
  // once up front instead of on every ContainerSource::open()). False means
  // "measure me by reading".
  virtual bool digest(uint32_t& size, uint32_t& crc) const {
    (void)size;
    (void)crc;
    return false;
  }
};

// A whole file as a section (the book EPUB).
class FileSectionStream final : public SectionStream {
 public:
  // Checksums the file. False if it cannot be read or is over `maxBytes`.
  bool open(const char* path, uint32_t maxBytes);
  bool restart() override;
  int read(uint8_t* output, size_t capacity) override;
  bool digest(uint32_t& size, uint32_t& crc) const override;
  uint32_t size() const { return size_; }

 private:
  std::string path_;
  HalFile file_;
  uint32_t size_ = 0;
  uint32_t crc_ = 0;
  bool measured_ = false;
};

class ContainerSource {
 public:
  // Small sections are copied in; `stream` is referenced, not owned.
  bool addMemorySection(SectionType type, const uint8_t* data, size_t size);
  bool addStreamSection(SectionType type, SectionStream& stream);
  // Measures and checksums every section and builds the table. Call after
  // all add*() calls, and again to rewind before (re)sending.
  bool open(const std::array<uint8_t, MAC_BYTES>& senderMac);
  uint32_t totalBytes() const { return static_cast<uint32_t>(header_.totalBytes()); }
  const ContainerHeader& header() const { return header_; }
  int read(uint8_t* output, size_t capacity);

 private:
  struct Pending {
    SectionType type = SectionType::ReadingStats;
    SectionStream* stream = nullptr;
    size_t memoryOffset = 0;
    size_t memorySize = 0;
  };
  bool rewindSection(size_t index);

  std::array<Pending, MAX_SECTIONS> pending_{};
  size_t pendingCount_ = 0;
  // Stats + book record fit here together.
  std::array<uint8_t, MAX_STATS_BYTES + MAX_BOOK_RECORD_BYTES> memory_{};
  size_t memoryUsed_ = 0;
  ContainerHeader header_{};
  std::array<uint8_t, MAX_TABLE_BYTES> table_{};
  size_t tableBytes_ = 0;
  size_t tablePosition_ = 0;
  size_t sectionIndex_ = 0;
  uint32_t sectionRemaining_ = 0;
  bool open_ = false;
};

enum class VerifyResult : uint8_t { Ok, Corrupt, UnsupportedVersion, ReadError };

// Checks RECEIVE_PATH: table CRC, total size and every section's CRC.
VerifyResult verifyReceived(ContainerHeader& header);
// Copies section `index` of RECEIVE_PATH to `destination` (overwritten).
bool extractSection(const ContainerHeader& header, size_t index, const char* destination);
// Reads a small section of RECEIVE_PATH into memory.
bool readSection(const ContainerHeader& header, size_t index, uint8_t* output, size_t capacity, size_t& length);
void discardReceived();
// Installs section `index` of RECEIVE_PATH as a new file at `destination`
// (via a hidden temp file next to it + rename). Refuses to overwrite an
// existing file.
bool installSectionAsFile(const ContainerHeader& header, size_t index, const std::string& destination);

// Writes `data` to `path` through a temp file (`tempPath`, default `path`.tmp)
// + rename, so a power cut leaves either the old file or the new one.
bool writeFileAtomic(const char* path, const uint8_t* data, size_t size, const char* tempPath = nullptr);

// /.crosspoint/synced_stats/device_<mac>.bin - the same file the existing
// Sync Stats screen writes, so the Reading Stats screen sums it the same way.
bool storePeerStats(const std::array<uint8_t, MAC_BYTES>& peerMac, const uint8_t* data, size_t size);

}  // namespace nearby_sync
