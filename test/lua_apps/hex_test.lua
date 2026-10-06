-- Logic tests for apps/hex/logic.lua (run by ctest: LuaAppLogic_hex).
local X = dofile(APPS .. "/hex/logic.lua")

-- math.random, seeded: Lua here has 32-bit integers, so a hand-rolled LCG overflows.
math.randomseed(8)
local function rand(n) return math.random(1, n) end

local function at(g, c, r) return X.index(g, c, r) end

-- Neighbours: six inside the board, fewer on the edges and corners.
do
    local g = X.new(5)
    assert(#X.neighbours(g, at(g, 3, 3)) == 6)
    assert(#X.neighbours(g, at(g, 1, 1)) == 2, "the acute corner")
    assert(#X.neighbours(g, at(g, 5, 1)) == 3, "the obtuse corner")
end

-- Black wins top to bottom, White left to right; turns alternate.
do
    local g = X.new(4)
    for r = 1, 4 do
        assert(g.turn == X.BLACK and X.play(g, at(g, 2, r)))
        if r < 4 then assert(X.play(g, at(g, 4, r))) end
    end
    assert(g.winner == X.BLACK, "a straight column joins top and bottom")
    assert(not X.play(g, at(g, 1, 1)), "no moves after a win")

    g = X.new(3)
    -- A chain that goes along the diagonal (up-right steps) also counts.
    X.play(g, at(g, 1, 2)) -- black
    X.play(g, at(g, 1, 3)) -- white
    X.play(g, at(g, 3, 1)) -- black
    X.play(g, at(g, 2, 3)) -- white
    X.play(g, at(g, 2, 1)) -- black
    assert(g.winner == 0)
    assert(X.play(g, at(g, 3, 3))) -- white: bottom row left to right
    assert(g.winner == X.WHITE)
    assert(not X.play(g, at(g, 3, 2)))
end

-- A full board always has exactly one winner.
do
    for _ = 1, 50 do
        local g = X.new(5)
        local order = {}
        for i = 1, 25 do order[i] = i end
        for i = 25, 2, -1 do
            local j = rand(i)
            order[i], order[j] = order[j], order[i]
        end
        for _, i in ipairs(order) do
            if g.winner ~= 0 then break end
            X.play(g, i)
        end
        assert(g.winner ~= 0, "someone wins")
        assert(not (X.connected(g, X.BLACK) and X.connected(g, X.WHITE)))
    end
end

-- Distances: an empty board needs n cells; own stones are free; a full
-- opposing wall blocks.
do
    local g = X.new(5)
    assert(X.distance(g, X.BLACK) == 5 and X.distance(g, X.WHITE) == 5)
    g.cells[at(g, 3, 1)], g.cells[at(g, 3, 2)] = X.BLACK, X.BLACK
    assert(X.distance(g, X.BLACK) == 3)
    for c = 1, 5 do g.cells[at(g, c, 4)] = X.WHITE end
    assert(X.distance(g, X.BLACK) >= 9999, "walled off")
    assert(X.distance(g, X.WHITE) == 0)
    -- The path marks run edge to edge.
    g = X.new(5)
    local marks = {}
    X.distance(g, X.BLACK, marks)
    local rows = {}
    for i in pairs(marks) do rows[X.rowOf(g, i)] = true end
    for r = 1, 5 do assert(rows[r], "a cell on the path in every row") end
end

-- The computer takes a winning cell and blocks the opponent's.
do
    for level = 2, 3 do
        local g = X.new(5)
        for c = 1, 4 do g.cells[at(g, c, 3)] = X.WHITE end -- White needs only (5,3) ...
        g.cells[at(g, 5, 2)] = X.BLACK                     -- ... now that (5,2) is taken
        g.turn, g.moves = X.WHITE, 5
        assert(X.chooseMove(g, level, rand) == at(g, 5, 3), "takes the win")
        g.turn = X.BLACK
        assert(X.chooseMove(g, level, rand) == at(g, 5, 3), "blocks the win")
    end
end

-- Stronger levels beat weaker ones.
local function match(a, b, n, games)
    local winsA = 0
    for k = 1, games do
        local g = X.new(n)
        local aIsBlack = k % 2 == 1
        while g.winner == 0 do
            local mine = (g.turn == X.BLACK) == aIsBlack
            X.play(g, X.chooseMove(g, mine and a or b, rand))
        end
        if (g.winner == X.BLACK) == aIsBlack then winsA = winsA + 1 end
    end
    return winsA
end

do
    local normalVsEasy = match(2, 1, 7, 10)
    local hardVsNormal = match(3, 2, 7, 10)
    print(string.format("7x7: normal beat easy %d/10, hard beat normal %d/10", normalVsEasy, hardVsNormal))
    assert(normalVsEasy >= 7)
    assert(hardVsNormal >= 5)
end

-- Save and restore.
do
    local g = X.new(7)
    for _ = 1, 9 do X.play(g, X.chooseMove(g, 2, rand)) end
    local s = X.serialize(g)
    local back = X.deserialize(s)
    assert(back and X.serialize(back) == s and back.turn == g.turn)
    assert(X.deserialize("7|1|0|" .. string.rep("0", 48)) == nil)
end

print("hex logic ok")
