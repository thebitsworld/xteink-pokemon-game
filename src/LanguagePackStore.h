#pragma once

#include <cstddef>

// Where the language pack (lib/I18n/LanguagePack.h) lives on the device: the
// unused `spiffs` flash partition, mapped in place so it costs no RAM. A pack
// copied to the SD card at PACK_PATH (from the SD card assets ZIP, or the
// Wi-Fi download) is installed into it at boot when it differs from the one
// there. The simulator reads the SD file straight into memory instead.
namespace language_pack_store {

constexpr const char* PACK_PATH = "/.crosspoint/language-pack.bin";
// Where a user copies a downloaded pack by hand: moved to PACK_PATH at boot.
constexpr const char* USER_PACK_PATH = "/language-pack.bin";
constexpr const char* PACK_URL =
    "https://github.com/thebitsworld/xteink-pokemon-game/releases/latest/download/language-pack.bin";

// Boot: installs a newer pack from the SD card, maps the installed pack and
// hands it to I18n and the hyphenation registry. Run after Storage.begin()
// and before I18N.setLanguage().
void begin();

// Checks a pack file's header and CRC (the download's validator).
bool fileIsValidPack(const char* path);

bool installed();
// Languages and hyphenation patterns in the installed pack (0 without one).
size_t languageCount();

}  // namespace language_pack_store
