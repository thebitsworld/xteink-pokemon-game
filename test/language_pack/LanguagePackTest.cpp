#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

#include "I18n.h"
#include "LanguagePack.h"

namespace {

std::vector<uint8_t> readFile(const char* path) {
  std::ifstream in(path, std::ios::binary);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void builtInLanguagesNeedNoPack() {
  assert(language_pack::attach(nullptr, 0));
  I18N.setLanguage(Language::VI);
  assert(I18N.getLanguage() == Language::VI);
  assert(std::strcmp(tr(STR_BACK), "Quay lại") == 0);
  I18N.setLanguage(Language::DE);  // not built in, no pack: English
  assert(I18N.getLanguage() == Language::EN);
  assert(std::strcmp(tr(STR_BACK), "Back") == 0);
  assert(!I18n::isAvailable(Language::DE));
  assert(I18n::isAvailable(Language::VI));
}

void packLanguagesComeBackOnceAttached(std::vector<uint8_t>& pack) {
  language_pack::Header header{};
  assert(language_pack::parseHeader(pack.data(), pack.size(), header));
  assert(header.totalSize == pack.size());
  assert(language_pack::crcMatches(pack.data(), header));
  assert(language_pack::attach(pack.data(), pack.size()));
  assert(language_pack::hasLanguage("DE") && language_pack::hasLanguage("PT2"));
  assert(!language_pack::hasLanguage("EN") && !language_pack::hasLanguage("VI"));

  I18N.refreshLanguagePack();  // the German request made before the pack was there
  assert(I18N.getLanguage() == Language::DE);
  assert(std::strcmp(tr(STR_BACK), "Zurück") == 0);
  assert(std::strcmp(tr(STR_LANGUAGE), "Sprache") == 0);
  I18N.setLanguage(Language::FR);
  assert(std::strcmp(tr(STR_CANCEL), "Annuler") == 0);
  assert(I18n::isAvailable(Language::FR));

  language_pack::Hyphenation hyphenation{};
  assert(language_pack::findHyphenation("de", hyphenation) && hyphenation.size > 0 &&
         hyphenation.rootOffset < hyphenation.size);
  assert(!language_pack::findHyphenation("en", hyphenation));  // stays in the firmware

  assert(language_pack::attach(nullptr, 0));
  I18N.refreshLanguagePack();
  assert(I18N.getLanguage() == Language::EN);
  assert(std::strcmp(tr(STR_CANCEL), "Cancel") == 0);
}

void damagedPacksAreRefused(std::vector<uint8_t> pack) {
  language_pack::Header header{};
  std::vector<uint8_t> shortPack(pack.begin(), pack.begin() + 100);
  assert(!language_pack::attach(shortPack.data(), shortPack.size()));
  pack[pack.size() / 2] ^= 0x5A;
  assert(language_pack::parseHeader(pack.data(), pack.size(), header));
  assert(!language_pack::crcMatches(pack.data(), header));
  pack[0] = 'Y';
  assert(!language_pack::parseHeader(pack.data(), pack.size(), header));
  assert(!language_pack::attach(pack.data(), pack.size()));
  assert(!language_pack::attached());
}

}  // namespace

int main(int argc, char** argv) {
  assert(argc == 2);
  std::vector<uint8_t> pack = readFile(argv[1]);
  assert(pack.size() > language_pack::HEADER_BYTES);
  builtInLanguagesNeedNoPack();
  packLanguagesComeBackOnceAttached(pack);
  damagedPacksAreRefused(pack);
  std::printf("language pack tests passed (%zu bytes)\n", pack.size());
  return 0;
}
