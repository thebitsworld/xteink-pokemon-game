# CLAUDE.md — handoff notes

Notes for Claude in a later session (or on another machine). Not project documentation: what shipped is in
[CHANGELOG.md](CHANGELOG.md), how things work is under [docs/](docs/). This file keeps what a new session
needs and would otherwise rediscover the hard way. The long session-by-session narrative it used to hold
is in git history (before 2026-10-08).

## Where things stand (2026-10-08)

- Fork of CrossInk (ESP32-C3/S3 firmware for Xteink X3/X4/X4 Pro e-ink readers) with a Pokémon game that
  levels from real reading time, and Lua apps installed from an App Store. `[crossink] version` 1.6.1,
  `[pokemon] version` 1.8.0-rc.2 in `platformio.ini`; latest tag `v1.8.0-rc.2`.
- `main` has a large `[Unreleased]` section: the Pokémon band/tile on every Home theme (selectable with
  the buttons; the menu's Pokémon entry only appears when the band is hidden), Settings > System >
  Features switches (Pokémon, Applications, Slideshow), Poké Ball / gamepad icons, and many Lua apps.
- Lua apps in `apps/` (the App Store installs straight from this folder on GitHub, so pushed apps are live
  without a firmware release): the 12 CrossPlay-inspired games, Bazaar, Whodunit, Dungeon Map,
  Tide & Paper, Calculator, Notes, Mind Dial, Forehead, Inner Circle, Cult Ledger, Toy Front, plus the
  CrossSmudge ones. CrossPlay apps needing data or a network (Trivia, Connections, Wikipedia, HN, xkcd,
  Instapaper, Study, Wallpapers) were skipped on purpose.
- Open, undecided: a translation mechanism for Lua apps (all English today; the user wants to discuss it
  first). Firmware strings: the fork's own Pokémon strings are untranslated in most languages (fall back
  to English); Vietnamese is complete.

## Standing conventions

- Commit locally; push, tag or release only when the user says so in that session. A `v*.*.*` tag push
  builds both firmwares in CI and publishes a release (a hyphenated tag is a prerelease). Push with
  `git -c credential.helper='!gh auth git-credential' push origin <ref>`; never run `gh auth setup-git`.
- Code and docs in separate commits. Ask before design-level changes, with a proposal. Only format code
  you wrote (no clang-format installed: match the style). Run builds one at a time.
- Never `git add` `images/`, `books/` or Bookerly fonts; no `custom_sdkconfig` on X4 Pro.
- The user writes Vietnamese; reply in Vietnamese.

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
