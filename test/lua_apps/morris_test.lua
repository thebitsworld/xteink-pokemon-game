-- Logic tests for apps/morris/logic.lua, game.lua, ai.lua and the board's
-- notation and cursor (run by ctest: LuaAppLogic_morris).
local C = dofile(APPS .. "/morris/logic.lua")
local G = dofile(APPS .. "/morris/game.lua")(C)
local AI = dofile(APPS .. "/morris/ai.lua")(C)
local V = dofile(APPS .. "/morris/draw.lua")(C, {})
local I = dofile(APPS .. "/morris/input.lua")(C, {})

math.randomseed(77)
local function rand(n) return math.random(1, n) end

local W, Bk = C.WHITE, C.BLACK
local function mv(from, to, rem) return from + to * 32 + (rem or 0) * 1024 end

-- A position: lists of White's and Black's points, `side` to move, pieces
-- still in hand (0 by default: the moving phase).
local function setup(white, black, side, handW, handB, flying)
    local g = G.new(flying)
    for _, p in ipairs(white) do C.put(g, p, W) end
    for _, p in ipairs(black) do C.put(g, p, Bk) end
    g.hand[W], g.hand[Bk] = handW or 0, handB or 0
    g.side = side or W
    return g
end

-- The points joined to p.
local function joined(p)
    local t, a = {}, C.ADJ[p]
    while a > 0 do
        t[#t + 1] = a % 32
        a = a // 32
    end
    return t
end

local function has(list, m)
    for _, x in ipairs(list) do if x == m then return true end end
    return false
end

-- Adjacency: 32 joins, each both ways, along the lines; 2 to 4 per point.
do
    local edges = 0
    for p = 1, 24 do
        local adj = joined(p)
        assert(#adj >= 2 and #adj <= 4, "point " .. p)
        local px, py = C.xy(p)
        for _, q in ipairs(adj) do
            edges = edges + 1
            -- Joined points share a row or a column.
            local qx, qy = C.xy(q)
            assert(px == qx or py == qy, p .. "-" .. q)
            assert(has(joined(q), p), "join " .. p .. "-" .. q .. " both ways")
        end
    end
    assert(edges == 64, "32 joins")
    local function adj(p)
        local t = joined(p)
        table.sort(t)
        return table.concat(t, ",")
    end
    assert(adj(1) == "2,10")
    assert(adj(5) == "2,4,6,8")
    assert(adj(11) == "4,10,12,19")
    assert(adj(17) == "16,18,20")
    assert(adj(24) == "15,23")
    assert(adj(12) == "7,11,16", "no join across the centre")
    -- Every point lies on two lines; the geometry is the 7x7 grid's.
    for p = 1, 24 do assert(C.PL1[p] and C.PL2[p] and C.PL1[p] ~= C.PL2[p]) end
    local x, y = C.xy(13)
    assert(x == 4 and y == 3)
    x, y = C.xy(24)
    assert(x == 6 and y == 6)
end

-- Mills: every line closes; a move along its own line does not close it.
do
    for l = 1, 16 do
        local a, b, c = C.line(l, 1), C.line(l, 2), C.line(l, 3)
        local g = setup({ a, b }, {}, W, 1, 0)
        assert(C.closes(g, 0, c, W), "line " .. l)
        C.put(g, c, W)
        assert(C.inMill(g, a) and C.inMill(g, b) and C.inMill(g, c))
    end
    -- 1,2 White; moving 3's neighbour 15 up to 3 closes 1-2-3.
    local g = setup({ 1, 2, 15 }, { 22 }, W)
    assert(C.closes(g, 15, 3, W))
    -- A piece sliding along a line onto its end does not close that line by itself.
    g = setup({ 1, 2 }, { 22 }, W)
    assert(not C.closes(g, 2, 3, W), "1,2 -> 1,3 is two pieces")
end

-- Placing: a mill offers one move per piece it may take.
do
    local g = setup({ 1, 2 }, { 10, 22, 4 }, W, 7, 6)
    local list = C.legalMoves(g)
    assert(has(list, mv(0, 3, 10)) and has(list, mv(0, 3, 22)) and has(list, mv(0, 3, 4)))
    assert(not has(list, mv(0, 3)), "a mill must take a piece")
    assert(has(list, mv(0, 5)))
    assert(#list == 18 + 3, "18 quiet placements plus 3 mills, got " .. #list)
end

-- The removal rule: pieces in a mill are safe unless all are in mills.
do
    -- Black: mill 22-23-24 plus a loose piece on 4.
    local g = setup({ 1, 2, 15 }, { 22, 23, 24, 4 }, W)
    local list = C.legalMoves(g)
    assert(has(list, mv(15, 3, 4)))
    assert(not has(list, mv(15, 3, 22)), "a piece in a mill is safe")
    -- Black: only the mill: any of its pieces may go.
    g = setup({ 1, 2, 15 }, { 22, 23, 24 }, W)
    list = C.legalMoves(g)
    assert(has(list, mv(15, 3, 22)) and has(list, mv(15, 3, 23)) and has(list, mv(15, 3, 24)))
end

-- Phases: moving along joins only; flying with three pieces (when on).
do
    local g = setup({ 1, 5, 9, 13 }, { 22, 23, 24 }, W)
    assert(C.phase(g, W) == 2 and C.phase(g, Bk) == 3)
    local list = C.legalMoves(g)
    for _, m in ipairs(list) do
        local from, to = m % 32, (m // 32) % 32
        assert(has(joined(from), to), "a move goes to a joined point")
    end
    g.side = Bk
    list = C.legalMoves(g)
    -- 3 pieces x 17 empty points.
    assert(#list == 3 * 17, "flying: " .. #list)
    g = setup({ 1, 5, 9, 13 }, { 22, 23, 24 }, Bk, 0, 0, false)
    assert(C.phase(g, Bk) == 2, "flying off")
    list = C.legalMoves(g)
    assert(#list == 3, "22-10, 23-20 and 24-15 only, got " .. #list)
end

-- Playing through the game: placements, a mill, the phase change.
do
    local g = G.new(true)
    local function play(from, to, rem)
        assert(G.play(g, mv(from, to, rem)), "illegal " .. from .. "-" .. to .. "x" .. (rem or 0))
    end
    play(0, 1); play(0, 22); play(0, 2); play(0, 23)
    assert(not G.play(g, mv(0, 3)), "must take a piece")
    play(0, 3, 22)
    assert(g.on[Bk] == 1 and g.hand[Bk] == 7 and g.hand[W] == 6)
    assert(g.side == Bk and #g.history == 15)
    -- Replaying the history gives the same position.
    local r = G.replay(g.history, true)
    for p = 1, 24 do assert(r.b[p] == g.b[p]) end
    assert(r.side == g.side and r.hand[1] == g.hand[1])
    -- Taking back.
    local t = G.takeBack(g, 1)
    assert(t.b[22] == Bk and t.b[3] == 0 and t.side == W)
end

-- Losing: down to two pieces, or no legal move.
do
    -- White takes Black's third-last piece (Black: 3 pieces, flying).
    local g = setup({ 1, 2, 15, 10 }, { 23, 24, 21 }, W)
    G.play(g, mv(15, 3, 21))
    assert(g.result == "white" and g.reason == "pieces")
    -- Black blocked: 1 boxed in by White on 2 and 10, flying off.
    g = setup({ 2, 10, 23, 14, 6 }, { 1, 3, 15, 24 }, W, 0, 0, false)
    -- Black's moves: 1: 2,10 taken; 3: 2,15 taken; 15: 3,14,24 taken; 24: 15,23 taken.
    -- White moves 6-5 (no mill), and Black has no legal move.
    assert(G.play(g, mv(6, 5)))
    assert(g.result == "white" and g.reason == "blocked", tostring(g.result) .. " " .. tostring(g.reason))
    -- With flying on, three pieces are never blocked; four are.
    g = setup({ 2, 10, 23, 14, 6 }, { 1, 3, 15, 24 }, W, 0, 0, true)
    assert(G.play(g, mv(6, 5)) and g.result == "white")
end

-- Draws: 50 moves each without a mill, and threefold repetition.
do
    local g = setup({ 1, 4, 7 }, { 22, 19, 16 }, W, 0, 0, false)
    -- Each side shuffles 1-2-1 / 22-23-22: the position repeats.
    local seq = { mv(1, 2), mv(22, 23), mv(2, 1), mv(23, 22) }
    local i = 0
    while not g.result do
        i = i + 1
        assert(G.play(g, seq[(i - 1) % 4 + 1]))
        assert(i < 20)
    end
    assert(g.result == "draw" and g.reason == "threefold repetition", g.reason)
    -- The position after White's first move comes back at plies 5 and 9.
    assert(i == 9, "third time after 9 plies, got " .. i)
    -- The no-mill count: 100 plies without repeating (forced via the counter).
    g = setup({ 1, 4, 7 }, { 22, 19, 16 }, W, 0, 0, false)
    g.half = G.NO_MILL_LIMIT - 1
    assert(G.play(g, mv(1, 2)))
    assert(g.result == "draw" and g.reason:find("50 moves"))
    -- A mill starts the count afresh.
    g = setup({ 1, 2, 15 }, { 22, 19, 16, 4 }, W, 0, 0, false)
    g.half = 90
    assert(G.play(g, mv(15, 3, 4)) and g.half == 0 and not g.result)
    -- Placements never count.
    g = G.new(true)
    assert(G.play(g, mv(0, 1)) and g.half == 0)
end

-- Notation and the cursor.
do
    assert(V.describe(mv(0, 1)) == "a7")
    assert(V.describe(mv(1, 10, 24)) == "a7-a4 xg1")
    assert(I.step(1, 1, 0) == 2, "right along the top")
    assert(I.step(1, 0, 1) == 10, "down the left side")
    assert(I.step(4, 1, 0) == 5)
    assert(I.step(11, 0, -1) == 4)
    assert(I.step(3, 1, 0) == 1, "wraps")
    assert(I.step(12, 1, 0) == 13, "across the centre")
    -- Every point reaches every other with the four arrows.
    for p = 1, 24 do
        local seen, todo, n = { [p] = true }, { p }, 1
        while #todo > 0 do
            local q = table.remove(todo)
            for _, d in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) do
                local r = I.step(q, d[1], d[2])
                if not seen[r] then seen[r], n = true, n + 1; todo[#todo + 1] = r end
            end
        end
        assert(n == 24, "reachable from " .. p)
    end
end

-- The AI: legal moves, mills taken, a mill blocked, games end.
local EASY = { depth = 1, random = 25, nodes = 2000 }
local NORMAL = { depth = 3, nodes = 5000 }
local HARD = { depth = 12, nodes = 8000 }
do
    -- It closes a mill when it can (four pieces: no flying back later).
    local g = setup({ 1, 2, 15, 9 }, { 22, 19, 16, 4 }, W)
    local m = AI.choose(g, NORMAL, rand)
    assert(m // 1024 > 0 and (m // 32) % 32 == 3, "closes the mill")
    -- It blocks a mill while placing.
    g = setup({ 1, 2 }, { 24 }, Bk, 7, 8)
    m = AI.choose(g, NORMAL, rand)
    assert(m == mv(0, 3), "blocks 1-2-3, got " .. m)
    -- The search leaves the position as it was.
    local before = table.concat(g.b, ",") .. g.side .. g.hand[1] .. g.hand[2] .. table.concat(g.lc, ",")
    AI.choose(g, HARD, rand)
    assert(before == table.concat(g.b, ",") .. g.side .. g.hand[1] .. g.hand[2] .. table.concat(g.lc, ","))
end

local function game(white, black, flying)
    local g = G.new(flying)
    local plies = 0
    while not g.result do
        local L = g.side == W and white or black
        local m = AI.choose(g, L, rand)
        assert(m and has(C.legalMoves(g), m), "AI move is legal")
        assert(G.play(g, m))
        plies = plies + 1
        assert(plies < 2000)
    end
    return g.result, plies
end

do
    -- Random play terminates too (the draw rules end every game).
    local RANDOM = { depth = 1, random = 100 }
    for _ = 1, 20 do game(RANDOM, RANDOM, true) end
    for _ = 1, 3 do game(RANDOM, RANDOM, false) end
    game(EASY, NORMAL, false)
end

-- Hard beats Easy most of the time (both colours).
do
    local wins, losses, n = 0, 0, 0
    for i = 1, 6 do
        local hardWhite = i % 2 == 1
        local r = game(hardWhite and HARD or EASY, hardWhite and EASY or HARD, true)
        n = n + 1
        if r ~= "draw" then
            if (r == "white") == hardWhite then wins = wins + 1 else losses = losses + 1 end
        end
    end
    assert(wins >= 4 and losses <= 1, "Hard vs Easy: " .. wins .. " won, " .. losses .. " lost of " .. n)
    -- Normal beats Easy more often than not.
    wins = 0
    for i = 1, 4 do
        local nw = i % 2 == 1
        if game(nw and NORMAL or EASY, nw and EASY or NORMAL, true) == (nw and "white" or "black") then wins = wins + 1 end
    end
    assert(wins >= 2, "Normal vs Easy: " .. wins .. " of 4")
end

-- The menu's side swap only applies to new games.
do
    local saved = {}
    smudge = setmetatable({
        get_bounds = function() return 480, 800 end,
        get_metrics = function() return { top_padding = 0, header_height = 40, button_hints_height = 40 } end,
        has_touch = function() return false end,
        dofile = function(name) return dofile(APPS .. "/morris/" .. name) end,
        save = function(k, v) saved[k] = v end,
    }, { __index = function() return function() end end })
    local S = { LEVELS = { { name = "1" }, { name = "2" }, { name = "3" } },
                records = { { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } },
                human = W, side = W, mode = 1, flying = true, game = G.new(true) }
    G.play(S.game, mv(0, 1))
    function S.vsDevice() return S.mode <= #S.LEVELS end
    function S.newGame(mode) S.mode, S.human = mode, S.side end
    function S.refresh() end
    function S.show() end
    local screen = dofile(APPS .. "/morris/mmenu.lua")(C, S)
    for _ = 1, 5 do screen.button("down") end -- Resume, 3 levels, Two players, the side
    screen.button("confirm")
    assert(S.side == Bk and saved.side == tostring(Bk), "the side swaps")
    assert(S.human == W, "the game in progress keeps your side")
    assert(S.flying == true)
    screen.button("confirm")
    assert(S.side == W and S.flying == false and saved.flying == "0" and S.game.flying,
           "flying applies to the next game")
    screen.button("confirm")
    screen.button("confirm")
    assert(S.side == W and S.flying == true, "four choices round")
    screen.button("confirm")
    S.newGame(1)
    assert(S.human == Bk, "a new game takes the new side")
    smudge = nil
end

print("morris logic ok")
