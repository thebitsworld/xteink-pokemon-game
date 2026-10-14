-- Logic tests for apps/coup/logic.lua, game.lua, ai.lua and the notation in draw.lua (run by ctest: LuaAppLogic_coup).
local C = dofile(APPS .. "/coup/logic.lua")
local AI = dofile(APPS .. "/coup/ai.lua")(C)
local G = dofile(APPS .. "/coup/game.lua")(C)
local V = dofile(APPS .. "/coup/draw.lua")(C, {})

-- math.random, seeded: Lua here has 32-bit integers, so a hand-rolled LCG overflows.
math.randomseed(77)
local function rand(n) return math.random(1, n) end

local K, A, E, H, R, CN, P = 1, 2, 3, 4, 5, 6, 7
local B, HID = 8, C.HIDDEN -- add for Black, for face down

local function square(f, r) return (11 - r) * 10 + f end
local function fileOf(p) return p % 10 end
local function rankOf(p) return 11 - p // 10 end
local function mvFrom(mv) return mv % 256 end
local function mvTo(mv) return mv // 256 end
local function sq(name) return square(name:byte(1) - 96, tonumber(name:sub(2))) end

-- A position from a list like { "Ke1", "kd10", "Ra9", "?R:Pa1" } (lower
-- case = Black; "?R:P" = face down with role R, really P).
local LETTERS = { k = K, a = A, e = E, h = H, r = R, c = CN, p = P }
local function setup(side, pieces)
    local g = G.empty(side)
    for _, s in ipairs(pieces) do
        local role, real, at = s:match("^%?(%a):(%a)(%a%d+)$")
        if role then
            local add = role == role:lower() and B or 0
            G.put(g, sq(at), HID + add + LETTERS[role:lower()], LETTERS[real:lower()])
        else
            local ch = s:sub(1, 1)
            local p = LETTERS[ch:lower()]
            if ch == ch:lower() then p = p + B end
            G.put(g, sq(s:sub(2)), p)
        end
    end
    g.reps = { G.hash(g) }
    return g
end

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

