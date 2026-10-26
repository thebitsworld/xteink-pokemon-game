# CLAUDE.md — handoff notes

Notes for Claude in a later session (or on another machine). Not project documentation: what shipped is in
[CHANGELOG.md](CHANGELOG.md), how things work is under [docs/](docs/). This file keeps what a new session
needs and would otherwise rediscover the hard way. The long session-by-session narrative it used to hold
is in git history (before 2026-10-08).

## Where things stand (2026-10-19, v1.9.1)

- Fork of CrossInk (ESP32-C3/S3 firmware for Xteink X3/X4/X4 Pro e-ink readers) with a Pokémon game that
  levels from real reading time, Lua apps installed from an App Store, and a Game Boy emulator on the
  X4 Pro. `[crossink] version` 1.6.1, `[pokemon] version` 1.9.1; latest release `v1.9.1` (stable). Its
  release also carries `xteink-pokemon-sd-card-assets.zip` and `xteink-pokemon-apps.zip` (every app in
  `apps/` under `.crosspoint/apps/`), uploaded by hand - CI only publishes the firmware.
- Pokémon pace (1.9.0, plan and simulations in docs/development/pokemon-pace-plan.md, re-run with
  `scripts/dev/simulate_pokemon_pace.py`): battle XP is Gen 5-scaled `battleVictoryXp(Lo, Lp)` (x1, gyms
  and wild alike); road trainers from the crosslingua fork (`lib/Pokemon/PokemonTrainers.*`, 30-min roll
  2/5 with pity, max 2 waiting, fixed 10 XP + an item); items go straight to the bag and are listed once
  on an "Items received" Message screen (`PokemonState::receivedItems`); uncaught species x3, legendary
  1/10, Mewtwo from 90%, stones 1/10 pity 9; HP heal max(1, 1.5% max HP)/min; 2 queue slots reserved for
  evolutions/moves. Save format v11 (437-byte state). Trainer class art: pret/pokered gfx/trainers,
  generated into `images/pokemon/trainers/classes/` (in the SD card assets ZIP).
- Lua apps in `apps/` (the App Store installs straight from this folder on GitHub, so pushed apps are live
  without a firmware release): 52 apps - the CrossPlay-inspired games and tools written here (plus
  Tic-Tac-Toe and Xiangqi, 2026-10-13) plus 11 from
  CrossSmudge. CrossPlay apps needing data or a network were skipped on purpose.
- X3 app policy: an app must stay under 75 KB in `pokemon-simulator-X3` (the 64-bit simulator reports
  15-35% more than a real X3, where ~65 KB of Lua is the measured ceiling). An app that cannot fit lists
  `"devices": ["x4pro"]` in its manifest and catalog entry instead of losing features. On the X4 Pro the
  Lua heap is in PSRAM (2 MB limit).
- Game Boy (X4 Pro only, `CROSSINK_GAMEBOY`): Peanut-GB core in `lib/PeanutGB` for DMG ROMs; Walnut-CGB
  v1.2.4 (`lib/WalnutCGB`, MIT, local forward-declaration edit) for ROMs with header 0x143 bit 7, compiled
  alone with renamed symbols in `src/walnut_cgb_impl.c` behind `src/walnut_cgb_port.h`. Screen in
  `src/activities/gameboy/`; Applications lists a built-in Game Boy row (ROM picker = FileBrowser
  `Mode::PickGameBoyRom`). Settings/romdir in `/.crosspoint/gameboy.cfg`; `<rom>.state` quick save,
  `<rom>.resume` + `APP_STATE.lastSleepFromGameBoy` for wake-into-game. The simulator needs `CROSSINK_SIMULATOR_PSRAM_TOTAL=8388608` to open a ROM.
  Never commit ROMs (`game/` is untracked).
