---
title: Xteink X4 Pro Support Roadmap
parent: Development
nav_order: 7
---

# Xteink X4 Pro Support — Roadmap

Handoff plan for bringing the Pokémon game to the Xteink X4 Pro, alongside the existing
X3/X4 build. Written so any session (this one or a fresh one, on any machine) can pick up
exactly one phase and execute it without needing the rest of this conversation's context.

**All 5 phases are done and merged to `main`** (via `feat/X4Pro-support`, merge commit
`6db52b9f`), released starting at `v0.3.0` (2026-09-10) and confirmed on physical X4 Pro
hardware (`7ddfc760`). This document is kept as a historical record and as the template for
any *next* new-device port — read a phase's own section for what actually happened and why,
rather than treating anything here as still-pending work. For the project's current overall
status, see `CLAUDE.md`'s "Current status" section and `CHANGELOG.md`.

---

## Why this project needs it, and what's already true

The game currently ships one firmware (`pokemon-x3`) for the Xteink X3/X4, both ESP32-C3
devices sharing one binary. The **Xteink X4 Pro** is a completely different device — an
ESP32-S3 (Xtensa, 8MB PSRAM) with an 800×480 panel, capacitive touch (GT911), and only
two navigation buttons (Left/Right) plus Power — no physical confirm/back buttons, no
D-pad. CrossInk upstream (`uxjulia/CrossInk`) added full X4 Pro support starting at tag
`v1.5.1-rc-6`; this fork is currently based on CrossInk **1.5.0**, one release behind.

Goal: a `pokemon-x4-pro` firmware environment, released as a separate `firmware-x4-pro.bin`
asset alongside the existing `firmware-x3-x4.bin`, with the whole Pokémon game playable via
touch (X4 Pro has no physical d-pad to fall back on).

### Findings from the initial evaluation (2026-09-10) — read before starting

These are measured facts, not guesses. Re-verify anything time-sensitive (upstream tags,
SDK commit) since this doc may be read much later.

1. **`freeink-sdk` (the hardware SDK submodule) already fully supports the X4 Pro.**
   `BoardConfig::XTEINK_X4_PRO`, the SSD1677/UC8179 display driver, GT911 touch, SDMMC SD
   card, dual-channel frontlight — all confirmed working on real hardware per
   `freeink-sdk/docs/xteink-x4pro-support.md` ("All peripherals are up"). No SDK-level
   hardware work is needed.
2. **This fork's app code already has partial X4 Pro awareness**: `src/main.cpp`
   (`handleX4ProFrontlightDoubleClick`, recovery-mode key combo), `SettingsActivity.cpp`,
   `HalGPIO.cpp`, `UIThemeTokens.h`, `MappedInputManager.cpp` all reference
   `BoardConfig::isX4Pro()` / `SIMULATOR_DEVICE_X4PRO` already. This came from CrossInk 1.5.0
   itself, not from this fork's own work — it means the base CrossInk layer is already
   touch/X4-Pro-aware; only the **Pokémon-specific** UI needs new work.
3. **A simulator environment for X4 Pro already exists**: `[env:x4-pro-simulator]` in
   `platformio.ini`. It has no Pokémon flags yet, but proves the profile itself works and
   gives an easy way to eyeball the 800×480 layout without hardware.
4. **Pokémon UI is already largely resolution-adaptive**: ~23 call sites in
   `src/activities/pokemon/PokemonActivity.cpp` use `renderer.getScreenWidth()` /
   `getScreenHeight()` rather than hardcoded X3 dimensions.
5. **Artwork does not need to be regenerated.** Pokédex cards are pre-rendered at
   472×708 (portrait) / 288×432 (landscape) — both fit inside 528×792 (X3) and 480×800
   (X4 Pro) equally. Species icons/sprites are small and already screen-independent.
6. **The release scripts are already device-agnostic.** `scripts/rename_firmware.py` names
   the output firmware from `custom_firmware_device_type` (a per-env PlatformIO option), so
   an `[env:pokemon-x4-pro]` with `custom_firmware_device_type = x4-pro` automatically
   produces `firmware-x4-pro.bin`, which the OTA updater's asset-name matcher
   (`isMatchingFirmwareAssetName()` in `src/network/OtaUpdater.cpp`) already recognizes by
   stem-prefix matching — no changes needed there either.
7. **Flash budget is not a constraint, and this was measured, not estimated.** A real build
   of `pio run -e sticky` (same ESP32-S3 MCU family, no Pokémon module) produced:

   | Build | Flash used | % of 6,553,600 B | Free |
   |---|---|---|---|
   | `default` (C3, no Pokémon) | 6,261,393 B | 95.5% | 280,208 B |
   | `pokemon-x3` (C3, with Pokémon) | 6,368,384 B | 97.2% | 185,216 B |
   | `sticky` (S3, no Pokémon) | 6,069,488 B | 92.6% | 484,112 B |

   The Pokémon module costs +106,991 B on the C3 build. Projected onto the S3 baseline:
   ≈6,069,488 + 107,000 ≈ 6.18 MB (~94%), leaving **~370-450 KB free — more headroom than
   X3 currently has.** Flash is not expected to block this work at any phase.
