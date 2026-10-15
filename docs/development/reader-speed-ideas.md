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

## Status (2026-10-15) and the plan for every display controller

**Shelved on 2026-10-15** at the user's request; nothing of it is in a release. Step 1 was built and
tested as `v1.8.7-beta.1-x4pro` (beta and tag deleted). Its code stays on the branch
`feat/reader-speed-1` on GitHub, not merged: `src/activities/reader/ReaderPerf.h` (writes
`/.crosspoint/reader-perf.txt`), a 20 MHz SPI clock on the UC8279-0x68 only (`Uc8279X4Driver::spiHz`),
and the two redundant UC8279 DTM1 writes dropped (`flushDeferredBase`, `_baseRestored`; for every
UC8279 variant of the X4 Pro).

What the tester's X4 Pro (UC8279-0x68, 20 MHz) measured over 40 page turns, 8 of them with the gray
(anti-aliasing) pass: 897 ms per turn on average, 3721 ms at most (a new chapter or an image page);
compose 127 ms, base refresh 676 ms (HALF cleanups every few pages included), and 470 ms more on each
gray page (156 ms render, 313 ms show). So the panel's waveforms dominate - at 20 MHz a 60 KB plane
takes ~25 ms - and the SPI/redundant-write work gains little on B/W turns. Only 8 of 40 turns got the
gray pass because CrossInk skips it on pages passed through while turning quickly (and for
monochrome-raster SD fonts). No v1.8.6 baseline was measured (it has no reader-perf.txt). The best
next step remains pre-rendering the next page (item A below): it takes the ~127 ms compose off every
turn on every device.

The goal is speed-ups that are right for **every** controller, not only the tester's panel. Sort each
change by how much it depends on the panel:

- **A - software only (safe on every device):** pre-render the next page (PSRAM boards; X3/X4 when the
  heap allows), keep the section file open between turns, one combined state write on open,
  incremental indexing for a cold chapter, no framebuffer loan on PSRAM boards.
- **B - fewer bytes, same panel input (prove it, then enable for every batch):** the SDK's host tests
  (`freeink-sdk/libs/display/FreeInkDisplay/test/host/run_pro.py`, `run_uc8279.py`) drive the real
  drivers against a recording `EpdBus`. Add a controller RAM model: apply every write (including
  PTIN/PTL windows) to two plane images, and at every refresh command snapshot both planes, the LUTs and
  the registers. Run the same reader sequences (page turns, image pages, HALF cleanup, sleep, Game Boy)
  through the old and the new driver code: identical snapshots mean the panel gets exactly what it got
  before, so the change is safe on every batch of that controller. Then: UC8279 X4 (the 120 KB already
  dropped; also stop rewriting the 120 off-screen gate rows, ~20% of every plane), UC8179, SSD1677
  (X4/X4 Pro: defer the post-refresh BW+RED rewrite), UC8253/UC8279 on the X3.
- **C - electrical/timing (SPI clock, waveforms, PLL): only on tested panels.** Keep the allowlist by
  controller + variant (today 20 MHz on UC8279-0x68 only); maybe an experimental Settings switch
  ("Display bus 20 MHz", off by default on untested batches) so owners of other batches can try and
  report.

Order: (1) fix the host-test stubs - they no longer build since our own SDK edits (`micros`,
`heap_caps_free`, `EpdBus::spiHz/setSpiHz` are missing from `test/host/pro_stubs`), then add the RAM
model; (2) prove the UC8279 change and drop any variant limit on it; (3) the A items; (4) B for the
other drivers one by one; (5) widen C only with testers of other batches.

