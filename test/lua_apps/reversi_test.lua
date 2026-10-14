-- Logic tests for apps/reversi/logic.lua and ai.lua (run by ctest: LuaAppLogic_reversi).
local L = dofile(APPS .. "/reversi/logic.lua")
local AI = dofile(APPS .. "/reversi/ai.lua")(L)

-- math.random, seeded: Lua here has 32-bit integers, so a hand-rolled LCG overflows.
math.randomseed(31)
local function rand(n) return math.random(1, n) end

local E, B, W = L.EMPTY, L.BLACK, L.WHITE
local function sq(name) return L.square(name:byte(1) - 96, tonumber(name:sub(2))) end

-- A position from a picture: 8 rows of "." (empty), "x" (black), "o" (white).
local function board(rows)
    local b = L.newBoard()
    for r = 1, 8 do
        for c = 1, 8 do
            local ch = rows[r]:sub(c, c)
            b[L.square(c, r)] = ch == "x" and B or (ch == "o" and W or E)
        end
    end
    return b
end
local function game(rows, side)
    local g = L.new()
    g.b, g.side = board(rows), side
    L.settle(g)
    return g
end
local function movesOf(b, side)
    local out = {}
    local n = L.moves(b, side, out)
    local names = {}
    for i = 1, n do names[i] = L.name(out[i]) end
    return table.concat(names, " ")
end

-- The start: black moves first with the four classic replies.
do
    local g = L.new()
    assert(g.side == B)
    assert(movesOf(g.b, B) == "d3 c4 f5 e6", movesOf(g.b, B))
    assert(movesOf(g.b, W) == "e3 f4 c5 d6", movesOf(g.b, W))
    local nb, nw, ne = L.counts(g.b)
    assert(nb == 2 and nw == 2 and ne == 60)
    assert(L.play(g, sq("d3")))
    assert(g.b[sq("d4")] == B and g.side == W and g.last == sq("d3"))
    nb, nw = L.counts(g.b)
    assert(nb == 4 and nw == 1)
end

-- One move flips lines in all eight directions at once, and stops at the
-- first own disc (and not past an empty square or the edge).
do
    local g = game({
        "x..x..x.",
        ".o.o.o..",
        "..ooo...",
        "xoo.oox.",
        "..ooo...",
        ".o.o.o..",
        "x..x..x.",
        "........",
    }, B)
    assert(L.flipCount(g.b, sq("d4"), B) == 16)
    assert(L.play(g, sq("d4")))
    for _, n in ipairs({ "b2", "c3", "d2", "d3", "f2", "e3", "b4", "c4", "e4", "f4", "c5", "b6", "d5", "d6", "e5", "f6" }) do
        assert(g.b[sq(n)] == B, "flipped " .. n)
    end
    local nb, nw = L.counts(g.b)
    assert(nb == 8 + 1 + 16 and nw == 0, nb .. " " .. nw)
end

-- Each direction alone.
do
    for _, d in ipairs(L.DIRS) do
        local b = L.newBoard()
        for i = 11, 88 do if b[i] ~= L.OFF then b[i] = E end end
        local from = 44
        b[from + d], b[from + 2 * d] = W, W
        b[from + 3 * d] = B
        assert(L.canPlay(b, from, B), "direction " .. d)
        assert(L.flipCount(b, from, B) == 2)
        local top = L.make(b, from, B, {}, 0)
        assert(top == 2 and b[from + d] == B and b[from + 2 * d] == B)
        -- A gap or the edge makes no line.
        local b2 = L.newBoard()
        for i = 11, 88 do if b2[i] ~= L.OFF then b2[i] = E end end
        b2[from + d] = W
        b2[from + 3 * d] = B
        assert(not L.canPlay(b2, from, B), "gap " .. d)
    end
    -- Off the edge: a white line up to the border is not closed.
    local b = board({ "......oo", "......o.", "........", "........", "........", "........", "........", "........" })
    assert(not L.canPlay(b, sq("f1"), B) and not L.canPlay(b, sq("g3"), B))
    assert(L.flipCount(b, sq("f1"), B) == 0)
end

-- A move that flips nothing is illegal, as is a taken square.
do
    local g = L.new()
    assert(not L.play(g, sq("a1")), "flips nothing")
    assert(not L.play(g, sq("d4")), "taken")
    assert(not L.play(g, sq("c3")), "diagonal with no line")
    assert(L.moveCount(g) == 0 and g.side == B)
    -- Make and unmake restore the board.
    local before = {}
    for i = 0, 99 do before[i] = g.b[i] end
    local fs = {}
    local top = L.make(g.b, sq("f5"), B, fs, 0)
    L.unmake(g.b, sq("f5"), B, fs, 0, top)
    for i = 0, 99 do assert(g.b[i] == before[i]) end
end