8. **Two real risks, not flash:**
   - **No shared git history with upstream CrossInk.** This fork has only ~114-120 commits;
     its very first commit (`ff8f841e`, message "Xteink Pokemon Game v0.1.0") is a **squashed
     snapshot** of CrossInk 1.5.0 + the Pokémon module, not a fork with preserved history. A
     plain `git merge <upstream-tag>` has no common ancestor and would conflict on
     essentially every one of CrossInk's ~630+ files. Phase 1 below handles this with a
     `git replace --graft` before merging, so the merge becomes a real 3-way merge instead
     of an all-file conflict.
   - **The Battle screen menus are hand-drawn, not touch-enabled.** `isListScreen()` in
     `PokemonActivity.cpp` explicitly excludes `Screen::Battle` and `Screen::BattleMoves`
     because they draw their own 2-column button grid (`renderBattleMenu()` /
     `renderBattleMoveMenu()`) instead of going through the generic `fui::list()` widget,
     which is the thing that already carries touch support
     (`props.inputMask = fui::InputTouch`). Since the X4 Pro has no physical confirm button
     to fall back on, these two screens are unplayable on X4 Pro until they get touch hit
     regions. This is Phase 3's main item.

---

## How to use this document across sessions

- Each phase states **Status**, **Prerequisites**, **Goal**, **Steps**, **Files touched**,
  and **Definition of done** (concrete, runnable verification).
- Update the **Status** line of a phase to `Done (commit <hash>)` when it's finished, and
  add a short note under it the same way `CLAUDE.md` and `pokemon-battle-roadmap.md`
  already do for the Pokémon battle system — a few sentences on what actually happened,
  any deviation from the plan, and the measured result (flash %, test pass count, etc.).
  This is how a later session picks up the trail without replaying this conversation.
- Do not start a phase whose prerequisites aren't met — check the actual repo state
  (`git log`, `git branch`, file existence), not just this document, since it may be stale.
- Regenerate any time-sensitive fact (upstream tag hashes, SDK submodule commit) if it's
  been more than a few weeks since the "Findings" section above was written.

---

## Phase 1 — Establish a real merge ancestor, then merge upstream CrossInk v1.5.1-rc-6

**Status:** Done (commit `414958d8` on `feat/X4Pro-support`, build/test verification below)

A prepared remote branch `origin/upstream-history` (1608 commits of real CrossInk/CrossPoint
Reader history, not something this session created) turned out to already exist and made
this far more precise than the plan above assumed. Its tip (`e5f6b6e5`, "chore: clear
formatter and static-analysis debt") matched this fork's squashed root commit (`ff8f841e`,
"Xteink Pokemon Game v0.1.0") almost exactly outside Pokémon-specific paths — the only
difference was internal planning docs (`docs/plans/*`, `AGENTS.md`) deleted as part of the
public-release squash. `e5f6b6e5` in turn has *real* (not grafted) ancestry back through
`0de623ed` (`v1.5.0`, hash-for-hash identical to the real upstream tag), which is a real
ancestor of `v1.5.1-rc-6`. So the graft target was `e5f6b6e5`, not `v1.5.0` directly as
originally planned — a closer match produces a more accurate merge-base and fewer spurious
conflicts. `git merge-base` correctly resolved to `4b2199b7` ("Update release manifests for
v1.5.0", one commit past the tag) automatically once grafted.

Result: **61 conflicted files**, not ~630 — confirming the graft approach worked. Handled as:
- **3 files kept deleted** (`AGENTS.md`, `.github/workflows/release.yml`,
  `.github/workflows/release_candidate.yml`) — these existed pre-squash and were
  deliberately removed for the public repo (internal dev notes; CrossInk-org-targeting CI
  workflows that `docs/release-checklist.md` already warns must not accidentally publish
  CrossInk/Sticky/X4/Pages artifacts from this fork).
- **50 files taken wholesale from upstream** — confirmed via `git log ff8f841e..HEAD
  --name-only` that none of them were touched by this fork's own Pokémon-era commits, so
  upstream's version is authoritative.
- **8 files merged by hand** because this fork's own commits touched them:
  `README.md`, `CHANGELOG.md`, `docs/development/README.md`, `docs/file-formats.md`,
  `docs/installation.md`, `src/components/themes/lyra/LyraCarouselTheme.cpp`,
  `src/simulator/SimulatorSmokeTest.cpp`, `test/CMakeLists.txt`. Notable non-trivial ones:
  - `docs/file-formats.md` had a genuine version-number collision: this fork's own
    abandoned "1.6 parity experiment" branch had independently bumped the `section.bin`
    cache format to its own "Version 63" (image-margin clamping) via a bespoke
    `lib/Epub/Epub/SectionCacheFormat.h` abstraction, while real upstream's "Version 63"
    means something entirely different (paragraph base direction) en route to their real
    "Version 66". Since the actual `Section.cpp`/`ParsedText.cpp` code was taken wholesale
    from upstream (untouched by this fork), `SectionCacheFormat.h` became fully orphaned
    dead code — deleted it, and trimmed the one test
    (`test/footnote_cache/FootnoteCacheTest.cpp`) that still referenced it, keeping the
    still-valid `footnote_cache::` assertions in that same test.
  - `src/simulator/SimulatorSmokeTest.cpp` had a real rename collision: this fork's Pokémon
    smoke-test work had renamed `SmokeStep::ReaderInput`/`runReaderInputScript()` to the more
    generic `SmokeStep::InputScript`/`runInputScript()` (since Pokémon's own smoke script
    reuses the same runner). Upstream independently generalized the same function via a new
    `inputCompletionStep` member (defaulting to `Done`) to support a new FileBrowserSettings
    flow. Kept this fork's naming (already used by multiple call sites) and adopted
    upstream's `inputCompletionStep` generalization into it. Also had to fix one orphaned
    `SmokeStep::ReaderInput` reference inside upstream's new `buildFileBrowserInputScript()`
    that git auto-merged cleanly (no conflict marker) but referenced an enum value this
    fork had already renamed away — a latent compile error a text-based merge cannot catch;
    caught it here by tracing every remaining reference by hand.
  - `README.md`/`docs/installation.md`: kept this fork's Pokémon-focused content entirely,
    dropped upstream's generic CrossInk sections (device list, feature list, Inky
    install instructions, Nix dev shell, PR policy, Ko-fi ask) — none of that applies to
    this fork's own product README, and re-adding an X4 Pro device claim here is Phase 5's
    job once the device is actually supported, not Phase 1's.
