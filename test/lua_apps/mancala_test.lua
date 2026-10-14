-- Logic tests for apps/mancala/logic.lua and ai.lua (run by ctest: LuaAppLogic_mancala).
local C = dofile(APPS .. "/mancala/logic.lua")
local AI = dofile(APPS .. "/mancala/ai.lua")(C)

math.randomseed(17)
local function rand(n) return math.random(1, n) end

-- A position: `pits` lists indexes 1..14 (stores at 7 and 14).
local function setup(pits, side, ec)
    local s = C.new(side or 1, 4, ec)
    for p = 1, 14 do s[p] = pits[p] end
    return s
end

-- Start: 4 seeds in each of the twelve pits, empty stores.
do
    for seeds = 3, 6 do
        local s = C.new(1, seeds)
        assert(C.total(s) == 12 * seeds and s[7] == 0 and s[14] == 0)
        local list = {}
        assert(C.legal(s, list) == 6 and list[1] == 1 and list[6] == 6)
        assert(C.isLegal(s, 3) and not C.isLegal(s, 7) and not C.isLegal(s, 9))
        s = C.new(2, seeds)
        assert(C.legal(s, list) == 6 and list[1] == 8 and C.isLegal(s, 13) and not C.isLegal(s, 14))
    end
end

-- Plain sowing: one seed a pit, counter-clockwise, the turn passes.
do
    local s = C.new(1)
    local info = {}
    C.play(s, 1, info) -- 1 -> 2, 3, 4, 5
    assert(s[1] == 0 and s[2] == 5 and s[5] == 5 and s[6] == 4 and s[7] == 0)
    assert(not info.extra and info.cap == 0 and info.from == 1 and info.last == 5 and s[C.SIDE] == 2)
    assert(info.changed == (1 | 2 | 4 | 8 | 16))
    -- Player 2 sows into its own store at 14 and on to player 1's pits.
    C.play(s, 12, info) -- 12 -> 13, 14, 1, 2
    assert(s[12] == 0 and s[13] == 5 and s[14] == 1 and s[1] == 1 and s[2] == 6 and s[C.SIDE] == 1)
end

-- Extra turn: the last seed in your own store.
do
    local s = C.new(1)
    local info = {}
    C.play(s, 3, info) -- 3 -> 4, 5, 6, 7
    assert(info.extra and s[7] == 1 and s[C.SIDE] == 1, "extra turn")
    s = C.new(2)
    C.play(s, 10, info) -- 10 -> 11, 12, 13, 14
    assert(info.extra and s[14] == 1 and s[C.SIDE] == 2, "extra turn for player 2")
end

-- Sowing skips the other player's store and wraps around.
do
    --               1  2  3  4  5  6   7  8  9 10 11 12 13 14
    local s = setup({ 0, 0, 0, 0, 0, 10, 0, 1, 1, 1, 1, 1, 1, 0 })
    local info = {}
    C.play(s, 6, info) -- 7, 8..13 (6 seeds), skip 14, 1, 2, 3; 3 was empty: it takes 11 (2)
    assert(s[7] == 1 + 3 and s[8] == 2 and s[13] == 2 and s[14] == 0 and s[1] == 1 and s[2] == 1 and s[4] == 0)
    assert(info.last == 3 and info.cap == 3 and s[3] == 0 and s[11] == 0 and s[C.SIDE] == 2, "skip the other store")
    -- Player 2 skips store 7.
    s = setup({ 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 10, 0 }, 2)
    C.play(s, 13, info) -- 14, 1..6, skip 7, 8, 9, 10; 10 was empty: it takes 4 (2)
    assert(s[14] == 1 + 3 and s[1] == 2 and s[6] == 2 and s[7] == 0 and s[8] == 1 and s[10] == 0 and s[4] == 0)
    assert(info.cap == 3 and s[C.SIDE] == 1)
    -- 13 seeds lap the board and the last lands in the emptied starting pit:
    -- a capture of it and the facing pit.
    s = setup({ 13, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0 })
    C.play(s, 1, info) -- 2..6, 7, 8..13, skip 14, 1
    assert(info.last == 1 and info.cap == 1 + 3 and s[1] == 0 and s[13] == 0 and s[7] == 1 + 4, "full lap capture")
    assert(C.total(s) == 15)
