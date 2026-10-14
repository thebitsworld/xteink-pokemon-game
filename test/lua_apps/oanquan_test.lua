-- Logic tests for apps/oanquan/logic.lua and ai.lua (run by ctest: LuaAppLogic_oanquan).
local C = dofile(APPS .. "/oanquan/logic.lua")
local AI = dofile(APPS .. "/oanquan/ai.lua")(C)

math.randomseed(31)
local function rand(n) return math.random(1, n) end

local function mv(p, d) return p * 2 + (d > 0 and 1 or 0) end

-- A position: `rows` lists squares 1..12, quans the two mandarin flags.
local function setup(rows, quanL, quanR, side)
    local s = C.new(side or 1)
    for p = 1, 12 do s[p] = rows[p] end
    s[13], s[14] = quanL, quanR
    -- Keep the stone count at 50: the rest goes to player 2's captures.
    local stones = 0
    for p = 1, 12 do stones = stones + s[p] end
    s[16] = 50 - stones
    s[18] = 2 - quanL - quanR
    return s
end

local function checkTotals(s, where)
    local stones, quans = C.totals(s)
    assert(stones == 50, (where or "") .. ": stones " .. stones)
    assert(quans == 2, (where or "") .. ": mandarins " .. quans)
end

-- Start: five stones in each of the ten squares, a mandarin in each end.
do
    local s = C.new()
    for p = 2, 6 do assert(s[p] == 5 and s[p + 6] == 5) end
    assert(s[1] == 0 and s[7] == 0 and s[13] == 1 and s[14] == 1)
    checkTotals(s, "start")
    local list = {}
    assert(C.legal(s, list) == 10)
    assert(C.isLegal(s, mv(2, 1)) and not C.isLegal(s, mv(8, 1)) and not C.isLegal(s, mv(1, 1)))
end

-- Simple sowing that ends on a mandarin square: the turn ends.
do
    --        Q  b1 b2 b3 b4 b5 Q  t1 t2 t3 t4 t5
    local s = setup({ 0, 0, 0, 0, 2, 0, 0, 5, 5, 5, 5, 5 }, 1, 1)
    local info = {}
    C.play(s, mv(5, 1), info) -- 5 -> 6, 7; next is square 8 with stones: pick up and continue
    -- 8's five stones go to 9..12 and 1; next is 2 (empty) then 3 (empty): stop.
    assert(s[6] == 1 and s[7] == 1 and s[8] == 0 and s[9] == 6 and s[12] == 6 and s[1] == 1, "sow and continue")
    assert(info.laps == 1 and info.capD == 0 and s[21] == 2)
    checkTotals(s, "continue")
end

-- Next square a non-empty mandarin square: the turn ends.
do
    local s = setup({ 0, 0, 0, 0, 0, 1, 0, 5, 5, 5, 5, 5 }, 1, 1)
    local info = {}
    C.play(s, mv(6, -1), info) -- 6 -> 5; next 4 empty, then 3 empty: stop
    assert(s[5] == 1 and info.capD == 0)
    s = setup({ 0, 1, 0, 0, 0, 0, 0, 5, 5, 5, 5, 5 }, 1, 1)
    C.play(s, mv(2, 1), info) -- 2 -> 3; next 4 empty, 5 empty: stop
    s = setup({ 0, 0, 0, 0, 0, 1, 0, 5, 5, 5, 5, 5 }, 1, 1)
    C.play(s, mv(6, 1), info) -- 6 -> 7 (mandarin square); next 8 has stones: picked up
    assert(info.laps == 1)
    s = setup({ 0, 0, 0, 0, 1, 0, 0, 5, 5, 5, 5, 5 }, 1, 1)
    C.play(s, mv(5, 1), info) -- 5 -> 6; next is 7, the mandarin square: turn ends
    assert(info.laps == 0 and info.capD == 0 and s[6] == 1 and s[7] == 0 and s[14] == 1, "stop before mandarin")
    checkTotals(s, "stop")
end

