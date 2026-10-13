---
title: Game Boy
nav_order: 1.45
---

# Game Boy (X4 Pro)

The X4 Pro firmware can play original Game Boy (DMG) games. Copy a `.gb` ROM
file to the SD card, then open it from the file browser like a book.

Only play ROMs you own. No games come with the firmware.

## Controls

The game opens three times its original size. Upright, the controls sit below
it as on a Game Boy: the D-pad on the left, B and A on the right, and a row of
**EXIT**, **SEL**, **START** and **SETTINGS** at the bottom. Sideways, the
D-pad and **SETTINGS** are left of the game and the buttons right of it.

| Game Boy | Touch | Buttons |
| --- | --- | --- |
| D-pad | D-pad | Up / Down / Left / Right |
| A | **A** | Power |
| B | **B** | - |
| Start | **START** | Confirm |
| Select | **SEL** | Back |
| Leave the game | **EXIT** | - |
| Settings, quick save and load | **SETTINGS** | - |

Tap the title strip above the game (in landscape, the panel above the D-pad)
to show or hide a status line: how many times a second the screen updates and
how fast the game runs against a real Game Boy.

## Settings

**SETTINGS** pauses the game and opens a list; tap a row (or pick it with
Up/Down and Confirm), and **Back to game** or Back returns to the game.

- **Orientation**: upright, sideways, upright upside down, or sideways the
  other way round.
- **Screen speed** (X4 Pros with a UC8279 screen only): how much the screen
  shortens each update - D40, D30 or D25, the lower the faster with a little
  more ghosting - and whether a full cleaning refresh clears the ghosting every
  30 or 60 seconds. D30 with a cleaning refresh every 60 seconds is the
  default. Other screens use the standard refresh.
- **Status line**: the same as tapping the title strip.
- **Quick save** and **Quick load**: see below.

Orientation, screen speed and the status line are kept for every game, in
`/.crosspoint/gameboy.cfg`.

Hold a touch button to keep it pressed. If a ROM cannot be opened, the error
screen closes with Back or a tap.

## Saves

Games with a battery-backed cartridge (most RPGs, for example) save next to the
ROM: `Tetris.gb` saves to `Tetris.sav`. The save is written when you leave with
EXIT, and once a minute while you play if the game changed it. The previous
save is kept as `.sav.bak`. A `.sav` from another emulator works if it is the
plain cartridge RAM.

**Quick save** in SETTINGS saves the whole game as it is at that moment, next
to the ROM (`Tetris.state`), whether or not the game has its own saves. Each
game has one quick save, replaced by the next. **Quick load** goes back to it:
tap it twice, since everything played since the quick save is lost.

If the reader goes to sleep while a game is open (the Power button), the game
is saved the same way (`Tetris.resume`) and the next time the reader is
turned on it opens the game where it was, instead of the home screen. Hold
Back while it starts to go to the home screen instead.

## Limits

- X4 Pro only: the ROM is loaded into the X4 Pro's extra memory (PSRAM), which
  the X3 and X4 do not have.
- Game Boy only: Game Boy Color-only games (`.gbc`) do not run, and MBC7
  cartridges are not supported.
- No sound.
- The game runs at full speed, but the e-ink screen updates only about four
  to five times a second (X4 Pros with a UC8279 screen controller, which
  redraw only the part of the picture that changed) or about once or twice a
  second (other X4 Pro screens), so fast action games are hard to play.
  Turn-based and puzzle games work best. On the faster screens a full refresh
  every minute clears any ghost images.
- Grey shades are drawn as dot patterns.
- The screen stays on and the reader does not go to sleep while a game is
  open, so leave with EXIT when you are done.

## Speed log

Each game session adds a line to `/.crosspoint/gameboy-perf.txt` on the SD
card: the game, how fast the emulator ran against a real Game Boy, how often
the screen updated, the reader's screen controller, how long each part of a
screen update took, how many pictures were skipped because nothing had changed
and how much picture data each update sent. Send it along when you report a slow or choppy game.

## Credits

The emulator is [Peanut-GB](https://github.com/deltabeard/Peanut-GB) by Mahyar
Koshkouei (MIT), with parts from SameBoy by Lior Halphon. The game screen
started from the [Ink-boy](https://github.com/hellominecraft913-dev/Ink-boy)
CrossInk fork (MIT).
