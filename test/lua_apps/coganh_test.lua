-- Logic tests for apps/coganh/logic.lua, game.lua, ai.lua and the board's
-- notation and cursor (run by ctest: LuaAppLogic_coganh).
local C = dofile(APPS .. "/coganh/logic.lua")
local G = dofile(APPS .. "/coganh/game.lua")(C)
local AI = dofile(APPS .. "/coganh/ai.lua")(C)
local V = dofile(APPS .. "/coganh/draw.lua")(C, {})
local I = dofile(APPS .. "/coganh/input.lua")(C, {})

math.randomseed(77)
local function rand(n) return math.random(1, n) end

local W, Bk = C.WHITE, C.BLACK
local function mv(from, to) return from + to * 32 end
-- Point at row r (0 at the top), column c.
local function P(r, c) return r * 5 + c + 1 end

-- A position: lists of White's and Black's points, `side` to move.
local function setup(white, black, side, forced)
    local g = G.new(forced)
    for p = 1, 25 do g.b[p] = 0 end
    for _, p in ipairs(white) do g.b[p] = W end
    for _, p in ipairs(black) do g.b[p] = Bk end
    g.on[W], g.on[Bk] = #white, #black
    g.side = side or W
    g.reps = ""
    return g
end

local function has(list, m)
    for _, x in ipairs(list) do if x == m then return true end end
    return false
end

