-- Logic tests for apps/xiangqi/logic.lua, game.lua, ai.lua and the notation in draw.lua (run by ctest: LuaAppLogic_xiangqi).
local C = dofile(APPS .. "/xiangqi/logic.lua")
local AI = dofile(APPS .. "/xiangqi/ai.lua")(C)
local G = dofile(APPS .. "/xiangqi/game.lua")(C)
local V = dofile(APPS .. "/xiangqi/draw.lua")(C, {})

-- math.random, seeded: Lua here has 32-bit integers, so a hand-rolled LCG overflows.
math.randomseed(90)
local function rand(n) return math.random(1, n) end

local K, A, E, H, R, CN, P = 1, 2, 3, 4, 5, 6, 7

-- Points and moves (see logic.lua).
local function square(f, r) return (11 - r) * 10 + f end
local function fileOf(p) return p % 10 end
local function rankOf(p) return 11 - p // 10 end
local function mvFrom(mv) return mv % 256 end
local function mvTo(mv) return mv // 256 end
local B = 8 -- add to a piece for Black

-- "e5" -> square (files a..i from Red's left, ranks 1..10 from Red's side).
local function sq(name) return square(name:byte(1) - 96, tonumber(name:sub(2))) end

-- A position from a list like { "Ke1", "kd10", "Ra9" } (lower case = Black).
local LETTERS = { k = K, a = A, e = E, h = H, r = R, c = CN, p = P }
local function setup(side, pieces)
    local g = G.empty(side)
    for _, s in ipairs(pieces) do
        local ch = s:sub(1, 1)
        local p = LETTERS[ch:lower()]
        if ch == ch:lower() then p = p + B end
        G.put(g, sq(s:sub(2)), p)
    end
    g.reps = { G.hash(g) }
    return g
end

