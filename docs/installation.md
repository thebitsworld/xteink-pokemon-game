---
title: Installation
nav_order: 2
---

# Installation

## Supported devices

**Xteink X3** and **Xteink X4 Pro**. Do not install this build on a Sticky or another device.

X4 Pro support (touch-only UI, portrait orientation) is new as of this release, verified on
the simulator; physical-device acceptance is still pending.

**X3 and X4 Pro firmware are not interchangeable.** They target different chips (ESP32-C3 vs.
ESP32-S3) — installing the wrong one will not boot. Always match the asset name
(`xteink-pokemon-x3-*` or `xteink-pokemon-x4-pro-*`) to your device.

Download the current package from the [GitHub releases page](https://github.com/thebitsworld/xteink-pokemon-game/releases).

## Full installation

1. Download the ZIP that matches your device: `xteink-pokemon-x3-full-v<version>.zip` for X3,
   or `xteink-pokemon-x4-pro-full-v<version>.zip` for X4 Pro.
2. Extract the ZIP. Do not copy the ZIP file itself to the SD card.
3. Power off the device and put its SD card in the computer.
4. Back up any `/.crosspoint/pokemon*.bin` files if present. Earlier `pokemon-v2-a.bin` and `pokemon-v2-b.bin` saves migrate automatically.
5. Copy `update.bin` to the SD-card root and merge the extracted `pokemon` folder into the root.
6. **Do not format the SD card. Do not delete any existing folder. Do not replace any existing folder.** Keep your books, sleep covers, settings, reading progress, and Pokémon saves in place.
7. Confirm `update.bin` and the visible `pokemon` folder are directly at the SD-card root.
8. If updating from a build that stored artwork under `/.crosspoint/pokemon`, use this full installation once. Do not move or delete the Pokémon save files in `/.crosspoint`.
9. Safely eject the card and return it to the device.
10. Open **Settings → System → SD Card Firmware Update**, select `update.bin`, and confirm.

The full ZIP contains the firmware and required artwork. It contains no Pokémon saves, books, reader configuration, or reading data. Artwork is identical between X3 and X4 Pro (same files, same paths) — only the firmware binary inside each ZIP differs.

## Firmware-only Wi-Fi update

Use this only when `/pokemon/` is already installed at the SD-card root. If the artwork is still under `/.crosspoint/pokemon/`, install the full ZIP once instead.

1. Download the firmware that matches your device: `xteink-pokemon-x3-v<version>.bin`
   or `xteink-pokemon-x4-pro-v<version>.bin`.
2. Leave the SD card inside the device and open **File Transfer**.
3. Open the address displayed by the device in a browser.
4. Upload the `.bin` to the SD-card root.
5. Exit File Transfer.
6. Open **Settings → System → SD Card Firmware Update**, select the file, and confirm.

File Transfer only copies the firmware to the card. The firmware update always runs from the device's own settings.
