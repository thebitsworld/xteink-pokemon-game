# CLAUDE.md — Session Handoff Notes

Context notes for Claude in a later session (or on another machine) to continue this work without rediscovering everything from scratch. This file is NOT official project documentation — it's just a handoff notebook, and can be cleaned up/deleted once the in-progress work is done.

## Current status (as of 2026-09-12 — read this first)

- **Version `v0.4.0`**, on `main`, working tree clean. `main` is the only branch anyone should be working from right now — every feature branch below has already been merged in and none are currently active.
- **Everything already shipped**, newest first:
  - **`v0.4.0`** (2026-09-11): CrossInk engine synced to upstream **v1.5.1** (Quick Lock, custom boot screens, configurable page-turn gestures, tap-to-hide status bar, EPUB table rendering, a batch of touch/sleep/EPUB reliability fixes). Plus, unrelated to the CrossInk sync: swipe up/down paging for long lists on touch devices, real PokeAPI sprites decorating Battle/Bag/Badges, and the Pokémon main menu/Bag category picker/PC Box sort screen redrawn as button grids instead of plain text lists.
  - **`v0.3.1`** (2026-09-11): releases are now fully automated — pushing a `v*.*.*` tag builds both device firmwares in CI and publishes a GitHub Release with `CHANGELOG.md`'s matching section as the notes. Physical-hardware confirmation for X3 **and** X4 (not just X4 Pro).
  - **`v0.3.0`** (2026-09-10): **Xteink X4 Pro is a fully supported device** — full touch UI, portrait-only by design, confirmed on physical hardware (`7ddfc760`). Full writeup: [docs/development/pokemon-x4pro-roadmap.md](docs/development/pokemon-x4pro-roadmap.md) (all 5 phases done — the doc's own top line says so, don't skim past it as if it were still a live plan).
  - **`v0.2.0`** and earlier: the full turn-based battle system (gyms, Elite Four, catching, moves/TM/HM, items) — see [docs/development/pokemon-battle-roadmap.md](docs/development/pokemon-battle-roadmap.md).
  - **Full detail for all of the above, in the proper end-user changelog format, is in [CHANGELOG.md](CHANGELOG.md).** That file, not this one, is the authoritative record of what shipped in each version — prefer it over the historical narrative kept further down in this file.
