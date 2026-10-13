---
title: Game Boy screen speed vs Ink-boy
parent: Development
nav_order: 7
---

# Game Boy screen speed: this fork vs Ink-boy

This firmware's Game Boy emulator for the X4 Pro started from the
[Ink-boy](https://github.com/hellominecraft913-dev/Ink-boy) CrossInk fork (MIT).
Both run the [Peanut-GB](https://github.com/deltabeard/Peanut-GB) emulator, so
the differences come from how the picture reaches the e-ink screen. This page
explains why Ink-boy's picture comes out in inverted black and white, and what
was changed here to make the screen update about three times as fast.
The changes are written up step by step, with code, in
[Making the Game Boy screen faster](gameboy-speed-guide.md).

Compared against Ink-boy as of 12 October 2026 and this fork's v1.8.2.

## In short

Measured on an X4 Pro whose e-ink controller is a UC8279 (variant 0x68), playing
Pokémon Red, figures from `/.crosspoint/gameboy-perf.txt`:

| | Screen refresh | Screen updates per second |
| --- | --- | --- |
| Stock refresh, as Ink-boy drives it | ~600 ms | ~1.3 |
| This fork, v1.8.1 | ~276 ms | ~3 |
| This fork, v1.8.2 | ~190 ms | 4-5 while the picture changes |

The emulator itself runs at full Game Boy speed (~60 frames a second) in both;
only the e-ink screen limits how smooth a game looks.

## Why Ink-boy's black and white are inverted

Peanut-GB hands the front end each line of the picture as shades 0 to 3, the
Game Boy's own palette values. On the Game Boy, **0 is white and 3 is black**
(1 and 2 are light and dark grey; see
[Pan Docs, BGP](https://gbdev.io/pandocs/Palettes.html)).

Ink-boy draws it the other way round
([GameBoyActivity.cpp](https://github.com/hellominecraft913-dev/Ink-boy/blob/main/src/activities/gameboy/GameBoyActivity.cpp)):

```cpp
// 4-shade line buffer to CrossInk BW: shades 0/1 are black, 2/3 are white.
...
if (shade >= 2) continue;  // 2 and 3 stay white, 0 and 1 are drawn black
```

So white backgrounds come out black and black text comes out white. It also
cuts the four shades down to two, so both greys are lost.

This fork draws shade 0 as white and 3 as black, and the two greys as dot
patterns (`shadeIsBlack()` in
[GameBoyActivity.cpp](../../src/activities/gameboy/GameBoyActivity.cpp)).

Ink-boy's newer Game Boy Color path (Walnut-CGB) documents its own output as
"0 (black), 3 (white)", which suits that path, but Peanut-GB's original Game
Boy output uses the opposite order.

## What makes the screen faster here

### In the game screen (every X4 Pro)

| | Ink-boy | This fork | Effect |
| --- | --- | --- | --- |
| Where the emulator runs | In the main loop, on the same CPU core as the screen refresh | In its own task on the other core (core 0) | Streaming a picture to the screen is no longer slowed down ~3x by the emulator |
| The Game Boy frame being shown | Held until the e-ink refresh ends | Handed back to the emulator as soon as it is drawn | The emulator never waits for the screen |
| A screen update asked for during a refresh | Lost (the request flag is cleared after the refresh) | Kept; the next update starts right away | No wait for the next 250 ms tick |
| How often the screen may update | At most every 250 ms | As soon as the last picture is on screen (fast refresh only) | The rate follows the screen's real speed |
| Drawing the game picture | Clears the screen and redraws everything, pixel by pixel | Builds each row a byte at a time from dither tables; title and buttons are redrawn only when they change | ~34 ms down to ~10 ms per picture |
| Drawing during a refresh | No: draw, then wait for the refresh | The next picture is drawn while the screen is still refreshing (fast refresh only) | Drawing time is hidden |

### In the e-ink driver (UC8279 variant 0x68 only)

These need changes to the display driver, so this fork carries its own copy of
[freeink-sdk](https://github.com/Free-Ink/freeink-sdk) (`freeink-sdk/`, every
local edit marked `// xteink-pokemon:`). Ink-boy uses the stock driver.

| | Stock (Ink-boy) | This fork | Effect |
| --- | --- | --- | --- |
| Waveform | The panel's built-in fast waveform | The X3 UC8279's fast (DU) waveform loaded by the reader, every phase shortened to 40% | Waveform ~540 ms down to ~200 ms |
| Frame rate (PLL) | 0x0E (~20 ms a frame) | 0x0F (~11.5 ms a frame) | Part of the same gain |
| Display link (SPI) | 10 MHz | 20 MHz (the controller's rating) | Each picture is sent twice as fast |
| What is sent and refreshed | The whole screen, as two full pictures (new and old) | Only the rectangle that changed; a picture that did not change is not refreshed at all | ~74 ms of sending down to ~18 ms, and a shorter waveform (~166 ms) |
| Ghosting | - | A full cleaning refresh every minute | Light ghosting that does not build up |

Together: about 600 ms per update with the stock refresh, about 190 ms in v1.8.2.

## How it was found

The speed was measured, not guessed. Each game session writes a line to
`/.crosspoint/gameboy-perf.txt` with the emulator's speed, the screen updates
per second, and how long each part of a screen update took (drawing, sending the
new picture, setting up, the waveform, sending the old picture). Test builds
(1.8.1-beta.1 to beta.7, 1.8.2-beta.1 to beta.4) tried one change at a time on a
real X4 Pro and kept what helped:

- Forcing another temperature on the panel (TSSET above 0x5A) made it slower
  and flash: dropped.
- Shortening the waveform below ~35-40% left visible ghosting: 40% was kept.
- Scanning only the changed rows (the PT_SCAN bit) made no measurable
  difference: dropped.

## Limits

- The driver changes are only switched on for the UC8279 variant 0x68 screen,
  the one they were tested on. X4 Pro batches also ship SSD1677 and UC8179
  controllers; those keep the stock refresh (and the 250 ms limit) until someone
  tests them, but still get the game-screen fixes above.
- About 5 updates a second is close to this panel's limit. Going further needs
  an even shorter waveform, which brings back ghosting.
- Ink-boy now also plays Game Boy Color (`.gbc`) games through Walnut-CGB; this
  fork plays original Game Boy (`.gb`) games only.
