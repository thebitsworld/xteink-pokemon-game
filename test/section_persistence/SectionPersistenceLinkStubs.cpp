#include <Epub.h>
#include <GfxRenderer.h>
#include <Serialization.h>

#include "Epub/Page.h"
#include "Epub/hyphenation/Hyphenator.h"
#include "Epub/parsers/ChapterHtmlSlimParser.h"

Epub::Epub(std::string path, const std::string& cacheDir) : filepath(std::move(path)), cachePath(cacheDir) {}

const std::string& Epub::getCachePath() const { return cachePath; }
const std::string& Epub::getLanguage() const {
  static const std::string language = "en";
  return language;
}
BookMetadataCache::SpineEntry Epub::getSpineItem(int) const { return {}; }
BookMetadataCache::TocEntry Epub::getTocItem(int) const { return {}; }
int Epub::getTocItemsCount() const { return 0; }
int Epub::getTocIndexForSpineIndex(int) const { return -1; }
bool Epub::readItemContentsToStream(const std::string&, Print&, size_t, bool) const { return false; }

bool CssParser::loadFromCache() { return false; }

namespace {
constexpr char asciiToLower(const char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; }
constexpr size_t FNV_OFFSET_BASIS =
    sizeof(size_t) == 8 ? static_cast<size_t>(14695981039346656037ULL) : static_cast<size_t>(2166136261U);
constexpr size_t FNV_PRIME =
    sizeof(size_t) == 8 ? static_cast<size_t>(1099511628211ULL) : static_cast<size_t>(16777619U);
constexpr size_t fnv1aMix(size_t hash, unsigned char byte) { return (hash ^ byte) * FNV_PRIME; }
}  // namespace

// Minimal real implementations (not stubbed to a constant) - this test's
// CssParser only stubs out loadFromCache(), but rulesBySelector_'s
// unordered_map still hashes/compares real keys whenever it is touched
// (e.g. clear()), so these bodies must behave like CssParser.cpp's real ones.
size_t CssParser::SvHash::operator()(std::string_view sv) const noexcept {
  size_t h = FNV_OFFSET_BASIS;
  for (char c : sv) h = fnv1aMix(h, asciiToLower(c));
  return h;
}
size_t CssParser::SvHash::operator()(const std::string& s) const noexcept { return operator()(std::string_view(s)); }
size_t CssParser::SvHash::operator()(CompositeKey k) const noexcept {
  size_t h = FNV_OFFSET_BASIS;
  for (std::string_view piece : k.pieces) {
    for (char c : piece) h = fnv1aMix(h, asciiToLower(c));
  }
  return h;
}

bool CssParser::SvEqual::operator()(std::string_view a, std::string_view b) const noexcept {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (asciiToLower(a[i]) != asciiToLower(b[i])) return false;
  }
  return true;
}
bool CssParser::SvEqual::operator()(const std::string& a, std::string_view b) const noexcept {
  return operator()(std::string_view(a), b);
}
bool CssParser::SvEqual::operator()(std::string_view a, const std::string& b) const noexcept {
  return operator()(a, std::string_view(b));
}
bool CssParser::SvEqual::operator()(const std::string& a, const std::string& b) const noexcept {
  return operator()(std::string_view(a), std::string_view(b));
}
bool CssParser::SvEqual::operator()(CompositeKey k, std::string_view sv) const noexcept {
  size_t total = 0;
  for (std::string_view piece : k.pieces) total += piece.size();
  if (total != sv.size()) return false;
  size_t i = 0;
  for (std::string_view piece : k.pieces) {
    for (char c : piece) {
      if (asciiToLower(c) != asciiToLower(sv[i++])) return false;
    }
  }
  return true;
}
bool CssParser::SvEqual::operator()(std::string_view sv, CompositeKey k) const noexcept { return operator()(k, sv); }

void Hyphenator::setPreferredLanguage(const std::string&) {}

ChapterHtmlSlimParser::~ChapterHtmlSlimParser() = default;
bool ChapterHtmlSlimParser::beginParse() { return false; }
ChapterHtmlSlimParser::ParseStatus ChapterHtmlSlimParser::parseStep() { return ParseStatus::Error; }
bool ChapterHtmlSlimParser::finishParse() { return true; }
void ChapterHtmlSlimParser::abortParse() {}
void ChapterHtmlSlimParser::releaseInputFile() {}

bool Page::serialize(FsFile& file) const {
  constexpr uint32_t marker = 0x50414745;
  return serialization::tryWritePod(file, marker);
}

std::unique_ptr<Page> Page::deserialize(FsFile& file) {
  uint32_t marker = 0;
  if (!serialization::tryReadPod(file, marker) || marker != 0x50414745) return nullptr;
  return std::make_unique<Page>();
}

uint16_t Page::imageEstimateUnits(uint16_t) const { return 0; }
