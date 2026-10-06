-- Logic tests for apps/connectfour/logic.lua (run by ctest: LuaAppLogic_connectfour).
local C4 = dofile(APPS .. "/connectfour/logic.lua")

local function play(g, cols)
    for _, c in ipairs(cols) do assert(C4.drop(g, c), "drop failed in column " .. c) end
end

-- Discs stack from the bottom; a full column refuses more.
do
    local g = C4.new()
    for i = 1, 6 do assert(C4.drop(g, 1) == i) end
    assert(C4.drop(g, 1) == nil and not C4.canDrop(g, 1))
end

-- Horizontal, vertical and both diagonal wins are found, with the line.
do
    local g = C4.new()
    play(g, { 1, 1, 2, 2, 3, 3, 4 })
    assert(g.winner == 1 and #g.line == 4)
    assert(C4.drop(g, 5) == nil, "no moves after a win")

    g = C4.new()
    play(g, { 1, 2, 1, 2, 1, 2, 1 })
    assert(g.winner == 1)

    g = C4.new() -- rising diagonal for player 1: (1,1) (2,2) (3,3) (4,4)
    play(g, { 1, 2, 2, 3, 3, 4, 3, 4, 4, 7, 4 })
    assert(g.winner == 1, "rising diagonal")

    g = C4.new() -- falling diagonal for player 1: (4,1) (3,2) (2,3) (1,4)
    play(g, { 4, 3, 3, 2, 2, 1, 2, 1, 1, 7, 1 })
    assert(g.winner == 1, "falling diagonal")
end

-- A full board without four in a row is a draw.
do
    -- A full board (21 discs each) with no four in any direction.
    local rowsPattern = {
        { 2, 1, 2, 1, 2, 2, 1 },
        { 1, 1, 2, 2, 2, 1, 2 },
        { 2, 2, 1, 2, 2, 2, 1 },
        { 1, 1, 1, 2, 1, 1, 1 },
        { 2, 1, 1, 1, 2, 1, 2 },
        { 2, 1, 2, 1, 2, 1, 2 },
    }
    local g = C4.new()
    for r = 1, 6 do
        for c = 1, 7 do g.cells[C4.index(c, r)] = rowsPattern[r][c] end
    end
    -- Independent scan: the pattern really has no four in a row.
    for r = 1, 6 do
        for c = 1, 7 do
            for _, d in ipairs({ { 1, 0 }, { 0, 1 }, { 1, 1 }, { 1, -1 } }) do
                local ec, er = c + d[1] * 3, r + d[2] * 3
                if ec >= 1 and ec <= 7 and er >= 1 and er <= 6 then
                    local v, same = g.cells[C4.index(c, r)], true
                    for k = 1, 3 do
                        if g.cells[C4.index(c + d[1] * k, r + d[2] * k)] ~= v then same = false end
                    end
                    assert(not same, "pattern has a four")
                end
            end
        end
    end
    -- Take the last disc back out and drop it again.
    local last = g.cells[C4.index(7, 6)]
    g.cells[C4.index(7, 6)] = 0
    for c = 1, 7 do g.heights[c] = 6 end
    g.heights[7] = 5
    g.moves, g.turn = 41, last
    assert(C4.drop(g, 7) == 6)
    assert(g.winner == -1, "full board should be a draw")
end

-- The computer takes a winning move, and blocks the opponent's.
do
    local g = C4.new()
    play(g, { 1, 7, 2, 7, 3 }) -- player 2 to move; player 1 threatens column 4
    assert(C4.bestMove(g, 2) == 4, "must block")

    g = C4.new()
    play(g, { 7, 1, 7, 2, 6, 3 }) -- player 1 to move; player 2 threatens 4... player 1 has 7,7,6
    -- Player 1 to move: 1 has no win; must block column 4.
    assert(C4.bestMove(g, 3) == 4, "must block the open three")

    g = C4.new()
    play(g, { 1, 7, 2, 7, 3, 6 }) -- player 1 to move with 1,2,3 on the bottom row
    assert(C4.bestMove(g, 1) == 4, "must take the win")
    assert(C4.bestMove(g, 5) == 4, "deeper search still takes the win")
end

-- A deep search leaves the board unchanged.
do
    local g = C4.new()
    play(g, { 4, 4, 3 })
    local before = table.concat(g.cells, ",")
    C4.bestMove(g, 5)
    assert(table.concat(g.cells, ",") == before, "search must undo its moves")
end

print("connectfour logic ok")