- New submodule `assets/tabler-icons` (added by upstream) initialized via
  `git submodule update --init --recursive`; `freeink-sdk` bumped to the commit the merge
  staged automatically (`2400379`, "Merge pull request #72 ... fix/onepage-sd-power-rail").
- The temporary `git replace --graft` was removed after the merge commit landed — it was
  only needed to compute a correct 3-way merge; the merge commit's second parent
  (`v1.5.1-rc-6` itself) is a permanent, real link into upstream history from here on,
  verified by `git merge-base --is-ancestor v1.5.0 HEAD` still returning true with the
  graft deleted.

**Verification result (commit `ec666a15`, same session)**: the initial merge commit
(`414958d8`) built and passed the 61-file conflict resolution cleanly, but a full build/test
pass surfaced ~10 more latent breakages that git's line-based merge couldn't catch — every
one was a case where this fork's abandoned pre-squash "1.6 parity experiment" and upstream's
real implementation touched the *same file* in non-overlapping hunks, so git combined them
without a conflict marker even though the result didn't compile or didn't match either
side's real intent. Found and fixed by actually building, not by re-reading diffs:
- `platformio.ini`: `pokemon-x3`/`pokemon-simulator-X3` (this fork's own envs, invisible to
  upstream's restructuring) were missing the now-mandatory `-DARDUINO_USB_MODE=1` /
  `-DCROSSINK_APP_CAP_USB_DRIVE=0` (moved out of `[base]` into each env; newly enforced by
  `include/AppCapabilities.h`'s `#error` guards). Also bumped the pinned `crossink-simulator`
  SDK commit (`d07c681` → `95be4c2`, "fix: add missing shutdown shims") since the merged
  `main.cpp` calls `Storage.shutdown()`, which the old-pinned simulator stub didn't have yet.
- `GfxRenderer`/image-decoder API drift: `PokemonArt.cpp`, `BaseTheme.cpp`,
  `ImageDimsProbe.cpp`, `Jpeg`/`PngToFramebufferConverter.cpp`, and `ButtonNavigator.cpp` all
  called APIs (`BitmapBwPolicy`, `getTextPixelHeight`, `validateAndStoreDimensions`, the old
  `initializer_list`-based `ButtonNavigator::Buttons`) that only existed in this fork's own
  abandoned experiment, removed/redesigned in upstream's real code. Updated call sites to the
  current API, or took the whole file from upstream where nothing fork-specific depended on it.
- A **real navigation bug already present in upstream's own tagged v1.5.1-rc-6** (confirmed
  byte-identical to the tag, not introduced by this merge): `Button::Back` is enum value `0`,
  so `Buttons{X}` (upstream's `Buttons` is now a fixed 2-slot `std::array`) leaves the second
  slot value-initialized to `Back` — a single-button release/press handler also silently
  fires on Back. `FileBrowserActivity.cpp` and `RecentBooksGridActivity.cpp` both did this for
  every directional button; fixed by duplicating the button (`Buttons{X, X}`), matching how
  real 2-button call sites already do it safely. Worth an upstream bug report at some point.
- `scripts/build_web.py`/`CrossPointWebServer.cpp`: kept the generated web-page byte arrays as
  `uint8_t[]` (this fork's own choice) rather than upstream's `char[]`, since host/x86 `char`
  is signed and the same gzip bytes that compile fine as unsigned on the ESP32 toolchain
  become narrowing-conversion errors past 127 on the simulator build. `reinterpret_cast` to
  `const char*` only at the 4 `send_P`/`sendHtmlContent` call sites that need it.
- Deleted two native tests that only ever existed in this fork's own abandoned history (never
  upstream, unrelated to Pokémon): `ClippingHighlightGeometryTest.cpp` (pinned to a
  now-replaced constexpr redesign) and the `validateAndStoreDimensions`-based half of
  `ImageDecoderSafetyTest.cpp` (that safety check now lives in a protected, no-longer
  directly-testable method). Added a `vTaskDelay` test stub the surviving
  `yieldDuringDecode()` assertions needed once `ImageToFramebufferDecoder.cpp` came from
  upstream (calls `vTaskDelay` directly instead of Arduino's `delay()`).

**Confirmed green**: `pio run -e pokemon-x3` succeeds — **Flash 93.1% used, 439,232 B free**,
down from the pre-merge 97.0%/185,216 B thanks to upstream's font-compression work (a big,
unplanned win for X4 Pro's own headroom too). Native suite: **333/333 tests pass**. Simulator
(`pio run -e pokemon-simulator-X3 -t run_simulator`) boots, runs its full smoke script
including the Pokémon activity, and exits cleanly (only pre-existing "missing art asset" log
lines from the incomplete local sandbox, unrelated to this merge).

**Phase 1 is fully done.** Safe to start Phase 2.

**Prerequisites:** none (this is the first phase). Must run before Phase 2 (X4 Pro needs
the upstream env as a template) but Phase 3/4 (touch, layout) can technically be prototyped
against the existing `x4-pro-simulator` env without waiting for this phase, if someone
wants to parallelize.

**Goal:** Bring this fork's CrossInk base up to `v1.5.1-rc-6` (or later, if a newer stable
tag exists by the time this runs), so the upstream `[env:x4-pro]` hardware environment,
touch/dark-mode/USB-MSC work, and any bug fixes become available to build on — without
losing any of this fork's ~114+ Pokémon-related commits or its own fixes (OTA release URL
override, README rewrite, etc.).

**Steps:**

1. Add the upstream remote and fetch it (read-only, safe):
   ```sh
   git remote add crossink-upstream https://github.com/uxjulia/CrossInk.git
   git fetch crossink-upstream --tags
   ```
2. Confirm the fork's actual base version. Check `[crossink] version =` in `platformio.ini`
   (was `1.5.0` as of this writing) and diff the fork's very first commit against the
   matching upstream tag, **excluding Pokémon-specific paths**, to confirm they match:
   ```sh
   git log --oneline | tail -5    # find the squashed root commit, e.g. ff8f841e
   git diff ff8f841e crossink-upstream/v1.5.0 -- \
     ':!lib/Pokemon' ':!src/pokemon' ':!src/activities/pokemon' \
     ':!src/components/pokemon' ':!scripts/*pokemon*' ':!docs/*pokemon*' \
     ':!CLAUDE.md' ':!docs/development/pokemon*'
   ```
   A small/empty diff confirms the base is v1.5.0. If it's not empty or not v1.5.0, find
   the correct matching tag first — don't assume.
3. **Graft the ancestor** so git treats the matched upstream tag as this fork's root's
   parent (this changes how `git merge`/`git log` compute ancestry locally — it does not
   rewrite any commit the fork already has, and is trivially reversible with
   `git replace -d`):
   ```sh
   git replace --graft ff8f841e crossink-upstream/v1.5.0   # use the actual root hash + confirmed tag
   ```
4. Work on the dedicated branch (create if not already on it):
   ```sh
   git checkout -b feat/X4Pro-support   # or: git checkout feat/X4Pro-support if it exists
   ```
5. Merge the target upstream tag:
   ```sh
   git merge crossink-upstream/v1.5.1-rc-6
   ```
   This is now a genuine 3-way merge — conflicts should only appear where both sides
   touched the same lines, not on every file.
6. Resolve conflicts. Expect the highest-conflict-risk files to be:
   - `platformio.ini` — **keep** `[env:pokemon-x3]` and its
     `CROSSINK_OTA_RELEASE_URL` override (see `docs/pokemon-mechanics.md` / `CLAUDE.md` for
     why that override exists — it must not regress), while accepting upstream's new
     `[env:x4-pro]` / `[env:x4-pro-debug]` / `[env:x4-classic]` sections and any base-section
     changes.
   - `src/main.cpp`, `src/MappedInputManager.cpp`/`.h`, `src/activities/settings/SettingsActivity.cpp`
     (this fork inserted a Pokémon menu entry here), `src/components/UITheme*` — resolve by
     keeping both sides' additions (fork's Pokémon hooks + upstream's X4 Pro/dark-mode work).
7. Bump the `freeink-sdk` submodule to the commit upstream v1.5.1-rc-6 pins, then
   `git submodule update --init --recursive`.
8. **Gate**: do not proceed to any other phase until this passes cleanly.

**Files touched:** `platformio.ini`, `src/main.cpp`, `src/MappedInputManager.cpp/.h`,
`src/activities/settings/SettingsActivity.cpp`, `src/components/UITheme*`, `freeink-sdk`
(submodule pointer), plus whatever else the real merge conflicts on (do not predict the
full list — resolve what `git merge` actually flags).

**Definition of done:**
```sh
export PATH="$HOME/.platformio/penv/bin:$PATH"
pio run -e pokemon-x3                          # must build clean, no size-check failure
cd test && ctest -R Pokemon --output-on-failure # must be 19/19 (or current count) passing
pio run -e pokemon-simulator-X3 -t run_simulator # must boot without crash/error in the log
```
Record the resulting flash % for `pokemon-x3` in this doc's phase note (compare against the
pre-merge baseline to catch any unexpected regression from the upstream bump itself).