- Game Boy screen speed: on UC8279 variant 0x68 only (`fastRefreshValidated()`, the user's panel) the
  fast presets (SETTINGS > Screen speed: D50/D40/D30/D25 x clean 30/60 s, default D40/60) use the X3
  UC8279 DU bank scaled to that %, PLL 0x0F and 20 MHz display SPI. The driver sends/drives only the changed rectangle and skips
  unchanged frames; the next picture is requested 50 ms after the last (no 250 ms cap) and drawn while
  the waveform runs (overlapped refresh). Measured: ~190 ms per refresh (wave ~166 ms, ~14 KB per
  plane), 4-5 updates/s while the picture changes (1.8.1: 276 ms, ~3/s; stock ~614 ms, ~1.4/s); draw
  ~10 ms (byte-table drawing, buttons redrawn only on change). Other panels stay stock until tested.
  Findings from the betas: forced TSSET above 0x5A is slower and flashes, PLL has no effect on the
  OTP waveform, DU below ~35% ghosts, PT_SCAN 0 (scan only the window's gates) changed nothing.
  `displays_per_s` in `/.crosspoint/gameboy-perf.txt` depends on how much the picture changes - read
  it with the `unchanged` column. Beyond ~5/s only a shorter waveform helps, which ghosts.
- Game Boy emulation speed (1.8.3): one emulated frame in three is drawn (DRAW_EVERY, each core's
  frame_skip steered per frame); Walnut-CGB's ~50 KB context goes to internal RAM when 48 KB stay free,
  its TU is -O2. Pokemon Red 100% (6 ms a frame). Pokemon Gold walking the overworld at D40 measures ~83% on the user's X4 Pro (2026-10-26, serial `perf:` lines, ~80 samples: v1.9.1 82.6%, feat/bluetooth 84.3%; ~19 ms a frame, draw ~23 ms) - the older 96-97% was a lighter scene. Ink-boy 1.2.0's skip-the-1-ms-sleep-while-behind measured no gain (83.2%) and was reverted; its direct ROM 16/32-bit reads stay. BT on puts the Walnut state in PSRAM (largest internal block ~34 KB) at about the same speed. GBC colours: 17 grey levels as a
  4x4 Bayer dither (DMG uses 0/4/12/16); SETTINGS > Colours Contrast (default; night scenes - narrow
  palette span - stretched onto levels 4..16), Smooth (17 levels, the tester found it busy), Brightness.
  Settings menu taps: orientation changes wait for the finger to lift, and the touch registry is cleared
  while it shows (game target ids collide with the front-button hints).
- Bluetooth (X4 Pro only, `CROSSINK_BLUETOOTH`, after 1.10.0-beta.1, not released yet): the SDK's
  `BleKeyboardHost` (`FREEINK_CAP_BLE_HID_HOST=1`) on `h2zero/NimBLE-Arduino @ 2.5.1` (2.3.8 does not
  compile against the SDK's `onPassKeyDisplay`), NimBLE options as build flags - no custom_sdkconfig.
  `src/BluetoothInput.*` maps raw button edges (`RawButtonEvent::code()`) to 8 controls (learned codes in
  `/.crosspoint/bluetooth_buttons.bin`, else HID usage defaults) and injects Up/Down/Left/Right/A=Confirm
  /B=Back into `MappedInputManager` (`remote*` arrays, beside the simulator ones; edges last one frame,
  a release in its press's frame waits one). The Game Boy skips remote-held buttons in its physical
  mask (there Confirm = Start) and reads `bt::heldControls()`. Setting `bluetoothEnabled` (no category,
  switched from Settings > System > Bluetooth, `BluetoothActivity`). Radio: started at the end of
  setup() unless it is a network boot, full CPU speed while up, `stopForSleep()` in enterDeepSleep().
  S3 options in `[bluetooth]` (X4 Pro, X4 Classic). X3/X4: `[firmware_tuned_bt_c3]` adds CrossPoint 2.0's BT
  options to the rebuilt C3 core (CONFIG_BT_CTRL_RUN_IN_FLASH_ONLY + `post:scripts/patch_arduino_ble_controller.py`
  links libbtdm_app_flash.a); the rebuilt core lands in the shared framework-libs package, so switching
  between `default` and `pokemon-x3` rebuilds it. C3 radio needs a 48 KB free block and pauses in Lua apps
  (`bt::pauseForMemory` in LuaAppActivity). Flash: pokemon-x4-pro 96.5%, x4-classic 92.5%, pokemon-x3 99.0%
  (~66 KB left - the next X3 feature needs room found first; saved plan, not started:
  docs/development/flash-size-plan.md - language pack + hyphenation in the unused `spiffs` partition). Released
  as v1.10.0-beta.2 (2026-10-25, branch feat/bluetooth). Tried on the user's X4 Pro with a BSP D6mini
  (2026-10-26, unreleased fixes on the branch): HID mode works after the SDK gamepad decode; learning takes
  one code per press (`learnFrom`); touch codes match the nearest learned spot (`controlForTouch`).
  pokemon-x3 flash 99.0% after these.
  X3/X4 (C3) radio policy since 85fac408 (2026-10-26), ported from CrossPoint's BlePageTurner RadioPolicy.h
  (fetch with `gh api repos/crosspoint-reader/crosspoint-reader/contents/lib/BlePageTurner/src/RadioPolicy.h`):
  `bt::policyTick(Place)` from main.cpp - radio only in the reader (menus over it count, Settings does not)
  with a bond, after 1.5 s without layout (`SectionLayoutHooks` in lib/Epub Section.cpp stop it from the render
  task under `radioMutex`), and on the Bluetooth screen; 80 KB free + 32 KB block to start, rollback if the
  block goes, 30 s idle off, 3 heap-in-pieces refusals -> ESP.restart into the book (RTC memo). Stopping and
  restarting NimBLE splits the C3 heap (67 KB free, no 48 KB block after one cycle), hence no pausing for
  Settings. Settings RAM: only the list on screen is built (`buildSettingsList`), the base list is freed when
  no `BaseSettingsListHold` holds it (it used to be a boot-to-off static), BASE_SETTINGS_CAPACITY = 113. X3 is
  USB-locked: the Bluetooth screen shows start failures as "free/largest" KB; crash_report.txt on the SD card
  decodes with the matching ELF (its app desc elf_sha256 at offset 0x90 of the esp_app_desc).
- Game Boy library (`GameLibraryActivity`, opened by the Applications row; `GameLibraryStore` keeps favourites
  and recents in `/.crosspoint/gameboy_library.txt`; GameBoyActivity notes each started ROM). Scans 3 folders
  deep from `/`, skipping hidden folders, `pokemon`, `sleep`, `fonts`, `dictionaries`; max 200 games.
- How to play guides (1.8.4): `guide.md` / `guide.vi.md` in an app
  folder, shown by `GuideViewerActivity` (Markdown subset, not the EPUB reader - that would add recents,
  reading time and Pokemon credit). "?" on Applications rows (tap; hold Select on X3/X4);
  `smudge.has_guide()`/`open_guide()` push it over the running Lua app. The shared `menu.lua` (identical
  copy in every game; keep it byte-identical) adds "How to play" above Exit and shrinks its buttons to
  fit. UI fonts have no CJK: guides must not use Chinese characters. New games: caro, reversi, oanquan,
  tienlen, then coup (hidden xiangqi), dots, patience (FreeCell + Spider), logic (Hashi, Lights Out,
  Slitherlink) - X3 sim peaks 56-64 KB - and hexland (the Catan-like game: you + 3 AI; ~81 KB, so
  `devices: ["x4pro"]`, ~104 KB there). After v1.8.5 (2026-10-15, pushed to main, no firmware needed):
  phom, backgammon, morris, samloc, mancala, coganh, codebreak, and Kakuro + Light Up in logic 1.1.0
  - all 57-62 KB on the X3 sim (Hashi Hard in logic 64 KB). Remaining ideas: docs/development/game-ideas.md. Measuring on the X4 Pro simulator: the Applications list has an
  extra Game Boy row, and set `CROSSINK_SIMULATOR_PSRAM_TOTAL=8388608`.
- Measuring a Lua app needs no `src/` hooks now: run the simulator binary with `SDL_VIDEODRIVER=offscreen`,
  `CROSSPOINT_SIM_INPUT_SCRIPT` (`ms:DOWN;ms:CONFIRM:1000;ms:TAP:x,y;...;ms:QUIT`) and
  `CROSSPOINT_SIM_SCREENSHOTS` (`ms:path.bmp`); the app's peak lands in `fs_/.crosspoint/apps-memory.txt`
  when it exits cleanly (Back out of it). `SDL_VIDEODRIVER=dummy` gives no screenshots.
- Language pack (released in 2.0.0, 2026-10-26): `custom_i18n_builtin_langs = vi` (EN always);
  the other 26 languages + 8 hyphenation tries (EN and DE stay) are in `language-pack.bin` from
  `scripts/build_language_pack.py` (release workflow attaches it). Format and reader: `lib/I18n/LanguagePack.*`
  (strings by FNV-1a key hash, `STRING_KEY_HASHES` generated by gen_i18n.py); device side
  `src/LanguagePackStore.*`: boot installs `/language-pack.bin` or `/.crosspoint/language-pack.bin` into the
  `spiffs` partition (body, read-back CRC, header last) and mmaps it, logging to
  `/.crosspoint/language-pack-log.txt`; the simulator reads the SD file. Readers keep their factory partition
  table (updates write only the app): the user's X3 has `spiffs` 1 MB at 0xEF0000, so the pack must stay
  under 1 MB (`MAX_PACK_BYTES` in the builder; ~0.92 MB now).
  Hyphenation for packed languages: `setPackedHyphenationPatterns` (host tests define
  HYPHENATION_ALL_BUILTIN). Download: language picker's last row -> `NetworkBootTarget::DOWNLOAD_LANGUAGES`
  -> `LanguagePackDownloadActivity` -> restart. Tried on the user's X3 (2026-10-26): SD install 3.3 s, languages
  switch fine; its real table (`/.crosspoint/partitions.txt`, written once at boot): app0/app1 0x770000 each,
  spiffs 0xEF0000 1 MB, coredump 0xFF0000. The Wi-Fi download needs a stable release carrying the pack (2.0.0 on).
  X4 Pro factory table (freeink-sdk/docs/xteink-x4pro-support.md): spiffs 80 KB at 0xFD0000 - the pack cannot go
  there, so 2.0.0 left X4 Pro/Classic in English. Fix on fix/language-pack-psram (5978c64d, unreleased): when
  `packGoesToRam()` (PSRAM present and partition missing/too small; `-DLANGUAGE_PACK_FORCE_RAM` forces it for
  testing) the SD file is read into PSRAM - at boot only for a packed menu language, else via
  `ensureLoaded()` (language picker) or `setPackedHyphenationLoader` (first book needing packed patterns).
  A user's X4 Pro flashed with CrossPoint has partitions.csv's table (spiffs 3.4 MB) - the table depends on
  how the reader was last fully flashed, not the model.
- Metalio E-INK4 Plus (2026-10-21, research only, nothing built): ESP32-S31 + 4.7" ED047TC2 on a
  parallel bus via EPDiy (no controller), so a PaperBoy/InkBoy-style per-pixel pulse scan could reach
  ~30 scans/s (estimate). Notes: docs/development/metalio-e-ink4-plus.md. kuslota/InkBoy is not Ink-boy.
- Open, undecided: a translation mechanism for Lua apps (all English today; the user wants to discuss it
  first). Firmware strings: the fork's own Pokémon strings are untranslated in most languages (fall back
  to English); Vietnamese is complete.

## Standing conventions

- Commit locally; push, tag or release only when the user says so in that session. A `v*.*.*` tag push
  builds the three firmwares (X3/X4, X4 Pro, X4 Classic) in CI and publishes a release (a hyphenated tag is a prerelease). Push with
  `git -c credential.helper='!gh auth git-credential' push origin <ref>`; never run `gh auth setup-git`.
- Code and docs in separate commits. Ask before design-level changes, with a proposal. Only format code
  you wrote (no clang-format installed: match the style). Run builds one at a time.
- Never `git add` `images/`, `books/` or Bookerly fonts; no `custom_sdkconfig` on X4 Pro.
- The user writes Vietnamese; reply in Vietnamese.

## freeink-sdk is vendored (git subtree)

- `freeink-sdk/` is a copy of github.com/Free-Ink/freeink-sdk added with `git subtree --squash` (at
  `69937018`, 2026-10-12), not a submodule, so its drivers can be changed here. Only its Lucide icons
  stay a submodule (`freeink-sdk-lucide`, used by `scripts/generate_icons.py` only).
- Take upstream changes with
  `git subtree pull --prefix=freeink-sdk https://github.com/Free-Ink/freeink-sdk.git main --squash`
  and resolve conflicts with our own edits like any merge. Keep the subtree's squash commits as git
  made them (their `git-subtree-*` lines are what the next pull reads).
- Mark every local SDK edit with a `// xteink-pokemon:` comment and list it here:
  - `setFastRefreshTuning(tsset, pll)` in `PanelDriver.h`, `FreeInkDisplay.h/.cpp`, acted on by
    `Uc8279X4Driver` (forced temperature and PLL for Fast refreshes; the Game Boy sets PLL 0x0F).
  - `setFastRefreshDuScale(percent)` (same files, plus `Uc8279X4Driver::loadScaledDuLut`): Fast
    refreshes on the X3 UC8279 DU bank with its frame counts scaled.
  - `lastRefreshTimings()` (same files; `RefreshTimings` in `PanelDriver.h`, phase clock in
    `Uc8279X4Driver`): where the last refresh spent its time.
  - `EpdBus::setSpiHz`, `FreeInkDisplay::setDisplaySpiHz`: change the display SPI clock at runtime.
  - `setFastRefreshDiffWindow(on)` (same files as the DU scale; `findChangedWindow`, `streamWindow`,
    `sendPartialWindow` in `Uc8279X4Driver`): Fast refreshes send and drive only the changed rectangle
    (PTIN/PTL window) and skip unchanged frames; `RefreshTimings::planeBytes` reports the bytes sent.
  - `pulseScan(...)` / `endPulseDrive()` (PanelDriver.h, FreeInkDisplay.h/.cpp, `Uc8279X4Driver::
    loadPulseLut`/`streamPulseOldPlane`): pulse drive - NEW plane = target, OLD = target with the driven
    pixels flipped, one-pulse LUT built from the DU bank's levels for its 0x22/0x23 tables; tested
    through Applications > Pulse test (`PulseTestActivity`, `src/util/PulsePlanner.h`). `wait=false`
    + `pulseFinish()` overlap planning with the waveform. Measured: wave ~36 ms fixed + ~11.6 ms per
    frame at PLL 0x0F (zeroing a LUT group's two trailing bytes - repeat counts - drives nothing and
    leaves the 36 ms), send ~14-24 ms for 12-18 KB/plane; ~10 scans/s in a game, smooth but ghosty;
    the tester liked 2 frames per pulse, then 8x3 (less ghosting). 1.8.6-beta.3: `preparePulsePlanes`
    builds both window planes into PSRAM before `pulseFinish` (overlapping the last waveform),
    `sendPulsePlanes` sends them in long writes; sending during the last waveform
    (beta.3's Early send) froze the screen - the UC8279 takes no RAM writes during a refresh;
    SETTINGS > Picture Small = x2. Measured: Arduino SPI ~0.6 ms per KB per plane (DMA was worse),
    D40 Small x2 only ~10% faster (the ~160 ms waveform dominates). PLL 0x1F/0x3F: ~7.6 ms frames and
    ~18 ms fixed vs ~11.7/~25 at 0x0F (the fixed part also grows with the window's rows). Game Boy
    presets (1.8.5): Pulse 8x3 (P8x3-60), Pulse polish (P8x3P-60: 4 more pulses on the area that
    changed once the planner is idle AND the picture stood still 0.3 s; PulsePlanner::polish) and
    Pulse polish+ (P8x3PP-60: idle, and still OR 1.5 s since the last polish). Betas tried and
    dropped: fewer frames (blur), 0x1F "fast" (more ghost), polishing while pixels still changed
    (patchy, flicker). Pulse test shows only with -DCROSSINK_PULSE_TEST=1. Default stays D40.
  - BleKeyboardHost (`libs/network/BleKeyboardHost`): `parseGamepadLayout`/`gamepadMaskOf` read a HID
    gamepad's hat, left stick and buttons from the report map and emit one raw edge per direction/button
    (`RawButtonEvent::GAMEPAD_REPORT_ID` 0xFD; byteIndex 0 = direction 1-4, 1 = button n; default keycodes
    = the axis-pad usages); a Digitizer-page report map (0x0D) makes each touch one tap named by its spot
    (`TOUCH_REPORT_ID` 0xFE, value = X/16, byteIndex = Y/16, lost lift released after 2 s). Found with a
    BSP D6mini: HID mode's hat rests at 0xFF and broke the rest-frame guess; V3 mode is a touchscreen.
    `-DFREEINK_BLE_HID_REPORT_DEBUG=1` (e.g. via `PLATFORMIO_BUILD_FLAGS`) dumps every report, the report
    map and the parsed gamepad layout on serial.
  - `RefreshTimings` reports the last COMPLETED refresh with running `refreshes`/`skipped` counts (an
    overlapped refresh completes on the next call). Not an SDK file but related:
    `HalDisplay::displayBufferOverlapped`/`finishOverlapped` (SPI-locked shadow-free async refresh).

## Constraints that still bite

- **Lua app memory**: the X3 simulator caps a Lua app at 75 KB (76800 B); kept as the limit until someone
  measures a real X3/X4. Peaks are logged to `/.crosspoint/apps-memory.txt` when an app exits cleanly.
  What made the big games fit: one screen module in memory at a time (load on demand, drop and
  `collectgarbage` before loading the next), AI/generators loaded per use, data as strings rather than
  tables (Cult Ledger's cards, Sea Battle's grids), integers for moves, no closures in hot loops, capped
  recursion. Tide & Paper even splits its table screen into a drawing half and an input half loaded in
  turn. Apps are compiled to a bytecode cache at launch, so mid-run `dofile` is cheap.
- Lua on the device is `LUA_32BITS`: integers wrap at 2^31 (take typed numbers as floats), floats have ~7
  digits, hand-rolled LCGs overflow (use `math.random`).
- `PokemonState` may only be appended to after byte 115; `PokemonRecord` stays 48 bytes (battle data lives
  in the side files); move/item names are plain C strings, not i18n.
- After editing `lib/I18n/translations/*.yaml`, an incremental build may not regenerate i18n: rebuild
  clean before trusting it.

## Building and testing (Linux, this machine)

```sh
export PATH="$HOME/.platformio/penv/bin:$HOME/.platformio/packages/tool-cmake/bin:$PATH"
pio run -e pokemon-x3            # or pokemon-x4-pro; base firmware: -e default
pio run -e pokemon-simulator-X3  # or pokemon-x4-pro-simulator; non-Pokemon: -e simulator
cd test && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j8 && cd build && ctest -j8
python3 scripts/run_simulator_smoke_test.py --no-build --env pokemon-simulator-X3 [--pokemon|--lua-apps|--home-navigation [--theme T]]
python3 scripts/run_simulator_smoke_test.py --no-build --env pokemon-x4-pro-simulator [--home-themes|--pokemon|--lua-apps]
```

- A firmware `pio run` may delete the simulator build dirs: rebuild a simulator before running it.
- Lua app logic tests: `test/lua_apps/<app>_test.lua`, listed in `test/lua_apps/CMakeLists.txt`; run one
  with `test/build/lua_apps/LuaAppLogicTest apps test/lua_apps/<app>_test.lua`. Balance-sensitive games
  (Cult Ledger, Toy Front, Tide & Paper) keep their simulated-player win rates as assertions.
- The smoke test's reader route gives the game a starter and asserts that turning pages credits Pokémon
  reading time (the reader was once silently unwired for months). Its button page turns use the front
  Right button: injected `PageForward` never reaches the reader.
- Measuring or screenshotting a Lua app in the simulator needs temporary hooks (start an app directly,
  scripted keys/taps, screenshots); they lived in a scratch script, not in git — re-create them when
  needed and revert `src/` before committing.
- Simulator SD card: `fs_/` (gitignored); copy `images/pokemon` to `fs_/pokemon` for the art, and an app
  folder to `fs_/.crosspoint/apps/<id>` to try it. `scripts/dev/edit_pokemon_save.py` fills a save for UI
  testing ([guide](docs/development/pokemon-save-edit-tool.md)).

### Machine setup (outside git — redo on a new machine)

- apt: `build-essential cmake libexpat1-dev zlib1g-dev libsdl2-dev libssl-dev python3-venv`;
  `git submodule update --init --recursive`.
- `~/.platformio/platforms/espressif32/platform.json`: replace the `tool-scons` package with
  `{"type": "tool", "optional": true, "owner": "platformio", "version": "~4.41101.0"}` and delete that
  folder's `__pycache__` (the pinned scons 4.8.1 mirror lacks `SCons/Tool/FortranCommon.py`, so linking
  `firmware.elf` fails).
- If `pokemon-x3` fails linking with "gap between .eh_frame and .flash.tdata": add
  `.rodata.__func__.0`, `.rodata.control_construct.str1.4` and `.rodata.tlsf_realloc.str1.4` to the
  `*libheap.a:tlsf.*(...)` rule in
  `~/.platformio/packages/framework-arduinoespressif32-libs/esp32c3/ld/sections.ld`.
- No sudo without asking; check with `sudo -n true`.

## Where to read first

- Game mechanics: [docs/development/pokemon-mechanics.md](docs/development/pokemon-mechanics.md).
- Lua apps and their API: [apps/README.md](apps/README.md), [docs/applications.md](docs/applications.md).
- Release steps: [docs/release-checklist.md](docs/release-checklist.md).
