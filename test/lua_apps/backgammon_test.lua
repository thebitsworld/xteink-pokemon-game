-- Logic tests for apps/backgammon/logic.lua, ai.lua and game.lua (run by ctest: LuaAppLogic_backgammon).
local saved = {}
smudge = { save = function(k, v) saved[k] = v end }
local C = dofile(APPS .. "/backgammon/logic.lua")
local AI = dofile(APPS .. "/backgammon/ai.lua")(C)
-- game.lua with a minimal app state (main.lua's S).
local S = { LEVELS = { "Easy", "Normal", "Hard" }, mode = 4, R = {}, nR = 0, left = 0, sources = {}, cur = 1,
            dt = {}, ds = {}, dcur = 1, records = { { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 } } }
function S.vsDevice() return S.mode <= 3 end
local G = dofile(APPS .. "/backgammon/game.lua")(C, S)

math.randomseed(31)
local function rand(n) return math.random(1, n) end
local function now() return os.clock() * 1000 end

-- A side from { [point] = count, bar = n, off = n } (own numbering); the
-- rest are borne off unless `off` is given.
local function side(t)
    local s = {}
    for i = 1, 26 do s[i] = 0 end
    local n = 0
    for k, v in pairs(t) do
        if k == "bar" then s[25] = v elseif k ~= "off" then s[k] = v end
        n = n + v
    end
    s[26] = t.off or (15 - n)
    return s
end

local function key(me, opp) return table.concat(me, ",") .. "|" .. table.concat(opp, ",") end

-- Every position a roll can lead to, the slow way: every order of the
-- dice, every checker, no shortcuts.
local function brute(me, opp, d1, d2)
    local R = {}
    local n = C.dice(d1, d2, R)
    local need, only = C.turnInfo(me, opp, d1, d2)
    local out, count = {}, 0
    local function rec(R, n, depth)
        if depth > need then
            local k = key(me, opp)
            if not out[k] then count = count + 1 end
            out[k] = true
            return
        end
        for k = 1, n do
            local d = R[k]
            if depth > 1 or not only or d == only then
                for f = 25, 1, -1 do
                    if C.target(me, opp, f, d) then
                        local hit = C.apply(me, opp, f, d)
                        local R2 = {}
                        for j = 1, n do
                            if j ~= k then R2[#R2 + 1] = R[j] end
                        end
                        rec(R2, n - 1, depth + 1)
                        C.undo(me, opp, f, d, hit)
                    end
                end
            end
        end
    end
    rec(R, n, 1)
    return out, count
end

-- The generator's positions (asserting none twice) and the moves.
local function generated(me, opp, d1, d2)
    local set, moves, count = {}, {}, 0
    local before = key(me, opp)
    AI.gen(me, opp, d1, d2, function(m, o, b, n)
        local k = key(m, o)
        assert(not set[k], "position generated twice")
        set[k] = true
        count = count + 1
        moves[#moves + 1] = AI.moveOf(b, n)
    end)
    assert(key(me, opp) == before, "board not restored")
    return set, count, moves
end

local function sameAsBrute(me, opp, d1, d2)
    local ref, n = brute(me, opp, d1, d2)
    local set, m = generated(me, opp, d1, d2)
    for k in pairs(ref) do assert(set[k], "missing a position for " .. d1 .. "-" .. d2) end
    assert(n == m, "count " .. m .. " vs " .. n .. " for " .. d1 .. "-" .. d2)
    return m
end

-- The position after a move string, on copies.
local function after(me, opp, steps)
    local m, o = C.copy(me), C.copy(opp)
    C.play(m, o, steps)
    return m, o
end

-- The start ------------------------------------------------------------------
do
    local w, b = C.newSide(), C.newSide()
    assert(C.pips(w) == 167 and C.pips(b) == 167)
    local n = 0
    for i = 1, 26 do n = n + w[i] end
    assert(n == 15)
    -- Every roll from the start: the generator matches brute force, and
    -- every move uses all the dice.
    for a = 1, 6 do
        for c = a, 6 do
            sameAsBrute(w, b, a, c)
            local _, _, moves = generated(w, b, a, c)
            for _, mv in ipairs(moves) do assert(#mv == (a == c and 8 or 4)) end
        end
    end
    -- 6-6 from the start: 24/18(2) 13/7(2) is one of them.
    local set = generated(w, b, 6, 6)
    local m, o = after(w, b, C.step(24, 6) .. C.step(24, 6) .. C.step(13, 6) .. C.step(13, 6))
    assert(set[key(m, o)])
    assert(C.target(w, b, 24, 5) == nil, "24/19 lands on Black's 6 point (5 checkers)")
    assert(C.target(w, b, 24, 6) == 18)
end

-- Hits and the bar -----------------------------------------------------------
do
    local me = side({ [13] = 2, [8] = 13 })
    local opp = side({ [16] = 1, [24] = 14 }) -- a blot on my 9 point
    assert(C.target(me, opp, 13, 4) == 9)
    local hit = C.apply(me, opp, 13, 4)
    assert(hit and opp[25] == 1 and opp[16] == 0 and me[9] == 1)
    C.undo(me, opp, 13, 4, hit)
    assert(opp[25] == 0 and opp[16] == 1 and me[9] == 0 and me[13] == 2)

    -- With a checker on the bar, it must enter first.
    me = side({ bar = 1, [13] = 5, [6] = 9 })
    opp = side({ [6] = 2, [5] = 2, [12] = 11 }) -- holds my 19 and 20 points
    assert(C.target(me, opp, 13, 3) == nil, "bar first")
    assert(C.target(me, opp, 25, 6) == nil and C.target(me, opp, 25, 5) == nil, "19 and 20 blocked")
    assert(C.target(me, opp, 25, 4) == 21)
    local _, _, moves = generated(me, opp, 6, 4)
    for _, mv in ipairs(moves) do assert(mv:byte(1) - 64 == 25, "enters first") end
    sameAsBrute(me, opp, 6, 4)
    sameAsBrute(me, opp, 4, 4)

    -- A closed board: no move at all.
    opp = side({ [1] = 2, [2] = 2, [3] = 2, [4] = 2, [5] = 2, [6] = 2, [12] = 3 })
    local need = C.turnInfo(me, opp, 6, 3)
    assert(need == 0)
    _, _, moves = generated(me, opp, 6, 3)
    assert(#moves == 1 and moves[1] == "")

    -- Two on the bar and doubles: both enter, then two more steps.
    me = side({ bar = 2, [6] = 13 })
    opp = side({ [12] = 15 })
    assert(C.turnInfo(me, opp, 3, 3) == 4)
    _, _, moves = generated(me, opp, 3, 3)
    for _, mv in ipairs(moves) do assert(mv:sub(1, 4) == C.step(25, 3) .. C.step(25, 3)) end
    -- Two on the bar, one die blocked: only one checker enters.
    opp = side({ [6] = 2, [12] = 13 }) -- holds my 19 point
    assert(C.turnInfo(me, opp, 6, 2) == 1)
end

-- Using the dice ---------------------------------------------------------------
do
    -- Only one die can be played and the larger one cannot: the smaller.
    -- A single checker on my 10 point, the opponent holding my 3 and 4:
    -- 10/4 is blocked, and 10/9 then 9/3 too.
    local me = side({ [10] = 1, off = 14 })
    local opp = side({ [22] = 2, [21] = 2, [12] = 11 })
    local need, only = C.turnInfo(me, opp, 6, 1)
    assert(need == 1 and only == 1)
    -- Either die can be played but not both: the larger. A checker on 12,
    -- the opponent holding my 4: 12/6 then 6/4 blocked, 12/10 then 10/4
    -- blocked.
    me = side({ [12] = 1, off = 14 })
    opp = side({ [21] = 2, [12] = 13 }) -- holds my 4 (and my 13)
    need, only = C.turnInfo(me, opp, 6, 2)
    assert(need == 1 and only == 6, "must play the 6")
    local _, _, moves = generated(me, opp, 6, 2)
    assert(#moves == 1 and moves[1] == C.step(12, 6))
    sameAsBrute(me, opp, 6, 2)
    -- The human rule check agrees: the 2 is refused, the 6 allowed.
    local R = {}
    local n = C.dice(6, 2, R)
    assert(not G.legal(me, opp, 12, 2, R, n, need, only))
    assert(G.legal(me, opp, 12, 6, R, n, need, only))

    -- Must use both: two checkers; one order would waste a die.
    me = side({ [8] = 1, [3] = 1, off = 13 })
    opp = side({ [20] = 2, [12] = 13 }) -- holds my 5
    -- 8 -3-> 5 blocked; 3 -3-> off not allowed (8 is outside home).
    -- 8 -2-> 6 then 6 -3-> 3 (or 3 -3-> off once 8 is home): both dice.
    need = C.turnInfo(me, opp, 3, 2)
    assert(need == 2)
    R = {}
    n = C.dice(3, 2, R)
    assert(G.legal(me, opp, 8, 2, R, n, 2, nil))
    assert(not G.legal(me, opp, 3, 2, R, n, 2, nil), "3/1 leaves the 3 unplayable")
    sameAsBrute(me, opp, 3, 2)

    -- Doubles played as far as they go: 5-5 with the opponent on my 3, so
    -- only 13/8(2).
    me = side({ [13] = 2, [8] = 1, off = 12 })
    opp = side({ [22] = 2, [12] = 13 })
    need = C.turnInfo(me, opp, 5, 5)
    assert(need == 2, "13/8(2) only: " .. need)
    sameAsBrute(me, opp, 5, 5)
end

-- Bearing off ----------------------------------------------------------------
do
    local opp = side({ [12] = 15 })
    local me = side({ [6] = 2, [4] = 3, [1] = 1 })
    assert(C.target(me, opp, 6, 6) == 0, "exact")
    assert(C.target(me, opp, 4, 6) == nil, "6 point still occupied")
    assert(C.target(me, opp, 4, 4) == 0)
    assert(C.target(me, opp, 1, 3) == nil, "a higher die only from the highest point")
    me = side({ [4] = 2, [2] = 1 })
    assert(C.target(me, opp, 4, 6) == 0, "higher die from the highest point")
    assert(C.target(me, opp, 2, 6) == nil)
    assert(C.target(me, opp, 4, 1) == 3, "may still move inside")
    -- Not all home: no bearing off.
    me = side({ [7] = 1, [3] = 2 })
    assert(C.target(me, opp, 3, 3) == nil)
    -- A checker on the bar: no bearing off either.
    me = side({ bar = 1, [3] = 2 })
    assert(C.target(me, opp, 3, 3) == nil)
    -- Moving in then bearing off in the same turn.
    me = side({ [8] = 1, [2] = 1 })
    local need = C.turnInfo(me, opp, 2, 2)
    assert(need == 4)
    sameAsBrute(me, opp, 2, 2)
    sameAsBrute(me, opp, 6, 5)
    me = side({ [2] = 1, [1] = 1 })
    assert(not C.apply(me, opp, 2, 2) and me[26] == 14 and me[2] == 0)
end

-- Scoring ----------------------------------------------------------------------
do
    local w = side({})
    assert(w[26] == 15)
    assert(C.score(w, side({ [6] = 14, off = 1 })) == 1, "single game")
    assert(C.score(w, side({ [6] = 15, off = 0 })) == 2, "gammon")
    assert(C.score(w, side({ [6] = 14, [19] = 1, off = 0 })) == 3, "backgammon: in the winner's home")
    assert(C.score(w, side({ [6] = 14, bar = 1, off = 0 })) == 3, "backgammon: on the bar")
    assert(C.score(w, side({ [6] = 13, [20] = 1, off = 1 })) == 1, "one off: single, wherever the rest is")
end

-- The generator against brute force in positions from real games ------------
do
    local total = 0
    for game = 1, 3 do
        local s = { C.newSide(), C.newSide() }
        local turn, plies = 1, 0
        while plies < 400 do
            if plies % 4 == 0 then
                for a = 1, 6 do
                    for c = a + 1, 6 do total = total + sameAsBrute(s[turn], s[3 - turn], a, c) end
                end
            end
            if plies % 16 == 0 then
                local d = rand(6)
                total = total + sameAsBrute(s[turn], s[3 - turn], d, d)
            end
            local mv = AI.choose(s[turn], s[3 - turn], rand(6), rand(6), 1, rand, now, 1e9)
            C.play(s[turn], s[3 - turn], mv)
            plies = plies + 1
            if s[turn][26] == 15 then break end
            turn = 3 - turn
        end
    end
    assert(total > 500, total)
end

-- Human input: the destinations of a picked checker ---------------------------
do
    local w, b = C.newSide(), C.newSide()
    local R = {}
    local n = C.dice(5, 3, R)
    local need, only = C.turnInfo(w, b, 5, 3)
    local T, P = {}, {}
    G.dests(w, b, 13, R, n, need, only, T, P)
    -- 13/10, 13/8 and 13/5 (both dice), nearest first.
    assert(table.concat(T, ",") == "10,8,5", table.concat(T, ","))
    assert(#P[3] == 4)
    G.dests(w, b, 24, R, n, need, only, T, P)
    -- 24/21, 24/16 (24/19 is blocked; 21 then 16 with both).
    assert(table.concat(T, ",") == "21,16", table.concat(T, ","))
    -- Bearing off shows as point 0.
    local me, opp = side({ [3] = 1, [1] = 1 }), side({ [12] = 15 })
    n = C.dice(6, 1, R)
    need, only = C.turnInfo(me, opp, 6, 1)
    G.dests(me, opp, 3, R, n, need, only, T, P)
    assert(T[#T] == 0)
end

-- AI: legal moves only, games finish -----------------------------------------
do
    local function legal(me, opp, d1, d2, mv)
        local ref = brute(me, opp, d1, d2)
        local m, o = after(me, opp, mv)
        return ref[key(m, o)] == true
    end
    for level = 1, 3 do
        local s = { C.newSide(), C.newSide() }
        local turn, plies = 1, 0
        while true do
            local d1, d2 = rand(6), rand(6)
            local mv = AI.choose(s[turn], s[3 - turn], d1, d2, level, rand, now, 1e9)
            if d1 ~= d2 or plies % 4 == 0 then
                assert(legal(s[turn], s[3 - turn], d1, d2, mv), "illegal move at level " .. level)
            end
            C.play(s[turn], s[3 - turn], mv)
            plies = plies + 1
            assert(plies < 2000, "game does not end")
            if s[turn][26] == 15 then break end
            turn = 3 - turn
        end
        assert(s[turn][26] == 15)
    end
end

-- Levels: Normal and Hard beat Easy -------------------------------------------
local function match(l1, l2, games)
    local won = 0
    for g = 1, games do
        local lv = g % 2 == 1 and { l1, l2 } or { l2, l1 }
        local s = { C.newSide(), C.newSide() }
        local turn = g % 2 == 1 and 1 or 2
        while true do
            local mv = AI.choose(s[turn], s[3 - turn], rand(6), rand(6), lv[turn], rand, now, 1e9)
            C.play(s[turn], s[3 - turn], mv)
            if s[turn][26] == 15 then break end
            turn = 3 - turn
        end
        if lv[turn] == l1 then won = won + 1 end
    end
    return won
end
do
    local w = match(3, 1, 10)
    assert(w >= 7, "Hard won only " .. w .. " of 10 against Easy")
    w = match(2, 1, 20)
    assert(w >= 14, "Normal won only " .. w .. " of 20 against Easy")
end

-- A capped Hard search still returns a legal move -----------------------------
do
    local w, b = C.newSide(), C.newSide()
    local cap = AI.NODE_CAP
    AI.NODE_CAP = 10
    local mv = AI.choose(w, b, 3, 1, 3, rand, now, 1e9)
    AI.NODE_CAP = cap
    local set = generated(w, b, 3, 1)
    local m, o = after(w, b, mv)
    assert(set[key(m, o)])
end

-- game.lua: turns, undo, the result and saving --------------------------------
do
    local function check(g)
        for k = 1, 2 do
            local t = 0
            for i = 1, 26 do t = t + g.b[k][i] end
            assert(t == 15)
        end
        for p = 1, 24 do assert(not (g.b[1][p] > 0 and g.b[2][25 - p] > 0), "point " .. p .. " shared") end
    end
    for game = 1, 20 do
        G.new()
        local g = S.game
        assert(g.d1 ~= g.d2 and g.opening)
        assert(g.turn == (g.d1 > g.d2 and 1 or 2))
        while not g.result do
            -- Play the turn with picks and moves as the screen does, with
            -- some take-backs.
            while S.left > 0 do
                assert(#S.sources > 0)
                G.pick(S.sources[rand(#S.sources)])
                assert(#S.dt > 0)
                G.move(S.ds[rand(#S.dt)])
                check(g)
                if rand(8) == 1 then
                    local before = g.steps
                    G.undo()
                    assert(g.steps == before:sub(1, -3))
                    check(g)
                end
            end
            -- What was played is a legal full move.
            local snapshotSteps = g.steps
            if not g.result then
                local text = G.serialize(S.mode, g)
                local mode, g2 = G.parse(text)
                assert(mode == S.mode and G.serialize(mode, g2) == text, "save and restore")
            end
            assert(#snapshotSteps // 2 == g.need)
            G.done()
            check(g)
        end
        local w = g.b[g.result]
        assert(w[26] == 15 and g.pts == C.score(w, g.b[3 - g.result]))
    end
    local r = S.records[4]
    assert(r[1] + r[2] == 20 and r[3] + r[4] >= 20)
    assert(saved.game == "", "a finished game is not kept")
    assert(saved.rec4 == table.concat(r, ","))
end

print("backgammon: all tests passed")