local function neighbours(p)
    local t = {}
    for d = 1, 8 do
        local q = C.NB[(p - 1) * 8 + d]
        if q > 0 then t[#t + 1] = q end
    end
    table.sort(t)
    return t
end

local function snapshot(g)
    return table.concat(g.b, ",") .. "|" .. g.side .. "|" .. g.on[1] .. "," .. g.on[2] .. "|" .. g.ct .. "|" .. g.ply
end

-- The board: 25 points, 40 orthogonal joins and 16 diagonal ones (one per
-- square, between its two strong corners); strong points have diagonals.
do
    local edges = 0
    for p = 1, 25 do
        for _, q in ipairs(neighbours(p)) do
            edges = edges + 1
            assert(has(neighbours(q), p), "join " .. p .. "-" .. q .. " both ways")
            local r1, c1 = C.rc(p)
            local r2, c2 = C.rc(q)
            assert(math.abs(r1 - r2) <= 1 and math.abs(c1 - c2) <= 1)
            if r1 ~= r2 and c1 ~= c2 then assert(C.strong(p) and C.strong(q), "diagonals join strong points") end
        end
        -- Opposite directions are opposite neighbours.
        for d = 1, 4 do
            local a, b = C.NB[(p - 1) * 8 + d], C.NB[(p - 1) * 8 + d + 4]
            if a > 0 and b > 0 then
                local ra, ca = C.rc(a)
                local rb, cb = C.rc(b)
                local r, c = C.rc(p)
                assert(ra + rb == 2 * r and ca + cb == 2 * c, "a line through " .. p)
            end
        end
    end
    assert(edges == 2 * (40 + 16), "56 joins, got " .. edges / 2)
    assert(table.concat(neighbours(P(0, 0)), ",") == "2,6,7", "corner: strong")
    assert(table.concat(neighbours(P(0, 1)), ",") == "1,3,7", "edge, weak")
    assert(table.concat(neighbours(P(0, 2)), ",") == "2,4,7,8,9", "edge, strong")
    assert(#neighbours(P(2, 2)) == 8, "centre: eight")
    assert(#neighbours(P(1, 1)) == 8, "b4: strong, eight")
    assert(table.concat(neighbours(P(1, 2)), ",") == "3,7,9,13", "c4: weak, four")
    assert(C.strong(13) and not C.strong(8) and C.strong(1))
end

-- The start: 8 each on the outer ring, White first, both sides mirror images.
do
    local g = G.new(false)
    assert(g.on[W] == 8 and g.on[Bk] == 8 and g.side == W)
    for p = 1, 25 do
        local r, c = C.rc(p)
        local ring = r == 0 or r == 4 or c == 0 or c == 4
        assert((g.b[p] ~= 0) == ring, "point " .. p)
        if g.b[p] ~= 0 then assert(g.b[26 - p] == 3 - g.b[p], "mirror " .. p) end
    end
    assert(g.b[P(2, 4)] == Bk and g.b[P(2, 0)] == W, "middle row: Black right, White left")
    local list = C.legalMoves(g)
    for _, m in ipairs(list) do
        assert(g.b[m % 32] == W and g.b[m // 32] == 0 and has(neighbours(m % 32), m // 32))
    end
end

-- Ganh along each kind of line.
do
    local cases = {
        { { 12, 14 }, 18, "row" },
        { { 8, 18 }, 12, "column" },
        { { 7, 19 }, 14, "diagonal" },
        { { 9, 17 }, 12, "other diagonal" },
        { { 1, 3 }, 7, "edge row" }, -- into 2 from 7
    }
    for i, k in ipairs(cases) do
        local a, b = k[1][1], k[1][2]
        local to = (i == 5) and 2 or 13
        local g = setup({ k[2] }, { a, b, 25 }, W)
        if i == 5 then g.b[k[2]] = W end
        local before = snapshot(g)
        assert(C.ganhCount(g.b, to, W) == 2, k[3])
        local n = C.make(g, mv(k[2], to))
        assert(n == 2 and g.b[a] == W and g.b[b] == W and g.on[W] == 3 and g.on[Bk] == 1, k[3])
        C.unmake(g, mv(k[2], to))
        assert(snapshot(g) == before, "unmake " .. k[3])
    end
    -- Three lines at once ("chau 6"): row, column, diagonal; 9 steps in.
    local g = setup({ 9 }, { 12, 14, 8, 18, 7, 19, 1 }, W)
    local n = C.make(g, mv(9, 13))
    assert(n == 6 and g.on[W] == 7 and g.on[Bk] == 1, "six at once, got " .. n)
    -- A weak point has no diagonals: 2 and 14 do not flank 8.
    g = setup({ 3 }, { 2, 14, 12 }, W)
    assert(C.ganhCount(g.b, 8, W) == 0)
    assert(C.make(g, mv(3, 8)) == 0 and g.b[2] == Bk and g.b[14] == Bk)
    -- Not opposite: 12 and 7 around 13.
    g = setup({ 18 }, { 12, 7 }, W)
    assert(C.make(g, mv(18, 13)) == 0)
    -- A ganh needs both sides: one enemy and one empty point.
    g = setup({ 18 }, { 12 }, W)
    assert(C.make(g, mv(18, 13)) == 0)
    -- One enemy and one friend.
    g = setup({ 18, 14 }, { 12 }, W)
    assert(C.make(g, mv(18, 13)) == 0)
end

-- No self-capture: stepping between two enemies is safe; only the mover's
-- enemies are converted.
do
    local g = setup({ 8, 20 }, { 12, 14, 24 }, W)
    -- White 8 to 13 sits between Black 12 and 14: it converts them, not the other way.
    assert(C.make(g, mv(8, 13)) == 2 and g.b[13] == W)
    -- Black stepping between two White pieces is safe for Black.
    g = setup({ 12, 14, 1 }, { 18, 25 }, Bk)
    assert(C.make(g, mv(18, 13)) == 2 and g.b[13] == Bk and g.b[12] == Bk and g.b[14] == Bk)
end

-- Vay: a piece, or a group, with no way out is converted.
do
    -- Black 1 in the corner: White on 2 and 7; White steps 11-6.
    local g = setup({ 2, 7, 11 }, { 1, 25 }, W)
    local before = snapshot(g)
    local n = C.make(g, mv(11, 6))
    assert(n == 1 and g.b[1] == W and g.on[Bk] == 1, "corner piece surrounded")
    assert(table.concat(C.lastConverted(g), ",") == "1")
    C.unmake(g, mv(11, 6))
    assert(snapshot(g) == before)
    -- A group of three: Black 1, 2, 6 next to White 3 and 7, its only free
    -- point 11; White 16 steps to 11.
    g = setup({ 3, 7, 12, 16 }, { 1, 2, 6, 25 }, W)
    n = C.make(g, mv(16, 11))
    assert(n == 3 and g.b[1] == W and g.b[2] == W and g.b[6] == W, "group of three, got " .. n)
    -- The same group with a way out stays.
    g = setup({ 3, 7, 16 }, { 1, 2, 6, 25 }, W)
    assert(C.make(g, mv(16, 17)) == 0 and g.b[1] == Bk)
    -- A piece with no way out of its own whose group has one stays: Black 1
    -- (next to 2, 6, 7) is joined to 6, which still has 11.
    g = setup({ 2, 7, 16 }, { 1, 6, 25 }, W)
    assert(C.make(g, mv(16, 17)) == 0, "6 still has 11")
    g = setup({ 2, 7, 16 }, { 1, 6, 25 }, W)
    assert(C.make(g, mv(16, 11)) == 2, "1 and 6 surrounded")
    -- Ganh first, then vay on what is left: White 18 to 13 flanks 12 and 14
    -- and shuts in Black 8 (3, 7, 9 and now 13 are White).
    g = setup({ 18, 3, 7, 9 }, { 12, 14, 8, 25 }, W)
    n = C.make(g, mv(18, 13))
    -- 8: neighbours 3, 7, 9, 13 all White now -> surrounded too.
    assert(n == 3 and g.b[8] == W, "ganh 2 then surrounded 1, got " .. n)
    -- Black surrounds the same way.
    g = setup({ 1, 25 }, { 2, 7, 11 }, Bk)
    n = C.make(g, mv(11, 6))
    assert(n == 1 and g.b[1] == Bk)
    -- The mover's own stuck pieces are never converted by its move.
    g = setup({ 2, 7, 6 }, { 1, 25 }, Bk)
    -- Black 1 is already stuck, Black moves 25 elsewhere: no conversion of its own.
    assert(C.make(g, mv(25, 24)) == 0 and g.b[1] == Bk)
end

-- The real game: conversions recorded, the end, replay and take back.
do
    local g = setup({ 2, 7, 11 }, { 1 }, W)
    assert(G.play(g, mv(11, 6)))
    assert(g.result == "white" and g.reason == "pieces" and g.on[W] == 4 and g.on[Bk] == 0)
    assert(g.vay == 1 and g.ganh == 0 and g.conv[1] == 1)
    assert(V.gains(g) == ": surrounded 1")
    g = setup({ 18 }, { 12, 14, 1 }, W)
    assert(G.play(g, mv(18, 13)) and g.ganh == 2 and g.vay == 0 and not g.result)
    assert(V.gains(g) == ": ganh 2" and V.describe(g.last) == "c2-c3")
    -- Illegal moves are refused.
    g = G.new(false)
    assert(not G.play(g, mv(21, 13)), "not joined")
    assert(not G.play(g, mv(1, 7)), "not your piece")
    assert(not G.play(g, mv(21, 22)), "taken")
    -- A real game, replayed and taken back.
    local AIL = { depth = 2, nodes = 2000 }
    for _ = 1, 12 do
        if g.result then break end
        assert(G.play(g, (AI.choose(g, AIL, rand))))
    end
    local r = G.replay(g.history, false)
    assert(r and table.concat(r.b, ",") == table.concat(g.b, ",") and r.side == g.side)
    local t = G.takeBack(g, 1)
    assert(G.moveCount(t) == G.moveCount(g) - 1 and t.side ~= g.side)
    assert(G.play(t, g.last) and table.concat(t.b, ",") == table.concat(g.b, ","))
end

-- Draws: threefold repetition and 50 quiet moves each.
do
    local g = setup({ 21 }, { 5 }, W)
    local seq = { mv(21, 22), mv(5, 4), mv(22, 21), mv(4, 5) }
    g.reps = ""
    local i = 0
    while not g.result do
        i = i + 1
        assert(G.play(g, seq[(i - 1) % 4 + 1]))
        assert(i < 20)
    end
    assert(g.result == "draw" and g.reason == "threefold repetition", g.reason)
    g = setup({ 21 }, { 5 }, W)
    g.half = G.QUIET_LIMIT - 1
    assert(G.play(g, mv(21, 22)) and g.result == "draw" and g.reason:find("50 moves"))
    g = setup({ 18 }, { 12, 14, 1 }, W)
    g.half = 90
    assert(G.play(g, mv(18, 13)) and g.half == 0)
end

-- The "mo" rule: after Black opens 13 between 12 and 14, White must ganh there.
do
    for _, forced in ipairs({ true, false }) do
        local g = setup({ 18, 22 }, { 12, 13, 14, 1 }, Bk, forced)
        assert(G.play(g, mv(13, 8)))
        local list = C.legalMoves(g)
        if forced then
            assert(#list == 1 and list[1] == mv(18, 13), "forced: only 18-13")
            assert(C.forcedPoint(g) == 13)
            assert(not G.play(g, mv(22, 23)))
        else
            assert(#list > 1 and C.forcedPoint(g) == 0)
        end
    end
    -- Nothing forced when no White piece can reach the opened point.
    local g = setup({ 22 }, { 12, 13, 14, 1 }, Bk, true)
    assert(G.play(g, mv(13, 8)) and C.forcedPoint(g) == 0)
end

-- Notation and the cursor.
do
    assert(V.pointName(21) == "a1" and V.pointName(1) == "a5" and V.pointName(13) == "c3" and V.pointName(25) == "e1")
    assert(I.step(1, 1, 0) == 2 and I.step(5, 1, 0) == 6, "right runs on to the next row")
    assert(I.step(25, 1, 0) == 1 and I.step(1, -1, 0) == 25)
    assert(I.step(3, 0, 1) == 8 and I.step(23, 0, 1) == 3, "down wraps the column")
    assert(I.step(13, 1, 0, true) == 12 and I.step(13, 0, -1, true) == 18, "turned board")
end

-- The AI: legal moves, takes a ganh, leaves the position as it was.
local EASY = { depth = 1, random = 30, nodes = 2000 }
local NORMAL = { depth = 3, nodes = 5000 }
local HARD = { depth = 12, nodes = 12000 }
do
    local g = setup({ 18, 21 }, { 12, 14, 5, 4 }, W)
    for _, L in ipairs({ NORMAL, HARD }) do
        local m = AI.choose(g, L, rand)
        assert(m == mv(18, 13), "takes the ganh, got " .. tostring(m))
    end
    -- It sees a surround.
    g = setup({ 2, 7, 11 }, { 1, 15 }, W)
    assert(AI.choose(g, NORMAL, rand) == mv(11, 6), "surrounds the corner")
    g = G.new(false)
    local before = snapshot(g)
    local m = AI.choose(g, HARD, rand)
    assert(has(C.legalMoves(g), m) and snapshot(g) == before)
end

local function game(white, black, forced)
    local g = G.new(forced)
    local plies = 0
    while not g.result do
        local L = g.side == W and white or black
        local m = AI.choose(g, L, rand)
        assert(m and has(C.legalMoves(g), m), "AI move is legal")
        assert(G.play(g, m))
        assert(g.on[1] + g.on[2] == 16, "pieces are converted, never removed")
        plies = plies + 1
        assert(plies < 2000)
    end
    return g.result, plies
end

do
    local RANDOM = { depth = 1, random = 100 }
    for _ = 1, 20 do game(RANDOM, RANDOM, false) end
    for _ = 1, 5 do game(RANDOM, RANDOM, true) end
    game(EASY, NORMAL, true)
end

-- Hard beats Easy most of the time (both colours); Normal beats Easy.
do
    local wins, losses = 0, 0
    for i = 1, 6 do
        local hardWhite = i % 2 == 1
        local r = game(hardWhite and HARD or EASY, hardWhite and EASY or HARD, false)
        if r ~= "draw" then
            if (r == "white") == hardWhite then wins = wins + 1 else losses = losses + 1 end
        end
    end
    assert(wins >= 4 and losses <= 1, "Hard vs Easy: " .. wins .. " won, " .. losses .. " lost of 6")
    wins = 0
    for i = 1, 4 do
        local nw = i % 2 == 1
        if game(nw and NORMAL or EASY, nw and EASY or NORMAL, false) == (nw and "white" or "black") then
            wins = wins + 1
        end
    end
    assert(wins >= 3, "Normal vs Easy: " .. wins .. " of 4")
end

-- The menu's choice only applies to new games.
do
    local saved = {}
    smudge = setmetatable({
        get_bounds = function() return 480, 800 end,
        get_metrics = function() return { top_padding = 0, header_height = 40, button_hints_height = 40 } end,
        has_touch = function() return false end,
        text_width = function() return 100 end,
        dofile = function(name) return dofile(APPS .. "/coganh/" .. name) end,
        save = function(k, v) saved[k] = v end,
    }, { __index = function() return function() end end })
    local S = { LEVELS = { { name = "1" }, { name = "2" }, { name = "3" } },
                records = { { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } },
                human = W, side = W, mode = 1, forced = false, game = G.new(false) }
    G.play(S.game, mv(16, 12))
    function S.vsDevice() return S.mode <= #S.LEVELS end
    function S.newGame(mode) S.mode, S.human = mode, S.side end
    function S.refresh() end
    function S.show() end
    local screen = dofile(APPS .. "/coganh/gmenu.lua")(C, S)
    for _ = 1, 5 do screen.button("down") end -- Resume, 3 levels, Two players, the side
    screen.button("confirm")
    assert(S.side == Bk and saved.side == tostring(Bk), "the side swaps")
    assert(S.human == W, "the game in progress keeps your side")
    screen.button("confirm")
    assert(S.side == W and S.forced == true and saved.forced == "1" and not S.game.forced)
    -- The board screen draws in every state without errors.
    S.m = smudge.get_metrics()
    S.w, S.h, S.cur = 480, 800, 18
    S.legal = C.legalMoves(S.game)
    local D = dofile(APPS .. "/coganh/draw.lua")(C, S)
    D.draw()
    S.human, S.picked = Bk, 7
    D.draw()
    assert(S.B.flip)
    smudge = nil
end

print("coganh logic ok")
