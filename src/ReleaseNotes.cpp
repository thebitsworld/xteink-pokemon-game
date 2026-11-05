#include "ReleaseNotes.h"

#include <I18n.h>

#include <cstring>

namespace {
constexpr StrId LANGUAGES_PATH[] = {StrId::STR_SETTINGS_TITLE, StrId::STR_CAT_SYSTEM, StrId::STR_SYSTEM_DEVICE,
                                    StrId::STR_DOWNLOAD_LANGUAGES};
constexpr StrId HYPHENATION_PATH[] = {StrId::STR_SETTINGS_TITLE, StrId::STR_CAT_READER, StrId::STR_READER_PAGE_LAYOUT,
                                      StrId::STR_HYPHENATION_PACKS};
static_assert(sizeof(LANGUAGES_PATH) / sizeof(StrId) <= RELEASE_NOTES_MAX_PATH);
static_assert(sizeof(HYPHENATION_PATH) / sizeof(StrId) <= RELEASE_NOTES_MAX_PATH);
}  // namespace

const ReleaseNoteItem RELEASE_NOTES[] = {
    {StrId::STR_NOTES_2_0_0_LANGUAGES_TITLE, StrId::STR_NOTES_2_0_0_LANGUAGES_BODY, LANGUAGES_PATH,
     sizeof(LANGUAGES_PATH) / sizeof(StrId), nullptr},
    {StrId::STR_NOTES_2_0_0_HYPHENATION_TITLE, StrId::STR_NOTES_2_0_0_HYPHENATION_BODY, HYPHENATION_PATH,
     sizeof(HYPHENATION_PATH) / sizeof(StrId), nullptr},
    {StrId::STR_NOTES_2_0_0_BOOKS_TITLE, StrId::STR_NOTES_2_0_0_BOOKS_BODY, nullptr, 0, nullptr},
    {StrId::STR_RELEASE_NOTES_FULL_CHANGELOG, StrId::_COUNT, nullptr, 0, RELEASE_NOTES_CHANGELOG_URL},
};
const size_t RELEASE_NOTES_COUNT = sizeof(RELEASE_NOTES) / sizeof(RELEASE_NOTES[0]);

bool releaseNotesNeedLanguageUpdate() {
  for (size_t i = 0; i < RELEASE_NOTES_COUNT; ++i) {
    const ReleaseNoteItem& item = RELEASE_NOTES[i];
    if (!I18N.hasTranslation(item.title)) return true;
    if (item.body != StrId::_COUNT && !I18N.hasTranslation(item.body)) return true;
  }
  return false;
}

// Languages live in the language pack here, not in per-language downloads: nothing to offer.
bool releaseNotesOfferLanguageUpdate(const char*, const char*, const uint16_t) { return false; }