---

## Phase 2 — Add the `pokemon-x4-pro` hardware environment

**Status:** Done (commit `2ce2a09e`)

Both `[env:pokemon-x4-pro]` and `[env:pokemon-x4-pro-simulator]` built clean on the first
try — no include/config fixes needed, confirming the Pokémon lib code really is
MCU-agnostic. The real, merged-in `[env:x4-pro]` differed from this doc's pre-merge
template in a few load-bearing ways worth noting for later phases: it already carries
`-DARDUINO_USB_MODE=1` (the exact flag `pokemon-x3` was missing back in Phase 1),
`-DUSB_PRODUCT`/`-DUSB_MANUFACTURER` strings, and `-DFREEINK_FB_PSRAM=1` (framebuffer in
PSRAM) — all copied over as-is. `[env:pokemon-x4-pro-simulator]` extends `simulator-base`
directly and re-declares the X4 Pro simulator flags itself, mirroring exactly how
`[env:pokemon-simulator-X3]` already does it (PlatformIO's `extends` needs the `env:`
section referenced as a "base" section like `simulator-base`, not another `env:` section by
bare name — attending an `[env:x4-pro-simulator]` env directly is not valid `extends`
syntax, confirmed by an immediate parse error when first tried).

Verified:
```
pio run -e pokemon-x4-pro                              # Flash 92.7%, 478,320 B free
pio run -e pokemon-x4-pro-simulator -t run_simulator    # boots, runs clean
pio run -e pokemon-x3                                   # still clean, unaffected
```

