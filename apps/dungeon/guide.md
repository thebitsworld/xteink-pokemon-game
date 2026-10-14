# Dungeon Map

## Goal

A logic puzzle. Fill in the walls of a dungeon map so that every rule below holds. Every map has exactly one solution, and the map completes itself the moment your walls are right.

## The rules

- The number beside each row and above each column is how many walls it holds.
- Every dead end - an open cell with only one open neighbour - holds a monster, and every monster stands in a dead end.
- Every chest lies in a 3x3 treasure room: nine open cells, that one chest, and exactly one way out.
- Outside treasure rooms, passages are one cell wide: no 2x2 square may be all open.
- All the open cells are joined up into one dungeon.

Monsters and chests are already drawn and their cells are always open. Neighbours are up, down, left and right (not diagonal).

## Levels

- Easy: 6x6.
- Medium: 8x8.
- Hard: 8x8 with a treasure room.

The timer shows how long you have taken; the menu keeps your best time per level.

## Marking cells

A cell is a wall (filled black), open (a small dot) or unknown (blank). Dots are only notes for you: the map is checked on walls alone, so blank cells count as open. A row or column number is bold until that line has exactly its number of walls, then turns plain.

## Controls

Buttons (X3/X4): Left/Right and the side buttons Up/Down move the cursor (it wraps around the edges). Confirm cycles the cell: wall, open, unknown. Back opens the menu; your map is kept. When the map is complete, Confirm makes a new one.

Touch (X4 Pro): the Wall/Open switch at the top picks what a tap puts down. Tap a cell to mark it, tap it again to clear it. Menu is at the top right. When the map is complete, tap the map for a new one.

## Tips

- A monster's cell has exactly one open neighbour: if three of its sides are already walls or the edge, the fourth is open, and so on.
- A row whose number is 0 is all open; a number equal to the size is all walls.
- Any open cell that is not a monster needs at least two open neighbours.
- Look for 2x2 blocks with three open cells: the fourth must be a wall.
- On Hard, find the room first: the chest's 3x3 room has walls all round except one gap.
