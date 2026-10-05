---
title: Applications (Lua apps)
nav_order: 1.4
---

# Applications (Lua apps)

Besides the Pokémon game, the firmware can run small games and utilities written in
[Lua](https://www.lua.org/). They live on the SD card, so installing, updating or removing one
never needs a firmware update. Open them from **Home > Applications**.

The app engine comes from [CrossSmudge](https://github.com/Mumfee/CrossSmudge) (MIT licensed), so
apps written for CrossSmudge run here unchanged: 2048, Blackjack, Sudoku, Tetris, Wordle, the
Codex deckbuilder and more.

## Installing apps

There are three ways to install an app. Each app is a folder holding a `manifest.json`, its Lua
scripts, and optionally an `icon.raw` icon.

1. **App Store (Wi-Fi):** in **Applications > App Store**, connect to Wi-Fi, pick an app and choose
   **Install app**. Installed apps can be upgraded, reinstalled or uninstalled from the same page.
   The store currently lists CrossSmudge's community catalog.
2. **Web manager:** start **File Transfer** on the reader, open its address in a browser and go to
   **Applications**. Drag an app folder or a `.zip` of it onto the page to install it; the page also
   lists installed apps, their size, and lets you delete them.
3. **SD card:** copy the app folder into one of these folders on the SD card:
   `/.crosspoint/applications/`, `/.crosssmudge/applications/` or `/applications/`.

The App Store and the web manager install into `/.crosssmudge/applications/`.

## Using apps

- **Applications** lists every installed app with its icon, version and author, followed by
  **App Store** and **App Settings**. App Settings sorts the list alphabetically or by how often
  each app is used.
- Inside an app, the buttons (or touch) do whatever the app says in its button hints; **Back**
  usually saves and leaves the app.
- If an app's script fails, the reader shows an **Application Error** screen with the message
  instead of crashing; press Back to return to the list.
- Each app runs with at most 75 KB of memory. Apps keep their own saved data, separate from your
  books and your Pokémon save.

## Writing apps

See CrossSmudge's
[Application Developer Guide](https://github.com/Mumfee/CrossSmudge/blob/main/apps/README.md) for
the manifest format, the `icon.raw` format, the lifecycle callbacks (`on_init`, `on_draw`,
`on_button`, `on_touch`, ...) and the full `smudge.*` drawing, input and file API.

Differences in this firmware:

- The `bitter10` ... `bitter16` font names draw with LexendDeca, since Bitter is not built in.
- The simulator smoke test runs a small app from `test/lua_apps/smoke/`
  (`scripts/run_simulator_smoke_test.py --env pokemon-simulator-X3 --lua-apps`), which is also a
  minimal working example of an app.