end

-- Capture: the last seed in an empty pit of yours takes the facing pit too.
do
    --               1  2  3  4  5  6  7  8  9 10 11 12 13 14
    local s = setup({ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 5, 0, 0, 0 })
    local info = {}
    C.play(s, 1, info) -- 1 -> 2 (empty); 14 - 2 = 12 is empty: no capture
    assert(info.cap == 0 and s[2] == 1)
    s = setup({ 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 5, 0, 1, 0 })
    C.play(s, 2, info) -- 2 -> 3, facing 11 with 5: capture 6
    assert(info.cap == 6 and info.capPit == 3 and s[3] == 0 and s[11] == 0 and s[7] == 6, "capture")
    assert(s[C.SIDE] == 2)
    -- Landing in an empty pit on the other side captures nothing.
    s = setup({ 1, 0, 0, 0, 0, 2, 0, 0, 3, 0, 0, 0, 0, 0 })
    C.play(s, 6, info) -- 7, 8 (empty, the other side's)
    assert(info.cap == 0 and s[8] == 1 and s[7] == 1)
    -- Player 2 captures into store 14.
    s = setup({ 0, 0, 4, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0 }, 2)
    C.play(s, 9, info) -- 9 -> 10, facing 4 is empty
    assert(info.cap == 0)
    s = setup({ 0, 0, 0, 4, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0 }, 2)
    C.play(s, 9, info) -- 9 -> 10, facing 4 with 4: capture 5
    assert(info.cap == 5 and s[14] == 5 and s[4] == 0 and s[10] == 0 and s[C.SIDE] == 1)
end

-- Empty capture option: the lone seed is taken even when the facing pit is empty.
do
    local s = setup({ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 5, 0, 0, 0 }, 1, true)
    local info = {}
    C.play(s, 1, info)
    assert(info.cap == 1 and s[2] == 0 and s[7] == 1, "empty capture on")
    s = setup({ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 5, 0, 0, 0 }, 1, false)
    C.play(s, 1, info)
    assert(info.cap == 0 and s[2] == 1 and s[7] == 0, "empty capture off")
end

-- The end: a side empty after a move, the other player takes the rest.
do
    --               1  2  3  4  5  6   7  8  9 10 11 12 13 14
    local s = setup({ 0, 0, 0, 0, 0, 1, 20, 2, 0, 3, 0, 0, 0, 22 })
    local info = {}
    C.play(s, 6, info) -- last seed in store 7, but player 1's side is now empty
    assert(s[C.OVER] == 1 and s[7] == 21 and s[14] == 27 and info.sweep1 == 0 and info.sweep2 == 5)
    for p = 1, 13 do if p ~= 7 then assert(s[p] == 0) end end
    assert(C.winner(s) == 2 and C.legal(s, {}) == 0 and not C.isLegal(s, 8))
    -- A capture can empty the other side.
    s = setup({ 0, 1, 0, 0, 0, 2, 10, 0, 0, 0, 3, 0, 0, 10 })
    C.play(s, 2, info) -- capture 3 + 1, player 2 has nothing left
    assert(s[C.OVER] == 1 and info.cap == 4 and info.sweep1 == 2 and s[7] == 16 and s[14] == 10)
    -- A draw.
    s = setup({ 0, 0, 0, 0, 0, 1, 23, 0, 0, 0, 0, 0, 1, 23 })
    C.play(s, 6, info)
    assert(s[C.OVER] == 1 and s[7] == 24 and s[14] == 24 and C.winner(s) == 0)
end

-- Replay (saved games and take-backs).
do
    local s = C.new(2, 5, true)
    local hist = ""
    local list = {}
    for _ = 1, 12 do
        local n = C.legal(s, list)
        if n == 0 then break end
        local p = list[rand(n)]
        C.play(s, p)
        hist = hist .. C.moveChar(p)
    end
    local sides, info = {}, {}
    local r = C.replay(2, 5, true, hist, nil, info, sides)
    for i = 1, C.SIZE do assert(r[i] == s[i]) end
    assert(sides[1] == 2)
    assert(C.replay(1, 5, true, hist) == nil or hist:byte(1) - 64 <= 6, "replay refuses a wrong history")
    local half = C.replay(2, 5, true, hist, 3)
    assert(half[C.PLY] == 3)
end

-- Random games: seeds conserved, games end.
local function playGame(chooser1, chooser2, starter, seeds, ec)
    local s = C.new(starter, seeds, ec)
    local total = 12 * (seeds or 4)
    local list, info = {}, {}
    local plies = 0
    while s[C.OVER] == 0 do
        local n = C.legal(s, list)
        assert(n > 0, "no legal move in a running game")
        local m = (s[C.SIDE] == 1 and chooser1 or chooser2)(s, list, n)
        assert(C.isLegal(s, m), "illegal move " .. tostring(m))
        C.play(s, m, info)
        assert(C.total(s) == total, "seeds conserved")
        for p = 1, 14 do assert(s[p] >= 0) end
        plies = plies + 1
        assert(plies <= 500, "game did not end")
    end
    assert(s[7] + s[14] == total)
    return s
end

local function randomPlayer(_, list, n) return list[rand(n)] end

do
    for g = 1, 400 do playGame(randomPlayer, randomPlayer, g % 2 + 1, 3 + g % 4, g % 3 == 0) end
end

-- AI: legal moves at every level, finished games, and levels in order.
local function aiPlayer(level, limit)
    return function(s)
        local m = AI.choose(s, level, rand, nil, nil, limit)
        assert(C.isLegal(s, m), "AI move is legal")
        return m
    end
end

local function match(levelA, levelB, games, limit)
    local winsA = 0
    for g = 1, games do
        local a, b = aiPlayer(levelA, limit), aiPlayer(levelB, limit)
        local s
        if g % 2 == 0 then
            s = playGame(a, b, 1, 4)
            local w = C.winner(s)
            if w == 1 then winsA = winsA + 1 elseif w == 0 then winsA = winsA + 0.5 end
        else
            s = playGame(b, a, 1, 4)
            local w = C.winner(s)
            if w == 2 then winsA = winsA + 1 elseif w == 0 then winsA = winsA + 0.5 end
        end
    end
    return winsA / games
end

do
    local r = match(2, 1, 40)
    print(string.format("Normal vs Easy: %.2f", r))
    assert(r >= 0.6, "Normal beats Easy")
    r = match(3, 1, 20, 8000)
    print(string.format("Hard vs Easy: %.2f", r))
    assert(r >= 0.75, "Hard beats Easy")
    r = match(3, 2, 20, 8000)
    print(string.format("Hard vs Normal: %.2f", r))
    assert(r >= 0.5, "Hard holds Normal")
    for seeds = 3, 6 do
        local s = playGame(aiPlayer(3, 3000), aiPlayer(3, 3000), 1, seeds, seeds == 6)
        assert(s[C.OVER] == 1)
    end
end

-- The AI takes a big capture and extra turns when it sees them.
do
    --               1  2  3  4  5  6  7  8  9 10 11 12 13 14
    local s = setup({ 1, 0, 0, 1, 0, 0, 0, 0, 9, 0, 0, 0, 1, 0 })
    for level = 2, 3 do assert(AI.choose(s, level, rand) == 4, "capture 10") end
    -- Pit 6 (one seed) gives an extra turn and then pit 5 another one.
    s = setup({ 0, 0, 0, 0, 2, 1, 0, 0, 0, 0, 0, 0, 6, 0 })
    for level = 2, 3 do assert(AI.choose(s, level, rand) == 6, "extra turns") end
end

-- Hard's search stays within its node budget, and the clock stops it.
do
    local t = os.clock()
    local s = C.new(1)
    for _ = 1, 5 do AI.choose(s, 3, rand) end
    print(string.format("Hard: %.1f ms a move from the start (host, 40000 nodes)", (os.clock() - t) * 200))
    local ms = 0
    local function clock()
        ms = ms + 1
        return ms
    end
    assert(C.isLegal(s, AI.choose(s, 3, rand, clock, 5, 1000000)))
    assert(ms < 40, "clock stops the search")
end

print("mancala: all tests passed")
