---
title: Game Boy
nav_order: 1.45
---

# Game Boy (X4 Pro)

The X4 Pro firmware can play original Game Boy (DMG) games. Copy a `.gb` ROM
file to the SD card, then open it from the file browser like a book.

Only play ROMs you own. No games come with the firmware.

## Controls

The game opens upright, three times its original size, with the controls
below it as on a Game Boy: the D-pad on the left, B and A on the right, and a
row of **EXIT**, **SEL**, **START** and **ROTATE** at the bottom. **ROTATE**
turns the reader sideways, with the D-pad left of the game and the buttons
right of it; **ROTATE** (bottom left) turns it back. Each game starts upright.

| Game Boy | Touch | Buttons |
| --- | --- | --- |
| D-pad | D-pad | Up / Down / Left / Right |
| A | **A** | Power |
| B | **B** | - |
| Start | **START** | Confirm |
| Select | **SEL** | Back |
| Leave the game | **EXIT** | - |
| Turn the screen | **ROTATE** | - |

Hold a touch button to keep it pressed. If a ROM cannot be opened, the error
screen closes with Back or a tap.

## Saves

Games with a battery-backed cartridge (most RPGs, for example) save next to the
ROM: `Tetris.gb` saves to `Tetris.sav`. The save is written when you leave with
EXIT, and once a minute while you play if the game changed it. The previous
save is kept as `.sav.bak`. A `.sav` from another emulator works if it is the
plain cartridge RAM.

Saving inside the game (from its own menu) is still needed - there are no save
states.

## Limits

- X4 Pro only: the ROM is loaded into the X4 Pro's extra memory (PSRAM), which
  the X3 and X4 do not have.
- Game Boy only: Game Boy Color-only games (`.gbc`) do not run, and MBC7
  cartridges are not supported.
- No sound.
- The game runs at full speed, but the e-ink screen updates about four times a
  second, so fast action games are hard to play. Turn-based and puzzle games
  work best.
- Grey shades are drawn as dot patterns.
- The screen stays on and the reader does not go to sleep while a game is
  open, so leave with EXIT when you are done.

## Credits

The emulator is [Peanut-GB](https://github.com/deltabeard/Peanut-GB) by Mahyar
Koshkouei (MIT), with parts from SameBoy by Lior Halphon. The game screen
started from the [Ink-boy](https://github.com/hellominecraft913-dev/Ink-boy)
CrossInk fork (MIT).