- **No known open TODOs in code** — `grep -rn "TODO\|FIXME"` across `lib/Pokemon/`, `src/pokemon/`, `src/activities/pokemon/`, `src/components/pokemon/` returns nothing (checked 2026-09-12).
- **This machine (as of 2026-09-12) has none of the dev toolchain set up**: no `python`/`python3`, no PlatformIO, no `node`, and the `freeink-sdk`/`assets/tabler-icons` submodules are **not checked out** (`git submodule status` shows both with a `-` prefix). The "Quick build recipe" section below was written and verified on a *different*, Linux machine — its paths (`/home/vutq/...`) and assumptions (`~/.platformio/penv/bin` already populated) do not apply here as-is. Before attempting any build on this machine: install Python 3 and PlatformIO, run `git submodule update --init --recursive`, and expect first-run toolchain downloads to take a while. Don't assume the recipe's exact commands work unmodified on Windows/PowerShell — verify each step.
- **Standing conventions, still in force**: don't push to `origin` or publish a release without the user's explicit go-ahead in that session (release automation now exists via tag push, but *creating and pushing* the tag is still the user's call, not something to do autonomously). Commit code changes and doc/roadmap updates as separate commits. Only format newly-authored code with clang-format — never blanket-reformat pre-existing CrossInk files.

## What this project is

This is a fork of **CrossInk** (ESP32-C3/S3 firmware for the Xteink X3/X4/X4 Pro e-ink readers), extended with a **Pokémon game module** — Pokémon leveling up based on real reading time (not pure grinding). See [docs/pokemon-game.md](docs/pokemon-game.md) (user-facing doc) and [docs/development/pokemon-mechanics.md](docs/development/pokemon-mechanics.md) (detailed technical doc — **read this one first** to understand the game mechanics).

## Historical narrative (superseded by CHANGELOG.md — kept for implementation detail, not for current status)

Everything below this point describes work session-by-session, at a level of detail the end-user `CHANGELOG.md` deliberately doesn't carry (exact commits, flash-byte deltas, false starts, the reasoning behind a design choice). It is **all done and shipped** as of `v0.4.0` — none of it is "in progress." Use it when you need to know *why* something is built the way it is or *which commit* touched it; use `CHANGELOG.md` and the two roadmap docs when you just need to know *what currently exists*.

### Xteink X4 Pro support (shipped `v0.3.0`–`v0.3.1`, 2026-09-10/11)

Built on `feat/X4Pro-support`, merged to `main` at `6db52b9f`. The full 5-phase plan, hardware findings (SDK already supported the device; the two real risks were the missing common git ancestor with upstream CrossInk, and the hand-drawn Battle menus having no touch hit-regions), measured flash numbers per phase, and the complete session-by-session log (merge conflicts and their fixes, the touch-hit-test implementation, the smoke-test-script bugs found and fixed along the way, the landscape-vs-portrait scope decision) all live in **[docs/development/pokemon-x4pro-roadmap.md](docs/development/pokemon-x4pro-roadmap.md)** — every phase section states its own final status and result inline, so read it there rather than expecting a duplicate of that log here.

One thing surfaced during this work worth flagging on its own since it's not this fork's bug: **a real navigation bug in upstream CrossInk's own `v1.5.1-rc-6` tag** (confirmed byte-identical, not introduced by this merge) where a single-button `Buttons{X}` release/press handler also silently fires on Back, in `FileBrowserActivity.cpp` and `RecentBooksGridActivity.cpp`. Worked around locally; worth reporting upstream if that hasn't happened yet.

### Newer feature work after X4 Pro shipped, before the v1.5.1 sync (2026-09-11, all merged to `main`)

Four small feature branches, each merged individually, none documented anywhere except their own commit messages and the `CHANGELOG.md` `[0.4.0]` entry until now:

- **`feat/pokeapi-sprite-decor`** (`efca8c0f`): Battle now shows your own Pokémon from behind using real PokeAPI sprites (matching the classic games' perspective); Bag, Battle Bag, and Badges show an icon before each item/badge name instead of text-only rows. Follow-up fix (`e46006df`): `BattleSwitch`'s name text and HP bar were drawing on top of the icon.
- **`feat/touch-swipe-scroll`** (`adc4bdc9`): swipe up/down now pages through long lists (Bag, Pokédex, PC Box, reader menus) on touch-only devices (X4 Pro), matching what the physical Up/Down buttons already did on X3.
- **`feat/pokemon-menu-grid`** (`ea1890d1`): the Pokémon main menu and Bag category picker now render as 2-column button grids instead of plain text lists (`f649a402`). PC Box was tried as a grid too (`d2cc29ae`) but reverted back to a single-column list with icons after review, fixing a name-truncation bug along the way (`3f07c262`); its sort picker did become single-column buttons (`e6503ba3`).
- **Release pipeline hardening**, alongside the above: `fix/pokedex-detail-touch-back` (`707c2910`) added a touch-only back affordance to the Pokédex detail card, which deliberately skips the standard header to maximize card size — reuses the header's own back-button hit-region. Then release automation itself landed: tag-push-triggered CI builds (`861ac776`), OTA-compatible asset naming + failure diagnostics (`bc4c9b33`), and splitting each device into its own CI job after the combined job started exhausting CI disk space (`8f9762ca`).

### CrossInk upstream sync to v1.5.1 (shipped `v0.4.0`, 2026-09-11)

`feat/crossink-v1.5.1` merged tag `v1.5.1` of `uxjulia/CrossInk` (`39acaa1b`), then merged to `main` (`503fffe8`). This is a real upstream sync (not the ancestor-graft trick Phase 1 of the X4 Pro work needed — by this point `main` already had genuine shared history with upstream from that earlier merge), bringing in Quick Lock, custom boot screens, configurable page-turn gestures, tap-to-hide status bar, EPUB table rendering, and assorted reliability fixes. See `CHANGELOG.md`'s `[v1.5.1] - 2026-09-10` section (CrossInk's own changelog, reproduced there) for the complete upstream list.

### Fixes worth knowing the root cause of

- **Every Pokemon list screen was clipping its last row** (`95179ecc`, reported by the user as "the TM/HM row is missing" then corrected to "every menu screen is missing its last row, with a scrollbar now showing"): `buildList()`'s `screen.setContentMargin()` call computed its margin in raw-screen coordinates, but `Screen::setContentMargin()` insets from `frame_.safeRect()` (already shrunk by the device's 9px/3px top/bottom viewable margin) - so the margin got double-applied, silently shrinking the list's content rect by 12px below what `listBounds_` (`rowCount_ * rowHeight_`) promised, dropping exactly one row. Predates X4 Pro work entirely - traced to upstream's `v1.5.1-rc-6` merge (`414958d8`), which newly wired `safeArea` into that clamp; X3 was affected before any X4-Pro-specific change. Fixed by subtracting the same viewable margin back out in `buildList()`. Also fixed a related bug found while testing this: `scripts/dev/edit_pokemon_save.py`'s `add-party-member` bumped the save header's `recordCount` without updating the cached `payloadBytes` field, which `decodeSnapshotHeader()` independently checks on load - caused a real "save file corrupt" error (`a1089ade`).
- **Fainted Pokemon now require Revive/Max Revive** (`72ef328b`): `useConsumable()` previously let a plain Potion/Full Restore/status cure/PP restore quietly act on a fainted Pokemon (`currentHp == 0`). Fixed: those items now require `currentHp > 0` and return `NotApplicable` otherwise; only Revive/Max Revive (item ids 15/16) can act on a fainted one, restoring 50%/100% of max HP (their `effectValue` now read as a percentage for these two ids specifically) rather than curing status.
- **Wi-Fi OTA update pointed at the wrong repo** (`9952a2c5`): `pokemon-x3` never overrode `CROSSINK_OTA_RELEASE_URL`, so "Check for Update" silently pulled firmware from upstream `uxjulia/CrossInk` (matching asset name `firmware-x3-x4.bin`), which would flash a Pokemon-less build. Fixed by overriding the URL to this fork's own releases in that environment (and, since, in `pokemon-x4-pro` too).

