-- Logic tests for apps/go/logic.lua (run by ctest: LuaAppLogic_go).
local G = dofile(APPS .. "/go/logic.lua")

-- math.random, seeded: Lua here has 32-bit integers, so a hand-rolled LCG overflows.
math.randomseed(19)
local function rand(n) return math.random(1, n) end

local function at(g, c, r) return G.index(g, c, r) end

-- Places stones without turn order: "B" and "W" rows of a small board.
local function board(n, rows, turn)
    local g = G.new(n)
    for r, row in ipairs(rows) do
        for c = 1, n do
            local ch = row:sub(c, c)
            g.cells[at(g, c, r)] = (ch == "B") and G.BLACK or ((ch == "W") and G.WHITE or G.EMPTY)
        end
    end
    g.turn = turn or G.BLACK
    g.moves = 10
    return g
end

-- Capturing a single stone and a group; liberties are counted once.
do
    local g = board(5, { ".B...", "BW...", ".B...", ".....", "....." })
    local size, libs = G.scanGroup(g, at(g, 2, 2))
    assert(size == 1 and libs == 1)
    assert(G.play(g, at(g, 3, 2)) == 1 and g.cells[at(g, 2, 2)] == G.EMPTY and g.captured[1] == 1)
    g = board(5, { "WWB..", "B.B..", ".....", ".....", "....." })
    -- (1,1)-(2,1) white group; black at (1,2) and (3,1); playing (2,2) captures both.
    assert(G.play(g, at(g, 2, 2)) == 2)
    assert(g.cells[at(g, 1, 1)] == G.EMPTY and g.cells[at(g, 2, 1)] == G.EMPTY)
end

-- Suicide is refused, unless it captures.
do
    local g = board(5, { ".W...", "W....", ".....", ".....", "....." })
    assert(G.play(g, at(g, 1, 1)) == nil, "suicide")
    assert(g.cells[at(g, 1, 1)] == G.EMPTY and g.turn == G.BLACK)
    g = board(5, { ".WB..", "WB...", "B....", ".....", "....." })
    assert(G.play(g, at(g, 1, 1)) == 2, "filling the last liberty captures first")
end

-- Ko: the single stone just captured may not be retaken at once.
do
    local g = board(5, { ".BW..", "B.BW.", ".BW..", ".....", "....." }, G.WHITE)
    -- White plays (2,2): captures black (3,2)? Black (3,2) has libs at... set
    -- up the classic shape instead:
    g = board(5, { ".BW..", "BW.W.", ".BW..", ".....", "....." }, G.BLACK)
    assert(G.play(g, at(g, 3, 2)) == 1, "black takes the white stone at (2,2)")
    assert(g.ko == at(g, 2, 2))
    assert(G.play(g, at(g, 2, 2)) == nil, "white may not retake at once")
    assert(G.play(g, at(g, 5, 5)) == 0) -- white plays elsewhere
    G.play(g, at(g, 5, 4))              -- black plays elsewhere
    assert(G.play(g, at(g, 2, 2)) == 1, "now the retake is allowed")
end

-- Passing twice ends the game; area scoring with komi.
do
    local g = board(5, { "..B..", "..B..", "..B..", "..BW.", "..BW." })
    -- Black: 5 stones + the two left columns (10) = 15; White: 2 stones; the
    -- rest touches both colours.
    local b, w = G.score(g)
    assert(b == 15 and w == 2 + G.KOMI, "black " .. b .. " white " .. w)
    g.turn = G.BLACK
    assert(G.pass(g) and not g.over and G.pass(g) and g.over)
    assert(G.winner(g) == G.BLACK)
    assert(G.play(g, at(g, 5, 1)) == nil, "no moves after the end")
end

-- The computer: takes a capture, saves its own stone, never fills its eye,
-- and passes in a finished position.
do
    for level = 2, 3 do
        local g = board(5, { ".B...", "BW...", ".B...", ".....", "....." }, G.BLACK)
        assert(G.chooseMove(g, level, rand) == at(g, 3, 2), "captures the white stone in atari")
        g = board(5, { ".B...", "BW...", ".B...", ".....", "....." }, G.WHITE)
        assert(G.chooseMove(g, level, rand) == at(g, 3, 2), "the only escape")
        -- A living black group with two eyes: black passes rather than fill them.
        g = board(5, { "B.B.B", "BBBBB", "WWWWW", "W.W.W", "WWWWW" }, G.BLACK)
        g.passes = 1
        assert(G.chooseMove(g, level, rand) == nil, "passes")
    end
end

-- Whole games between the computer players end, with every move legal.
do
    for _, n in ipairs({ 9 }) do
        for game = 1, 3 do
            local g = G.new(n)
            local plies = 0
            while not g.over do
                local i = G.chooseMove(g, (g.turn == G.BLACK) and 3 or 2, rand)
                if i then
                    assert(G.play(g, i) ~= nil, "a legal move")
                else
                    G.pass(g)
                end
                plies = plies + 1
                assert(plies < 400, "the game ends")
            end
            local b, w = G.score(g)
            assert(b + w - G.KOMI <= n * n)
        end
    end
end

-- Save and restore.
do
    local g = G.new(9)
    for _ = 1, 12 do
        local i = G.chooseMove(g, 2, rand)
        if i then G.play(g, i) else G.pass(g) end
    end
    local s = G.serialize(g)
    local back = G.deserialize(s)
    assert(back and G.serialize(back) == s)
    assert(G.deserialize("9|1|0|0|0|0|0|" .. string.rep("0", 80)) == nil)
end

print("go logic ok")
