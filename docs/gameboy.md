---
title: Game Boy
nav_order: 1.45
---

# Game Boy (X4 Pro)

The X4 Pro firmware can play Game Boy and Game Boy Color games. Copy a `.gb`
or `.gbc` ROM file to the SD card, then open **Applications > Game Boy**: it
lists the folders and games on the card (starting in the folder of the last
game you played), and a tap on a game starts it. Leaving the game returns to
Applications. A ROM also opens from the file browser like a book.

Game Boy Color games show in shades of grey: each colour becomes the grey of
its brightness, drawn as dot patterns like the Game Boy's own greys.

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
- **Picture**: **Large (x3)**, the default, or **Small (x2)**. The smaller
  picture has fewer pixels to send, so the screen keeps up better - about 25%
  more updates a second on the Pulse speeds, about 10% on the D speeds.
- **Screen speed** (X4 Pros with a UC8279 screen only): how much the screen
  shortens each update - D50, D40, D30 or D25: D50 is the slowest but the
  sharpest with the least ghosting, the lower numbers faster with a little more
  ghosting - and whether a full cleaning refresh clears the ghosting every 30 or
  60 seconds. D40 with a cleaning refresh every 60 seconds is the default.
  **Pulse 8x3**, **Pulse polish** and **Pulse polish+** (experimental) drive
  only the pixels that are changing, one short pulse at a time, about ten
  times a second: each changed pixel gets 8 pulses, and the rest of the screen
  is left alone. Motion is smoother and calmer than on the D speeds, with more
  ghosting behind moving things, which a cleaning refresh every 60 seconds
  clears. **Pulse polish** also gives the area that moved 4 more pulses once
  every pixel has finished and the picture has stood still for a moment,
  which clears most of it sooner; **Pulse polish+** stops waiting for the
  picture to stand still when 1.5 seconds have gone by without a polish
  (holding a direction on a map). Motion is smoother, a moving sprite stays half-changed
  for a moment, and a cleaning refresh every 60 seconds clears what is left.
  Other screens use the standard refresh.
- **Colours** (Game Boy Color games): how colours become greys. **Contrast**
  (the default) uses the Game Boy's four greys and keeps a picture the game
  darkens - night in Pokémon Gold and Silver - dark but readable (the grass
  does not turn into one dark patch). **Smooth** gives each colour its own
  grey out of 17, drawn as fine dot patterns: closer to the colours, but
  grainier. **Brightness** gives each colour the one of the four greys nearest
  its brightness, without the night adjustment.
- **Status line**: the same as tapping the title strip.
- **Quick save** and **Quick load**: see below.

Orientation, screen speed, colours and the status line are kept for every game, in
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
- MBC7 cartridges and the Game Boy Camera are not supported, and some Game Boy
  Color games may run slower or less accurately than Game Boy ones.
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

Game Boy games run on [Peanut-GB](https://github.com/deltabeard/Peanut-GB) by
Mahyar Koshkouei (MIT), and Game Boy Color games on
[Walnut-CGB](https://github.com/Mr-PauI/Walnut-CGB) by Mr. Paul (MIT), a
Peanut-GB rewrite; both carry parts from SameBoy by Lior Halphon (MIT). The
game screen started from the [Ink-boy](https://github.com/hellominecraft913-dev/Ink-boy)
CrossInk fork (MIT), whose Game Boy Color integration showed how to fit
Walnut-CGB beside Peanut-GB.
