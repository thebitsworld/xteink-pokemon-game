#pragma once

#include <cstdint>

#include "I18nKeys.h"
/**
 * Internationalization (i18n) system for CrossPoint Reader
 */

class I18n {
 public:
  static I18n& getInstance();

  // Disable copy
  I18n(const I18n&) = delete;
  I18n& operator=(const I18n&) = delete;

  // Get localized string by ID
  const char* get(StrId id) const;

  const char* operator[](StrId id) const { return get(id); }

  Language getLanguage() const { return _language; }
  // A language that is neither built in nor in the installed language pack
  // shows English; the request is remembered, so installing the pack later
  // (refreshLanguagePack) brings it back.
  void setLanguage(Language lang);
  // Call after the language pack was attached or detached.
  void refreshLanguagePack() { setLanguage(_requested); }
  static bool isBuiltin(Language lang);
  // Built in, or in the installed language pack.
  static bool isAvailable(Language lang);
  const char* getLanguageName(Language lang) const;
  static Language languageFromCode(const char* code);

  // Get all unique characters used in a specific language
  // Returns a sorted string of unique characters
  static const char* getCharacterSet(Language lang);

 private:
  I18n() : _language(Language::EN), _requested(Language::EN) {}

  Language _language;
  Language _requested;
  int _packLanguage = -1;  // language_pack index of _language, or -1 (built in)
};

// Convenience macros
#define tr(id) I18n::getInstance().get(StrId::id)
#define I18N I18n::getInstance()
