#include "LanguageBundle.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <ZipFile.h>
#include <mbedtls/sha256.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "CrossPointSettings.h"

namespace {

constexpr char TAG[] = "LANGBUNDLE";
constexpr char MANIFEST[] = ".crossink/bundle-manifest.txt";
constexpr size_t MAX_MANIFEST_BYTES = 8192;
constexpr size_t EXTRACT_CHUNK = 1024;

bool endsWith(const std::string& value, const char* suffix) {
  const size_t length = std::strlen(suffix);
  return value.size() > length && value.compare(value.size() - length, length, suffix) == 0;
}

bool startsWith(const std::string& value, const char* prefix) { return value.rfind(prefix, 0) == 0; }

// Only flat files in the two folders the firmware reads: a bundle cannot write anywhere else.
bool allowedEntry(const std::string& name) {
  constexpr char LANGUAGES[] = ".crossink/languages/";
  constexpr char HYPHENATION[] = ".crossink/hyphenation/";
  size_t prefix = 0;
  if (startsWith(name, LANGUAGES) && endsWith(name, ".yaml")) prefix = sizeof(LANGUAGES) - 1;
  if (startsWith(name, HYPHENATION) && endsWith(name, ".cphyph")) prefix = sizeof(HYPHENATION) - 1;
  return prefix != 0 && name.find('/', prefix) == std::string::npos && name.find('\\') == std::string::npos &&
         name.find("..") == std::string::npos;
}

bool extractEntry(ZipFile& zip, const std::string& name) {
  const std::string target = "/" + name;
  const std::string staged = target + ".tmp";
  size_t expected = 0;
  if (!zip.getInflatedFileSize(name.c_str(), &expected)) {
    LOG_ERR(TAG, "Missing from bundle: %s", name.c_str());
    return false;
  }
  HalFile out;
  if (!Storage.openFileForWrite(TAG, staged.c_str(), out)) return false;
  const bool read = zip.readFileToStream(name.c_str(), out, EXTRACT_CHUNK);
  const size_t written = out.fileSize();
  const bool closed = out.close();
  if (!read || !closed || written != expected) {
    LOG_ERR(TAG, "Could not unpack %s (%u of %u bytes)", name.c_str(), static_cast<unsigned>(written),
            static_cast<unsigned>(expected));
    Storage.remove(staged.c_str());
    return false;
  }
  if (Storage.exists(target.c_str())) Storage.remove(target.c_str());
  return Storage.rename(staged.c_str(), target.c_str());
}

// Returns the number of files installed, or -1.
int extractBundle(const std::string& path) {
  ZipFile zip(path);
  size_t size = 0;
  std::unique_ptr<uint8_t, decltype(&std::free)> manifest(zip.readFileToMemory(MANIFEST, &size, true), &std::free);
  if (!manifest || size == 0 || size > MAX_MANIFEST_BYTES) {
    LOG_ERR(TAG, "No manifest in %s", path.c_str());
    return -1;
  }
  if (!Storage.ensureDirectoryExists("/.crossink/languages") ||
      !Storage.ensureDirectoryExists("/.crossink/hyphenation")) {
    return -1;
  }
  int installed = 0;
  const char* line = reinterpret_cast<const char*>(manifest.get());
  while (*line) {
    const char* end = std::strchr(line, '\n');
    std::string name = end ? std::string(line, end - line) : std::string(line);
    if (!name.empty() && name.back() == '\r') name.pop_back();
    if (!name.empty()) {
      if (!allowedEntry(name)) {
        LOG_ERR(TAG, "Refused entry: %s", name.c_str());
        return -1;
      }
      if (!extractEntry(zip, name)) return -1;
      ++installed;
    }
    if (!end) break;
    line = end + 1;
  }
  return installed;
}

bool installFrom(const char* path) {
  if (!Storage.exists(path)) return false;
  const int installed = extractBundle(path);
  if (installed <= 0) {
    LOG_ERR(TAG, "Bundle not installed; set aside: %s", path);
    Storage.remove(language_bundle::FAILED_PATH);
    Storage.rename(path, language_bundle::FAILED_PATH);
    return false;
  }
  Storage.remove(path);
  LOG_INF(TAG, "Installed %d files from %s", installed, path);
  return true;
}

}  // namespace

namespace language_bundle {

bool installPending() {
  // The file you copied yourself wins over an older download.
  const bool fromRoot = installFrom(ROOT_PATH);
  const bool fromDownload = installFrom(DOWNLOAD_PATH);
  return fromRoot || fromDownload;
}

void applyPreferredLanguage() {
  if (std::strcmp(SETTINGS.languageCode, I18N.getCode()) == 0) return;
  auto catalog = std::make_unique<I18n::Catalog>();
  if (I18N.discover(*catalog) != language_cache::Result::Ok) return;
  I18n::Option selected;
  for (size_t i = 0; i < catalog->count; ++i) {
    if (std::strcmp(catalog->code(i), SETTINGS.languageCode) != 0) continue;
    if (catalog->disabled(i) || !*catalog->file(i)) return;
    catalog->select(i, selected);
    break;
  }
  if (!selected.path[0]) return;
  language_cache::Installed installed;
  const auto result = I18N.prepare(selected.path, installed);
  if (result != language_cache::Result::Ok) {
    LOG_ERR(TAG, "Could not install %s: %s", selected.path, language_cache::resultName(result));
    return;
  }
  std::strcpy(SETTINGS.languageCode, installed.metadata.code);
  SETTINGS.languageCacheGeneration = installed.generation;
  if (!SETTINGS.saveToFile()) return;
  LOG_INF(TAG, "Installed saved language %s; restarting", SETTINGS.languageCode);
  // Not silentRestart(): it draws, and the fonts are not up yet this early in the boot.
  ESP.restart();
}

bool fileMatchesSha256(const std::string& path, const std::string& expectedHex) {
  if (expectedHex.size() != 64) return false;
  HalFile file;
  if (!Storage.openFileForRead(TAG, path.c_str(), file)) return false;
  mbedtls_sha256_context hash;
  mbedtls_sha256_init(&hash);
  bool ok = mbedtls_sha256_starts(&hash, 0) == 0;
  constexpr size_t BUFFER = 1024;
  auto buffer = std::make_unique<uint8_t[]>(BUFFER);
  while (ok) {
    const int count = file.read(buffer.get(), BUFFER);
    if (count < 0) ok = false;
    if (count <= 0) break;
    ok = mbedtls_sha256_update(&hash, buffer.get(), static_cast<size_t>(count)) == 0;
  }
  uint8_t digest[32];
  ok = ok && mbedtls_sha256_finish(&hash, digest) == 0;
  mbedtls_sha256_free(&hash);
  file.close();
  if (!ok) return false;
  char hex[65];
  for (size_t i = 0; i < sizeof(digest); ++i) std::snprintf(hex + i * 2, 3, "%02x", digest[i]);
  return expectedHex == hex;
}

}  // namespace language_bundle
