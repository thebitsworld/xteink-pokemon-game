#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "LanguageHyphenator.h"

struct LanguageEntry {
  const char* cliName;
  const char* primaryTag;
  const LanguageHyphenator* hyphenator;
};

struct LanguageEntryView {
  const LanguageEntry* data;
  size_t size;

  const LanguageEntry* begin() const { return data; }
  const LanguageEntry* end() const { return data + size; }
};

// Returns the Liang-backed hyphenator for a given primary language tag (e.g., "en", "fr").
const LanguageHyphenator* getLanguageHyphenatorForPrimaryTag(const std::string& primaryTag);

// Exposes the list of supported languages primarily for tooling/tests.
LanguageEntryView getLanguageEntries();

// Patterns of a language that is not built in, read in place from the
// language pack (the bytes must stay valid until cleared). False for a tag
// with no such slot (English is built in).
bool setPackedHyphenationPatterns(const char* primaryTag, const uint8_t* data, size_t size, size_t rootOffset);
void clearPackedHyphenationPatterns();
// Called once, the first time a book asks for a packed language that has no
// patterns yet: lets the device load a language pack it keeps on the SD card
// (S3 readers whose spiffs partition is too small) only when a book needs it.
void setPackedHyphenationLoader(void (*loader)());
// The tags setPackedHyphenationPatterns() accepts.
const char* const* packedHyphenationTags(size_t& count);
