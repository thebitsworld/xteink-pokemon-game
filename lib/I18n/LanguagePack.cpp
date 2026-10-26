#include "LanguagePack.h"

#include <cstring>

namespace language_pack {
namespace {

constexpr size_t DIRECTORY_ENTRY_BYTES = 16;
constexpr size_t TABLE_ENTRY_BYTES = 8;

const uint8_t* gPack = nullptr;
Header gHeader{};

uint16_t read16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

uint32_t read32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

bool codeEquals(const uint8_t* stored, const char* code) {
  const size_t length = strlen(code);
  if (length > 4) return false;
  for (size_t i = 0; i < 4; ++i) {
    const uint8_t want = i < length ? static_cast<uint8_t>(code[i]) : 0;
    if (stored[i] != want) return false;
  }
  return true;
}

const uint8_t* languageEntry(const int index) {
  return gPack + HEADER_BYTES + static_cast<size_t>(index) * DIRECTORY_ENTRY_BYTES;
}

const uint8_t* hyphenationEntry(const int index) {
  return gPack + HEADER_BYTES + (gHeader.languageCount + static_cast<size_t>(index)) * DIRECTORY_ENTRY_BYTES;
}

bool rangeFits(const uint32_t offset, const uint64_t length, const uint32_t total) {
  return offset <= total && length <= total - offset;
}

// Every offset in the directory points inside the pack, so lookups need no checks.
bool directoryValid(const uint8_t* pack, const Header& header) {
  const uint32_t total = header.totalSize;
  for (size_t i = 0; i < header.languageCount; ++i) {
    const uint8_t* entry = pack + HEADER_BYTES + i * DIRECTORY_ENTRY_BYTES;
    const uint32_t table = read32(entry + 4);
    const uint32_t count = read32(entry + 8);
    const uint32_t blob = read32(entry + 12);
    if (table % 4 != 0 || !rangeFits(table, static_cast<uint64_t>(count) * TABLE_ENTRY_BYTES, total)) return false;
    if (blob > total) return false;
    // Strings start inside the pack; that they end there is the CRC's job
    // (checked when the pack is installed).
    for (uint32_t e = 0; e < count; ++e) {
      if (read32(pack + table + e * TABLE_ENTRY_BYTES + 4) >= total - blob) return false;
    }
  }
  for (size_t i = 0; i < header.hyphenationCount; ++i) {
    const uint8_t* entry = pack + HEADER_BYTES + (header.languageCount + i) * DIRECTORY_ENTRY_BYTES;
    const uint32_t data = read32(entry + 4);
    const uint32_t size = read32(entry + 8);
    if (data % 4 != 0 || !rangeFits(data, size, total) || read32(entry + 12) >= size) return false;
  }
  return true;
}

}  // namespace

uint32_t crc32Update(uint32_t crc, const uint8_t* data, const size_t size) {
  crc = ~crc;
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

bool parseHeader(const uint8_t* bytes, const size_t size, Header& out) {
  if (bytes == nullptr || size < HEADER_BYTES) return false;
  Header header{};
  header.magic = read32(bytes);
  header.format = read16(bytes + 4);
  header.languageCount = read16(bytes + 6);
  header.hyphenationCount = read16(bytes + 8);
  header.totalSize = read32(bytes + 12);
  header.crc32 = read32(bytes + 16);
  header.contentId = read32(bytes + 20);
  if (header.magic != MAGIC || header.format != FORMAT_VERSION) return false;
  const uint64_t directoryEnd =
      HEADER_BYTES + (static_cast<uint64_t>(header.languageCount) + header.hyphenationCount) * DIRECTORY_ENTRY_BYTES;
  if (header.totalSize < directoryEnd) return false;
  out = header;
  return true;
}

bool crcMatches(const uint8_t* pack, const Header& header) {
  return crc32Update(0, pack + HEADER_BYTES, header.totalSize - HEADER_BYTES) == header.crc32;
}

bool attach(const uint8_t* pack, const size_t size) {
  gPack = nullptr;
  gHeader = Header{};
  if (pack == nullptr) return true;
  Header header{};
  if (!parseHeader(pack, size, header) || header.totalSize > size || !directoryValid(pack, header)) return false;
  gHeader = header;
  gPack = pack;
  return true;
}

bool attached() { return gPack != nullptr; }

const Header& header() { return gHeader; }

int languageIndex(const char* code) {
  if (gPack == nullptr || code == nullptr) return -1;
  for (int i = 0; i < gHeader.languageCount; ++i) {
    if (codeEquals(languageEntry(i), code)) return i;
  }
  return -1;
}

bool hasLanguage(const char* code) { return languageIndex(code) >= 0; }

const char* lookup(const int languageIndex, const uint32_t keyHash) {
  if (gPack == nullptr || languageIndex < 0 || languageIndex >= gHeader.languageCount) return nullptr;
  const uint8_t* entry = languageEntry(languageIndex);
  const uint8_t* table = gPack + read32(entry + 4);
  uint32_t low = 0;
  uint32_t high = read32(entry + 8);
  while (low < high) {
    const uint32_t middle = low + (high - low) / 2;
    const uint32_t hash = read32(table + middle * TABLE_ENTRY_BYTES);
    if (hash == keyHash) {
      return reinterpret_cast<const char*>(gPack + read32(entry + 12) +
                                           read32(table + middle * TABLE_ENTRY_BYTES + 4));
    }
    if (hash < keyHash) {
      low = middle + 1;
    } else {
      high = middle;
    }
  }
  return nullptr;
}

bool findHyphenation(const char* tag, Hyphenation& out) {
  if (gPack == nullptr || tag == nullptr) return false;
  for (int i = 0; i < gHeader.hyphenationCount; ++i) {
    const uint8_t* entry = hyphenationEntry(i);
    if (!codeEquals(entry, tag)) continue;
    out.data = gPack + read32(entry + 4);
    out.size = read32(entry + 8);
    out.rootOffset = read32(entry + 12);
    return true;
  }
  return false;
}

}  // namespace language_pack
