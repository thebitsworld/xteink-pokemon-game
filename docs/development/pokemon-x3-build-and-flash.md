---
title: Build & Flash to X3
parent: Development
nav_order: 6
---

# Building the firmware and flashing an X3

Step-by-step instructions to build the firmware (the Pokémon-enabled build) yourself and flash it onto a real X3 device.

## Before you start

**The X3 has no user-accessible USB data port** (see [docs/user-guide.md](../user-guide.md), Troubleshooting section: *"The X3 has no user-accessible USB data port"*). This means:
- `pio run --target upload` (flashing over a USB cable, the usual approach for a typical dev board) **does not work** on the X3.
- The **only** way to flash firmware is via the **SD card**, triggered from the on-device Settings menu.

## Step 1 — Set up the environment (one-time)

```sh
export PATH="$HOME/.platformio/penv/bin:$PATH"
cd /home/vutq/project/xteink-pokemon-game
git submodule update --init --recursive   # only needed if freeink-sdk is empty
```

If PlatformIO isn't installed, or `pio` isn't on the default PATH, it's usually at `~/.platformio/penv/bin/pio` (use the `export PATH` line above).

## Step 2 — Build the Pokémon-enabled firmware

```sh
pio run -e pokemon-x3
```

Use the `pokemon-x3` environment specifically (not `default`) — only this environment sets the `CROSSINK_ENABLE_POKEMON` flag.

Flash/RAM numbers print at the end of the build log (`RAM:`, `Flash:` %). If the build fails from exceeding the partition size, see the size-constraint section of the [Pokémon Battle Roadmap](./pokemon-battle-roadmap.md).

## Step 3 — Get the firmware file

Once the build finishes, `rename_firmware.py` automatically copies the firmware to a convenient name, at:

```
.pio/build/pokemon-x3/firmware-x3-x4.bin
```

## Step 4 — Copy the `.bin` file onto the X3's SD card

Pick one of the two approaches:

### Option A — Remove the SD card and use a computer (most reliable)

1. Power off the X3, remove the SD card, and insert it into a computer.
2. Copy `firmware-x3-x4.bin` to the **root of the SD card** (do not create a subfolder).
3. **Do not format the card, and do not delete or rename any other folder** — books, reading progress, and Pokémon saves (`/.crosspoint/pokemon*.bin`) must be left untouched.
4. Put the card back into the X3.

### Option B — No need to remove the card, use File Transfer over Wi-Fi

1. On the X3: open **File Transfer**.
2. On a computer or phone on the same Wi-Fi network, open the IP address the X3 displays in a browser.
3. Upload `firmware-x3-x4.bin` to the SD-card root through that web interface.
4. Exit File Transfer on the X3.

## Step 5 — Trigger the update on the device

On the X3: **Settings → System → SD Card Firmware Update** → select the `firmware-x3-x4.bin` file you just copied → confirm.

## Safety notes

- **Back up before flashing an unfamiliar build for the first time**: save copies of `/.crosspoint/pokemon-a.bin` and `pokemon-b.bin` first, just in case — even though the save-format changes on this branch (see the [Pokémon Battle Roadmap](./pokemon-battle-roadmap.md)) were designed to stay backward-compatible with older saves.
- If the firmware crashes, the X3 automatically writes a crash report to the SD-card root (no USB required) — check that log file if something goes wrong after flashing.
- During stages where no UI yet uses a given new feature, flashing that build will **look and behave identically** to the previous one — there's nothing new to "see" on screen yet. This step only confirms "the firmware flashes, doesn't crash, and the save isn't corrupted" — it is not yet the step for trying out the new feature. Check the roadmap to see which stage first has UI for a given feature.

## See also

- [Getting Started](./getting-started.md) — installing PlatformIO, building for other environments
- [Pokémon Battle Roadmap](./pokemon-battle-roadmap.md) — progress, technical constraints, remaining work
- [docs/installation.md](../installation.md) — the official update process for end users (pre-packaged release build)
