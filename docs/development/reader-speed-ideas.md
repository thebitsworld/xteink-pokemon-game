# Reader speed: ideas to do later

Where time goes when opening a book and turning a page, and what could make it faster. From a
read-through of the code on 2026-10-15; nothing here is measured yet (release builds compile the
`LOG_DBG` timings out). Line numbers drift: search for the function names.

## Where the time goes

- **Cold open of a chapter**: the ZIP inflate of the chapter to `html/N.html` on the SD card, then
  the XML parse and layout of the whole chapter behind the "Indexing" popup
  (`INDEXING_FULL_SECTION` is the default; `INDEXING_INCREMENTAL` builds pages up to the one shown).
- **Warm open**: many small SD reads and writes in `EpubReaderActivity::onEnter` (bookmarks,
  clippings, progress, stats, `APP_STATE` and `RECENT_BOOKS` saves), and a first page that is always
  a HALF refresh with a grayscale pass.
- **Page turn on the X4 Pro (UC8279, anti-aliased text, the default)**: the page is rendered three
  times (BW, then the two gray planes; nothing pre-renders the next page), then about 420 KB go to
  the panel at 10 MHz - ~340 ms of SPI before the two waveforms. About 120 KB of that is written
  twice: `transitionGrayscaleBase` restores DTM1 that `copyGrayscaleLsb` overwrites next, and
  `cleanupGrayscaleBuffers` rewrites DTM1 that `displayGray` has just restored. 20% of every
  plane is the 120 off-screen gate rows written white.
- **X3/X4 (SSD1677, UC8253)**: the gray planes are rendered in 80-row strips (12-14 traversals of
  the page), and the overlapped refresh the SDK measured (1274 -> 822 ms) is mostly lost on the C3
  because two 48 KB plane buffers rarely fit.

## Ideas, best value first

| # | Idea | Expected gain | Risk | Devices |
|---|---|---|---|---|
| 1 | Display SPI at 20 MHz in the reader (the Game Boy already runs the UC8279-0x68 at 20 MHz) | ~170 ms per AA turn | low | X4 Pro |
| 2 | Drop the two redundant UC8279 plane writes | ~96 ms per turn | low | X4 Pro |
| 3 | Log each step (render passes, `lastRefreshTimings`) to `/.crosspoint/reader-perf.txt` | real numbers | none | all |
| 4 | Pre-render the next page's BW and gray planes into PSRAM while reading | 100 ms+ | medium | X4 Pro |
| 5 | Overlapped refresh for the UC8279/UC8179 (render while the waveform runs) | up to a few 100 ms | medium | X4 Pro, X4 |
| 6 | Open: no framebuffer loan on PSRAM boards, one combined state write, incremental indexing on the C3 | 100s of ms on a cold chapter | low-medium | all |
| 7 | X3/X4: defer the SSD1677 post-refresh BW+RED rewrite; taller gray strips when the heap allows | tens of ms to ~80 ms | medium | X3/X4 |

DMA on the display bus was tried for the Game Boy in 1.8.6-beta.1/2 and gave ~5% (the time goes
in preparing the planes, not on the wire), so it is not worth bringing to the reader.

Suggested order: 3 + 1 + 2 in one X4 Pro beta, then 4 + 5 from its numbers, then 6, then 7 (which
needs X3 builds and tests).

Key files: `src/activities/reader/EpubReaderActivity.cpp` (`renderContents`, `pageTurn`,
`idlePrewarmNextPage`), `src/activities/reader/EpubGrayscale.cpp`, `src/activities/reader/ReaderUtils.h`,
`lib/Epub/Epub/Section.cpp`, `lib/GfxRenderer/GfxRenderer.cpp`,
`freeink-sdk/libs/display/FreeInkDisplay/src/driver/Uc8279X4Driver.cpp`.
