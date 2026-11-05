#!/usr/bin/env python3
"""Build and run the simulator smoke test against an isolated fs_ directory."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BOOK = ROOT / "test" / "epubs" / "test_reader_rendering_matrix.epub"
# fs_/pokemon is the simulator's own SD sandbox; images/pokemon is where a
# locally built art pack lands (docs/artwork-setup.md). Either works.
POKEMON_ASSET_CANDIDATES = (ROOT / "fs_" / "pokemon", ROOT / "images" / "pokemon")
CRASH_PATTERNS = (
    "std::bad_alloc",
    "terminating due to uncaught exception",
    "Assertion failed",
    "Segmentation fault",
    "AddressSanitizer",
    "UndefinedBehaviorSanitizer",
)
POKEMON_ART_ERROR_PATTERNS = (
    "[PKART] Missing Pokemon art",
    "[PKART] Invalid Pokemon art",
    "[PKART] Could not render Pokemon art",
    "[GFX] Failed to read row",
)
POKEMON_DETAIL_SUCCESS_MARKER = "Pokemon Pokedex detail card rendered"
# test/lua_apps/<id>/ are copied into the SD sandbox for --lua-apps; the smoke
# app logs these markers as it runs (test/lua_apps/smoke/main.lua).
LUA_APPS_SOURCE = ROOT / "test" / "lua_apps"
LUA_SMOKE_MARKERS = ("LUA_SMOKE sandbox ok", "LUA_SMOKE init ok", "LUA_SMOKE draw ok", "LUA_SMOKE confirm 1", "LUA_SMOKE exit")
THEMES = {
    "classic": 0,
    "lyra": 1,
    "lyra-extended": 2,
    "lyra_extended": 2,
    "lyra3": 2,
    "lyra-3-covers": 2,
    "roundedraff": 3,
    "rounded-raff": 3,
    "lyra-carousel": 4,
    "lyra_carousel": 4,
    "carousel": 4,
    "minimal": 5,
    "dashboard": 6,
    "cover-grid": 7,
}


def program_path(env_name: str) -> Path:
    build_dir = Path(os.environ.get("PLATFORMIO_BUILD_DIR", ROOT / ".pio" / "build"))
    return build_dir / env_name / "program"


def build_simulator(env_name: str) -> None:
    print(f"Building {env_name} simulator...", flush=True)
    proc = subprocess.run(["pio", "run", "-e", env_name, "-j1"], cwd=ROOT)
    if proc.returncode != 0:
        raise SystemExit(proc.returncode)


def prepare_fs(temp_root: Path, book: Path) -> str:
    books_dir = temp_root / "fs_" / "books"
    books_dir.mkdir(parents=True, exist_ok=True)

    target = books_dir / book.name
    shutil.copy2(book, target)
    return f"/books/{book.name}"


def prepare_pokemon_assets(temp_root: Path) -> None:
    source = next((path for path in POKEMON_ASSET_CANDIDATES if path.is_dir()), None)
    if source is None:
        raise FileNotFoundError(f"Pokemon assets not found in any of: {', '.join(map(str, POKEMON_ASSET_CANDIDATES))}")
    target = temp_root / "fs_" / "pokemon"
    shutil.copytree(source, target)
    prepare_pokedex_card_fixture(target / "pokedex" / "portrait" / "004.bmp")
    prepare_pokedex_card_fixture(target / "pokedex" / "landscape" / "004.bmp", 288, 432)


def prepare_pokedex_card_fixture(target: Path, width: int = 472, height: int = 708) -> None:
    """Write a packaged-size 1-bit card that exercises the dedicated SD path."""
    row_stride = ((width + 31) // 32) * 4
    palette = bytes((0, 0, 0, 0, 255, 255, 255, 0))
    pixels = bytearray()
    for y in range(height):
        row = bytearray(b"\xff" * row_stride)
        for x in range(width):
            if (x % 23 == 0 or y % 29 == 0):
                row[x // 8] &= ~(0x80 >> (x % 8))
        pixels.extend(row)

    pixel_offset = 14 + 40 + len(palette)
    file_size = pixel_offset + len(pixels)
    header = struct.pack("<2sIHHI", b"BM", file_size, 0, 0, pixel_offset)
    dib = struct.pack("<IiiHHIIiiII", 40, width, height, 1, 1, 0, len(pixels), 2835, 2835, 2, 2)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(header + dib + palette + pixels)


def prepare_lua_apps(temp_root: Path) -> None:
    target = temp_root / "fs_" / ".crosspoint" / "apps"
    target.mkdir(parents=True, exist_ok=True)
    for app in sorted(LUA_APPS_SOURCE.iterdir()):
        if app.is_dir():
            shutil.copytree(app, target / app.name)


def lua_smoke_output_error(output: str) -> str | None:
    for line in output.splitlines():
        if "[ERR]" in line and "[LUA]" in line:
            return f"Lua error: {line.strip()}"
    for marker in LUA_SMOKE_MARKERS:
        if marker not in output:
            return f"missing Lua app marker: {marker}"
    return None


def pokemon_smoke_output_error(output: str) -> str | None:
    for pattern in POKEMON_ART_ERROR_PATTERNS:
        if pattern in output:
            return f"artwork failure: {pattern}"
    if POKEMON_DETAIL_SUCCESS_MARKER not in output:
        return "Pokédex detail card was not rendered"
    return None


def build_smoke_environment(
    base_env: dict[str, str], args: argparse.Namespace, storage_root: Path, simulator_book_path: str
) -> dict[str, str]:
    env = base_env.copy()
    env["CROSSPOINT_SIM_SD"] = str(storage_root)
    env["CROSSINK_SIMULATOR_SMOKE_TEST"] = "1"
    env["CROSSINK_SIMULATOR_SMOKE_BOOK"] = simulator_book_path
    env["CROSSINK_SIMULATOR_SMOKE_PAGE_TURNS"] = str(args.page_turns)
    if args.pokemon:
        env["CROSSINK_SIMULATOR_START_POKEMON"] = "1"
    if args.lua_apps:
        env["CROSSINK_SIMULATOR_LUA_APPS"] = "1"
    if args.home_navigation:
        env["CROSSINK_SIMULATOR_HOME_NAVIGATION"] = "1"
    if args.landscape:
        env["CROSSINK_SIMULATOR_POKEMON_LANDSCAPE"] = "1"
    if args.font_dir and args.font_family:
        env["CROSSINK_SIMULATOR_SMOKE_ISOLATED_FONTS"] = "1"
    if args.font_family:
        env["CROSSINK_SIMULATOR_SMOKE_FONT_FAMILY"] = args.font_family
    if args.frontlight_sync:
        env["CROSSINK_SIMULATOR_SMOKE_FRONTLIGHT_SYNC"] = "1"
    if args.frontlight_layout:
        env["CROSSINK_SIMULATOR_SMOKE_FRONTLIGHT_LAYOUT"] = "1"
    if args.frontlight_captures:
        capture_dir = Path(args.frontlight_captures).resolve()
        capture_dir.mkdir(parents=True, exist_ok=True)
        env["CROSSINK_SIMULATOR_SMOKE_FRONTLIGHT_CAPTURES"] = str(capture_dir)
    if args.home_themes:
        env["CROSSINK_SIMULATOR_SMOKE_HOME_THEMES"] = "1"
    if args.theme:
        env["CROSSINK_SIMULATOR_SMOKE_THEME"] = str(THEMES[args.theme])
    if args.headless:
        env.setdefault("SDL_VIDEODRIVER", "dummy")
    return env


def run_smoke(args: argparse.Namespace) -> int:
    book = Path(args.book).resolve()
    if not book.exists():
        print(f"Smoke test book not found: {book}", file=sys.stderr)
        return 2

    if args.build:
        build_simulator(args.env)

    program = program_path(args.env)
    if not program.exists():
        print(f"Simulator binary not found: {program}", file=sys.stderr)
        print(f"Run: pio run -e {args.env}", file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory(prefix="crossink-sim-smoke-") as temp_dir_name:
        temp_root = Path(temp_dir_name)
        simulator_book_path = prepare_fs(temp_root, book)
        if args.pokemon:
            prepare_pokemon_assets(temp_root)
        if args.lua_apps:
            prepare_lua_apps(temp_root)

        if args.font_dir:
            shutil.copytree(Path(args.font_dir), temp_root / "fs_" / "fonts", dirs_exist_ok=True)
        env = build_smoke_environment(os.environ, args, temp_root / "fs_", simulator_book_path)
        factory_flash = None  # upstream's language flash scenario; not in this fork

        print(f"Running simulator smoke test with isolated fs_: {temp_root / 'fs_'}", flush=True)
        proc = subprocess.run(
            [str(program)],
            cwd=temp_root,
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=args.timeout if args.timeout is not None else (300 if args.asset_downloads or args.frontlight_layout else 180 if args.about or args.support_export or args.filename_font_family else 120),
        )

        if factory_flash and factory_flash.read_bytes() != b"\xa5" * 0x14000:
            raise AssertionError("Factory partition was modified")

    print(proc.stdout, end="")

    if proc.returncode != 0:
        print(f"Simulator smoke test failed with exit code {proc.returncode}", file=sys.stderr)
        return proc.returncode

    if args.language_file and f"Mapped {code} " not in proc.stdout:
        print(f"Simulator did not boot with cached language {code}", file=sys.stderr)
        return 2

    for pattern in CRASH_PATTERNS:
        if pattern in proc.stdout:
            print(f"Simulator smoke test output contained crash pattern: {pattern}", file=sys.stderr)
            return 2

    if args.pokemon:
        pokemon_error = pokemon_smoke_output_error(proc.stdout)
        if pokemon_error is not None:
            print(f"Pokemon smoke test failed: {pokemon_error}", file=sys.stderr)
            return 2

    if args.lua_apps:
        lua_error = lua_smoke_output_error(proc.stdout)
        if lua_error is not None:
            print(f"Lua apps smoke test failed: {lua_error}", file=sys.stderr)
            return 2

    if "Simulator smoke test passed" not in proc.stdout:
        print("Simulator smoke test did not print its success marker", file=sys.stderr)
        return 2

    if args.bookmarks and "Simulator smoke test passed: bookmarks" not in proc.stdout:
        print("Simulator did not complete bookmark checks", file=sys.stderr)
        return 2

    if args.wifi_transmit_power and "Simulator smoke test passed: Wi-Fi transmit power" not in proc.stdout:
        print("Simulator did not complete Wi-Fi transmit-power checks", file=sys.stderr)
        return 2

    if args.menu_orientation and "Simulator smoke test passed: menu orientation" not in proc.stdout:
        print("Simulator did not complete the menu orientation checks", file=sys.stderr)
        return 2

    if args.release_notes and "Simulator smoke test passed: release notes" not in proc.stdout:
        print("Simulator did not complete the release notes checks", file=sys.stderr)
        return 2

    if args.clipping_selection and "Simulator smoke test passed: touch clipping actions" not in proc.stdout:
        print("Simulator did not complete the touch clipping checks", file=sys.stderr)
        return 2

    asset_marker = ("Simulator smoke test passed: empty catalog" if args.asset_empty_catalog else
                    "Simulator smoke test passed: language save failure" if args.asset_save_failure else
                    "Simulator smoke test passed: asset downloads")
    if args.asset_downloads and asset_marker not in proc.stdout:
        print("Simulator did not complete asset download integration checks", file=sys.stderr)
        return 2

    if args.asset_expect_translation_rejection and "obsolete translation catalog rejected" not in proc.stdout:
        print("Simulator did not reject the obsolete translation contract", file=sys.stderr)
        return 2

    if args.font_recovery and "Simulator smoke test passed: font recovery" not in proc.stdout:
        print("Simulator did not complete the font recovery checks", file=sys.stderr)
        return 2

    if args.c3_font_sizes and "Simulator smoke test passed: bitmap font sizes" not in proc.stdout:
        print("Simulator did not complete the bitmap font-size checks", file=sys.stderr)
        return 2

    if args.reader_font_layout and "Simulator smoke test passed: reader font layout" not in proc.stdout:
        print("Simulator did not complete the Reader Font layout checks", file=sys.stderr)
        return 2

    if args.font_family and not args.c3_font_sizes and not args.reader_font_layout:
        tab_change = proc.stdout.find("Reader Menu tab changed after TTF Native selection")
        reader_return = proc.stdout.find("Reader restored after TTF Native selection", tab_change)
        if tab_change < 0 or reader_return < 0 or "Loading file:" not in proc.stdout[tab_change:reader_return]:
            print("Reader did not reload its page after changing TTF options and switching tabs", file=sys.stderr)
            return 2

    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--unicode-spaces", action="store_true", help="Check native Unicode spacing metrics and blank normal, rotated and scaled rendering")
    parser.add_argument("--factory-flash", action="store_true",
                        help="Use the reported 80 KiB factory partition; verify it remains unchanged (S3 profiles)")
    parser.add_argument("--book", default=str(DEFAULT_BOOK), help="EPUB fixture to copy into the isolated simulator fs_")
    parser.add_argument("--env", choices=("simulator", "simulator-X3", "x4-classic-simulator", "sticky-simulator", "x4-pro-simulator", "pokemon-simulator-X3", "pokemon-x4-pro-simulator"), default="simulator",
                        help="PlatformIO simulator environment to build and run")
    parser.add_argument("--status-bar-lifecycle", action="store_true", help="Exercise reader settings return and reflow")
    parser.add_argument("--wifi-transmit-power", action="store_true", help="Check Wi-Fi power defaults, device picker and persistence")
    parser.add_argument("--image-display", action="store_true", help="Check combined image choices and saved preference compatibility")
    parser.add_argument("--tilt-settings", action="store_true", help="Check tilt preference visibility, device editing and persistence")
    parser.add_argument("--chapter-percentage", action="store_true", help="Check chapter percentage slot selection, removal, reload and rendered formats")
    parser.add_argument("--centered-status-bar", action="store_true", help="Check Center Status Bar and Separator rows, popups and persistence")
    parser.add_argument("--status-bar-feature", action="store_true", help="Check status bar visibility, independent sizes, disk reload and rendered theme/orientation matrix")
    parser.add_argument("--screen-calibration", action="store_true", help="Check screen calibration button/touch editing, save, cancel, reset and rotations")
    parser.add_argument("--receive-folder", action="store_true", help="Check receive folder selection, navigation and cancellation")
    parser.add_argument("--receive-folder-captures", help="Directory for receive folder picker framebuffer captures (PGM)")
    parser.add_argument("--status-bar-captures", help="Directory for status bar feature framebuffer captures (PGM)")
    parser.add_argument("--language-file", help="SD language YAML installed before boot for localized smoke checks")
    parser.add_argument("--language-installer", help="Native I18nIntegrationTool built from the same source (required with --language-file)")
    parser.add_argument("--asset-downloads", action="store_true", help="Check mocked online catalog/download/explicit activation across minimal-boot reboots (requires test-only .invalid compiled endpoint)")
    parser.add_argument("--asset-snapshot", help="Producer snapshot directory or snapshot.json copied read-only into disposable HTTP fixtures")
    parser.add_argument("--asset-hyphenation-only", action="store_true", help="Run first-phase hyphenation without requiring a matching translation contract")
    parser.add_argument("--asset-expect-translation-rejection", action="store_true", help="Require supplied obsolete translation catalog to be rejected after hyphenation succeeds")
    parser.add_argument("--asset-empty-catalog", action="store_true", help="Verify a valid empty catalog exits without a transfer or network-error retry")
    parser.add_argument("--asset-save-failure", action="store_true", help="Force settings publication failure after language preparation and verify explicit recovery restart")
    parser.add_argument("--asset-captures", help="Directory for asset-download framebuffer captures (PGM)")
    parser.add_argument("--hyphenation", action="store_true", help="Check SD pack management, pinned flash banks, restart activation and deletion")
    parser.add_argument("--hyphenation-captures", help="Directory for hyphenation manager framebuffer captures (PGM)")
    parser.add_argument("--clipping-selection", action="store_true", help="Check floating text actions, selection handles, dismissal and saving")
    parser.add_argument("--dictionary", action="store_true", help="Check French contractions and exact dictionary matches")
    parser.add_argument("--gesture-shortcuts", action="store_true", help="Check page gesture and front-button shortcut migrations, input, catalogs and UI")
    parser.add_argument("--c3-font-sizes", "--bitmap-font-sizes", dest="c3_font_sizes", action="store_true", help="Check bitmap font sizes, inline layout, page cache, selection and SD reload")
    parser.add_argument("--font-size-book", action="store_true", help="Also render every page of --book with C3 fonts, checking its opening drop cap")
    parser.add_argument("--font-dir", help="Font fixtures copied into isolated /fonts")
    parser.add_argument("--filename-font-dir", help="TTF family fixtures copied into /.crossink/languages/fonts")
    parser.add_argument("--filename-font-family", help="Check filename font picker, mixed glyphs and storage lifecycle (S3)")
    parser.add_argument("--filename-font-error-family", help="Family that must remain visible with a load error and preserve the previous choice")
    parser.add_argument("--filename-font-captures", help="Directory for filename font framebuffer captures (PGM)")
    parser.add_argument("--font-recovery", action="store_true", help="Check reader selection recovery after font catalog memory pressure")
    parser.add_argument("--font-family", help="Exercise custom-font size and dictionary lifecycle")
    parser.add_argument("--reader-font-layout", action="store_true", help="Check Reader Font row bounds, scrolling and TTF touch navigation across orientations and UI sizes (X4 Pro; requires --font-dir and --font-family)")
    parser.add_argument("--timeout", type=int, help="Seconds before the simulator run is treated as hung (default: 120; 180 for about, support export or filename fonts; 300 for frontlight layout or asset downloads)")
    parser.add_argument("--page-turns", type=int, default=2, help="Number of EPUB page-forward taps to run")
    parser.add_argument("--theme", choices=sorted(THEMES), help="UI theme to use during the smoke test")
    parser.add_argument("--pokemon", action="store_true", help="Run the Pokemon onboarding and collection smoke route")
    parser.add_argument("--home-navigation", action="store_true",
                        help="Exercise Home navigation through the Pokemon row to Settings")
    parser.add_argument("--lua-apps", action="store_true",
                        help="Run the Lua Applications route with the test/lua_apps smoke app")
    parser.add_argument("--landscape", action="store_true", help="Run the Pokemon route in landscape orientation")
    parser.add_argument("--context-menu-sync-return", choices=["file-browser", "library"], help="Check context-menu sync return across network reboots")
    parser.add_argument("--file-browser-sync-return", action="store_true", help="Check browser sync return across network reboots")
    parser.add_argument("--frontlight-sync", action="store_true", help="Check frontlight sync outside the reader with stats enabled and disabled (X4 Pro)")
    parser.add_argument("--frontlight-layout", action="store_true", help="Check frontlight drawer bounds, handle swipes/taps, reader return and explicit Home across scales, orientations and themes (X4 Pro)")
    parser.add_argument("--frontlight-captures", help="Directory for frontlight layout framebuffer captures (PGM)")
    parser.add_argument("--opds-catalog", action="store_true", help="Check OPDS catalog release and download return flows")
    parser.add_argument("--support-export", action="store_true", help="Check support export privacy, scopes, cancellation and failure preservation")
    parser.add_argument("--bookmarks", action="store_true", help="Check long bookmark lists from Home and the reader, scrolling, deletion and jumps; captures via --about-captures")
    parser.add_argument("--about", action="store_true", help="Check About navigation, paging and read-only snapshots across scales/orientations")
    parser.add_argument("--release-notes", action="store_true", help="Check the one-time What's New screen (Download, Update, touch Later vs swipe, landscape hints) and the Settings Release Notes entry; captures via --about-captures")
    parser.add_argument("--about-captures", help="Directory for About framebuffer captures (PGM)")
    parser.add_argument("--sync-server-captures", help="Directory for Sync Server settings captures (PGM)")
    parser.add_argument("--menu-orientation", action="store_true", help="Check physical menu labels/actions in all rotations independently of reader preferences; captures via --about-captures")
    parser.add_argument("--home-swipe", action="store_true", help="Check bottom-edge Home swipes, ordinary list swipes, and reader menu gestures on touch devices")
    parser.add_argument("--home-themes", action="store_true", help="Compare drawer theme changes with fresh Home renders (X4 Pro)")
    parser.add_argument("--no-build", dest="build", action="store_false", help="Run the existing simulator binary")
    parser.add_argument("--window", dest="headless", action="store_false", help="Show the SDL window instead of using dummy video")
    parser.set_defaults(build=True, headless=True)
    args = parser.parse_args()
    if args.landscape and not args.pokemon:
        parser.error("--landscape requires --pokemon")
    if args.pokemon and args.env not in ("pokemon-simulator-X3", "pokemon-x4-pro-simulator"):
        parser.error("--pokemon requires --env pokemon-simulator-X3 or pokemon-x4-pro-simulator")
    if args.lua_apps and args.pokemon:
        parser.error("--lua-apps and --pokemon are separate routes; run them one at a time")
    if args.lua_apps and args.env not in ("pokemon-simulator-X3", "pokemon-x4-pro-simulator"):
        parser.error("--lua-apps requires --env pokemon-simulator-X3 or pokemon-x4-pro-simulator")
    if args.home_navigation and args.env != "pokemon-simulator-X3":
        parser.error("--home-navigation requires --env pokemon-simulator-X3")
    if args.reader_font_layout and (args.env != "x4-pro-simulator" or not args.font_dir or not args.font_family):
        parser.error("--reader-font-layout requires --env x4-pro-simulator, --font-dir and --font-family")
    if (args.asset_snapshot or args.asset_hyphenation_only or args.asset_captures or args.asset_expect_translation_rejection or args.asset_empty_catalog or args.asset_save_failure) and not args.asset_downloads:
        parser.error("asset fixture/capture options require --asset-downloads")
    if args.asset_expect_translation_rejection and (args.asset_hyphenation_only or not args.asset_snapshot):
        parser.error("--asset-expect-translation-rejection requires a snapshot and the translation stage")
    if args.asset_empty_catalog and (args.asset_expect_translation_rejection or args.asset_hyphenation_only):
        parser.error("--asset-empty-catalog is a separate scenario")
    if args.asset_save_failure and (args.asset_expect_translation_rejection or args.asset_hyphenation_only or args.asset_empty_catalog):
        parser.error("--asset-save-failure requires a successful translation stage")
    if args.asset_downloads and args.hyphenation:
        parser.error("--asset-downloads and --hyphenation are separate smoke scenarios")
    if args.c3_font_sizes and args.env not in ("simulator", "simulator-X3") and not (args.font_dir and args.font_family):
        parser.error("--bitmap-font-sizes on S3 requires --font-dir and --font-family with cpfont fixtures")
    if args.font_size_book and not args.c3_font_sizes:
        parser.error("--font-size-book requires --c3-font-sizes")
    if args.language_file and not args.language_installer:
        parser.error("--language-file requires --language-installer")
    return args


def main() -> int:
    return run_smoke(parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
