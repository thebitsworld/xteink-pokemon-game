#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "NearbySyncCodec.h"

namespace {

int failures = 0;

#define CHECK(condition)                                                                \
  do {                                                                                  \
    if (!(condition)) {                                                                 \
      std::fprintf(stderr, "%s:%d check failed: %s\n", __FILE__, __LINE__, #condition); \
      ++failures;                                                                       \
    }                                                                                   \
  } while (false)

namespace ns = nearby_sync;

template <size_t N>
void setText(std::array<char, N>& out, const char* text) {
  out.fill('\0');
  std::memcpy(out.data(), text, std::strlen(text));
}

ns::ContainerHeader sampleHeader() {
  ns::ContainerHeader header;
  header.senderMac = {1, 2, 3, 4, 5, 6};
  header.sections[0] = {ns::SectionType::ReadingStats, 159, 0xAAAA0001U};
  header.sections[1] = {ns::SectionType::BookPosition, 40, 0xAAAA0002U};
  header.sections[2] = {ns::SectionType::PokemonSave, 90000, 0xAAAA0003U};
  header.sectionCount = 3;
  return header;
}

void crcMatchesStandardCrc32() {
  const char* text = "123456789";
  CHECK(ns::crc32Of(reinterpret_cast<const uint8_t*>(text), 9) == 0xCBF43926U);
}

void tableRoundTripsAndLocatesSections() {
  const ns::ContainerHeader header = sampleHeader();
  std::array<uint8_t, ns::MAX_TABLE_BYTES> bytes{};
  size_t written = 0;
  CHECK(ns::encodeContainerTable(header, bytes.data(), bytes.size(), written));
  CHECK(written == header.tableBytes());
  ns::ContainerHeader decoded;
  CHECK(ns::decodeContainerTable(bytes.data(), written, decoded) == ns::ParseResult::Ok);
  CHECK(decoded.senderMac == header.senderMac);
  CHECK(decoded.sectionCount == 3);
  CHECK(decoded.find(ns::SectionType::PokemonSave) == 2);
  CHECK(decoded.sectionOffset(2) == header.tableBytes() + 159 + 40);
  CHECK(decoded.totalBytes() == header.tableBytes() + 159 + 40 + 90000);
  CHECK(ns::decodeContainerTable(bytes.data(), written - 1, decoded) == ns::ParseResult::NeedMoreBytes);

  auto flipped = bytes;
  flipped[ns::HEADER_BYTES + 4] ^= 1U;
  CHECK(ns::decodeContainerTable(flipped.data(), written, decoded) == ns::ParseResult::Invalid);
  auto newer = bytes;
  newer[4] = ns::CONTAINER_VERSION + 1;
  CHECK(ns::decodeContainerTable(newer.data(), written, decoded) == ns::ParseResult::UnsupportedVersion);

  ns::ContainerHeader duplicate = header;
  duplicate.sections[1].type = ns::SectionType::ReadingStats;
  CHECK(!ns::encodeContainerTable(duplicate, bytes.data(), bytes.size(), written));
  ns::ContainerHeader empty;
  CHECK(!ns::encodeContainerTable(empty, bytes.data(), bytes.size(), written));

  // Only the book file may be bigger than the ordinary section limit.
  ns::ContainerHeader withBook = header;
  withBook.sections[3] = {ns::SectionType::BookFile, 20U * 1024U * 1024U, 0xAAAA0004U};
  withBook.sectionCount = 4;
  CHECK(ns::encodeContainerTable(withBook, bytes.data(), bytes.size(), written));
  CHECK(ns::decodeContainerTable(bytes.data(), written, decoded) == ns::ParseResult::Ok);
  CHECK(decoded.find(ns::SectionType::BookFile) == 3);
  ns::ContainerHeader bigStats = header;
  bigStats.sections[0].size = ns::MAX_SECTION_BYTES + 1;
  CHECK(!ns::encodeContainerTable(bigStats, bytes.data(), bytes.size(), written));
  withBook.sections[3].size = ns::MAX_BOOK_FILE_BYTES + 1;
  CHECK(!ns::encodeContainerTable(withBook, bytes.data(), bytes.size(), written));
}

void bookPositionRoundTripsAndRejectsBadInput() {
  ns::BookPositionRecord record;
  setText(record.path, "/Books/Il nome della rosa.epub");
  setText(record.title, "Il nome della rosa");
  record.percentBasisPoints = 4237;
  record.progressLength = 10;
  record.progress = {3, 0, 12, 0, 40, 0, 0x10, 0x27, 0, 0};
  std::array<uint8_t, ns::MAX_BOOK_RECORD_BYTES> bytes{};
  size_t written = 0;
  CHECK(ns::encodeBookPosition(record, bytes.data(), bytes.size(), written));
  ns::BookPositionRecord decoded;
  CHECK(ns::decodeBookPosition(bytes.data(), written, decoded));
  CHECK(decoded == record);
  CHECK(std::strcmp(ns::baseName(decoded.path.data()), "Il nome della rosa.epub") == 0);
  CHECK(!ns::decodeBookPosition(bytes.data(), written - 1, decoded));
  CHECK(!ns::decodeBookPosition(bytes.data(), written + 1, decoded));

  ns::BookPositionRecord relative = record;
  setText(relative.path, "Books/x.epub");
  CHECK(!ns::encodeBookPosition(relative, bytes.data(), bytes.size(), written));
  ns::BookPositionRecord badProgress = record;
  badProgress.progressLength = 7;
  CHECK(!ns::encodeBookPosition(badProgress, bytes.data(), bytes.size(), written));
  ns::BookPositionRecord unknownPercent = record;
  unknownPercent.percentBasisPoints = ns::UNKNOWN_PERCENT;
  unknownPercent.progressLength = 4;
  CHECK(ns::encodeBookPosition(unknownPercent, bytes.data(), bytes.size(), written));
  CHECK(ns::decodeBookPosition(bytes.data(), written, decoded));
  CHECK(decoded.progressLength == 4 && decoded.percentBasisPoints == ns::UNKNOWN_PERCENT);
}

ns::BookStatsWire sampleBookStats() {
  ns::BookStatsWire stats;
  stats.sessionCount = 12;
  stats.totalReadingSeconds = 18000;
  stats.totalPagesTurned = 420;
  stats.avgSecondsPerForwardPage = 41;
  stats.paceSampleCount = 300;
  stats.estimatedTimeLeftSeconds = 7200;
  stats.startDate = {2026, 9, 1};
  stats.timeOfDaySeconds = {100, 200, 300, 400};
  stats.dayOfWeekSeconds = {1, 2, 3, 4, 5, 6, 7};
  return stats;
}

void bookStatsTravelWithThePosition() {
  ns::BookPositionRecord record;
  setText(record.path, "/Books/Rosa.epub");
  setText(record.title, "Rosa");
  record.progressLength = 10;
  record.hasStats = true;
  record.stats = sampleBookStats();
  record.stats.finishedDate = {2026, 10, 2};
  record.stats.finishedDateManual = true;
  record.stats.isCompleted = true;
  std::array<uint8_t, ns::MAX_BOOK_RECORD_BYTES> bytes{};
  size_t written = 0;
  CHECK(ns::encodeBookPosition(record, bytes.data(), bytes.size(), written));
  ns::BookPositionRecord decoded;
  CHECK(ns::decodeBookPosition(bytes.data(), written, decoded));
  CHECK(decoded == record);
  CHECK(!ns::decodeBookPosition(bytes.data(), written - 1, decoded));
}

void mergingBookStatsNeverDoubleCounts() {
  ns::BookStatsWire sender = sampleBookStats();
  ns::BookStatsWire receiver;
  receiver.sessionCount = 3;
  receiver.totalReadingSeconds = 20000;  // read longer here, fewer sessions
  receiver.totalPagesTurned = 100;
  receiver.avgSecondsPerForwardPage = 55;
  receiver.paceSampleCount = 50;
  receiver.estimatedTimeLeftSeconds = 9999;
  receiver.startDate = {2026, 8, 15};
  receiver.timeOfDaySeconds = {500, 0, 0, 0};

  const ns::BookStatsWire merged = ns::mergeBookStats(receiver, sender);
  CHECK(merged.sessionCount == 12);
  CHECK(merged.totalReadingSeconds == 20000);
  CHECK(merged.totalPagesTurned == 420);
  CHECK(merged.avgSecondsPerForwardPage == 41 && merged.paceSampleCount == 300);  // more samples wins
  CHECK(merged.estimatedTimeLeftSeconds == 7200);                                  // follows the sender
  CHECK((merged.startDate == ns::WireDate{2026, 8, 15}));                          // earlier start
  CHECK((merged.timeOfDaySeconds == std::array<uint32_t, 4>{500, 200, 300, 400}));
  CHECK(!merged.isCompleted && !merged.finishedDate.valid());
  // Idempotent: syncing the same stats again changes nothing.
  CHECK(ns::mergeBookStats(merged, sender) == merged);
  // Nothing locally yet: the receiver just takes the sender's stats.
  CHECK(ns::mergeBookStats(ns::BookStatsWire{}, sender) == sender);

  // A manual date beats an automatic one either way round; completion sticks.
  ns::BookStatsWire manual = sender;
  manual.startDate = {2026, 9, 20};
  manual.startDateManual = true;
  manual.isCompleted = true;
  manual.finishedDate = {2026, 9, 30};
  ns::BookStatsWire automatic = receiver;
  automatic.finishedDate = {2026, 10, 1};
  const ns::BookStatsWire combined = ns::mergeBookStats(automatic, manual);
  CHECK((combined.startDate == ns::WireDate{2026, 9, 20}) && combined.startDateManual);
  CHECK(combined.isCompleted);
  CHECK((combined.finishedDate == ns::WireDate{2026, 10, 1}));  // later of two automatic dates
}

void statsPayloadRuleMatchesStatsSync() {
  std::array<uint8_t, 159> stats{};
  stats[0] = 3;
  CHECK(ns::isValidStatsPayload(stats.data(), 159));
  stats[0] = 1;
  CHECK(ns::isValidStatsPayload(stats.data(), 13));
  CHECK(!ns::isValidStatsPayload(stats.data(), 159));
  stats[0] = 2;
  CHECK(ns::isValidStatsPayload(stats.data(), 17));
  CHECK(!ns::isValidStatsPayload(stats.data(), 20));
}

void offerRoundTripsWithAnyCombinationOfSections() {
  ns::Offer full;
  full.totalBytes = 123456;
  full.chunkBytes = 1024;
  full.flags = ns::OFFER_HAS_POKEMON | ns::OFFER_HAS_STATS | ns::OFFER_HAS_BOOK | ns::OFFER_MOVE_POKEMON;
  full.pokemon.snapshotVersion = 10;
  full.pokemon.storeVersions = {3, 1, 1, 1};
  full.pokemon.leaderSpeciesId = 6;
  full.pokemon.leaderLevel = 55;
  full.pokemon.partyCount = 6;
  full.pokemon.badgeCount = 8;
  full.pokemon.caughtCount = 120;
  setText(full.pokemon.leaderName, "Fiammetta");
  setText(full.bookTitle, "Il nome della rosa");
  full.bookPercentBasisPoints = 4237;
  setText(full.bookPath, "/Books/Il nome della rosa.epub");
  full.bookFileBytes = 3 * 1024 * 1024;
  setText(full.senderName, "X3 di Davide");

  std::array<uint8_t, ns::OFFER_MAX_BYTES> bytes{};
  size_t written = 0;
  CHECK(ns::encodeOffer(full, bytes.data(), bytes.size(), written));
  CHECK(ns::hasOfferTag(bytes.data(), written));
  ns::Offer decoded;
  CHECK(ns::decodeOffer(bytes.data(), written, decoded));
  CHECK(decoded == full);
  CHECK(!ns::decodeOffer(bytes.data(), written - 1, decoded));
  CHECK(!ns::decodeOffer(bytes.data(), written + 1, decoded));
  CHECK(ns::decodeOffer(bytes.data(), written, decoded));
  CHECK(ns::totalWithBookFile(decoded) == 123456ULL + ns::SECTION_ENTRY_BYTES + 3 * 1024 * 1024);

  // A book offer needs an absolute path, and a file within the book limit.
  ns::Offer relativeBook = full;
  setText(relativeBook.bookPath, "Books/x.epub");
  CHECK(!ns::encodeOffer(relativeBook, bytes.data(), bytes.size(), written));
  ns::Offer hugeBook = full;
  hugeBook.bookFileBytes = ns::MAX_BOOK_FILE_BYTES + 1;
  CHECK(!ns::encodeOffer(hugeBook, bytes.data(), bytes.size(), written));

  // Stats only: the Pokemon/book fields are not on the wire at all.
  ns::Offer statsOnly;
  statsOnly.totalBytes = 200;
  statsOnly.chunkBytes = 1024;
  statsOnly.flags = ns::OFFER_HAS_STATS;
  setText(statsOnly.senderName, "X4 Pro");
  CHECK(ns::encodeOffer(statsOnly, bytes.data(), bytes.size(), written));
  CHECK(ns::decodeOffer(bytes.data(), written, decoded));
  CHECK(decoded == statsOnly);

  // Nothing to send, or "move" without a Pokemon save: refused.
  ns::Offer nothing = statsOnly;
  nothing.flags = 0;
  CHECK(!ns::encodeOffer(nothing, bytes.data(), bytes.size(), written));
  ns::Offer moveWithoutSave = statsOnly;
  moveWithoutSave.flags = ns::OFFER_HAS_STATS | ns::OFFER_MOVE_POKEMON;
  CHECK(ns::encodeOffer(moveWithoutSave, bytes.data(), bytes.size(), written));
  CHECK(!ns::decodeOffer(bytes.data(), written, decoded));
}

void resultCarriesTheReceiversStats() {
  ns::Result result;
  result.status = ns::RESULT_OK;
  result.deviceMac = {9, 8, 7, 6, 5, 4};
  result.statsLength = 17;
  result.stats[0] = 2;
  std::array<uint8_t, ns::RESULT_MAX_BYTES> bytes{};
  size_t written = 0;
  CHECK(ns::encodeResult(result, bytes.data(), bytes.size(), written));
  ns::Result decoded;
  CHECK(ns::decodeResult(bytes.data(), written, decoded));
  CHECK(decoded.status == ns::RESULT_OK && decoded.deviceMac == result.deviceMac && decoded.statsLength == 17);
  CHECK(!ns::decodeResult(bytes.data(), written - 1, decoded));

  const uint8_t bare = ns::RESULT_FAILED;
  CHECK(ns::decodeResult(&bare, 1, decoded));
  CHECK(decoded.status == ns::RESULT_FAILED && decoded.statsLength == 0);

  ns::Result badStats = result;
  badStats.stats[0] = 3;  // a v3 payload must be 159 bytes
  CHECK(!ns::encodeResult(badStats, bytes.data(), bytes.size(), written));
}

}  // namespace

int main() {
  crcMatchesStandardCrc32();
  tableRoundTripsAndLocatesSections();
  bookPositionRoundTripsAndRejectsBadInput();
  bookStatsTravelWithThePosition();
  mergingBookStatsNeverDoubleCounts();
  statsPayloadRuleMatchesStatsSync();
  offerRoundTripsWithAnyCombinationOfSections();
  resultCarriesTheReceiversStats();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  return 0;
}
