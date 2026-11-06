#pragma once

#include <string>

// All the SD language files and hyphenation packs in one ZIP, so that this fork
// needs no download server: xteink-pokemon-languages.zip from a GitHub release,
// either copied to the root of the SD card or downloaded by
// LanguageBundleDownloadActivity. The next boot unpacks it into .crossink/.
// Built by scripts/package_languages.py --bundle-output.
namespace language_bundle {

constexpr char ROOT_PATH[] = "/xteink-pokemon-languages.zip";
constexpr char DOWNLOAD_PATH[] = "/.crosspoint/language-bundle.zip";
constexpr char FAILED_PATH[] = "/.crosspoint/language-bundle.failed";
// Where the bundle is downloaded from; a test build can point at a prerelease, which releases/latest skips.
#ifndef CROSSINK_LANGUAGE_BUNDLE_BASE_URL
#define CROSSINK_LANGUAGE_BUNDLE_BASE_URL "https://github.com/thebitsworld/xteink-pokemon-game/releases/latest/download/"
#endif
constexpr char URL[] = CROSSINK_LANGUAGE_BUNDLE_BASE_URL "xteink-pokemon-languages.zip";
constexpr char SHA256_URL[] = CROSSINK_LANGUAGE_BUNDLE_BASE_URL "xteink-pokemon-languages.zip.sha256";

// Boot, before I18N.begin(): unpacks a waiting bundle and removes it. A bundle
// that cannot be unpacked is set aside, so it is not tried at every start.
// True when files were installed.
bool installPending();

// Boot, after I18N.begin() and a bundle was installed: a saved language that is
// not active yet (a reader upgraded from the old language pack) is installed
// from its new file, and the reader restarts into it.
void applyPreferredLanguage();

// The file's SHA-256 against 64 hex digits.
bool fileMatchesSha256(const std::string& path, const std::string& expectedHex);

}  // namespace language_bundle
