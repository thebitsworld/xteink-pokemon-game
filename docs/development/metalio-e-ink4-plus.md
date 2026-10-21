---
title: Metalio E-INK4 Plus (port notes)
parent: Development
nav_order: 9
---

# Metalio E-INK4 Plus: notes for a possible port

Research notes, collected 21 October 2026, on whether this firmware - and in
particular its Game Boy screen - could run on the Metalio E-INK4 Plus. Nothing
here has been built or measured on the device yet. Figures marked *estimate*
are back-of-the-envelope numbers, not measurements.

## The device

The E-INK4 Plus is the 4.7" sibling of the 3.97" Metalio E-Ink 4, made by
Shenzhen CloudZao. Its firmware is open source (MIT):
[CloudZao/Metalio-E-INK4-Plus](https://github.com/CloudZao/Metalio-E-INK4-Plus).
Hardware, from that README and shop listings:

| | Metalio E-INK4 Plus |
| --- | --- |
| SoC | ESP32-S31 (RISC-V, 320 MHz high-performance core + low-power core) |
| Flash / PSRAM | 16 MB flash; Octal PSRAM at 200 MHz (size not published, see below) |
| Panel | ED047TC2, 4.7", 1216x684 native (684x1216 portrait), 297 PPI, 16 greys |
| Panel drive | **Parallel 8-bit, no controller**: EPDiy + TPS65185 PMIC |
| Touch | FT6336U capacitive, plus virtual keys on the cover glass |
| Front light | Cool + warm channels |
| Audio | I2S mic and speaker through an external Bluetooth audio chip (UART AT) |
| Sensors | SC7A20H 3-axis accelerometer, PCF8563 RTC, BQ27220 fuel gauge |
| Storage | microSD (SDMMC 4-bit), 16 GB card included |
| Battery | 2000 mAh |
| Size / weight | 122 x 71 x 7.8 mm, about 100 g |
| Price | about US$99-106 |

Its SDK target is ESP-IDF v6.1 with `esp32s31`; it is a different chip from the
ESP32-S3 in the X4 Pro, so S3 binaries do not run on it. CrossPoint does not
support it yet ([crosspoint-reader#3648](https://github.com/crosspoint-reader/crosspoint-reader/issues/3648));
the 3.97" E-Ink 4 (ESP32-S3, SPI panel) is supported.

**PSRAM size.** Neither the README nor `sdkconfig.defaults.esp32s31` states it
(ESP-IDF detects it at boot). Shops list the module as "S31-NRV16"; in
Espressif's module names (e.g. ESP32-S31-WROOM-1-N16R16V) R16 means 16 MB PSRAM,
so 16 MB is likely but unconfirmed. **Settings > About** on the device shows it,
as does the boot log.

### Compared with the X4 Pro

| | Metalio E-INK4 Plus | Xteink X4 Pro |
| --- | --- | --- |
| Screen | 4.7", 1216x684, 297 PPI | 4.3", 800x480, ~219 PPI |
| Panel drive | Parallel, CPU drives the raw panel (EPDiy) | SPI to a controller (UC8279 / SSD1677 / UC8179) |
| SoC | ESP32-S31, 320 MHz RISC-V | ESP32-S3, 240 MHz Xtensa LX7 x2 |
| PSRAM | Octal, likely 16 MB | 8 MB |
| Battery | 2000 mAh | 1100 mAh |
| Size / weight | 122 x 71 x 7.8 mm, ~100 g | 111 x 69 x 5.95 mm, 72 g |
| Extras | Mic, speaker, accelerometer, cool/warm light | Magnetic back, pogo charging |

## Why the panel matters for the Game Boy

On the X4 Pro every speed-up in this fork works *around* the UC8279 controller:
it only accepts image planes and a waveform table, takes no RAM writes during a
refresh, and has a fixed ~36 ms overhead per scan. That is why the D presets top
out at 4-5 updates a second and pulse drive at about 10 scans a second
([Game Boy screen speed](gameboy-vs-ink-boy.md), and the pulse notes in
`CLAUDE.md`).

The E-INK4 Plus has no controller. The CPU clocks the panel's source and gate
lines itself through EPDiy, so it decides the voltage of every pixel on every
scan. This is the situation PaperBoy and Caster were designed for.

### Reference designs

- **Caster / Glider** (Wenting Zhang, Modos): an open-source FPGA e-ink
  controller that scans parallel panels at up to 60 Hz with per-pixel state, so
  a pixel starts changing on the next frame instead of waiting for a whole-screen
  waveform. Runs on a Spartan-6 with DDR3; 60 Hz is the *scan* rate - each pixel
  still needs several scans to settle, so motion shows trails.
  [Glider](https://github.com/Modos-Labs/Glider), [Caster](https://github.com/Modos-Labs/Caster).
- **PaperBoy** (same author): a Game Boy on a raw parallel panel with per-pixel
  counters. [gitlab.com/zephray/paperboy](https://gitlab.com/zephray/paperboy).
- **InkBoy** ([kuslota/InkBoy](https://github.com/kuslota/InkBoy), MIT; not the
  same project as Ink-boy, which this fork's Game Boy screen started from): about
  30 updates a second on a Waveshare ESP32-S3 board with a 200x200 SSD1681 panel.
  It borrows PaperBoy's idea but has to squeeze it through the controller: a
  per-pixel pulse budget, a one-frame waveform (~14-20 ms a scan), the panel
  supply left on between scans, 20 MHz SPI, and the per-pixel planner split over
  both cores. See its
  [How it works](https://github.com/kuslota/InkBoy/blob/main/docs/how-it-works.md).
  Its pulse budgets resemble this fork's Pulse 8x3; the difference is a 200x200
  panel with a one-frame waveform versus a UC8279 with ~36 ms fixed per scan.

### What a port could reach (estimate)

- **Planning cost.** InkBoy plans 40,000 panel pixels per scan and needs both
  cores for grayscale. On the E-INK4 Plus the per-pixel state can be kept at
  Game Boy resolution (160x144 = 23,040 pixels) and each pixel expanded to a 4x4
  block (640x576) only while writing a scan line. Planning then costs less than
  InkBoy's.
- **Bus time.** 8-bit parallel carries four 2-bit pixels per clock. A full
  1216x684 scan is about 208,000 clocks, roughly 10 ms at 20 MHz before line
  overhead - well inside a 33 ms slot.
- **CPU.** One 320 MHz RISC-V core for the emulator (Pokémon Red needs ~6 ms per
  frame on the X4 Pro's 240 MHz LX7), the other for planning and scanning.

So **about 30 scans a second, InkBoy-style, looks reachable**; a full-screen
60 Hz Caster-style scan with per-pixel state at panel resolution (~830,000
pixels, ~50 million updates a second) does not, without FPGA-like hardware.
As everywhere on e-ink, 30 scans a second means 30 pictures handed to the panel,
not 30 fully settled frames.

### What it would take

- Replace EPDiy's whole-screen waveform loop with a per-pixel pulse scan for the
  game area (the rest of the panel held), tuned for the ED047TC2: pulse
  budgets, voltages and a periodic cleaning refresh. InkBoy's tables are for a
  different panel and cannot be copied as they are.
- Port the firmware to ESP-IDF v6.1 / `esp32s31`, or run the emulator inside
  CloudZao's firmware, which already brings up the panel, touch, SD card and
  power management.
- **Risk:** InkBoy warns that custom waveforms and high refresh rates may cause
  permanent image retention or panel damage, and long-term panel life is not
  validated. The same applies here.

## Sources

- [CloudZao/Metalio-E-INK4-Plus](https://github.com/CloudZao/Metalio-E-INK4-Plus)
  and its [`sdkconfig.defaults.esp32s31`](https://github.com/CloudZao/Metalio-E-INK4-Plus/blob/main/sdkconfig.defaults.esp32s31)
- [GoGameGeek listing](https://gogamegeek.com/products/metalio-e-ink-4-plus-aluminum-alloy-e-ink-reader-4-7-inch),
  [LitNxt listing](https://www.litnxt.com/products/metalio-e-ink4-ai-e-reader-e-ink-touchscreen)
- [Xteink X4 Pro product page](https://www.xteink.com/nl/products/xteink-x4-pro-pocket-e-reader)
- [ESP32-S31 vs ESP32-S3](https://esp32.co.uk/esp32-s31-vs-esp32-s3-is-s31-the-new-s3/),
  [CNX Software on the ESP32-S31](https://www.cnx-software.com/?p=167703)
- [EPDiy](https://github.com/vroland/epdiy)
- [Modos Paper Monitor](https://crowdsupply.com/modos-tech/modos-paper-monitor)
