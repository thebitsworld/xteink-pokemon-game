---
title: Faster Game Boy screen (guide)
parent: Development
nav_order: 8
---

# Making the Game Boy screen faster on the X4 Pro

A step-by-step account of the changes that took this fork's Game Boy screen on
the X4 Pro from about 1.3 to about 4-5 updates a second, written so that
[Ink-boy](https://github.com/hellominecraft913-dev/Ink-boy) (where this fork's
Game Boy screen comes from) or any other CrossInk fork can do the same. A
shorter comparison is in [Game Boy screen speed vs Ink-boy](gameboy-vs-ink-boy.md).

All code referred to here is MIT-licensed, like Ink-boy, CrossInk and
freeink-sdk, and may be copied. Links point at the
[v1.8.2](https://github.com/thebitsworld/xteink-pokemon-game/tree/v1.8.2)
release:

- Game screen: [`src/activities/gameboy/GameBoyActivity.cpp`](https://github.com/thebitsworld/xteink-pokemon-game/blob/v1.8.2/src/activities/gameboy/GameBoyActivity.cpp)
  and [`.h`](https://github.com/thebitsworld/xteink-pokemon-game/blob/v1.8.2/src/activities/gameboy/GameBoyActivity.h)
- E-ink driver: [`freeink-sdk/libs/display/FreeInkDisplay/src/driver/Uc8279X4Driver.cpp`](https://github.com/thebitsworld/xteink-pokemon-game/blob/v1.8.2/freeink-sdk/libs/display/FreeInkDisplay/src/driver/Uc8279X4Driver.cpp)
  (every local change is marked `// xteink-pokemon:`)
- Display plumbing: [`lib/hal/HalDisplay.h`](https://github.com/thebitsworld/xteink-pokemon-game/blob/v1.8.2/lib/hal/HalDisplay.h),
  [`FreeInkDisplay.cpp`](https://github.com/thebitsworld/xteink-pokemon-game/blob/v1.8.2/freeink-sdk/libs/display/FreeInkDisplay/src/FreeInkDisplay.cpp),
  [`EpdBus.h`](https://github.com/thebitsworld/xteink-pokemon-game/blob/v1.8.2/freeink-sdk/libs/display/FreeInkDisplay/src/bus/EpdBus.h),
  [`PanelDriver.h`](https://github.com/thebitsworld/xteink-pokemon-game/blob/v1.8.2/freeink-sdk/libs/display/FreeInkDisplay/src/driver/PanelDriver.h)

## What was measured

On an X4 Pro with a UC8279 e-ink controller (variant byte 0x68), playing Pokémon
Red upright at 3x, each step added to the ones before it:

| Step | One screen refresh | Screen updates per second |
| --- | --- | --- |
| Stock Fast refresh (as Ink-boy drives it) | ~590 ms | ~1.3 |
| + shorter waveform, faster frame rate (B1, B2) | ~340 ms | ~2.3 |
| + 20 MHz display link (B3) | ~276 ms | ~3.0 |
| + only the changed rectangle (B4) | ~190 ms | ~2.9 (held back by the 250 ms period) |
| + no 250 ms period, faster drawing (A4, A5) | ~190 ms | ~4.1 |
| + drawing during the refresh (A6) | ~190 ms | about the same (~4.2); saves the ~10 ms of drawing |

The emulator itself runs at full speed (~60 frames a second) throughout. Updates
per second depend on the game: a still picture is skipped (B4), so compare runs
of the same scene.

The order matters: A1-A3 do not make the screen faster on their own, but without
them the driver changes are partly wasted (the refresh streams slower, or
updates are lost).

## 0. Fix the inverted picture first

Peanut-GB passes each line as the Game Boy's palette values in bits 1-0:
**0 is white, 3 is black**, 1 and 2 are light and dark grey
([Pan Docs](https://gbdev.io/pandocs/Palettes.html)). Ink-boy's
`drawGameFrame()` draws 0/1 as black and 2/3 as white, so the picture comes out
inverted. Draw 3 and 2 dark instead, and keep the greys as dot patterns:

```cpp
// Peanut-GB shade (0 white .. 3 black) -> black at panel pixel (x, y)?
bool shadeIsBlack(const uint8_t shade, const int x, const int y) {
  switch (shade) {
    case 3: return true;
    case 2: return ((x ^ y) & 1) == 0 || (x & 1) == 0;  // dark grey: 3 of 4
    case 1: return (x & 1) == 0 && (y & 1) == 0;        // light grey: 1 of 4
    default: return false;                              // white
  }
}
```

(Ink-boy's Walnut-CGB path for `.gbc` emits 0 = black, 3 = white on purpose,
so keep its conversion separate from Peanut-GB's.)

## A. In the game screen (any X4 Pro, no driver change)

### A1. Measure

Log what each part costs before changing anything. This fork appends one line
per game session to `/.crosspoint/gameboy-perf.txt` (`writePerfLog()`):
emulator frames per second, screen updates per second, drawing time, refresh
time, and with the driver changes below the refresh split into its phases
(sending the new picture, setup, waveform, sending the old picture). Every
decision below came from these numbers on a real X4 Pro; the simulator does not
model e-ink timing.

### A2. Run the emulator on the other CPU core

Ink-boy runs `runEmulation()` in `loop()`, on the same core as the render task
that streams the picture to the panel. Measured here, sharing the core made
streaming a picture about three times slower. Give the emulator its own
FreeRTOS task on core 0:

```cpp
void GameBoyActivity::startEmuTask() {
  emuRun.store(true, std::memory_order_release);
  emuStopped.store(false, std::memory_order_release);
  xTaskCreatePinnedToCore(&GameBoyActivity::emuTaskEntry, "GameBoyEmu", 12288, this, 1, &emuTask, 0);
}

void GameBoyActivity::emuTaskLoop() {
  while (emuRun.load(std::memory_order_acquire)) {
    runEmulation(nowMicros());
    // autosave timer, periodic cleaning refresh timer (B5) ...
    delay(1);  // lets core 0's idle task (and its watchdog) run
  }
}
```

`loop()` then only handles input and the display timer and ends with `delay(2)`.
Stop the task (and wait for it) at the top of `onExit()`, before saving.

### A3. Hand the frame back before the refresh, and do not lose requests

Two small bugs in the handoff cost speed:

- `render()` keeps its claim on the Game Boy frame (`renderFrame`) until
  `displayBuffer()` returns, hundreds of milliseconds later. With both of its
  buffers held, the emulator stands still for the whole refresh. Release the
  claim right after `drawGameFrame()` has copied the frame into the
  framebuffer (`releaseRenderedFrame()`).
- `render()` clears `displayDue` at its end. A request that `loop()` made
  during the refresh is wiped, and the screen waits for the next period.
  Clear it at the start instead:

```cpp
void GameBoyActivity::render(RenderLock&&) {
  if (!displayDue.exchange(false, std::memory_order_acq_rel)) return;
  ...
}
```

### A4. Draw faster

Drawing the game pixel by pixel, after clearing the whole screen and redrawing
every button, took ~34 ms per picture here. Two changes brought it to ~10 ms:

- **Whole bytes from tables.** The dither depends only on pixel parity, so the
  panel rows for one game line (landscape) or game column (upright) differ only
  by whether their row is even or odd. Build those two rows a byte at a time
  from a small table of `scale`-bit patterns per shade, then `memcpy` them into
  the framebuffer (`buildGameRow()`, `gamePatterns()`,
  `drawGameFrameLandscape()`, `drawGameFramePortrait()`). The output was
  checked to be byte-identical to the per-pixel version.
- **Keep the buttons.** The X4 Pro builds use `EINK_DISPLAY_SINGLE_BUFFER_MODE`,
  so the framebuffer still holds the last picture when `render()` runs. Clear
  the screen and redraw the title and buttons only when something they show
  changed (pressed buttons, the touched control, the orientation), and with
  every cleaning refresh (`chromeKey` in `render()`).

### A5. No fixed 250 ms period, once the refresh is fast

`DISPLAY_PERIOD_US = 250000` caps the screen at 4 updates a second. Keep it for
the stock refresh, which cannot go faster anyway. With the fast refresh (part
B), ask for the next picture 50 ms after the last request: thanks to A3, a
request made during a refresh starts the next one as soon as it ends, and the
50 ms only paces redraws of a still picture, which B4 skips.

### A6. Draw the next picture while the waveform runs

With B4 in place, the driver finishes a refresh from its own copy of the frame
(see B4), so the framebuffer is free as soon as the refresh has started. Start
the refresh without waiting (`FreeInkDisplay::displayBufferAsyncNoShadow()`,
which first finishes the refresh before) and draw the next picture meanwhile.
Take the SPI bus lock around the call: the SD card shares the bus
(`HalDisplay::displayBufferOverlapped()` and `finishOverlapped()`). Wait for the
last refresh before leaving the game or changing the driver settings. The gain
is the drawing time, about 10 ms per update.

## B. In the e-ink driver (UC8279 X4 panel)

Ink-boy, like CrossInk, uses freeink-sdk as a git submodule. These changes are
inside `Uc8279X4Driver`, so they need either a fork of freeink-sdk or a copy of
it in the repository (this fork copies it with `git subtree`). They are only
worth trying on a UC8279 X4 panel: the X4 Pro also ships with SSD1677 and
UC8179 controllers, whose drivers differ. The boot probe reports which one a
reader has (`BoardConfig::ACTIVE.displayController` and
`displayControllerVariant`).

**Turn these on only for the game, only on the panel they were tested on, and
put everything back when the game closes.** In this fork:

```cpp
bool fastRefreshValidated() {
  return BoardConfig::ACTIVE.displayController == BoardConfig::DisplayController::UC8279 &&
         BoardConfig::ACTIVE.displayControllerVariant == 0x68;
}
```

They were validated on one 0x68 unit by its owner over eleven test builds. They
drive the panel outside its factory settings (shorter waveform, higher frame
rate), so a new panel variant should be tried in a test build first.

### B1. A shorter waveform, loaded by the reader

The stock Fast refresh runs the panel's built-in (OTP) waveform: ~540 ms on this
panel. freeink-sdk already carries the X3 UC8279's B/W fast (DU) waveform
(`kUc8279X3_BwDu` in `lut/Uc8279X3Luts.h`). Load it into the LUT registers for
game refreshes with every phase's frame count scaled down, and keep the PSR
register's REG bit set so the panel uses the loaded LUT instead of its OTP one:

```cpp
// Groups of 7 bytes: repeat, four phase bytes (bits 7-6 drive level, bits 5-0
// frames), two trailing bytes. Only phase bytes longer than one frame scale;
// drive levels (voltages) stay as they are.
void Uc8279X4Driver::loadScaledDuLut(EpdBus& bus, const uint8_t percent) {
  for (size_t t = 0; t < 5; ++t) {
    uint8_t table[42];
    memcpy(table, &kUc8279X3_BwDu[t][1], sizeof(table));
    for (size_t i = 0; i < sizeof(table); ++i) {
      const size_t pos = i % 7;
      if (pos < 1 || pos > 4) continue;
      const uint8_t frames = table[i] & 0x3F;
      if (frames <= 1) continue;
      int scaled = (frames * percent + 50) / 100;
      scaled = scaled < 1 ? 1 : (scaled > 0x3F ? 0x3F : scaled);
      table[i] = static_cast<uint8_t>((table[i] & 0xC0) | scaled);
    }
    bus.cmd(kUc8279X3_BwDu[t][0]);
    bus.data(table, sizeof(table));
  }
}
// in startBwRefresh(): if (externalDu) loadScaledDuLut(bus, percent);
// and later: PSR byte 0 = externalDu ? psr0 : (psr0 & 0xDF)   (REG kept / cleared)
```

40% was the best trade: darker and with less ghosting than 30% or 20%, and
much faster than 50% and up.

### B2. A faster frame rate for game refreshes

Write PLL (`0x30`) = `0x0F` instead of the panel's `0x0E` for game refreshes
(about 11.5 ms a frame instead of 20.5 ms with the loaded waveform), and write
the stock value back before any other refresh. The PLL setting made no
difference to the built-in waveform, only to the loaded one.

Forcing another temperature (TSSET above `0x5A`) was also tried: it made the
panel slower and flash. Leave TSSET alone.

### B3. A 20 MHz display link

The driver runs the panel's SPI at 10 MHz; the controller is rated for 20 MHz.
Raising it for the game halves the time to send a picture (~64 to ~37 ms per
plane, two planes per refresh). freeink-sdk sets the clock once at start, so add
a way to change it with no refresh running:

```cpp
// EpdBus
void setSpiHz(uint32_t hz) { _spiHz = hz; _spi = SPISettings(hz, MSBFIRST, SPI_MODE0); }
// FreeInkDisplay: 0 = the driver's own clock
void FreeInkDisplay::setDisplaySpiHz(uint32_t hz) { _bus.setSpiHz(hz ? hz : _driver->spiHz()); }
```

No noise or broken pictures were seen at 20 MHz on the test unit.

### B4. Only the rectangle that changed

A UC8279 refresh streams the new picture into one RAM plane (DTM2), runs the
waveform, then streams the same picture into the other plane (DTM1) so the next
refresh can tell what changed: two full 60 KB planes for each update. Between
two Game Boy frames usually only part of the game area changes.

The driver keeps a copy of the frame that both RAM planes hold (it already had
a framebuffer-sized `_grayBase` snapshot for grayscale). For each game refresh:

1. Compare the new framebuffer with the copy, row by row, and find the
   rectangle that changed (rows, and bytes within a row).
2. If nothing changed, skip the refresh entirely.
3. Otherwise copy that rectangle into the snapshot; then, in partial mode,
   set the window to the rectangle (PTIN `0x91`, PTL `0x90` with the 120-gate
   offset this panel needs, x byte-aligned) and stream only the rectangle into
   DTM2; PTOUT `0x92`.
4. Start the waveform with the same window (the panel then drives only those
   pixels).
5. After the waveform, still in partial mode, stream the rectangle into DTM1
   from the snapshot, then PTOUT.

Any other refresh path that touches the planes (full refreshes, grayscale
pages, sleep) marks the copy invalid, and the next full refresh rebuilds it
(`_diffRefValid`). With a full-screen refresh in game mode, sync DTM1 from the
snapshot rather than the framebuffer so that the two planes and the copy always
match. Code: `findChangedWindow()`, `streamWindow()`, `sendPartialWindow()` and
the `_diffWindow` branch of `displayStart()` / `displayFinish()`.

Sending dropped from ~74 ms to ~18 ms per update (~14 KB per plane), and the
waveform itself got shorter (~198 to ~166 ms) with the smaller window. Scanning
only the window's gate rows (PTL's last byte, PT_SCAN, set to 0) made no further
difference and was left at the stock 0x01.

### B5. A cleaning refresh every minute

The short waveform leaves a little ghosting, which adds up. Every 60 seconds the
game asks for a Half refresh (the driver's full-screen scrub), and once when the
fast settings are switched on. The owner of the test unit found the remaining
ghosting acceptable.

## Where the limit is

At ~190 ms a refresh, of which ~166 ms is the waveform, about 5 updates a second
is close to what this panel can do. More speed would need an even shorter
waveform, which brings back ghosting. The e-ink Game Boy demos that reach 24-30
frames a second use different panels and raw parallel drivers, not this
controller.
