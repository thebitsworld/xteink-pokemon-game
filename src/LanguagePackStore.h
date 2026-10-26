#pragma once

#include <cstddef>
#include <cstdint>

// Where the language pack (lib/I18n/LanguagePack.h) lives on the device: the
// unused `spiffs` flash partition, mapped in place so it costs no RAM - or, on
// an S3 reader whose partition is too small, the SD card file read into PSRAM. A pack
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

// The S3 readers whose spiffs partition is too small keep the pack on the SD
// card and read it into PSRAM: at boot for a menu language from the pack,
// else on the first call of this (the language picker) or when a book needs
// a packed hyphenation. True once a pack is attached.
bool ensureLoaded();

// Why there are no extra languages although a pack is on the SD card.
enum class Problem : uint8_t { None, NoRoom, Damaged };
Problem problem();

bool installed();
// Languages and hyphenation patterns in the installed pack (0 without one).
size_t languageCount();

}  // namespace language_pack_store