-- The legal destinations of the piece on `from`, as a sorted "a1 b2" string.
local function dests(g, from)
    local out = {}
    for _, mv in ipairs(C.legalMoves(g)) do
        if mvFrom(mv) == sq(from) then
            local to = mvTo(mv)
            out[#out + 1] = string.char(96 + fileOf(to)) .. rankOf(to)
        end
    end
    table.sort(out)
    return table.concat(out, " ")
end

local function find(g, from, to)
    for _, mv in ipairs(C.legalMoves(g)) do
        if mvFrom(mv) == sq(from) and mvTo(mv) == sq(to) then return mv end
    end
    return nil
end
local function playAll(g, list)
    for _, m in ipairs(list) do
        local a, b = m:match("^(%a%d+)(%a%d+)$")
        local mv = find(g, a, b)
        assert(mv and G.play(g, mv), "illegal " .. m)
    end
end

local function perft(g, depth)
    if depth == 0 then return 1 end
    local moves = {}
    local n = C.pseudoMoves(g, moves)
    local side, total = g.side, 0
    for i = 1, n do
        C.make(g, moves[i])
        if not C.inCheck(g, side) then total = total + perft(g, depth - 1) end
        C.unmake(g, moves[i])
    end
    return total
end

-- Move generation from the start, against the published perft counts.
do
    local g = G.new()
    assert(#C.legalMoves(g) == 44, "44 moves at the start")
    assert(perft(g, 2) == 1920, "perft 2")
    assert(perft(g, 3) == 79666, "perft 3")
    assert(G.hash(g) == G.hash(G.new()), "make/unmake restore the position")
    g.side = C.BLACK
    assert(#C.legalMoves(g) == 44, "44 for Black too")
end

-- Each piece's moves.
do
    -- Horse: eight moves in the open; a piece next to it hobbles two of them.
    local g = setup(C.RED, { "Ke1", "kd10", "He5" })
    assert(dests(g, "e5") == "c4 c6 d3 d7 f3 f7 g4 g6", dests(g, "e5"))
    g = setup(C.RED, { "Ke1", "kd10", "He5", "pe6" })
    assert(dests(g, "e5") == "c4 c6 d3 f3 g4 g6", "leg above blocks d7 and f7: " .. dests(g, "e5"))
    g = setup(C.RED, { "Ke1", "kd10", "He5", "Pd5" })
    assert(dests(g, "e5") == "d3 d7 f3 f7 g4 g6", "leg on the left blocks c4 and c6: " .. dests(g, "e5"))
    -- Horse at the edge of the board.
    g = setup(C.RED, { "Ke1", "kd10", "Ha1" })
    assert(dests(g, "a1") == "b3 c2", dests(g, "a1"))
    g = setup(C.RED, { "Ke1", "kd10", "Hh1" })
    assert(dests(g, "h1") == "f2 g3 i3", dests(g, "h1"))
    g = setup(C.BLACK, { "Ke1", "kd10", "hb10", "hi9" })
    assert(dests(g, "b10") == "a8 c8 d9", dests(g, "b10"))
    assert(dests(g, "i9") == "g10 g8 h7", dests(g, "i9"))

    -- Elephant: two points diagonally, blocked on the eye, never over the river.
    g = setup(C.RED, { "Ke1", "kd10", "Ec1" })
    assert(dests(g, "c1") == "a3 e3", dests(g, "c1"))
    g = setup(C.RED, { "Ke1", "kd10", "Ec1", "Pd2" })
    assert(dests(g, "c1") == "a3", "eye blocked: " .. dests(g, "c1"))
    g = setup(C.RED, { "Ke1", "kd10", "Ec5" })
    assert(dests(g, "c5") == "a3 e3", "not over the river: " .. dests(g, "c5"))
    g = setup(C.BLACK, { "Ke1", "kd10", "ec6" })
    assert(dests(g, "c6") == "a8 e8", "Black's elephant stays home: " .. dests(g, "c6"))

    -- Advisor and general stay in the palace.
    g = setup(C.RED, { "Ke1", "kf10", "Ae2" })
    assert(dests(g, "e2") == "d1 d3 f1 f3", dests(g, "e2"))
    g = setup(C.RED, { "Ke1", "kf10", "Ad1" })
    assert(dests(g, "d1") == "e2", dests(g, "d1"))
    g = setup(C.RED, { "Kd3", "kf10" })
    assert(dests(g, "d3") == "d2 e3", "general at the palace corner: " .. dests(g, "d3"))
    g = setup(C.BLACK, { "Kd1", "kf8" })
    assert(dests(g, "f8") == "e8 f9", "Black's general: " .. dests(g, "f8"))

    -- Chariot: a rook.
    g = setup(C.RED, { "Ke1", "kd10", "Ra1", "pa4" })
    assert(dests(g, "a1") == "a2 a3 a4 b1 c1 d1", dests(g, "a1"))

    -- Cannon: moves like a chariot, captures over exactly one screen.
    g = setup(C.RED, { "Kf1", "kd10", "Ca3", "ha5", "ra8" })
    assert(dests(g, "a3") == "a1 a2 a4 a8 b3 c3 d3 e3 f3 g3 h3 i3", dests(g, "a3"))
    g = setup(C.RED, { "Kf1", "kd10", "Ca3", "ha5", "Pa6", "ra8" })
    assert(dests(g, "a3") == "a1 a2 a4 b3 c3 d3 e3 f3 g3 h3 i3", "two screens: " .. dests(g, "a3"))
    g = setup(C.RED, { "Kf1", "kd10", "Ca3", "Pa5", "ra8" })
    assert(dests(g, "a3"):find("a8"), "a screen of either colour")

    -- Soldier: forward; sideways too once over the river; never back.
    g = setup(C.RED, { "Ke1", "kd10", "Pc4" })
    assert(dests(g, "c4") == "c5", dests(g, "c4"))
    g = setup(C.RED, { "Ke1", "kd10", "Pc6" })
    assert(dests(g, "c6") == "b6 c7 d6", dests(g, "c6"))
    g = setup(C.RED, { "Ke1", "kd10", "Pa10" })
    assert(dests(g, "a10") == "b10", "last rank: sideways only")
    g = setup(C.BLACK, { "Ke1", "kd10", "pc7" })
    assert(dests(g, "c7") == "c6", dests(g, "c7"))
    g = setup(C.BLACK, { "Ke1", "kd10", "pc5" })
    assert(dests(g, "c5") == "b5 c4 d5", dests(g, "c5"))
end

-- The generals may not face each other; no move may leave your general attacked.
do
    local g = setup(C.RED, { "Ke1", "kd10" })
    assert(dests(g, "e1") == "e2 f1", "flying general: " .. dests(g, "e1"))
    g = setup(C.RED, { "Ke1", "ke10", "Pe6" })
    assert(dests(g, "e6") == "e7", "the soldier screens the generals: " .. dests(g, "e6"))
    g = setup(C.RED, { "Ke1", "kd10", "He2", "re8" })
    assert(dests(g, "e2") == "", "a pinned horse")
    g = setup(C.RED, { "Ke1", "kd10", "rf9" })
    assert(dests(g, "e1") == "e2", "not into the chariot's file: " .. dests(g, "e1"))
    -- Check from a cannon, a horse and a soldier.
    assert(C.inCheck(setup(C.RED, { "Ke1", "kd10", "ce5", "pe3" }), C.RED), "cannon check")
    assert(not C.inCheck(setup(C.RED, { "Ke1", "kd10", "ce5" }), C.RED), "a cannon needs a screen")
    assert(C.inCheck(setup(C.RED, { "Ke1", "kd10", "hd3" }), C.RED), "horse check")
    assert(not C.inCheck(setup(C.RED, { "Ke1", "kd10", "hd3", "Ad2" }), C.RED), "hobbled horse")
    assert(C.inCheck(setup(C.RED, { "Ke1", "kd10", "pe2" }), C.RED), "soldier check")
    assert(C.inCheck(setup(C.RED, { "Ke2", "kd10", "pd2" }), C.RED), "soldier from the side")
    assert(not C.inCheck(setup(C.RED, { "Ke2", "kd10", "pe1" }), C.RED), "soldiers do not attack backwards")
    assert(C.inCheck(setup(C.RED, { "Ke1", "ke10" }), C.RED), "facing generals")
end

-- Results: checkmate and stalemate both lose; draws.
do
    local g = setup(C.RED, { "Kd1", "ke10", "Ra9", "Rb8" })
    playAll(g, { "b8b10" })
    assert(g.result == "red" and g.reason == "checkmate", tostring(g.reason))
    assert(V.describe(g, g.last) == "Rb8-b10#", V.describe(g, g.last))
    assert(not G.play(g, C.legalMoves(g)[1] or 0), "no moves after the end")

    g = setup(C.RED, { "Kd1", "ke10", "Ra8", "Rf2" })
    playAll(g, { "a8a9" })
    assert(not C.inCheck(g, C.BLACK))
    assert(g.result == "red" and g.reason == "stalemate", "stalemate loses: " .. tostring(g.reason))

    g = setup(C.BLACK, { "Kd1", "ke10", "Ra8", "Rf2" })
    playAll(g, { "e10e9", "a8a9", "e9e10" })
    assert(not g.result)

    g = setup(C.RED, { "Kd1", "kf10", "Ra1", "ri10" })
    playAll(g, { "a1a2", "i10i9", "a2a1", "i9i10", "a1a2", "i10i9", "a2a1" })
    assert(not g.result)
    playAll(g, { "i9i10" })
    assert(g.result == "draw" and g.reason == "threefold repetition", tostring(g.reason))

    g = setup(C.RED, { "Kd1", "kf10", "Ra1", "ri10" })
    g.half = G.NO_CAPTURE_LIMIT - 1
    playAll(g, { "a1a2" })
    assert(g.result == "draw" and g.reason:find("without a capture"))

    g = setup(C.BLACK, { "Kd1", "kf10", "Rb5", "Ad3", "ef8" })
    playAll(g, { "f8d6" })
    assert(not g.result, "a chariot can still win")
    g = setup(C.RED, { "Kd1", "kf10", "Ae2", "rd3", "ef8" })
    playAll(g, { "e2d3" })
    assert(g.result == "draw" and g.reason:find("attacking"), "only guards left")
end

-- Notation, take-back and replay.
do
    local g = G.new()
    playAll(g, { "b3e3", "h10g8", "e3e7" })
    assert(V.describe(g, g.last) == "Ce3xe7", V.describe(g, g.last))
    local back = G.takeBack(g, 2)
    assert(back and C.moveCount(back) == 1 and back.board[sq("e3")] == CN and back.side == C.BLACK)
    local again = G.replay(g.history)
    assert(again and G.hash(again) == G.hash(g) and C.moveCount(again) == 3)
    assert(G.replay("zzz") == nil, "an illegal saved move")
end

-- The computer: mates in one at every level, takes a hanging chariot, and
-- never plays an illegal move.
do
    for level = 1, 3 do
        local depth, random = ({ 1, 2, 3 })[level], level == 1 and 100 or 0
        for _ = 1, 3 do
            local g = setup(C.RED, { "Kd1", "ke10", "Ra9", "Rb8", "pc5" })
            local mv = AI.choose(g, depth, random, rand)
            assert(G.play(g, mv) and g.result == "red", "mate in one at depth " .. depth)
        end
        -- Black mates too.
        local g = setup(C.BLACK, { "Ke1", "kf10", "ri2", "rh3" })
        local mv = AI.choose(g, depth, random, rand)
        assert(G.play(g, mv) and g.result == "black", "Black mates in one at depth " .. depth)
    end

    local g = setup(C.RED, { "Kd1", "ke10", "Ca3", "ha5", "ra8", "pi7" })
    local mv = AI.choose(g, 2, 0, rand)
    assert(mvTo(mv) == sq("a8"), "takes the chariot")

    local wins = 0
    for game = 1, 3 do
        g = G.new()
        local plies = 0
        local depth = game == 3 and 3 or 2
        while not g.result and plies < (game == 3 and 30 or 80) do
            local m
            if g.side == C.RED then
                m = AI.choose(g, depth, 0, rand)
            else
                local legal = C.legalMoves(g)
                m = legal[rand(#legal)]
            end
            assert(m and G.play(g, m), "an illegal device move")
            plies = plies + 1
        end
        if g.result == "red" then wins = wins + 1 end
    end
    assert(wins >= 1, "beats random play")
end

-- Search size: node counts per level from the start and from a middle game.
do
    local mid = G.new()
    playAll(mid, { "b3e3", "h10g8", "h1g3", "i10h10", "i1h1", "b8b4", "g4g5", "h8h4", "b1c3", "c7c6" })
    for _, pos in ipairs({ G.new(), mid }) do
        local line = {}
        for depth = 1, 3 do
            local t = os.clock()
            local mv, nodes = AI.choose(pos, depth, 0, rand)
            assert(mv and nodes < 60000, "node count at depth " .. depth .. ": " .. nodes)
            line[#line + 1] = string.format("d%d %d nodes %.0f ms", depth, nodes, (os.clock() - t) * 1000)
        end
        print("xiangqi search: " .. table.concat(line, ", "))
    end
    -- A time limit stops the search but still returns a move.
    local now = function() return os.clock() * 1000 end
    local mv = AI.choose(mid, 6, 0, rand, now, 50)
    assert(mv and G.play(mid, mv), "a move when time runs out")
end

-- The menu's side swap only applies to new games: a game in progress keeps its sides.
do
    local saved = {}
    smudge = setmetatable({
        get_bounds = function() return 480, 800 end,
        get_metrics = function() return { top_padding = 0, header_height = 40 } end,
        has_touch = function() return false end,
        dofile = function(name) return dofile(APPS .. "/xiangqi/" .. name) end,
        save = function(k, v) saved[k] = v end,
    }, { __index = function() return function() end end })
    local S = { LEVELS = { { name = "1" }, { name = "2" }, { name = "3" } },
                records = { { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } },
                human = C.RED, side = C.RED, mode = 1, game = G.new() }
    function S.vsDevice() return S.mode <= #S.LEVELS end
    function S.newGame(mode) S.mode, S.human = mode, S.side end
    function S.refresh() end
    function S.show() end
    local screen = dofile(APPS .. "/xiangqi/xqmenu.lua")(C, S)
    for _ = 1, 5 do screen.button("down") end -- Resume, 3 levels, Two players, the side
    screen.button("confirm")
    assert(S.side == C.BLACK and saved.side == tostring(C.BLACK), "the side swaps")
    assert(S.human == C.RED, "the game in progress keeps your side")
    S.newGame(1)
    assert(S.human == C.BLACK, "a new game takes the new side")
    smudge = nil
end

print("xiangqi logic ok")
