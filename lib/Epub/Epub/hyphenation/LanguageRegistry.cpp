#include "LanguageRegistry.h"

#include <algorithm>
#include <array>

#include <cstring>

#include "HyphenationCommon.h"
#include "generated/hyph-en.trie.h"
#if defined(HYPHENATION_ALL_BUILTIN)
#include "generated/hyph-de.trie.h"
#include "generated/hyph-es.trie.h"
#include "generated/hyph-fr.trie.h"
#include "generated/hyph-it.trie.h"
#include "generated/hyph-pl.trie.h"
#include "generated/hyph-pt.trie.h"
#include "generated/hyph-ru.trie.h"
#include "generated/hyph-sv.trie.h"
#include "generated/hyph-uk.trie.h"
#endif

namespace {

// Only English is compiled into the firmware; the other languages' patterns
// come from the language pack (setPackedHyphenationPatterns) and are empty -
// no hyphenation - until it is installed. Host tests and tools compile them
// all in (HYPHENATION_ALL_BUILTIN).
#if defined(HYPHENATION_ALL_BUILTIN)
#define PACKED_PATTERNS(tag) SerializedHyphenationPatterns tag##Patterns = tag##_patterns
#else
#define PACKED_PATTERNS(tag) SerializedHyphenationPatterns tag##Patterns {}
#endif
PACKED_PATTERNS(fr);
PACKED_PATTERNS(de);
PACKED_PATTERNS(ru);
PACKED_PATTERNS(es);
PACKED_PATTERNS(it);
PACKED_PATTERNS(sv);
PACKED_PATTERNS(uk);
PACKED_PATTERNS(pl);
PACKED_PATTERNS(pt);
#undef PACKED_PATTERNS

struct PackedPatterns {
  const char* tag;
  SerializedHyphenationPatterns* patterns;
};
constexpr PackedPatterns kPacked[] = {{"fr", &frPatterns}, {"de", &dePatterns}, {"ru", &ruPatterns},
                                      {"es", &esPatterns}, {"it", &itPatterns}, {"sv", &svPatterns},
                                      {"uk", &ukPatterns}, {"pl", &plPatterns}, {"pt", &ptPatterns}};

// English hyphenation patterns (3/3 minimum prefix/suffix length)
LanguageHyphenator englishHyphenator(en_patterns, isLatinLetter, toLowerLatin, 3, 3);
LanguageHyphenator frenchHyphenator(frPatterns, isLatinLetter, toLowerLatin);
LanguageHyphenator germanHyphenator(dePatterns, isLatinLetter, toLowerLatin);
LanguageHyphenator russianHyphenator(ruPatterns, isCyrillicLetter, toLowerCyrillic);
LanguageHyphenator spanishHyphenator(esPatterns, isLatinLetter, toLowerLatin);
LanguageHyphenator italianHyphenator(itPatterns, isLatinLetter, toLowerLatin);
LanguageHyphenator swedishHyphenator(svPatterns, isLatinLetter, toLowerLatin);
LanguageHyphenator ukrainianHyphenator(ukPatterns, isCyrillicLetter, toLowerCyrillic);
LanguageHyphenator polishHyphenator(plPatterns, isLatinLetter, toLowerLatin);
LanguageHyphenator portugueseHyphenator(ptPatterns, isLatinLetter, toLowerLatin);

using EntryArray = std::array<LanguageEntry, 10>;

const EntryArray& entries() {
  static const EntryArray kEntries = {{{"english", "en", &englishHyphenator},
                                       {"french", "fr", &frenchHyphenator},
                                       {"german", "de", &germanHyphenator},
                                       {"russian", "ru", &russianHyphenator},
                                       {"spanish", "es", &spanishHyphenator},
                                       {"italian", "it", &italianHyphenator},
                                       {"polish", "pl", &polishHyphenator},
                                       {"portuguese", "pt", &portugueseHyphenator},
                                       {"swedish", "sv", &swedishHyphenator},
                                       {"ukrainian", "uk", &ukrainianHyphenator}}};
  return kEntries;
}

}  // namespace

const LanguageHyphenator* getLanguageHyphenatorForPrimaryTag(const std::string& primaryTag) {
  const auto& allEntries = entries();
  const auto it = std::find_if(allEntries.begin(), allEntries.end(),
                               [&primaryTag](const LanguageEntry& entry) { return primaryTag == entry.primaryTag; });
  if (it == allEntries.end() || !it->hyphenator->hasPatterns()) return nullptr;
  return it->hyphenator;
}

bool setPackedHyphenationPatterns(const char* primaryTag, const uint8_t* data, const size_t size,
                                  const size_t rootOffset) {
  for (const auto& packed : kPacked) {
    if (std::strcmp(packed.tag, primaryTag) != 0) continue;
    *packed.patterns = SerializedHyphenationPatterns{rootOffset, data, data != nullptr ? size : 0};
    return true;
  }
  return false;
}

void clearPackedHyphenationPatterns() {
  for (const auto& packed : kPacked) *packed.patterns = SerializedHyphenationPatterns{};
}

const char* const* packedHyphenationTags(size_t& count) {
  static const char* const kTags[] = {"fr", "de", "ru", "es", "it", "sv", "uk", "pl", "pt"};
  count = sizeof(kTags) / sizeof(kTags[0]);
  return kTags;
}

LanguageEntryView getLanguageEntries() {
  const auto& allEntries = entries();
  return LanguageEntryView{allEntries.data(), allEntries.size()};
}
