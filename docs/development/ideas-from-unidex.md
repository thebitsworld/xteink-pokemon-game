# Ideas from unidex

Status: **saved, not started** (2026-10-26). The owner asked to keep these as ideas.

[Forrest404/unidex](https://github.com/Forrest404/unidex) is a pocket e-ink OS for the Waveshare
ESP32-S3-ePaper-1.54 (timetable, voice notes, reminders, a pet, a WiFi collection game, Lua apps, a
browser installer and emulator). It is licensed **PolyForm Noncommercial 1.0.0**, which does not mix
with this project's MIT licence: take ideas only, write any code here from scratch.

| Idea | What unidex does | Here today | Note |
|---|---|---|---|
| **Time limit for Lua apps** | `lua_sethook(L, hook, LUA_MASKCOUNT, 1000)`; a call into the app running over a few seconds raises "took too long" with the line, the app closes with "App error", the device stays up | Heap limit only (`LuaRunner`): an endless loop hangs the reader | Small (~20 lines), worth doing first |
| **WiFi collection game, Pokemon style** | Dex: every new network name nearby is logged with a rarity | - | Nearby networks spawn wild Pokemon, rare networks rare ones; keep only hashes of names (privacy), scan only when asked |
| **Battery gate for radios** | Low battery: WiFi, Meet, scans and recording answer "Battery too low" | Bluetooth has no gate | Goes with the Bluetooth work |
| **Browser emulator (WebAssembly)** | Firmware compiled with Emscripten behind small Arduino/IDF shims; store submissions are tried in it automatically | SDL simulator on the PC | Bigger job (days) |
| **Freeze escape** | Restarts itself after 30 s frozen; hold A+B 1 s to restart now | To check | Small |
| **"Sync everything" over USB** | One click on the web page: newest firmware, clock, calendar file, waiting notes | Web installer (`site/install.html`) | Later |
