#include <HalStorage.h>

#include <cstdio>
#include <string>
#include <vector>

#include "NearbySyncCodec.h"
#include "network/NearbySyncStore.h"

namespace {

int failures = 0;

#define CHECK(condition)                                                                \
  do {                                                                                  \
    if (!(condition)) {                                                                 \
      std::fprintf(stderr, "%s:%d check failed: %s\n", __FILE__, __LINE__, #condition); \
      ++failures;                                                                       \
    }                                                                                   \
  } while (false)

namespace ns = nearby_sync;
using Bytes = std::vector<uint8_t>;

Bytes pattern(const size_t size, const uint8_t seed) {
  Bytes bytes(size);
  for (size_t i = 0; i < size; ++i) bytes[i] = static_cast<uint8_t>(seed + i * 13);
  return bytes;
}

void put(const char* path, const Bytes& bytes) {
  HalFile file = Storage.open(path, O_WRONLY | O_CREAT | O_TRUNC);
  file.write(bytes.data(), bytes.size());
  file.close();
}

Bytes get(const char* path) {
  HalFile file = Storage.open(path, O_RDONLY);
  if (!file) return {};
  Bytes bytes(static_cast<size_t>(file.fileSize64()));
  file.read(bytes.data(), bytes.size());
  file.close();
  return bytes;
}

// A large section served in odd-sized pieces, like the Pokemon bundle.
class VectorStream final : public ns::SectionStream {
 public:
  explicit VectorStream(Bytes bytes) : bytes_(std::move(bytes)) {}
  bool restart() override {
    position_ = 0;
    ++restarts;
    return true;
  }
  int read(uint8_t* output, const size_t capacity) override {
    const size_t count = std::min<size_t>({capacity, bytes_.size() - position_, 300});
    std::copy_n(bytes_.data() + position_, count, output);
    position_ += count;
    return static_cast<int>(count);
  }
  int restarts = 0;

 private:
  Bytes bytes_;
  size_t position_ = 0;
};

Bytes statsV3() {
  Bytes stats = pattern(159, 5);
  stats[0] = 3;
  return stats;
}

Bytes stream(ns::ContainerSource& source) {
  Bytes out;
  uint8_t chunk[1024];
  for (;;) {
    const int count = source.read(chunk, sizeof(chunk));
    CHECK(count >= 0);
    if (count <= 0) break;
    out.insert(out.end(), chunk, chunk + count);
  }
  return out;
}

void containerRoundTripsThroughStorage() {
  Storage.clear();
  const Bytes stats = statsV3();
  const Bytes book = pattern(37, 1);
  VectorStream pokemon(pattern(50000, 9));

  ns::ContainerSource source;
  CHECK(source.addMemorySection(ns::SectionType::ReadingStats, stats.data(), stats.size()));
  CHECK(source.addMemorySection(ns::SectionType::BookPosition, book.data(), book.size()));
  CHECK(source.addStreamSection(ns::SectionType::PokemonSave, pokemon));
  CHECK(source.open({1, 2, 3, 4, 5, 6}));
  const Bytes container = stream(source);
  CHECK(container.size() == source.totalBytes());

  // Re-opening rewinds everything and yields identical bytes (a resend).
  CHECK(source.open({1, 2, 3, 4, 5, 6}));
  CHECK(stream(source) == container);

  put(ns::RECEIVE_PATH, container);
  ns::ContainerHeader header;
  CHECK(ns::verifyReceived(header) == ns::VerifyResult::Ok);
  CHECK((header.senderMac == std::array<uint8_t, 6>{1, 2, 3, 4, 5, 6}));

  uint8_t small[256];
  size_t length = 0;
  CHECK(ns::readSection(header, static_cast<size_t>(header.find(ns::SectionType::ReadingStats)), small, sizeof(small),
                        length));
  CHECK(Bytes(small, small + length) == stats);
  CHECK(ns::readSection(header, static_cast<size_t>(header.find(ns::SectionType::BookPosition)), small, sizeof(small),
                        length));
  CHECK(Bytes(small, small + length) == book);
  // Too big for the buffer: refused rather than truncated.
  CHECK(!ns::readSection(header, static_cast<size_t>(header.find(ns::SectionType::PokemonSave)), small, sizeof(small),
                         length));
  CHECK(ns::extractSection(header, static_cast<size_t>(header.find(ns::SectionType::PokemonSave)), "/out.bin"));
  CHECK(get("/out.bin") == pattern(50000, 9));
}

void corruptOrTruncatedContainersAreRejected() {
  Storage.clear();
  const Bytes stats = statsV3();
  VectorStream pokemon(pattern(3000, 4));
  ns::ContainerSource source;
  CHECK(source.addMemorySection(ns::SectionType::ReadingStats, stats.data(), stats.size()));
  CHECK(source.addStreamSection(ns::SectionType::PokemonSave, pokemon));
  CHECK(source.open({1, 1, 1, 1, 1, 1}));
  const Bytes container = stream(source);
  ns::ContainerHeader header;

  Bytes corrupt = container;
  corrupt[corrupt.size() - 1] ^= 0x01U;
  put(ns::RECEIVE_PATH, corrupt);
  CHECK(ns::verifyReceived(header) == ns::VerifyResult::Corrupt);

  put(ns::RECEIVE_PATH, Bytes(container.begin(), container.end() - 5));
  CHECK(ns::verifyReceived(header) == ns::VerifyResult::Corrupt);

  Storage.remove(ns::RECEIVE_PATH);
  CHECK(ns::verifyReceived(header) == ns::VerifyResult::ReadError);
}

void peerStatsLandInSyncedStatsAtomically() {
  Storage.clear();
  const Bytes stats = statsV3();
  CHECK(ns::storePeerStats({0xaa, 0xbb, 0xcc, 0x01, 0x02, 0x03}, stats.data(), stats.size()));
  CHECK(get("/.crosspoint/synced_stats/device_aabbcc010203.bin") == stats);
  CHECK(!Storage.exists("/.crosspoint/nearby-sync-stats.tmp"));
  Bytes invalid = stats;
  invalid[0] = 9;
  CHECK(!ns::storePeerStats({0xaa, 0xbb, 0xcc, 0x01, 0x02, 0x03}, invalid.data(), invalid.size()));
  CHECK(!ns::storePeerStats({}, stats.data(), stats.size()));
  // Still the previous valid copy.
  CHECK(get("/.crosspoint/synced_stats/device_aabbcc010203.bin") == stats);
}

void atomicWriteKeepsTheOldFileOnFailure() {
  Storage.clear();
  put("/p.bin", pattern(10, 1));
  Storage.setWriteLimit(3);
  CHECK(!ns::writeFileAtomic("/p.bin", pattern(10, 2).data(), 10));
  Storage.clearWriteLimit();
  CHECK(get("/p.bin") == pattern(10, 1));
  CHECK(!Storage.exists("/p.bin.tmp"));
  CHECK(ns::writeFileAtomic("/p.bin", pattern(10, 2).data(), 10));
  CHECK(get("/p.bin") == pattern(10, 2));
}

}  // namespace

int main() {
  containerRoundTripsThroughStorage();
  corruptOrTruncatedContainersAreRejected();
  peerStatsLandInSyncedStatsAtomically();
  atomicWriteKeepsTheOldFileOnFailure();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  return 0;
}