-- Pass: after black's move white has no move, so black plays again; when
-- neither side can move the game is over.
do
    local g = game({
        ".oxxxxxx",
        "........",
        "........",
        "...x....",
        "...o....",
        "........",
        "........",
        "........",
    }, B)
    assert(not g.result and not g.passed and g.side == B)
    assert(movesOf(g.b, B) == "a1 d6", movesOf(g.b, B))
    assert(L.play(g, sq("d6")))
    assert(movesOf(g.b, W) == "", "white has no move")
    assert(g.passed == W and g.side == B and not g.result, "white passes")
    assert(not L.play(g, sq("c1")), "a pass does not make a taken square legal")
    assert(L.play(g, sq("a1")))
    assert(g.result == "black" and not g.passed, "no white discs left: over")
    -- A side with no move when a position is set up passes too.
    local g2 = game({
        "...x....",
        "...o....",
        "........",
        "........",
        "........",
        "........",
        "........",
        "x.......",
    }, W)
    assert(g2.passed == W and g2.side == B, "white passes when settling")
    assert(L.play(g2, sq("d3")) and g2.result == "black")
end

-- Game end and counting: a full board, and a board neither side can play on.
do
    local g = game({
        "xxxxxxxx", "xxxxxxxx", "xxxxxxxx", "xxxxoooo",
        "oooooooo", "oooooooo", "oooooooo", "ooooooo.",
    }, B)
    -- Black h8? h7 white, h6 white ... up to h4 white, h3 black: legal.
    assert(L.play(g, sq("h8")))
    local nb, nw, ne = L.counts(g.b)
    assert(ne == 0 and nb + nw == 64)
    assert(g.result == (nb > nw and "black" or (nw > nb and "white" or "draw")))
    -- 32-32 is a draw.
    g = game({
        "xxxxxxxx", "xxxxxxxx", "xxxxxxxx", "xxxxxxxx",
        "oooooooo", "oooooooo", "oooooooo", "oooooooo",
    }, B)
    assert(g.result == "draw")
    -- Empty squares left but nobody can move: the most discs wins.
    g = game({
        "x.......", "........", "........", "........",
        "........", "........", "........", ".......o",
    }, B)
    assert(g.result == "draw")
    g = game({
        "xx......", "........", "........", "........",
        "........", "........", "........", ".......o",
    }, W)
    assert(g.result == "black")
end

-- History: replay and take-back.
do
    local g = L.new()
    local out = {}
    for _ = 1, 6 do
        L.moves(g.b, g.side, out)
        assert(L.play(g, out[1]))
    end
    assert(g.sides == "121212")
    local r = L.replay(g.history)
    for i = 0, 99 do assert(r.b[i] == g.b[i]) end
    assert(r.side == g.side and r.last == g.last)
    assert(L.backTo(g, nil) == 1)
    assert(L.backTo(g, B) == 2, "black to move now: back to black's previous turn")
    assert(L.backTo(g, W) == 1)
    local t = L.takeBack(g, 2)
    assert(L.moveCount(t) == 4 and t.side == B)
    assert(L.replay("~~") == nil, "bad history")
end

-- Every level plays only legal moves, and games finish.
do
    for level = 1, 3 do
        for _ = 1, 1 do
            local g = L.new()
            local plies = 0
            while not g.result do
                local mv = AI.choose(g, level, rand, nil, level == 3 and 400 or 300)
                assert(mv and L.canPlay(g.b, mv, g.side), "illegal move at level " .. level)
                assert(L.play(g, mv))
                plies = plies + 1
                assert(plies <= 60)
            end
            local nb, nw, ne = L.counts(g.b)
            assert(nb + nw + ne == 64)
        end
    end
end

-- Hard plays a perfect end: with 8 empty squares it searches to the end of
-- the game, and with 1 empty square left takes the better result.
do
    local g = L.new()
    while not g.result do
        local _, _, ne = L.counts(g.b)
        if ne <= 8 then break end
        assert(L.play(g, AI.choose(g, 1, rand)))
    end
    if not g.result then
        local _, _, ne = L.counts(g.b)
        local mv = AI.choose(g, 3, rand, nil, 1000000)
        assert(mv and L.canPlay(g.b, mv, g.side))
        assert(AI.depthDone == ne or L.moves(g.b, g.side, {}) == 1, "searched to the end: " .. tostring(AI.depthDone))
    end
end

-- Hard beats Easy most of the time (a small sample with a reduced node cap).
do
    local hardWins, games = 0, 6
    for k = 1, games do
        local g = L.new()
        local hardSide = k % 2 == 1 and B or W
        while not g.result do
            local mv
            if g.side == hardSide then mv = AI.choose(g, 3, rand, nil, 500) else mv = AI.choose(g, 1, rand) end
            assert(L.play(g, mv))
        end
        if g.result == (hardSide == B and "black" or "white") then hardWins = hardWins + 1 end
    end
    assert(hardWins >= 5, "Hard won only " .. hardWins .. " of " .. games)
    print("reversi: Hard beat Easy " .. hardWins .. "/" .. games)
end

print("reversi: all tests passed")
