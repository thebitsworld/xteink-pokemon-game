-- Logic tests for apps/dots/logic.lua, game.lua and ai.lua (run by ctest: LuaAppLogic_dots).
local D = dofile(APPS .. "/dots/logic.lua")
local G = dofile(APPS .. "/dots/game.lua")(D)
local AI = dofile(APPS .. "/dots/ai.lua")(D)

math.randomseed(77)
local function rand(n) return math.random(1, n) end

-- Line ids by dots: h(r, c) from dot (r, c) to (r, c + 1), v(r, c) from (r, c) to (r + 1, c).
local function h(g, r, c) return r * g.n + c + 1 end
local function v(g, r, c) return g.nh + r * (g.n + 1) + c + 1 end
local function box(g, r, c) return r * g.n + c + 1 end

local function playAll(g, list)
    for _, l in ipairs(list) do assert(D.play(g, l), "cannot play " .. l) end
end

-- A box's four sides as line ids.
local function sidesOf(g, r, c) return { h(g, r, c), h(g, r + 1, c), v(g, r, c), v(g, r, c + 1) } end

-- Board shape: line counts, ends, the boxes beside each line.
do
    for n = 3, 6 do
        local g = G.new(n)
        assert(g.L == 2 * n * (n + 1) and g.nb == n * n)
        local count = {}
        for l = 1, g.L do
            local r1, c1, r2, c2 = G.ends(g, l)
            assert(math.abs(r1 - r2) + math.abs(c1 - c2) == 1, "unit line")
            for _, b in ipairs({ g.b1:byte(l), g.b2:byte(l) }) do
                if b > 0 then count[b] = (count[b] or 0) + 1 end
            end
        end
        for b = 1, g.nb do assert(count[b] == 4, "every box has four sides") end
        for b = 1, g.nb do
            for k = 1, 4 do
                local l = g.bl:byte((b - 1) * 4 + k)
                assert(g.b1:byte(l) == b or g.b2:byte(l) == b, "box lines")
            end
        end
    end
end

-- Scoring and extra turns: the fourth side scores and the same player goes on.
do
    local g = G.new(3, 1)
    local s = sidesOf(g, 0, 0)
    assert(D.play(g, s[1]) == 0 and g.turn == 2)
    assert(D.play(g, s[2]) == 0 and g.turn == 1)
    assert(D.play(g, s[3]) == 0 and g.turn == 2)
    assert(D.play(g, s[4]) == 1, "fourth side scores")
    assert(g.turn == 2 and g.score[2] == 1 and g.score[1] == 0, "the scorer moves again")
    assert(g.owner[box(g, 0, 0)] == 2)
    assert(not D.play(g, s[1]), "a drawn line cannot be drawn again")
    -- Undo gives the box back and the turn to whoever drew it.
    assert(G.undo(g) == s[4] and g.turn == 2 and g.score[2] == 0 and g.owner[box(g, 0, 0)] == 0)
    assert(D.play(g, s[4]) == 1)
    -- The last turn's lines: only the scorer's.
    local last = G.lastTurn(g)
    assert(last[s[4]] and last[s[3]] == nil and G.lastPlayer(g) == 2)
end

-- One line closing two boxes at once scores both.
do
    local g = G.new(3, 1)
    local a, b = sidesOf(g, 0, 0), sidesOf(g, 0, 1) -- a[4] == b[3]: the shared line
    assert(a[4] == b[3])
    playAll(g, { a[1], a[2], a[3], b[1], b[2], b[4] })
    local who = g.turn
    assert(D.play(g, a[4]) == 2, "double box")
    assert(g.score[who] == 2 and g.turn == who)
end

-- End of game: every line drawn, scores add up, the winner, save and load.
do
    for n = 3, 6 do
        local g = G.new(n, 2)
        local order = {}
        for l = 1, g.L do order[l] = l end
        for i = g.L, 2, -1 do
            local j = rand(i)
            order[i], order[j] = order[j], order[i]
        end
        for i = 1, g.L do
            assert(not D.over(g) and D.winner(g) == nil)
            D.play(g, order[i])
        end
        assert(D.over(g) and g.score[1] + g.score[2] == n * n)
        local w = D.winner(g)
        assert(w == (g.score[1] > g.score[2] and 1 or (g.score[2] > g.score[1] and 2 or 0)))
        local back = G.decode(D.encode(g))
        assert(back and back.score[1] == g.score[1] and back.score[2] == g.score[2] and D.over(back))
        -- Undo all the way back to the start.
        while G.undo(g) do end
        assert(g.turn == 2 and g.score[1] == 0 and g.score[2] == 0)
        for b = 1, g.nb do assert(g.sides[b] == 0 and g.owner[b] == 0) end
    end
    assert(G.decode("garbage") == nil and G.decode("3,1,1,1") == nil, "bad saves are refused")
end

local function safeExists(g)
    for l = 1, g.L do
        if g.lines[l] == 0 and g.sides[g.b1:byte(l)] < 2 and g.sides[g.b2:byte(l)] < 2 then return true end
    end
    return false
