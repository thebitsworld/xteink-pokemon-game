# Game ideas for Lua apps

Games still to build as Lua apps in `apps/`, in rough order of priority. Each one ships like the
others: rules in their own module, the device players loaded only while they move, the shared
`menu.lua`, logic tests in `test/lua_apps/`, and `guide.md` + `guide.vi.md`. It must stay under 75 KB
in `pokemon-simulator-X3`, or list `"devices": ["x4pro"]` (see [applications.md](../applications.md)
and [apps/README.md](../../apps/README.md)).

Already built: Caro, Reversi, O An Quan, Tien Len, Co Up, Dots and Boxes, Patience (FreeCell,
Spider), Logic Puzzles (Hashi, Lights Out, Slitherlink, Kakuro, Light Up), Hexland, Phom, Backgammon, Nine Men's Morris, Sam Loc, Mancala, plus the earlier games.

## Next

1. **Logic Puzzles additions**: KenKen, Nurikabe, Masyu, Star Battle - each generated on the
   reader with a single solution, one module per puzzle (the X3 limit is close: ~64 KB at most).

2. **Cờ gánh**: Vietnamese folk game on a 5x5 grid.
3. **Mastermind**: code breaking with 1-bit symbols instead of colours.
4. **Peg Solitaire**: the classic English and European boards.

## Later

- **Backgammon doubling cube**: an option for the built game.
- **Texas Hold'em**: against device players that estimate their hand's strength.
- **Cribbage, Gin Rummy**: two-player card classics.
- **Go 9x9 quick mode**: a faster setting of the existing Go app.

## Needs a decision or data first

- **Sokoban**: needs a level set whose licence allows shipping it.
- **Word games** (crosswords, word search, Spelling Bee-style): need a word list, which costs memory
  and storage; a Vietnamese version needs its own list.

## Not for e-ink

Action and real-time games (shooters, racing, falling-block speed games): the screen updates only a
few times a second.
