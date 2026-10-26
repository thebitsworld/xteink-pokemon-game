# Flash size plan

Status: **language pack done** (2026-10-26, branch feat/language-pack): English + Vietnamese built in,
the rest in `language-pack.bin` (`scripts/build_language_pack.py`, `lib/I18n/LanguagePack.*`,
`src/LanguagePackStore.*`). Differences from the plan below: the install sources are the SD card root
(`/language-pack.bin`, moved to `/.crosspoint/`) and a Wi-Fi download from the latest release
(network boot, then restart - the boot installs it); the pack is not in the SD card assets ZIP but a
release asset of its own; no boot-time StrId table - each string is a binary search over the mapped
pack, so no RAM. The user's X3 kept its factory partition table (OTA/SD updates write only the app):
its `spiffs` is 1 MB at 0xEF0000, not partitions.csv's 3.4 MB at 0xC90000, so the pack must stay under
1 MB (`MAX_PACK_BYTES`): German hyphenation (206 KB) stays in the firmware and only used keys go in.
Measured: pokemon-x3 99.0% -> 87.1%; pack 0.92 MB.

## Where the flash goes (pokemon-x3, v1.10.0-beta.2)

`firmware.bin` is 6,500,096 B of the 6,553,600 B app slot (99%). Measured from the map file and
`riscv32-esp-elf-nm -S` on `.pio/build/pokemon-x3/firmware.elf`:

| What | Size |
|---|---|
| UI strings of the 26 languages other than English and Vietnamese | ~590 KB |
| Reader font Lexend Deca (4 sizes x 4 styles) | 490 KB |
| Hyphenation tries (10 languages; German alone 201 KB) | 342 KB |
| UI font Inter | 272 KB |
| Bluetooth controller, flash-only (X3/X4 only) | 235 KB |
| Web pages (already gzipped) | 164 KB |
| Full mbedTLS certificate bundle | 67 KB |

X4 Pro is at 96.5% and X4 Classic at 92.5%; most of the above is shared.

## Quick options (measured on pokemon-x3)

| Option | Saving | Cost |
|---|---|---|
| `custom_i18n_builtin_langs = vi` (English is always in) | **-677 KB** (built) | Other languages show English |
| Drop the German hyphenation trie / keep English only | -201 KB / ~-316 KB (from symbols) | No hyphenation for those books |
| No `-DENABLE_SERIAL_LOG` in release envs (keep it in debug envs) | **-147 KB** (built) | No serial log for bug reports |
| `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_CMN` (C3 rebuild only) | ~-45 KB (estimate) | Rare OPDS servers may fail TLS |
| Drop the reader font's bold-italic style | ~-75 KB (estimate) | Bold-italic drawn as bold or italic |

## Preferred plan: language pack in the `spiffs` partition

The partition table already has an unused `spiffs` partition (0xC90000, 3.4 MB) on every device.
Moving the 26 extra languages (~590 KB) and the hyphenation tries (~340 KB) there saves ~900 KB per
firmware with no feature lost. X3/X4 would go from 99% to ~85%.

Design (answers the objection that sank the 2026-09 "seed fonts into spiffs" idea: a pack must not
depend on passing through one particular release):

1. English and Vietnamese stay built into the firmware; the other languages live in a language pack
   written to the `spiffs` partition and read in place with `esp_partition_mmap` - no RAM copy,
   which matters on the C3 readers (an SD-card pack would need 20-35 KB of RAM for the active
   language).
2. **Every** firmware checks the pack at boot. Missing, damaged (magic/CRC) or too old: it installs
   it from `language-pack.bin` on the SD card (shipped in the SD card assets ZIP), and Settings >
   Language offers a Wi-Fi download. A firmware jump over several releases still works.
3. Strings are looked up by a hash of their key, not by `StrId` index, so a pack keeps working with
   a newer firmware: strings added since show in English, the rest stay translated. No reinstall
   after each OTA. A boot-time `StrId -> offset` table costs ~3 KB of RAM.
4. Without a pack the UI is English, as reduced-language builds already behave; the saved language
   setting is kept, so installing the pack brings it back.
5. CI builds the pack and attaches it to each release; hyphenation tries go in the same pack (they
   change rarely).

Risks / to check on hardware: erasing and writing ~1 MB of flash at runtime (a few seconds),
especially on the X3/X4; a partition left by another firmware is recognised by the magic and only
overwritten by an install.

Work: pack generator (from `gen_i18n.py` output and the trie headers), I18n and hyphenation readers
for the mmapped pack, installer (SD + Wi-Fi) and its Settings entry, CI step. About 1-2 days, on its
own branch, then a beta tried on a real reader. If X3/X4 needs room before that, the one-line
`custom_i18n_builtin_langs` option above can be used meanwhile.
