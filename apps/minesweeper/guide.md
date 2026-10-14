# Minesweeper

## Goal
Open every square that has no mine. Dig up a mine and the game is lost.

## Rules
- Pick a size: Easy 9x9 with 10 mines, Medium 12x12 with 24, Hard 16x16 with 40.
- Your first dig is always safe: no mine is on that square or next to it, so it opens an area.
- An open square shows how many mines touch it (up to 8 neighbours, diagonals included). A blank square has none, and its neighbours open by themselves.
- Flag a square you think hides a mine. "Mines left" counts mines minus flags. A flagged square cannot be dug.
- Digging an open number whose flags already match it opens all its other covered neighbours. A wrong flag there means a mine.
- Clear every safe square to win. The timer starts at the first dig; the best time for each size is kept.
- When you lose, all mines are shown, and wrong flags are crossed.

## Controls (X3/X4 buttons)
- Left/Right move the cursor across, Up/Down (the side buttons) move it up and down.
- Confirm digs the framed square (or opens around a finished number).
- Hold Confirm to place or remove a flag.
- Back opens the menu (Resume, sizes, Exit). After a game, Confirm starts a new one.

## Controls (X4 Pro touch)
- Tap a square to dig it. Hold a square to flag it.
- The Dig/Flag switch above the board turns taps into flags; tap it again for Dig.
- Tap Menu for the menu. After a game, tap the board for a new one.

## Tips
- A 1 touching only one covered square: that square is a mine.
- If a number already has all its mines flagged, its other neighbours are safe.
- Work outwards from what you are sure of, along the edge of the open area.
