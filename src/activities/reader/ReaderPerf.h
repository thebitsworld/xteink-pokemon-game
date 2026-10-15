#pragma once

#include <AppVersion.h>
#include <Arduino.h>
#include <HalStorage.h>

#include <cstdio>

// Page-turn timings of the EPUB reader, summed while a book is open and
// written as one line to /.crosspoint/reader-perf.txt when it closes (like
// the Game Boy's gameboy-perf.txt), so the reader's speed can be measured on
// a device: how long a page takes to compose, the B/W (or grayscale base)
// refresh, the two gray planes' render and the gray refresh with the B/W
// restore. Phases that did not run on a turn count as 0.
namespace ReaderPerf {

enum Phase : uint8_t { Compose, Base, GrayRender, GrayShow, PhaseCount };

struct Totals {
  uint32_t pages = 0;
  uint32_t grayPages = 0;
  uint64_t us[PhaseCount] = {};
  uint64_t totalUs = 0;
  uint32_t maxUs = 0;
};

inline Totals& totals() {
  static Totals t;
  return t;
}

// One page turn: the time of each phase, in microseconds.
inline void addPage(const uint32_t (&phaseUs)[PhaseCount], const bool gray) {
  Totals& t = totals();
  uint32_t total = 0;
  for (int i = 0; i < PhaseCount; ++i) {
    t.us[i] += phaseUs[i];
    total += phaseUs[i];
  }
  ++t.pages;
  if (gray) ++t.grayPages;
  t.totalUs += total;
  if (total > t.maxUs) t.maxUs = total;
}

// Writes the book's line (nothing for fewer than 3 pages) and starts over.
inline void flush(const char* panel, const uint32_t spiHz) {
  Totals& t = totals();
  if (t.pages >= 3) {
    const float n = static_cast<float>(t.pages);
    char line[200];
    const int len = snprintf(line, sizeof(line), "%s,%s,%lu,%lu,%lu,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f\n",
                             AppVersion::version(), panel, static_cast<unsigned long>(spiHz / 1000),
                             static_cast<unsigned long>(t.pages), static_cast<unsigned long>(t.grayPages),
                             t.totalUs / 1000.0f / n, t.maxUs / 1000.0f, t.us[Compose] / 1000.0f / n,
                             t.us[Base] / 1000.0f / n, t.us[GrayRender] / 1000.0f / n, t.us[GrayShow] / 1000.0f / n,
                             t.grayPages ? (t.us[GrayRender] + t.us[GrayShow]) / 1000.0f / t.grayPages : 0.0f);
    static constexpr char kPath[] = "/.crosspoint/reader-perf.txt";
    static constexpr char kHeader[] =
        "# version,panel,spi_khz,pages,gray_pages,page_ms_avg,page_ms_max,compose_ms,base_ms,gray_render_ms,"
        "gray_show_ms,gray_ms_per_gray_page\n";
    if (len > 0 && static_cast<size_t>(len) < sizeof(line)) {
      HalFile file = Storage.open(kPath, O_WRONLY | O_CREAT | O_APPEND);
      if (file) {
        if (file.size() == 0) file.write(kHeader, sizeof(kHeader) - 1);
        if (file.size() < 64 * 1024) file.write(line, static_cast<size_t>(len));
        file.close();
      }
    }
  }
  t = Totals{};
}

}  // namespace ReaderPerf
