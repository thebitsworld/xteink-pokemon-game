-- Logic tests for apps/checkers/logic.lua and ai.lua (run by ctest: LuaAppLogic_checkers).
local C = dofile(APPS .. "/checkers/logic.lua")
local AI = dofile(APPS .. "/checkers/ai.lua")(C)

-- math.random, seeded: Lua here has 32-bit integers, so a hand-rolled LCG overflows.
math.randomseed(2024)
local function rand(n) return math.random(1, n) end

local function sq(c, r) return C.index(c, r) end

local function empty(turn)
    local g = C.new()
    for i = 1, 64 do g.board[i] = 0 end
    g.turn = turn or C.LIGHT
    return g
end

local function find(moves, path)
    for _, mv in ipairs(moves) do
        if C.pathLen(mv) == #path then
            local same = true
            for n = 1, #path do if C.pathAt(mv, n) ~= path[n] then same = false end end
            if same then return mv end
        end
    end
    return nil
end

-- The opening: 12 men each on dark squares, Light to move with 7 steps.
do
    local g = C.new()
    local lm, lk = C.count(g.board, C.LIGHT)
    local dm, dk = C.count(g.board, C.DARK)
    assert(lm == 12 and dm == 12 and lk == 0 and dk == 0)
    assert(#C.moves(g) == 7)
    for _, mv in ipairs(C.moves(g)) do
        assert(C.rowOf(C.to(mv)) == C.rowOf(C.from(mv)) - 1, "light men move up")
    end
end

-- Capture is compulsory; men never move or jump backwards.
do
    local g = empty()
    g.board[sq(4, 6)] = 1
    g.board[sq(5, 5)] = 3
    g.board[sq(1, 8)] = 1
    local moves = C.moves(g)
    assert(#moves == 1 and C.capCount(moves[1]) == 1 and C.to(moves[1]) == sq(6, 4), "the only move is the jump")
    -- A man cannot jump backwards.
    g = empty()
    g.board[sq(4, 4)] = 1
    g.board[sq(5, 5)] = 3
    for _, mv in ipairs(C.moves(g)) do assert(C.capCount(mv) == 0) end
    -- A king can.
    g.board[sq(4, 4)] = 2
    local m = C.moves(g)
    assert(#m == 1 and C.capAt(m[1], 1) == sq(5, 5) and C.to(m[1]) == sq(6, 6), "kings jump backwards")
end

-- A jump chain is one move, may branch, and must be taken to the end.
do
    local g = empty()
    g.board[sq(1, 8)] = 1
    g.board[sq(2, 7)] = 3
    g.board[sq(4, 5)] = 3
    g.board[sq(2, 5)] = 3
    local moves = C.moves(g)
    -- 1,8 x 2,7 -> 3,6, then either x 4,5 -> 5,4 or x 2,5 -> 1,4.
    assert(#moves == 2)
    assert(find(moves, { sq(1, 8), sq(3, 6), sq(5, 4) }) and find(moves, { sq(1, 8), sq(3, 6), sq(1, 4) }))
    for _, mv in ipairs(moves) do assert(C.capCount(mv) == 2) end
    local mv = find(moves, { sq(1, 8), sq(3, 6), sq(5, 4) })
    C.play(g, mv)
    assert(g.board[sq(2, 7)] == 0 and g.board[sq(4, 5)] == 0 and g.board[sq(2, 5)] == 3 and g.board[sq(5, 4)] == 1)
    -- Matching by the squares picked so far.
    assert(#C.matching(moves, { sq(1, 8) }) == 2 and #C.matching(moves, { sq(1, 8), sq(3, 6), sq(1, 4) }) == 1)
end

-- A chain can finish on the square it started from (the piece is lifted).
do
    local g = empty()
    g.board[sq(3, 6)] = 2
    g.board[sq(4, 5)] = 3
    g.board[sq(6, 5)] = 3
    g.board[sq(6, 7)] = 3
    g.board[sq(4, 7)] = 3
    g.board[sq(8, 1)] = 3
    local loop = nil
    for _, mv in ipairs(C.moves(g)) do
        if C.capCount(mv) == 4 then loop = mv end
    end
    assert(loop and C.to(loop) == sq(3, 6), "four jumps around the square")
    C.play(g, loop)
    assert(g.board[sq(3, 6)] == 2, "the king is still there")
    local m = C.count(g.board, C.DARK)
    assert(m == 1)
end

-- A man crowned during a chain stops there.
do
    local g = empty()
    g.board[sq(3, 4)] = 1
    g.board[sq(4, 3)] = 3
    g.board[sq(6, 2)] = 3 -- a king could take this next, a new king may not
    g.board[sq(8, 8)] = 3
    local moves = C.moves(g)
    assert(#moves == 1 and C.capCount(moves[1]) == 1 and C.to(moves[1]) == sq(5, 2))
    C.play(g, moves[1])
    assert(g.board[sq(5, 2)] == 1, "not crowned yet: row 2 is not the far row")
    g = empty()
    g.board[sq(3, 3)] = 1
    g.board[sq(4, 2)] = 3
    g.board[sq(6, 2)] = 3
    g.board[sq(8, 8)] = 3
    moves = C.moves(g)
    assert(#moves == 1 and C.capCount(moves[1]) == 1, "crowning ends the chain")
    C.play(g, moves[1])
    assert(g.board[sq(5, 1)] == 2 and g.board[sq(6, 2)] == 3)
end

-- Winning by capturing everything, by blocking, and the idle draw.
do
    local g = empty()
    g.board[sq(4, 6)] = 1
    g.board[sq(5, 5)] = 3
    C.play(g, C.moves(g)[1])
    assert(g.winner == C.LIGHT, "no dark pieces left")

    g = empty(C.DARK)
    g.board[sq(1, 7)] = 3 -- blocked by two light men
    g.board[sq(2, 8)] = 1
    g.board[sq(4, 8)] = 1
    g.board[sq(8, 4)] = 1
    g.turn = C.LIGHT
    local step = find(C.moves(g), { sq(8, 4), sq(7, 3) })
    C.play(g, step)
    assert(g.winner == C.LIGHT, "dark has no legal move")

    g = empty()
    g.board[sq(1, 8)] = 2
    g.board[sq(8, 1)] = 4
    g.idle = C.IDLE_LIMIT - 1
    C.play(g, C.moves(g)[1])
    assert(g.winner == -1, "eighty quiet plies draw")
end

-- Save and restore round-trip; impossible boards are refused.
do
    local g = C.new()
    C.play(g, C.moves(g)[3])
    local s = C.serialize(g)
    local back = C.deserialize(s)
    assert(back and C.serialize(back) == s and back.turn == C.DARK)
    assert(C.deserialize("1|0|" .. string.rep("0", 63)) == nil)
    assert(C.deserialize("1|0|1" .. string.rep("0", 63)) == nil, "a piece on a light square")
end

-- The opponent: takes the double jump over the single, and does not hand
-- over a piece for nothing.
do
    local g = empty(C.DARK)
    g.board[sq(2, 3)] = 3
    g.board[sq(3, 4)] = 1
    g.board[sq(3, 6)] = 1
    g.board[sq(6, 3)] = 3
    g.board[sq(7, 4)] = 1
    g.board[sq(1, 8)] = 1
    local mv = AI.choose(g, 4, 20000, 0, rand)
    assert(C.capCount(mv) == 2, "the double jump")

    g = empty(C.DARK)
    g.board[sq(4, 3)] = 3
    g.board[sq(7, 6)] = 1
    g.board[sq(2, 6)] = 1
    -- 4,3 -> 3,4 or 5,4 are both safe; neither hands a piece over.
    mv = AI.choose(g, 4, 20000, 0, rand)
    C.play(g, mv)
    for _, reply in ipairs(C.moves(g)) do assert(C.capCount(reply) == 0, "nothing left hanging") end
end

-- Whole games: the searching opponent beats one that plays at random.
do
    local wins, draws = 0, 0
    for n = 1, 6 do
        local g = C.new()
        local plies = 0
        while g.winner == 0 do
            local mv
            if g.turn == C.DARK then
                mv = AI.choose(g, 4, 3000, 0, rand)
            else
                local moves = C.moves(g)
                mv = moves[rand(#moves)]
            end
            assert(mv and C.play(g, mv))
            plies = plies + 1
            assert(plies < 400)
        end
        if g.winner == C.DARK then wins = wins + 1 elseif g.winner == -1 then draws = draws + 1 end
    end
    assert(wins >= 5, "won " .. wins .. " of 6 against random play")
end

print("checkers logic ok")
