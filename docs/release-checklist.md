# X3 release checklist

Use this checklist for one exact commit and one exact firmware binary. A check
performed on another build is not evidence for the release candidate.

## Automated firmware release

Pushing a tag matching `v*.*.*` (e.g. `git tag v0.4.0 && git push origin v0.4.0`)
triggers `.github/workflows/release.yml`, which builds `pokemon-x3` and
`pokemon-x4-pro`, packages `xteink-pokemon-x3-x4-firmware-v<version>.bin`,
`xteink-pokemon-x4-pro-firmware-v<version>.bin`, a version-less
`-latest.bin` alias of each (so README.md/docs/installation.md can link
directly to `.../releases/latest/download/xteink-pokemon-<device>-firmware-latest.bin`
and always get the current build), and `SHA256SUMS.txt`. It extracts the
matching `## [<version>]` section from `CHANGELOG.md` via
`scripts/extract_changelog_section.py`, and publishes a GitHub Release with
those assets and that changelog section as the release notes.

If a firmware-only asset is ever published by hand instead of through this
workflow, re-upload the same two `-latest.bin` aliases to that release too -
the stable per-device download links depend on every release consistently
carrying them.

This only automates the two firmware assets. It does **not** run the full
release checklist below, and it does **not** build the artwork archive - the
Pokémon artwork pack is deliberately not committed to this repository (see
"Artwork archive" below). Build and upload
`xteink-pokemon-sd-card-assets.zip` to the same release by hand, same as
before.

Before pushing the tag:
- `CHANGELOG.md` must already have a `## [<version>]` section (without a
  leading `v`) - the workflow fails loudly if it doesn't find one, rather than
  publishing a release with empty notes.
- Physical-device acceptance (see "Physical X3 acceptance" below) should
  already be complete. The workflow has no way to gate on this - tagging is
  the deliberate "yes, ship it" action, same as it was for a manual release.

## Repository

- [ ] The release commit is identified.
- [ ] The working tree contains no unreviewed release changes.
- [ ] Submodules resolve to recorded commits.
- [ ] `LICENSE` preserves the inherited MIT notice.
- [ ] `NOTICE.md` and `docs/third-party-assets.md` match the sources actually
      used.
- [ ] No Pokémon images, converted cards, artwork packs, firmware binaries,
      personal saves, SD-card contents, credentials, or private paths are
      tracked in Git. Converted artwork is distributed only through the
      reviewed GitHub Release archive.
- [ ] Inherited GitHub workflows have been reviewed for this repository and
      cannot publish CrossInk, Sticky, X4, font, Pages, or catalog artifacts by
      mistake.
- [ ] Inherited funding links have been removed or explicitly approved by the
      named recipient.

## Artwork archive

Firmware and artwork ship as separate release assets - see
[Artwork setup](artwork-setup.md#5-build-the-public-artwork-archive). The
artwork archive (`xteink-pokemon-sd-card-assets.zip`) contains only the
`pokemon/` folder, is identical for every device, and is never covered by
`SHA256SUMS.txt` (that file only covers what CI publishes).

- [ ] The source revisions match `docs/third-party-assets.md`.
- [ ] `scripts/generate_pokemon_icon_art.py` completed locally.
- [ ] `scripts/generate_pokemon_pokedex_art.py` completed locally.
- [ ] The canonical local artwork directory contains all 850 required one-bit
      BMPs with the documented dimensions (614 species/item/Pokédex art + 151
      back sprites + 77 bag item icons + 8 badge icons).
- [ ] The native artwork-generator/packager tests pass (`ctest -R
      PokemonArtPack`), confirming the pack against `RIGHTS_AND_ATTRIBUTION.md`
      and the manifest/checksum rules `scripts/package_pokemon_v2_release.py`
      itself enforces.
- [ ] `xteink-pokemon-sd-card-assets.zip` contains only the `pokemon/` folder
      (sprites, heroes, items, badges, trainers, pokedex, `manifest.json`) -
      no firmware, no save files, no books, no settings, no cache files.
- [ ] Extracting `pokemon/` onto an SD card with an existing Pokémon save
      leaves that save's hash unchanged.
- [ ] The archive is uploaded as an additional asset on the same GitHub
      Release the tag push created.

## Automated checks

- [ ] Pokémon host tests pass.
- [ ] Artwork generator and packager tests pass (path-safety, manifest, and
      checksum coverage for the artwork pack).
- [ ] Portrait Pokémon simulator smoke route passes.
- [ ] Landscape Pokémon simulator smoke route passes.
- [ ] `pio run -e pokemon-x3` succeeds.
- [ ] The SHA-256 of the tested firmware binary is recorded.
- [ ] Test commands and results are copied into the draft release notes.

Automated checks do not approve a hardware release.

## Physical X3 acceptance

- [ ] Back up the SD card and both Pokémon snapshot files.
- [ ] Keep a known-good rollback firmware and verified recovery path available.
- [ ] Install the exact binary whose SHA-256 was recorded above.
- [ ] Complete starter, gender, and optional nickname setup with the physical
      side and front buttons.
- [ ] Confirm the header does not clip, selected rows retain their sprites, and
      list navigation works through every page.
- [ ] Confirm Party movement, PC deposit/withdrawal, PC sorting, Bag, Pokédex,
      and two-step reset.
- [ ] Open seen Pokédex entries in portrait and landscape and inspect contrast
      and load time.
- [ ] Read with real page turns through at least one five-minute checkpoint,
      exit the reader, and confirm EXP.
- [ ] Leave a book open without turning pages and confirm that no EXP is added.
- [ ] Reboot and confirm Party and EXP persist.
- [ ] Inspect supported dashboards in portrait and landscape.
- [ ] Open and close Pokémon repeatedly after leaving a book.
- [ ] Confirm that no new `crash_report.txt` was created.
- [ ] Record the completed results in an X3 device-test issue.

## Publication

- [ ] Review the final commit diff.
- [ ] Review the exact GitHub Actions configuration on the release commit.
- [ ] Confirm converted artwork is present only in the
      `xteink-pokemon-sd-card-assets.zip` release asset and is not committed
      to Git history.
- [ ] Confirm the release includes both firmware binaries (plus their
      `-latest.bin` aliases), `xteink-pokemon-sd-card-assets.zip`, and
      `SHA256SUMS.txt` (firmware only) with the approved public filenames.
- [ ] Confirm release notes link to `RIGHTS_AND_ATTRIBUTION.md` and the Rights
      or Attribution issue form.
- [ ] Put backup, installation, rollback, known limitations, test evidence, and
      the firmware assets' SHA-256 values in the release notes.
- [ ] Download every published asset again, verify each firmware binary's
      SHA-256 against `SHA256SUMS.txt`, and extract
      `xteink-pokemon-sd-card-assets.zip` to confirm it only contains the
      `pokemon/` folder.
- [ ] Confirm the live GitHub Pages / [Installation](installation.md) links
      point at the current per-device firmware downloads and at
      `xteink-pokemon-sd-card-assets.zip` for the artwork.
- [ ] Publish a new version as a stable release only after the physical X3 report passes every required item.
