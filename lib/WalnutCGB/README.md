# Walnut-CGB

[Walnut-CGB](https://github.com/Mr-PauI/Walnut-CGB) v1.2.4 by Mr. Paul (MIT),
a rewrite of [Peanut-GB](https://github.com/deltabeard/Peanut-GB) (MIT) with
Game Boy Color support. Used for Game Boy Color games on the X4 Pro; it is
compiled once, with its symbols renamed, in `src/walnut_cgb_impl.c`, behind
the opaque API in `src/walnut_cgb_port.h`.

Local changes, marked `xteink-pokemon:` (keep them when updating the core):

- A `struct gb_s;` forward declaration before the early prototypes in
  `src/walnut_cgb.h`, so the header compiles as C.
- Background and window are drawn a whole tile row (8 pixels) at a time
  through tables (`__gb_draw_tile_row`, `__gb_draw_bg_win_fast`) instead of
  pixel by pixel, taken from esp32-emu-game (same core, ESP32-S3): there that
  part went from ~3.7 to ~0.7 ms a drawn frame. In DMG mode the CGB tile
  attributes (VRAM bank 1) are ignored, as on the hardware; the original code
  read whatever a previous Game Boy Color game left there.
- The original renderer is kept as `__gb_draw_bg_win_ref`: defining
  `WGB_VERIFY` and `WGB_VERIFY_FAIL(gb, ref, got)` before including the header
  draws every line both ways and reports differences (checked with Pokemon
  Blue, dmg-acid2 and cgb-acid2: none).
- `WGB_PROF_MARK(i)` hooks in `__gb_draw_line`, empty by default, for timing
  the parts of a line.

Line endings were converted to LF.