end

local function captureExists(g)
    for b = 1, g.nb do
        if g.sides[b] == 3 then return true end
    end
    return false
end

-- Plays a whole game between two levels (0 = random lines); returns the game.
local function game(n, lv1, lv2, first)
    local g = G.new(n, first)
    local levels = { lv1, lv2 }
    while not D.over(g) do
        local lv = levels[g.turn]
        local l
        if lv == 0 then
            repeat l = rand(g.L) until g.lines[l] == 0
        else
            local captures, safe = captureExists(g), safeExists(g)
            l = AI.choose(g, lv, rand)
            assert(l and D.canPlay(g, l), "AI plays a legal line")
            if lv >= 2 and not captures and safe then
                -- Normal and Hard never hand over a box while a safe line exists.
                assert(g.sides[g.b1:byte(l)] < 2 and g.sides[g.b2:byte(l)] < 2, "no sacrifice while safe lines exist")
            end
            if lv == 2 and captures then
                local a, b = g.b1:byte(l), g.b2:byte(l)
                assert(g.sides[a] == 3 or g.sides[b] == 3, "Normal takes free boxes")
            end
        end
        D.play(g, l)
    end
    return g
end

-- Every level finishes games on every size, against random play and itself.
do
    for n = 3, 6 do
        for lv = 1, 3 do
            local g = game(n, lv, 0, 1 + n % 2)
            assert(D.over(g))
            g = game(n, lv, lv, 1)
            assert(D.over(g))
        end
    end
end

-- Takes a free box: a 3-sided box on offer is always taken (Easy, Normal).
do
    local g = G.new(4, 1)
    local s = sidesOf(g, 1, 1)
    playAll(g, { s[1], s[2], s[3] })
    for lv = 1, 2 do
        local l = AI.choose(g, lv, rand)
        assert(l == s[4], "takes the box")
    end
end

-- Hard declines the last two boxes of a long chain to keep control. On a
-- 4x4 board with every horizontal line drawn, each row is a 4-chain from
-- edge to edge; once the first row is opened, taking it all would leave
-- Hard to open the next one, so it takes two and hands back the last two.
do
    local g = G.new(4, 1)
    for r = 0, 4 do
        for c = 0, 3 do
            g.turn = 1
            D.play(g, h(g, r, c))
        end
    end
    g.turn = 1
    D.play(g, v(g, 0, 0)) -- player 1 opens the top row at its left end
    assert(g.turn == 2 and captureExists(g))
    D.play(g, assert(AI.choose(g, 3, rand) == v(g, 0, 1) and v(g, 0, 1), "takes the first box"))
    D.play(g, assert(AI.choose(g, 3, rand) == v(g, 0, 2) and v(g, 0, 2), "takes the second box"))
    assert(AI.choose(g, 3, rand) == v(g, 0, 4), "declines the last two boxes: draws the far edge")
    D.play(g, v(g, 0, 4))
    assert(g.turn == 1, "declining ends the turn")
    -- Normal (player 1) takes the two and has to open the next row.
    while not D.over(g) do D.play(g, AI.choose(g, g.turn == 2 and 3 or 2, rand)) end
    assert(g.score[2] >= 10, "control wins the rows: " .. g.score[2] .. "-" .. g.score[1])
end

-- Hard beats Normal in a sample of games (alternating who starts).
do
    local hardWins, normalWins = 0, 0
    for i = 1, 24 do
        local n = 3 + i % 3
        local first = 1 + i % 2
        local g = game(n, 3, 2, first)
        local w = D.winner(g)
        if w == 1 then hardWins = hardWins + 1 elseif w == 2 then normalWins = normalWins + 1 end
    end
    print(string.format("Hard vs Normal: %d-%d", hardWins, normalWins))
    assert(hardWins > normalWins * 2, "Hard should clearly beat Normal")
end

-- Normal beats Easy, Easy beats random lines.
do
    local nw, ew, er, rw = 0, 0, 0, 0
    for i = 1, 20 do
        local g = game(4, 2, 1, 1 + i % 2)
        if D.winner(g) == 1 then nw = nw + 1 elseif D.winner(g) == 2 then ew = ew + 1 end
        g = game(4, 1, 0, 1 + i % 2)
        if D.winner(g) == 1 then er = er + 1 elseif D.winner(g) == 2 then rw = rw + 1 end
    end
    print(string.format("Normal vs Easy: %d-%d, Easy vs random: %d-%d", nw, ew, er, rw))
    assert(nw > ew and er > rw, "levels are ordered")
end

-- The Hard search stays within its node cap.
do
    local g = G.new(6, 1)
    local maxNodes = 0
    while not D.over(g) do
        local l = AI.choose(g, 3, rand)
        maxNodes = math.max(maxNodes, AI.nodes())
        D.play(g, l)
    end
    assert(maxNodes <= 801, "node cap")
    print("Hard 6x6 max search nodes: " .. maxNodes)
end

print("dots: all tests passed")