-- Set-up: generals face up on e1/e10, every other starting point face down
-- with its role, and each side's 15 pieces conserved.
do
    for _ = 1, 20 do
        local deal = G.shuffle(rand)
        assert(G.validDeal(deal), deal)
        local g = G.new(deal)
        assert(g.board[sq("e1")] == K and g.board[sq("e10")] == K + B, "generals face up")
        local counts = { {}, {} }
        local hidden = 0
        for s = 11, 109 do
            local p = g.board[s]
            if p ~= C.EMPTY and p ~= C.OFF and p ~= K and p ~= K + B then
                assert(p >= HID, "only the generals face up")
                hidden = hidden + 1
                local side = (p & 8) // 8
                local real = g.secret[s]
                assert(real and real >= A and real <= P, "a real piece under each")
                counts[side + 1][real] = (counts[side + 1][real] or 0) + 1
            end
        end
        assert(hidden == 30, "30 face-down pieces")
        for side = 1, 2 do
            local c = counts[side]
            assert(c[A] == 2 and c[E] == 2 and c[H] == 2 and c[R] == 2 and c[CN] == 2 and c[P] == 5, "pieces conserved")
        end
        -- Roles follow the standard starting points.
        local roles = { a1 = R, b1 = H, c1 = E, d1 = A, f1 = A, g1 = E, h1 = H, i1 = R, b3 = CN, h3 = CN,
                        a4 = P, c4 = P, e4 = P, g4 = P, i4 = P }
        for name, role in pairs(roles) do
            assert(g.board[sq(name)] == HID + role, "Red role on " .. name)
            local f, r = name:sub(1, 1), tonumber(name:sub(2))
            assert(g.board[sq(f .. (11 - r))] == HID + B + role, "Black role mirrored")
        end
        assert(#C.legalMoves(g) == 44, "a face-down start moves like xiangqi's")
    end
    assert(not G.validDeal("AAEEHHRRCCPPPPPAAEEHHRRCCPPPPR"), "a bad deal")
    assert(not G.validDeal("short"))
end

-- Face-down pieces move as their role; face-up guards roam.
do
    -- A face-down piece on a chariot point (really a soldier) moves as a chariot.
    local g = setup(C.RED, { "Ke1", "kd10", "?R:Pa1", "pa5" })
    assert(dests(g, "a1") == "a2 a3 a4 a5 b1 c1 d1", dests(g, "a1"))
    -- ...then turns up and moves as a soldier.
    playAll(g, { "a1a3" })
    assert(g.board[sq("a3")] == P and g.secret[sq("a3")] == nil and g.secret[sq("a1")] == nil, "turned up")
    assert(g.flipped == P and V.describe(g, g.last) == "?a1-a3=P", V.describe(g, g.last))
    g.side = C.RED
    assert(dests(g, "a3") == "a4", "now a soldier: " .. dests(g, "a3"))

    -- Face-down advisor point: into the palace only. Face up: anywhere diagonally.
    g = setup(C.RED, { "Ke1", "kf10", "?A:Rd1" })
    assert(dests(g, "d1") == "e2", dests(g, "d1"))
    g = setup(C.RED, { "Ke1", "kf10", "Ad1" })
    assert(dests(g, "d1") == "c2 e2", "a face-up advisor leaves the palace: " .. dests(g, "d1"))
    g = setup(C.RED, { "Ke1", "kf10", "Ac5" })
    assert(dests(g, "c5") == "b4 b6 d4 d6", dests(g, "c5"))
    -- Face-down elephant point: its own side. Face up: over the river, eye still blocks.
    g = setup(C.RED, { "Ke1", "kd10", "?E:Hc1" })
    assert(dests(g, "c1") == "a3 e3", dests(g, "c1"))
    g = setup(C.RED, { "Ke1", "kd10", "Ec5" })
    assert(dests(g, "c5") == "a3 a7 e3 e7", "a face-up elephant crosses: " .. dests(g, "c5"))
    g = setup(C.RED, { "Ke1", "kd10", "Ec5", "pd6" })
    assert(dests(g, "c5") == "a3 a7 e3", "eye blocked: " .. dests(g, "c5"))
    -- Black's face-down soldier and cannon roles.
    g = setup(C.BLACK, { "Ke1", "kd10", "?p:Rc7", "?c:Hb8", "pb6", "Pb4" })
    assert(dests(g, "c7") == "c6", dests(g, "c7"))
    assert(dests(g, "b8"):find("b4"), "a cannon role jumps to capture")
    -- A face-down piece checks as its role (a horse point), and a roaming
    -- face-up advisor or elephant can check too.
    assert(C.inCheck(setup(C.RED, { "Ke1", "kd10", "?h:Ad3" }), C.RED), "face-down horse role checks")
    assert(C.inCheck(setup(C.RED, { "Ke2", "kd10", "ad3" }), C.RED), "a roaming advisor checks")
    assert(C.inCheck(setup(C.RED, { "Ke1", "kd10", "ec3" }), C.RED), "a roaming elephant checks")
    assert(not C.inCheck(setup(C.RED, { "Ke1", "kd10", "ec3", "Pd2" }), C.RED), "its eye blocked")
    assert(C.inCheck(setup(C.RED, { "Ke1", "ke10" }), C.RED), "facing generals")
end

-- Reveal on capture: the taken piece is shown and counted as lost.
do
    local g = setup(C.RED, { "Kd1", "ke9", "Ra1", "?r:Ha10", "?r:Ci10" })
    playAll(g, { "a1a10" })
    assert(g.taken == H and g.takenHidden and g.lost[2] == "H", "a face-down horse taken")
    assert(g.secret[sq("a10")] == nil and g.board[sq("a10")] == R, "the chariot stands there")
    assert(V.describe(g, g.last) == "Ra1xa10[H]", V.describe(g, g.last))
    -- A face-down piece capturing turns up, and the victim is shown too.
    g = setup(C.RED, { "Ke1", "kf10", "?C:Eb3", "?h:Pb8", "pb5" })
    playAll(g, { "b3b8" })
    assert(g.board[sq("b8")] == E and g.lost[2] == "P", "both shown")
    assert(V.describe(g, g.last) == "?b3xb8=E[P]", V.describe(g, g.last))
    -- The computer's pool follows from what is public only.
    g = G.new(G.shuffle(rand))
    local pool = G.pool(g)
    assert(pool[1][R] == 2 and pool[2][P] == 5, "a full pool at the start")
    for _ = 1, 40 do
        local legal = C.legalMoves(g)
        G.play(g, legal[rand(#legal)])
        if g.result then break end
    end
    local real = g.secret
    g.secret = setmetatable({}, { __index = function() error("the pool looked under a piece") end })
    pool = G.pool(g)
    G.averages(g)
    g.secret = real
    local actual = { {}, {} }
    for s = 11, 109 do
        if real[s] then
            local side = (g.board[s] & 8) // 8 + 1
            actual[side][real[s]] = (actual[side][real[s]] or 0) + 1
        end
    end
    for side = 1, 2 do
        local n, sum = 0, 0
        for t = A, P do
            assert(pool[side][t] == (actual[side][t] or 0), "the public pool matches what lies face down")
            n, sum = n + pool[side][t], sum + pool[side][t] * C.VALUE[t]
        end
        assert(g.avg[side] == (n > 0 and sum // n or 300), "the average")
    end
end

-- Results, replay, take-back with the deal.
do
    local g = setup(C.RED, { "Kd1", "ke10", "Ra9", "Rb8" })
    playAll(g, { "b8b10" })
    assert(g.result == "red" and g.reason == "checkmate", tostring(g.reason))
    g = setup(C.RED, { "Kd1", "kf10", "rd2" })
    playAll(g, { "d1d2" })
    assert(g.result == "draw" and g.reason:find("attacking"), "only generals left")
    g = setup(C.RED, { "Kd1", "kf10", "Ae2", "rd3" })
    playAll(g, { "e2d3" })
    assert(not g.result, "a face-up advisor can still attack")

    local deal = G.shuffle(rand)
    g = G.new(deal)
    for _ = 1, 12 do
        local legal = C.legalMoves(g)
        assert(G.play(g, legal[rand(#legal)]))
        if g.result then break end
    end
    local again = G.replay(deal, g.history)
    assert(again and G.hash(again) == G.hash(g), "replay with the deal")
    for s = 11, 109 do assert(again.secret[s] == g.secret[s], "same hidden layout") end
    local back = G.takeBack(g, 2)
    assert(back and C.moveCount(back) == C.moveCount(g) - 2)
    assert(G.replay("x", g.history) == nil, "a bad deal is refused")
end

-- The computer plays fair: reading or writing a hidden identity during its
-- search raises an error, and its move does not depend on them.
do
    local function forbid(g)
        local real = g.secret
        g.secret = setmetatable({}, { __index = function() error("the computer looked under a piece") end,
                                      __newindex = function() error("the computer wrote under a piece") end })
        return real
    end
    for trial = 1, 6 do
        local g = G.new(G.shuffle(rand))
        for _ = 1, trial * 3 do
            local legal = C.legalMoves(g)
            G.play(g, legal[rand(#legal)])
            if g.result then break end
        end
        if not g.result then
            local depth = trial % 3 + 1
            local real = forbid(g)
            local mv = AI.choose(g, depth, 0, rand)
            local h = G.hash(g)
            -- The board alone holds no identity, so the move is the same
            -- with nothing at all under the pieces.
            g.secret = nil
            local mv2 = AI.choose(g, depth, 0, rand)
            assert(mv == mv2 and G.hash(g) == h, "the same move whatever lies face down")
            g.secret = real
            assert(G.play(g, mv), "a legal device move")
        end
    end
end

-- The computer: mates in one at every level, takes a hanging chariot.
do
    for level = 1, 3 do
        local depth, random = ({ 1, 2, 3 })[level], level == 1 and 100 or 0
        local g = setup(C.RED, { "Kd1", "ke10", "Ra9", "Rb8", "?p:Pc7" })
        local mv = AI.choose(g, depth, random, rand)
        assert(G.play(g, mv) and g.result == "red", "mate in one at depth " .. depth)
    end
    local g = setup(C.RED, { "Kd1", "ke10", "Ca3", "ha5", "ra8", "pi7" })
    assert(mvTo(AI.choose(g, 2, 0, rand)) == sq("a8"), "takes the chariot")
end

-- Games end: device against random play and against itself, with the
-- face-down pieces turning up as they move.
do
    local wins, ended = 0, 0
    for game = 1, 4 do
        local g = G.new(G.shuffle(rand))
        local plies = 0
        while not g.result and plies < 300 do
            local m
            if g.side == C.RED or game == 4 then
                local real = g.secret
                g.secret = nil
                m = AI.choose(g, game == 4 and 1 or 2, 0, rand)
                g.secret = real
            else
                local legal = C.legalMoves(g)
                m = legal[rand(#legal)]
            end
            assert(m and G.play(g, m), "an illegal device move")
            plies = plies + 1
        end
        if g.result then ended = ended + 1 end
        if g.result == "red" and game < 4 then wins = wins + 1 end
    end
    assert(wins >= 2, "beats random play: " .. wins)
    assert(ended >= 3, "games end: " .. ended)
    -- Random games always end too (a face-down piece turning up counts as progress).
    for _ = 1, 10 do
        local g = G.new(G.shuffle(rand))
        local plies = 0
        while not g.result do
            local legal = C.legalMoves(g)
            assert(G.play(g, legal[rand(#legal)]))
            plies = plies + 1
            assert(plies < 2000, "a random game ends")
        end
    end
end

-- Levels: Hard beats Easy more often than not (sides alternate).
do
    local score = 0
    for game = 1, 6 do
        local g = G.new(G.shuffle(rand))
        local hard = game % 2
        local plies = 0
        while not g.result and plies < 240 do
            local real = g.secret
            g.secret = nil
            local m = g.side == hard and AI.choose(g, 3, 0, rand) or AI.choose(g, 1, 25, rand)
            g.secret = real
            assert(G.play(g, m))
            plies = plies + 1
        end
        if g.result == (hard == C.RED and "red" or "black") then score = score + 1
        elseif g.result and g.result ~= "draw" then score = score - 1 end
    end
    print("coup Hard vs Easy, wins minus losses of 6: " .. score)
    assert(score > 0, "Hard beats Easy")
end

-- Search size per level from the start and a middle game.
do
    local g = G.new(G.shuffle(rand))
    for _ = 1, 16 do
        local legal = C.legalMoves(g)
        G.play(g, legal[rand(#legal)])
    end
    for _, pos in ipairs({ G.new(G.shuffle(rand)), g }) do
        if not pos.result then
            local line = {}
            for depth = 1, 3 do
                local t = os.clock()
                local mv, nodes = AI.choose(pos, depth, 0, rand)
                assert(mv and nodes < 60000, "node count at depth " .. depth .. ": " .. nodes)
                line[#line + 1] = string.format("d%d %d nodes %.0f ms", depth, nodes, (os.clock() - t) * 1000)
            end
            print("coup search: " .. table.concat(line, ", "))
        end
    end
end

-- The menu's side swap only applies to new games: a game in progress keeps its sides.
do
    local saved = {}
    smudge = setmetatable({
        get_bounds = function() return 480, 800 end,
        get_metrics = function() return { top_padding = 0, header_height = 40 } end,
        has_touch = function() return false end,
        dofile = function(name) return dofile(APPS .. "/coup/" .. name) end,
        save = function(k, v) saved[k] = v end,
    }, { __index = function() return function() end end })
    local S = { LEVELS = { { name = "1" }, { name = "2" }, { name = "3" } },
                records = { { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } },
                human = C.RED, side = C.RED, mode = 1, game = G.new(G.shuffle(rand)) }
    function S.vsDevice() return S.mode <= #S.LEVELS end
    function S.newGame(mode) S.mode, S.human = mode, S.side end
    function S.refresh() end
    function S.show() end
    local screen = dofile(APPS .. "/coup/cumenu.lua")(C, S)
    for _ = 1, 5 do screen.button("down") end -- Resume, 3 levels, Two players, the side
    screen.button("confirm")
    assert(S.side == C.BLACK and saved.side == tostring(C.BLACK), "the side swaps")
    assert(S.human == C.RED, "the game in progress keeps your side")
    S.newGame(1)
    assert(S.human == C.BLACK, "a new game takes the new side")
    smudge = nil
end

print("coup logic ok")
