#include "I18n.h"

#include <cstddef>
#include <cstring>

#include "I18nStrings.h"
#include "LanguagePack.h"

using namespace i18n_strings;

bool I18n::isBuiltin(const Language language) {
  const auto raw = static_cast<uint8_t>(language);
  for (const uint8_t builtin : SORTED_LANGUAGE_INDICES) {
    if (builtin == raw) return true;
  }
  return false;
}

bool I18n::isAvailable(const Language language) {
  if (language >= Language::_COUNT) return false;
  return isBuiltin(language) || language_pack::hasLanguage(LANGUAGE_CODES[static_cast<size_t>(language)]);
}

I18n& I18n::getInstance() {
  static I18n instance;
  return instance;
}

const char* I18n::get(StrId id) const {
  const auto index = static_cast<size_t>(id);
  if (index >= static_cast<size_t>(StrId::_COUNT)) {
    return "???";
  }

  if (_packLanguage >= 0) {
    const char* packed = language_pack::lookup(_packLanguage, STRING_KEY_HASHES[index]);
    return packed != nullptr ? packed : STRINGS_EN_DATA + OFFSETS_EN[index];
  }

  // Use generated helper function - no hardcoded switch needed!
  const LangStrings lang = getLanguageStrings(_language);

  // 0xFFFF marks a string identical to English (not stored in this language's
  // blob): read it from the English blob instead. Any other value is a plain
  // byte offset, so a blob can use the full 16-bit range (see gen_i18n.py).
  const uint16_t off = lang.offsets[index];
  if (off == 0xFFFF) return STRINGS_EN_DATA + OFFSETS_EN[index];
  return lang.data + off;
}

void I18n::setLanguage(Language lang) {
  if (lang >= Language::_COUNT) {
    return;
  }
  _requested = lang;
  // Keep persisted settings untouched, but make every runtime language-dependent
  // behavior agree with the English string fallback in reduced-language builds.
  _packLanguage = isBuiltin(lang) ? -1 : language_pack::languageIndex(LANGUAGE_CODES[static_cast<size_t>(lang)]);
  _language = (isBuiltin(lang) || _packLanguage >= 0) ? lang : Language::EN;
}

const char* I18n::getLanguageName(Language lang) const {
  const auto index = static_cast<size_t>(lang);
  if (index >= static_cast<size_t>(Language::_COUNT)) {
    return "???";
  }
  return LANGUAGE_NAMES[index];
}

Language I18n::languageFromCode(const char* code) {
  for (uint8_t i = 0; i < getLanguageCount(); i++) {
    if (strcmp(code, LANGUAGE_CODES[i]) == 0) return static_cast<Language>(i);
  }
  return Language::EN;
}

// Generate character set for a specific language
const char* I18n::getCharacterSet(Language lang) {
  const auto langIndex = static_cast<size_t>(lang);
  if (langIndex >= static_cast<size_t>(Language::_COUNT)) {
    lang = Language::EN;  // Fallback to first language
  }

  return CHARACTER_SETS[static_cast<size_t>(lang)];
}
