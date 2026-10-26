#pragma once

#include <cstddef>
#include <cstdint>

// The UI languages (and hyphenation patterns) that are not built into the
// firmware, read in place from a pack built by scripts/build_language_pack.py
// (see its docstring for the layout). The bytes stay where they are - mapped
// flash on the device - so a pack costs no RAM.
//
// Strings are found by the FNV-1a hash of their key name (STRING_KEY_HASHES),
// so a pack built for another firmware version still works: a key it lacks
// falls back to English.
namespace language_pack {

constexpr uint32_t MAGIC = 0x4B504C58u;  // "XLPK"
constexpr uint16_t FORMAT_VERSION = 1;
constexpr size_t HEADER_BYTES = 32;

struct Header {
  uint32_t magic = 0;
  uint16_t format = 0;
  uint16_t languageCount = 0;
  uint16_t hyphenationCount = 0;
  uint32_t totalSize = 0;
  uint32_t crc32 = 0;
  uint32_t contentId = 0;
};

// Decodes and checks the fixed header (magic, format, directory fits in
// totalSize). Does not check the CRC: see crcMatches().
bool parseHeader(const uint8_t* bytes, size_t size, Header& out);
// CRC-32 (zlib) of everything after the header, as the builder computed it.
bool crcMatches(const uint8_t* pack, const Header& header);
uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t size);

struct Hyphenation {
  const uint8_t* data = nullptr;
  size_t size = 0;
  size_t rootOffset = 0;
};

// Makes `pack` (totalSize bytes, 4-byte aligned) the active pack; nullptr
// detaches. Returns false (and detaches) when the header or directory is not
// valid. The bytes must outlive the attachment.
bool attach(const uint8_t* pack, size_t size);
bool attached();
const Header& header();

// Language code as in LANGUAGE_CODES ("DE", "PT2", ...).
bool hasLanguage(const char* code);
// Index of the language for lookup(), or -1.
int languageIndex(const char* code);
// The string stored under keyHash for that language, or nullptr (English).
const char* lookup(int languageIndex, uint32_t keyHash);

// Primary language tag ("de"); false when the pack has no patterns for it.
bool findHyphenation(const char* tag, Hyphenation& out);

}  // namespace language_pack