### The Pokémon battle system (shipped `v0.2.0`, 2026-09-09, built on `feat/pokemon-battle-system`)

The full plan, technical constraints (flash budget, save-format append-only rules, the 48-byte `PokemonRecord` limit, i18n cost), design decisions with rationale, and a stage-by-stage task list (33+ stages, well beyond the original 8-stage roadmap, driven by real playtest feedback) all live in **[docs/development/pokemon-battle-roadmap.md](docs/development/pokemon-battle-roadmap.md)** — read that doc, not this section, for the detailed log. It covers: level-based move learning, items/TM/HM drops from reading, full turn-based battling with 6 status effects, catching with 4 ball types, 8 gyms + Elite Four in order, a Moveset management screen, switching mid-battle, using items mid-battle, and the Settings/Reset-Game screen (which also clears the auxiliary battle-data file so a reset game never inherits a previous playthrough's leftover HP/status).

**Constraints that are still load-bearing today** (i.e. still true post-v1.5.1-sync, still worth checking before touching save format or flash-sensitive code):
1. `PokemonState` can only be appended to **after byte 115** (`PokemonStore.cpp` hardcodes an offset for `sequence` before that point).
2. **Do not** widen `PokemonRecord` beyond 48 bytes — battle data (HP/PP/status/moveset) lives in a separate, double-buffered auxiliary file (`pokemon-battle-{a,b}.bin`).
3. Move/item names **do not** go through i18n (the per-language offset-table cost, and `gen_i18n.py --strip-unused` doesn't scan generated headers) — they're plain C strings in a generated header instead.
4. After editing anything under `lib/I18n/translations/*.yaml`, an incremental `pio run` can report "up to date" without actually rebuilding the generated i18n object — force a clean rebuild of that env before trusting behavior or flash numbers.

## Quick build recipe (verified on a Linux dev machine — re-verify before trusting on this one)

```sh
export PATH="$HOME/.platformio/penv/bin:$PATH"
cd /home/vutq/project/xteink-pokemon-game    # <- adjust to wherever this checkout actually lives
git submodule update --init --recursive      # required here - freeink-sdk/tabler-icons aren't checked out
pio run -e pokemon-x3        # Pokémon-enabled firmware, X3/X4 (ESP32-C3)
pio run -e pokemon-x4-pro    # Pokémon-enabled firmware, X4 Pro (ESP32-S3)
pio run -e default           # base firmware, no Pokémon — for size comparison
pio run -e pokemon-simulator-X3        # X3 simulator build (fast iteration, no device needed)
pio run -e pokemon-x4-pro-simulator    # X4 Pro simulator build
```

Real Flash/RAM numbers appear at the end of the build log (`RAM:`, `Flash:` percentages) and in the `check_firmware_size.py` output. Last confirmed numbers (2026-09-10, pre-`v1.5.1` sync, likely different now — remeasure): `pokemon-x3` 93.1% flash / 439,232 B free; `pokemon-x4-pro` 92.7% / 478,320 B free.

### Running the native test suite (much faster than a firmware build, no device needed)

PlatformIO bundles its own `cmake` if the machine has no system one:

```sh
export PATH="$HOME/.platformio/penv/bin:$HOME/.platformio/packages/tool-cmake/bin:$PATH"
cd test
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release   # fetches googletest on first run, takes a bit
cmake --build build -j4                          # builds every target (or --target <Name> for one)
cd build && ctest -R "Pokemon" --output-on-failure
```

Last confirmed count (2026-09-10, right after the `v1.5.1-rc-6` merge): **333/333** passing. `test/build/` is already in `.gitignore`.

### Simulator touch-testing tools (added during the X4 Pro work, still available)

`src/simulator/SimulatorSmokeTest.cpp` has scripted button- and touch-driven walkthroughs of the whole Pokémon UI, gated by env vars (`CROSSINK_SIMULATOR_SMOKE_TEST`, `CROSSINK_SIMULATOR_START_POKEMON`, `CROSSINK_SIMULATOR_POKEMON_BATTLE_TOUCH`, `CROSSINK_SIMULATOR_POKEMON_LANDSCAPE`) — see the X4 Pro roadmap doc's Phase 3 section for exact invocation and what each script covers. `scripts/dev/edit_pokemon_save.py` (full guide: [docs/development/pokemon-save-edit-tool.md](docs/development/pokemon-save-edit-tool.md)) can pre-seed a save (queue an encounter, add a party member, reset the battle store, etc.) so these scripts don't have to wait on real reading time.
