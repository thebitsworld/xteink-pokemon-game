---
title: Installation
nav_order: 2
---

# Installation

## Supported devices

**Xteink X3**, **Xteink X4**, **Xteink X4 Pro**, and **Xteink X4 Classic**. Do not install
this firmware on a Sticky, a plain (non-Pokémon) CrossInk device, or any other device.

Confirmed working on physical hardware on the X3, X4 and X4 Pro, including both locked and
unlocked devices. The X4 Classic build is new (1.10).

## Step 1 — Download the right firmware for your device

Firmware comes in three variants:

| Your device | Download |
| --- | --- |
| **Xteink X3 or Xteink X4** | [Latest X3/X4 firmware](https://github.com/thebitsworld/xteink-pokemon-game/releases/latest/download/xteink-pokemon-x3-x4-firmware-latest.bin) |
| **Xteink X4 Pro** | [Latest X4 Pro firmware](https://github.com/thebitsworld/xteink-pokemon-game/releases/latest/download/xteink-pokemon-x4-pro-firmware-latest.bin) |
| **Xteink X4 Classic** | [Latest X4 Classic firmware](https://github.com/thebitsworld/xteink-pokemon-game/releases/latest/download/xteink-pokemon-x4-classic-firmware-latest.bin) |

X3 and X4 share one firmware build. X4 Pro and X4 Classic use a different chip (ESP32-S3
vs. ESP32-C3), and each needs its own build: the X4 Classic has buttons only, no touchscreen
or frontlight. **Always match the file to your device** — the "latest" links above
always point at the newest release automatically, so you never need to hunt for a version
number.

Prefer to pick an exact version instead of always-latest? Every release on the
[releases page](https://github.com/thebitsworld/xteink-pokemon-game/releases) lists
`xteink-pokemon-x3-x4-firmware-v<version>.bin` and
`xteink-pokemon-x4-pro-firmware-v<version>.bin` and
`xteink-pokemon-x4-classic-firmware-v<version>.bin` individually, plus a `SHA256SUMS.txt` you
can check the download against.

**You cannot brick your device by picking the wrong file.** Before writing anything, the
firmware updater checks that the file matches your device's chip and fits the update
partition, and refuses to proceed with a clear error if it doesn't — it just won't install.
The X4 Pro and X4 Classic share a chip, though, so that check cannot tell their two files
apart: take extra care to pick the right one of those two.

## Step 2 — Install it

You have two options. Either works equally well; pick whichever is easier for you right
now.

### Option A — Over Wi-Fi (easiest, no computer needed)

1. On the device, open **Settings → System → Updates → Check for Updates**.
2. If a newer version is available, follow the on-screen prompt to download and install it.

This checks GitHub directly and always offers the newest release for your exact device —
there's nothing to download by hand.

### Option B — From an SD card (works without Wi-Fi)

1. Download the `.bin` file for your device (see Step 1) onto a computer.
2. Get the SD card out of the device — power it off first, or on X4 Pro use
   **Home → File Transfer → USB Drive** to access the card without removing it (eject the
   drive from your computer before unplugging it; the reader restarts to Home once it's
   safely ejected).
3. Copy the `.bin` file to the root of the SD card. **Do not format the card or touch any
   existing folder** — your books, settings, reading progress, and Pokémon save all stay
   exactly where they are.
4. Return the card to the device (or reconnect it) and power it back on.
5. Open **Settings → System → SD Card Firmware Update**, pick the `.bin` file you just
   copied, and confirm.

## Step 2.5 — Add the Pokémon artwork (do this once)

Sprites, item icons, badges, Pokédex cards, and Gym Leader/Champion portraits ship
separately from the firmware, as a `xteink-pokemon-sd-card-assets.zip` attached to every
release on the [releases page](https://github.com/thebitsworld/xteink-pokemon-game/releases).

1. Download `xteink-pokemon-sd-card-assets.zip` from the same release you got your
   firmware from.
2. Extract it on your computer — inside is a single `pokemon` folder.
3. Copy that `pokemon` folder to the **root** of the SD card, next to your `books` folder
   (not inside `.crosspoint` or any other folder). If a `pokemon` folder is already there
   from a previous install, let the new files overwrite it.
4. Do this the first time you install, and again any time a release note mentions new or
   changed artwork. Everything about the game still works without this step — Pokémon
   without artwork just show a placeholder icon instead of their sprite.

The artwork is identical for every supported device, so this ZIP is the same regardless of
whether you're on an X3, X4, or X4 Pro. See
[Rights and attribution](../RIGHTS_AND_ATTRIBUTION.md) and
[Third-party assets](third-party-assets.md) for where this artwork comes from and how to
request a correction or removal.

## Step 2.6 — Add apps without Wi-Fi (optional)

Applications (Home > Applications) can install its games and tools from the App Store over
Wi-Fi. To add them all at once without Wi-Fi instead:

1. Download `xteink-pokemon-apps.zip` from the same release.
2. Extract it to the **root** of the SD card. It holds a `.crosspoint/apps` folder - one
   folder per app - and merges into the hidden `.crosspoint` folder already there. (On a
   Mac or Windows the `.crosspoint` folder may be hidden; the extraction still works.)
3. The apps show up in Applications. The App Store keeps them up to date afterwards.

The same ZIP works on every reader. An app made for another reader shows as "Not for this
reader" and does not open.

## Step 2.7 — Other languages (optional)

The firmware has English and Vietnamese built in. The other 26 menu languages, and word
hyphenation for French, German, Spanish, Italian, Portuguese, Polish, Swedish, Russian and
Ukrainian books, come in a language pack (about 1.2 MB) that you install once:

- **Over Wi-Fi:** Settings > System > Device > Language > **Download more languages**. The
  reader joins Wi-Fi, downloads the pack and restarts to install it.
- **From an SD card:** download `language-pack.bin` from the same release, copy it to the
  **root** of the SD card and restart the reader. It installs the pack at start (a few
  seconds) and moves the file into `.crosspoint`.

Then pick your language in Settings > System > Device > Language. A firmware update keeps
the pack; text added in newer firmware shows in English until you download the pack again.
Without the pack, a reader set to another language shows English.

## Your saves are safe

An update never touches your books, reading statistics, or Pokémon save — those live in
completely separate files and nothing in the update process reads or rewrites them.
As always with any firmware update, backing up your SD card first is good practice, not a
requirement.

If you ever want a copy of your Pokémon progress specifically, the two files are
`/.crosspoint/pokemon-a.bin` and `/.crosspoint/pokemon-b.bin` at the SD card's root — back
up both together. See [The Pokémon game](pokemon-game.md#your-save-is-separate-from-your-books)
for more.

## Troubleshooting

- **"Firmware is for a different device"** — you downloaded the file for the other chip
  (X3/X4 vs. X4 Pro or X4 Classic). Go back to Step 1 and grab the correct one; nothing was written to
  the device.
- **Pokémon sprites show a plain placeholder instead of artwork** — you haven't copied
  `xteink-pokemon-sd-card-assets.zip`'s `pokemon` folder to the SD card root yet, or copied
  it to the wrong place. See Step 2.5.
- **"Check for Updates" says you're already up to date, but you're not** — this only checks
  once you're already on a version of this Pokémon-enabled firmware; a very old base
  CrossInk install may need one manual SD-card install first (Option B above) before Wi-Fi
  updates start finding new releases.

Building from source, or want the technical detail behind any of this? See
[Getting Started](development/getting-started.md) and
[Save-file formats](file-formats.md).