-- Capture: empty square then a full one; chain capture; two empties stop.
do
    --        Q  b1 b2 b3 b4 b5 Q  t1 t2 t3 t4 t5
    local s = setup({ 0, 2, 0, 0, 0, 0, 0, 0, 3, 0, 4, 1 }, 1, 1)
    -- Sow 2 from square 2 down (-1): 1, 12. Next 11 has 4: pick up, sow 10, 9, 8, 7.
    -- Next 6 empty, then 5 empty: stop.
    local info = {}
    C.play(s, mv(2, -1), info)
    assert(s[1] == 1 and s[12] == 2 and s[11] == 0 and s[10] == 1 and s[9] == 4 and s[8] == 1 and s[7] == 1)
    assert(info.capD == 0)

    s = setup({ 0, 0, 1, 0, 3, 0, 0, 2, 0, 0, 0, 0 }, 1, 1)
    -- Sow 1 from 3 up: 4. Next 5 has 3: pick up, sow 6, 7, 8. Next 9 empty, 10 empty: stop.
    C.play(s, mv(3, 1), info)
    assert(s[8] == 3 and info.capD == 0 and info.laps == 1)

    s = setup({ 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, 1, 1)
    s[5], s[7], s[9] = 0, 0, 0
    -- 3 -> 4; next 5 empty; 6 empty: nothing.
    C.play(s, mv(3, 1), info)
    assert(info.capD == 0)

    -- Single capture: 3 -> 4; 5 empty; 6 has 4 stones: captured. 7 is the
    -- mandarin square (not empty): chain stops.
    s = setup({ 0, 0, 1, 0, 0, 4, 0, 0, 0, 0, 0, 3 }, 1, 1)
    C.play(s, mv(3, 1), info)
    assert(info.capD == 4 and s[6] == 0 and s[15] == 4 and C.score(s, 1) == 4, "capture")
    assert(info.captured == (1 << 5))
    checkTotals(s, "capture")

    -- Chain: 2 -> 3; 4 empty, 5 (2) captured; 6 empty, 7 (mandarin, 7 stones) captured;
    -- 8 empty, 9 (3) captured; 10 has stones: stop.
    s = setup({ 0, 1, 0, 0, 2, 0, 7, 0, 3, 1, 0, 0 }, 1, 1)
    C.play(s, mv(2, 1), info)
    assert(info.capD == 12 and info.capQ == 1, "chain " .. info.capD)
    assert(s[14] == 0 and s[7] == 0 and s[17] == 1 and C.score(s, 1) == 22)
    assert(info.captured == ((1 << 4) | (1 << 6) | (1 << 8)))
    checkTotals(s, "chain")
end

-- Young mandarin (fewer than 5 stones with its mandarin) cannot be captured.
do
    -- 4 -> 5; 6 empty; 7 is the mandarin square with 3 stones: young, stop.
    local s = setup({ 0, 0, 0, 1, 0, 0, 3, 0, 2, 0, 0, 0 }, 1, 1)
    local info = {}
    C.play(s, mv(4, 1), info)
    assert(info.young and info.capD == 0 and s[14] == 1 and s[7] == 3, "young mandarin")
    -- With 5 stones it is captured: 15 points.
    s = setup({ 0, 0, 0, 1, 0, 0, 5, 2, 0, 0, 0, 0 }, 1, 1)
    C.play(s, mv(4, 1), info)
    assert(not info.young and info.capD == 5 and info.capQ == 1 and C.score(s, 1) == 15)
    -- A mandarin square whose mandarin is gone holds plain stones: any number is taken.
    s = setup({ 0, 0, 0, 1, 0, 0, 2, 2, 0, 0, 0, 0 }, 1, 0)
    C.play(s, mv(4, 1), info)
    assert(info.capD == 2 and info.capQ == 0)
    checkTotals(s, "young")
    -- An empty mandarin square (its mandarin gone) counts as an empty square:
    -- player 2 sows 11 -> 12; 1 is empty; 2 (3 stones) is captured.
    s = setup({ 0, 3, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 }, 0, 1, 2)
    C.play(s, mv(11, 1), info)
    assert(info.capD == 3 and s[2] == 0 and s[16] == 50 - 5 + 3, "capture past an empty mandarin square")
    checkTotals(s, "empty mandarin square")
end

-- Empty row: the player puts back 5 captured stones; borrowing when short.
do
    -- Player 1 moves 6 -> 7 (mandarin square has 5 already with mandarin: not
    -- empty, stop). Player 2's row ends empty? Set it up so player 2's row is empty.
    local s = setup({ 0, 0, 0, 0, 0, 1, 5, 0, 0, 0, 0, 0 }, 1, 1)
    s[16] = s[16] - 7 -- give player 1 seven captured stones, player 2 keeps the rest
    s[15] = 7
    local p2 = s[16]
    local info = {}
    C.play(s, mv(6, -1), info) -- 6 -> 5; 4 empty, 3 empty: stop
    assert(s[21] == 2 and info.refill == 5 and info.borrowed == 0)
    for p = 8, 12 do assert(s[p] == 1) end
    assert(s[16] == p2 - 5)
    checkTotals(s, "refill")

    -- Player 2 has 2 captured stones: borrows 3 from player 1.
    s = setup({ 0, 0, 0, 0, 0, 1, 5, 0, 0, 0, 0, 0 }, 1, 1)
    s[15], s[16] = s[16] - 2, 2
    local p1 = s[15]
    C.play(s, mv(6, -1), info)
    assert(info.refill == 5 and info.borrowed == 3 and s[20] == 3 and s[15] == p1 - 3 and s[16] == 0)
    checkTotals(s, "borrow")
    -- The debt is settled in the score.
    assert(C.score(s, 2) == 0 + 10 * s[18] - 3 and C.score(s, 1) == s[15] + 10 * s[17] + 3)

    -- Short again while in debt: no second loan, the game ends.
    s = setup({ 0, 0, 0, 0, 0, 1, 5, 0, 0, 0, 0, 0 }, 1, 1)
    s[15], s[16], s[20] = s[16] - 2, 2, 3
    C.play(s, mv(6, -1), info)
    assert(s[22] == 1 and info.refill == 0 and s[20] == 3, "second loan ends the game")
    assert(C.score(s, 2) == 2 - 3 + 10 * s[18])
    checkTotals(s, "second loan")

    -- Nothing to put back on either side: the game ends.
    s = setup({ 0, 0, 0, 0, 0, 1, 44, 0, 0, 0, 0, 0 }, 1, 1)
    s[16] = 0
    s[7] = 49
    C.play(s, mv(6, -1), info)
    assert(s[22] == 1 and s[15] == 1, "no stones to put back ends the game")
    checkTotals(s, "dry")
end

-- End of game: both mandarin squares empty; rows go to their owners.
do
    -- Left mandarin square already empty (mandarin captured by player 2).
    -- Player 1: 4 -> 5; 6 empty; 7 (mandarin, 5 stones) captured; 8 has stones: stop.
    local s = setup({ 0, 0, 3, 1, 0, 0, 5, 2, 0, 4, 0, 0 }, 0, 1)
    C.play(s, mv(4, 1), {})
    assert(s[22] == 1, "game over")
    -- Player 1: 5 (mandarin square) + 10 + row 3 + 1 (square 5).
    assert(C.score(s, 1) == 19, "p1 " .. C.score(s, 1))
    assert(C.score(s, 1) + C.score(s, 2) == 70)
    assert(C.winner(s) == 2)
    local list = {}
    assert(C.legal(s, list) == 0)
    checkTotals(s, "end")
end

-- A turn stops after MAX_LAPS pick-ups (a guard against endless sowing).
do
    local keep = C.MAX_LAPS
    C.MAX_LAPS = 1
    -- 5 -> 6, 7; pick up 8 -> 9..12, 1; next 2 has a stone: a second pick-up, refused.
    local s = setup({ 0, 1, 0, 0, 2, 0, 0, 5, 5, 5, 5, 5 }, 1, 1)
    local info = {}
    C.play(s, mv(5, 1), info)
    assert(info.stopped and info.laps == 1 and s[2] == 1 and s[21] == 2, "lap guard")
    checkTotals(s, "lap guard")
    C.MAX_LAPS = keep
end

-- Save and restore.
do
    local s = C.new(2)
    C.play(s, mv(9, 1), {})
    local r = C.deserialize(C.serialize(s))
    for i = 1, C.SIZE do assert(r[i] == s[i]) end
    assert(C.deserialize("1,2,3") == nil)
end

-- Random games: stones and mandarins conserved, games end, scores add to 70
-- when both mandarin squares are emptied.
local function playGame(chooser1, chooser2, starter)
    local s = C.new(starter)
    local list, info = {}, {}
    local plies = 0
    while s[22] == 0 do
        local n = C.legal(s, list)
        assert(n > 0, "no legal move in a running game")
        local m = (s[21] == 1 and chooser1 or chooser2)(s, list, n)
        assert(C.isLegal(s, m), "illegal move")
        C.play(s, m, info)
        checkTotals(s, "game")
        plies = plies + 1
        assert(plies <= C.MAX_PLY, "game did not end")
    end
    return s
end

local function randomPlayer(_, list, n) return list[rand(n)] end

do
    local natural = 0
    for g = 1, 300 do
        local s = playGame(randomPlayer, randomPlayer, g % 2 + 1)
        if C.empty(s, 1) and C.empty(s, 7) then
            natural = natural + 1
            assert(C.score(s, 1) + C.score(s, 2) == 70)
        end
    end
    assert(natural >= 250, "most random games end with both mandarins taken: " .. natural)
end

-- AI: legal moves at every level, finished games, and levels in order.
local function aiPlayer(level, limit)
    return function(s) return AI.choose(s, level, rand, limit) end
end

local function match(levelA, levelB, games)
    local winsA = 0
    for g = 1, games do
        local s = playGame(aiPlayer(levelA), aiPlayer(levelB), g % 2 + 1)
        local w = C.winner(s)
        if w == 1 then winsA = winsA + 1 elseif w == 0 then winsA = winsA + 0.5 end
    end
    return winsA / games
end

do
    local r = match(2, 1, 60)
    print(string.format("Normal vs Easy: %.2f", r))
    assert(r >= 0.6, "Normal beats Easy")
    r = match(3, 2, 30)
    print(string.format("Hard vs Normal: %.2f", r))
    assert(r >= 0.6, "Hard beats Normal")
    local s = C.new(1)
    for _ = 1, 20 do
        local m = AI.choose(s, 1, rand)
        assert(C.isLegal(s, m))
    end
end

-- Hard takes an obvious capture of a mandarin.
do
    local s = setup({ 0, 0, 0, 1, 0, 0, 9, 1, 0, 0, 2, 0 }, 1, 1)
    local m = AI.choose(s, 3, rand)
    assert(m == mv(4, 1), "Hard takes the mandarin")
    assert(AI.choose(s, 2, rand) == mv(4, 1), "Normal takes the mandarin")
end

-- Hard's search stays within its node budget and time.
do
    local t = os.clock()
    local s = C.new(1)
    for _ = 1, 10 do AI.choose(s, 3, rand) end
    print(string.format("Hard: %.1f ms a move from the start (host)", (os.clock() - t) * 100))
end

print("oanquan: all tests passed")