**Phase 2 is fully done.** Safe to start Phase 3 (touch support).

**Prerequisites:** Phase 1 done (needs the upstream `[env:x4-pro]` section as a template).

**Goal:** A buildable `[env:pokemon-x4-pro]` PlatformIO environment producing a flashable
X4 Pro firmware with the Pokémon module enabled, plus a matching simulator environment for
fast iteration without hardware.

**Steps:**

1. In `platformio.ini`, add `[env:pokemon-x4-pro]` modeled on upstream's `[env:x4-pro]`
   (added by Phase 1's merge) plus the Pokémon flags/scripts from the existing
   `[env:pokemon-x3]`:
   ```ini
   [env:pokemon-x4-pro]
   extends = base
   board = esp32-s3-devkitc1-n16r8
   board_build.mcu = esp32s3
   board_build.arduino.memory_type = dio_opi
   custom_firmware_device_type = x4-pro
   extra_scripts =
     ${base.extra_scripts}
     pre:scripts/generate_pokemon_v2_species_build.py
     pre:scripts/generate_pokemon_moves_build.py
     pre:scripts/generate_pokemon_stats_build.py
     pre:scripts/generate_pokemon_learnsets_build.py
     pre:scripts/generate_pokemon_items_build.py
     pre:scripts/generate_pokemon_gyms_build.py
   build_flags =
     ${base.build_flags}
     -DFREEINK_DEVICE_X4PRO=1
     -DCROSSINK_APP_CAP_TOUCH=1
     -DFREEINK_CAP_USB_MSC=1
     -DCROSSINK_APP_CAP_USB_DRIVE=1
     -DUSE_BLOCK_DEVICE_INTERFACE=1
     -DCROSSINK_FIRMWARE_DEVICE_TYPE=\"x4-pro\"
     -DCROSSINK_PRODUCT_XTEINK_POKEMON=1
     -DCROSSINK_ENABLE_POKEMON=1
     -DCROSSINK_POKEMON_DEFAULT_ENABLED=1
     -DCROSSINK_POKEMON_BETA_TOOLS=1
     ; Same reasoning as pokemon-x3's override - see CLAUDE.md "Recent fixes" -
     ; Wi-Fi "Check for Update" must pull from this fork's releases, not upstream's.
     -DCROSSINK_OTA_RELEASE_URL=\"https://api.github.com/repos/thebitsworld/xteink-pokemon-game/releases/latest\"
     -DENABLE_SERIAL_LOG
     -DLOG_LEVEL=1
   ```
   Double-check every flag against whatever upstream's actual merged-in `[env:x4-pro]`
   ends up looking like post-Phase-1 — the block above is a starting template from the
   pre-merge evaluation, not a guaranteed-current copy.
2. Add `[env:pokemon-x4-pro-simulator]` extending the existing `[env:x4-pro-simulator]` the
   same way `[env:pokemon-simulator-X3]` extends `[env:simulator-X3]` — copy that pattern.
3. Build it for the first time; expect to fix small include/config issues, not architecture
   problems (the Pokémon lib code itself is MCU-agnostic — it was already proven to compile
   fine standalone via the native test suite).

**Files touched:** `platformio.ini` only.

**Definition of done:**
```sh
export PATH="$HOME/.platformio/penv/bin:$PATH"
pio run -e pokemon-x4-pro                # builds clean; record Flash %
pio run -e pokemon-x4-pro-simulator -t run_simulator  # boots without crash
```
Also re-run the Phase 1 X3 verification commands to confirm adding the new env didn't
disturb the existing one.

---

## Phase 3 — Touch support for the Pokémon UI

**Status:** Done (commits `e8ab0693`, `1d69ea03`, `c387b648`, `afa7a891`, `8b98b5f4` on
`feat/X4Pro-support` - see `CLAUDE.md`'s Phase 3 section for the full detail). Battle-grid
touch + Message tap-to-dismiss added, then every list screen (including the battle-only
ones - ItemTarget/BattleSwitch/BattleBag/BattleBalls, which needed a pre-seeded save via
`edit_pokemon_save.py`) verified end to end with an automated touch script on
`pokemon-x4-pro-simulator`. `pokemon-x3` reconfirmed unaffected throughout (touch-gate audit
passes, flash unchanged). A pre-existing, unrelated smoke-test navigation bug (dating back to
Stage 10) was found and fixed along the way.

**Prerequisites:** Phase 2 done (need a buildable X4 Pro env — with `CAP_TOUCH=1` — to test
against; can partially prototype earlier against `x4-pro-simulator` alone if desired, but
can't confirm the Pokémon-specific gate below without the Pokémon flags on).

**Goal:** Every Pokémon screen operable by touch alone, since the X4 Pro has no physical
d-pad. This is the one phase with no shortcut — it is real, new UI code.

**Constraint to respect:** `scripts/check_app_touch_gate.py` runs as a post-build check and
**fails the build** if any touch-related symbol (`TouchRegistry`, `HalGPIO::hasTouch`, etc. —
see the script for the full list) links into a firmware built with `CAP_TOUCH=0` (i.e. the
X3 build). Any new touch code must go through the same conditional-compile pattern the
existing `TouchHeaderBackButton` / `mappedInput` touch helpers already use, so it compiles
to nothing on X3. Verify this by rebuilding `pokemon-x3` after each change in this phase —
the gate script will catch a violation immediately, with no ambiguity.

**Steps, in priority order:**

1. **Mandatory: the Battle button grids.** `renderBattleMenu()` and
   `renderBattleMoveMenu()` in `src/activities/pokemon/PokemonActivity.cpp` hand-draw a
   2-column grid (constants `BATTLE_MENU_COLUMNS`, `BATTLE_MENU_ROW_HEIGHT`, and the
   per-cell `buttonWidth`/`buttonHeight`/x/y math already computed at draw time). Add a
   hit-test that reuses this exact same math to map a touch coordinate to a button index,
   then routes to the same `activate()` path the physical-button selection already uses.
   Do not duplicate the layout arithmetic — extract/reuse it so the drawn cell and the
   tappable cell can never drift apart.
2. **Read-only screens**: `Screen::Summary`, `Screen::PokedexDetail`, `Screen::Message`
   (the three screens where `logicalCount()` returns 0). Confirm `TouchHeaderBackButton`
   already lets the player leave each of these by tap, and that `Screen::Message` can be
   dismissed by tap (it may currently only respond to a physical confirm button - check
   `activate()`'s handling for `Screen::Message`).
3. **List screens.** Every other Pokémon screen already renders through the shared
   `fui::list()` widget with `props.inputMask` including `fui::InputTouch` — but this has
   never actually been exercised with `CAP_TOUCH=1` before (X3 has no touch). Rebuild with
   `pokemon-x4-pro-simulator` and click through every list screen (Party, PC, Bag and its
   sub-categories, Pokédex, Moveset, GymList, Badges, ItemTarget, BattleSwitch, BattleBag,
   BattleBalls...) to confirm row height is comfortably tappable and the selection marker
   still renders correctly at the X4 Pro's `listScrollInset` (already defined per-device in
   `UIThemeTokens.h`).
4. Cross-check against how a couple of *non-Pokémon* CrossInk activities already handle
   touch (they've had it since before this fork existed) to match established convention
   rather than inventing a new one — do not guess at gesture/hit-region sizing from
   scratch.

**Files touched:** `src/activities/pokemon/PokemonActivity.cpp` (hit-test additions),
`src/activities/pokemon/PokemonActivity.h` (new helper declarations) — should not need to
touch `lib/Pokemon/*` (pure game logic, no UI).

**Definition of done:**
```sh
export PATH="$HOME/.platformio/penv/bin:$PATH"
pio run -e pokemon-x3                # MUST still build clean - touch-gate script is the check
pio run -e pokemon-x4-pro-simulator -t run_simulator
```
Manually drive the X4 Pro simulator with mouse clicks (standing in for touch) through the
full loop: starter selection → Party → wild encounter → Battle (tap FIGHT, tap a move, tap
BAG, tap an item, tap a target) → gym challenge → Badges → Pokédex. Every step must be
reachable without any keyboard/physical-button input.

---

## Phase 4 — Layout fixes for 800×480

**Status:** Scope changed by product decision (2026-09-10): **Pokemon is portrait-only, on
both X3 and X4 Pro - landscape is out of scope for now**, to revisit later. See `CLAUDE.md`'s
Phase 4 section for the full detail. Before this was decided, landscape was investigated and
confirmed broken on both devices (only ~172-173px available for the Battle HUD in landscape
on either device, not just X4 Pro), and a working compact-layout fix was written and verified
(commit `b2f4d920`) - it's kept in the code (currently unreachable) in case landscape support
resumes later. `PokemonActivity::onEnter()` now force-sets Portrait orientation unconditionally
(commit `d4c46297`), so this phase's remaining scope is just confirming portrait looks right
on both devices - already largely covered by Phase 1's fix and the generic width/height-derived
layout formulas (X4 Pro portrait, 480x800, has *more* room than X3 portrait, 528x792, not
less, so nothing X4-Pro-specific has been needed so far). Battle/Party/Summary spot-checked
clean in portrait; Pokédex detail not yet visually confirmed but its rendering path doesn't
depend on anything changed this phase.

**Prerequisites:** Phase 2 done (need the X4 Pro simulator to see the actual layout).
Can run in parallel with Phase 3 if two sessions split the work, since they touch mostly
disjoint concerns (hit-testing vs. sizing) — but both edit `PokemonActivity.cpp`, so
coordinate to avoid merge conflicts between the two efforts, or do them in one pass.

**Goal:** The Battle HUD and every other screen fit inside 800×480 without clipping,
matching quality already reached on X3 through Stages 16-27 of the original battle
roadmap (see `docs/development/pokemon-battle-roadmap.md`) — this phase is likely to need
a similarly iterative "build, look, adjust" loop, not a one-shot fix.

**Why this is the real risk, with numbers:** X3 is 792×528. X4 Pro is 800×480 — 8px wider
but **48px shorter**. Landscape orientation (528→480 height, i.e. losing height on what was
already the tighter axis) is the danger zone, exactly where Stages 16, 17, 24, 26, and 27
of the original battle-system work already needed multiple rounds of fixes on X3 alone.
Rough tally of the current landscape Battle HUD's fixed-height budget on X3
(`src/activities/pokemon/PokemonActivity.cpp`): two `panelHeight` boxes (92px each) + a
fixed-height battle log (90px) + a 2-row button menu (`BATTLE_MENU_ROW_HEIGHT` 64px × 2) ≈
402px of hard-coded vertical space alone, before header/hints/padding — already using
most of X3's 528px. On X4 Pro's 480px this **will not fit** without adjustment.

**Steps:**

1. Inventory every hardcoded vertical constant in `PokemonActivity.cpp` that isn't already
   derived from `getScreenHeight()`: `BATTLE_MENU_ROW_HEIGHT` (64), the Battle HUD
   `panelHeight` (92), the battle-log box height (90), `messageHeight` (160),
   `rowHeightForScreen()`'s two fixed values (96/64). There may be more by the time this
   phase starts — grep for hardcoded pixel literals the same way the initial evaluation did:
   `grep -oE "= *[0-9]{2,4};" src/activities/pokemon/PokemonActivity.cpp | sort | uniq -c`.
2. Convert the ones that are load-bearing for the vertical budget into values computed
   from `renderer.getScreenHeight()` (or looked up per-device via `UITheme`, whichever
   pattern the rest of the file already leans on) so the **same code** produces a
   comfortable X3 layout at 528px and a comfortable X4 Pro layout at 480px, rather than
   shipping a device-specific branch for every constant.
3. Iterate visually: `pio run -e pokemon-x4-pro-simulator -t run_simulator`, side by side
   with `pio run -e pokemon-simulator-X3 -t run_simulator` open at the same time, to catch
   any X3 regression immediately rather than at the end.

**Files touched:** `src/activities/pokemon/PokemonActivity.cpp` primarily; possibly
`lib/Pokemon/PokemonUiLayout.cpp` if the shared list-presentation constants
(`pokemonListPresentation()`) also need device-aware values.

**Definition of done:**
```sh
export PATH="$HOME/.platformio/penv/bin:$PATH"
pio run -e pokemon-x3
pio run -e pokemon-x4-pro
cd test && ctest -R Pokemon --output-on-failure   # layout constants aren't usually test-covered, but re-run for safety
pio run -e pokemon-simulator-X3 -t run_simulator
pio run -e pokemon-x4-pro-simulator -t run_simulator
```
Visually confirm on both simulators, side by side, in portrait only (landscape is out of
scope - see the Status note above): Battle screen, Party list, Summary, Pokédex detail —
nothing clipped, no overlap, selection marker fully visible.

---

## Phase 5 — Release

**Status:** Done (commits `0f42ca1f`, `2afa7705`, `ab7b94af`, `d9d6b56b` — released as
`v0.3.0`, tag `9656361d`/`4f2da6b7` note superseded by later `v1.5.1` naming, see below)

Both `scripts/build_pokemon_release.py` and `scripts/package_pokemon_v2_release.py` were
made device-aware (took a `--device`/equivalent argument rather than assuming X3), producing
`firmware-x4-pro*.bin` alongside the existing `firmware-x3-x4*.bin`. `README.md`/
`docs/installation.md` updated to list X4 Pro as supported, with an explicit warning that
the two firmware files are not interchangeable. `CHANGELOG.md` `[0.3.0]` entry added.

**Since this phase, releasing got fully automated** (commits `861ac776`, `bc4c9b33`,
`8f9762ca`, `v0.3.1`): pushing a `v*.*.*` tag now builds both device firmwares (each in its
own CI job, after `8f9762ca` fixed disk exhaustion from building both in one job), packages
firmware-only assets + checksums, and publishes a GitHub Release using the matching
`CHANGELOG.md` section as its notes automatically. Asset names later changed from
`xteink-pokemon-x3-*` to `xteink-pokemon-x3-x4-*` (`v0.3.1`) to make explicit both X3 and X4
share the one build. Manual `build_pokemon_release.py` invocation (the steps below) is now
only needed for a local test build, not for a real release.

<details>
<summary>Original steps (kept for reference / local test builds)</summary>


1. Build both environments and run the existing release packaging script for each:
   ```sh
   python3 scripts/build_pokemon_release.py --firmware .pio/build/pokemon-x3/firmware-x3-x4.bin --version vX.Y.Z --output <dir>
   python3 scripts/build_pokemon_release.py --firmware .pio/build/pokemon-x4-pro/firmware-x4-pro.bin --version vX.Y.Z --output <dir>
   ```
   (Confirm the actual output firmware filename `rename_firmware.py` produces for the new
   env before assuming `firmware-x4-pro.bin` — it's derived from `custom_firmware_device_type`,
   verify against the real build log.)
2. Merge the two `SHA256SUMS` outputs into one file covering both assets (matching how the
   existing release already lists one firmware + one checksum file, per
   `docs/release-checklist.md`).
3. Update `README.md` / `docs/installation.md` to list the X4 Pro as a supported device,
   and add an explicit warning that the two firmware files are **not** interchangeable
   between X3/X4 and X4 Pro (different MCU family — flashing the wrong one will not boot).
4. Add a `CHANGELOG.md` entry (English, following the existing `## [x.y.z]` /
   `### Added`/`### Changed`/`### Fixed` structure already used there).
5. Tag and hand off to the user for the actual `git push`/GitHub Release publish — this
   project's standing convention (see `CLAUDE.md`) is that **pushing is done manually by the
   user**, never autonomously by an agent session.

**Files touched:** `README.md`, `docs/installation.md`, `CHANGELOG.md`, plus whatever local
release-artifact files get produced (kept untracked, same as the existing `v0.2.0` release
files at the repo root — see `.gitignore`/existing convention, do not commit binaries).

**Definition of done:** Both firmware assets built, checksummed, and documented; a draft
release ready for the user's manual review and publish. Do not publish/push anything
without the user's explicit go-ahead in that session.

</details>

---

## Real-hardware acceptance (separate from the phases above)

**Status:** Done (commit `7ddfc760`, 2026-09-10). `README.md`/`docs/installation.md` now
say both X3 and X4 Pro are "confirmed working on physical hardware" (previously X4 Pro said
"physical-device acceptance is still pending" — that caveat is gone). This confirms the game
itself works end to end on a real X4 Pro device; it does not by itself mean every item in
the checklist below (frontlight, fuel gauge, crash-report absence, etc.) was individually
re-verified with a written record — treat this as **"shipped and working," not** a completed
paper checklist. If a future regression is suspected, redo the specific checklist item on
real hardware rather than assuming.

Only a physical X4 Pro can verify: touch responsiveness and the capacitive Home key, SDMMC
SD-card read reliability for artwork, dual-channel frontlight, battery/fuel-gauge reporting,
e-ink refresh speed per battle turn (the thing that drove the "trim gym teams" /
"restore gym teams" back-and-forth during X3 development — see Stages 12/14 in
`pokemon-battle-roadmap.md`), and that no `crash_report.txt` appears after normal play.
Follow `docs/release-checklist.md`'s "Physical X3 acceptance" section as a template,
substituting X4 Pro hardware and its own known quirks documented in
`freeink-sdk/docs/xteink-x4pro-support.md` (e.g. the GT911's touch axes are swapped at the
driver level — `swapXY = true` — already handled by the SDK profile, but worth confirming
end-to-end rather than assuming).
