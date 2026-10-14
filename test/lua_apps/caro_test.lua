-- Logic tests for apps/caro/logic.lua and ai.lua (run by ctest: LuaAppLogic_caro).
local C = dofile(APPS .. "/caro/logic.lua")
local AI = dofile(APPS .. "/caro/ai.lua")()

-- math.random, seeded: Lua here has 32-bit integers, so a hand-rolled LCG overflows.
math.randomseed(5)
local function rand(n) return math.random(1, n) end
local N = C.N

-- A game from rows of text ("X", "O", "."), row 0 first; `turn` to move.
local function setup(rows, turn)
    local g = C.new()
    for r, s in pairs(rows) do
        for c = 1, #s do
            local ch = s:sub(c, c)
            if ch == "X" or ch == "O" then
                g.b[C.idx(r, c - 1)] = ch == "X" and C.X or C.O
                g.n = g.n + 1
                g.hist[g.n] = C.idx(r, c - 1)
            end
        end
    end
    g.turn = turn
    return g
end

-- Plays cells given as { r, c } pairs alternately.
local function play(g, cells)
    for _, rc in ipairs(cells) do assert(C.play(g, C.idx(rc[1], rc[2])), "play failed at " .. rc[1] .. "," .. rc[2]) end
end

-- Basics: X first, turns alternate, taken and off-board cells refused.
do
    local g = C.new()
    assert(g.turn == C.X)
    assert(C.play(g, C.idx(7, 7)) and g.turn == C.O and g.b[C.idx(7, 7)] == C.X)
    assert(not C.play(g, C.idx(7, 7)), "taken cell")
    assert(not C.play(g, 0) and not C.play(g, N * N + 1), "off the board")
end

-- Five in a row wins in all four directions, at the edges and corners too,
-- with the line reported; four do not.
do
    local dirs = { { 0, 1 }, { 1, 0 }, { 1, 1 }, { 1, -1 } }
    local starts = {
        { { 0, 0 }, { 7, 3 }, { 14, 10 } },          -- across
        { { 0, 0 }, { 3, 14 }, { 10, 6 } },           -- down
        { { 0, 0 }, { 10, 10 }, { 5, 2 } },          -- down-right
        { { 0, 14 }, { 10, 4 }, { 4, 9 } },          -- down-left
    }
    for k, d in ipairs(dirs) do
        for _, s in ipairs(starts[k]) do
            for last = 1, 5 do -- the winning mark anywhere in the line
                local g = C.new()
                local line = {}
                for t = 0, 4 do line[#line + 1] = C.idx(s[1] + t * d[1], s[2] + t * d[2]) end
                -- O plays far from the line, in the middle rows away from it.
                local os, oi = {}, 0
                for i = 1, N * N do
                    local onLine = false
                    for _, j in ipairs(line) do if i == j then onLine = true end end
                    if not onLine and #os < 4 and i % 7 == 3 then os[#os + 1] = i end
                end
                local order = {}
                for t = 1, 5 do if t ~= last then order[#order + 1] = line[t] end end
                order[#order + 1] = line[last]
                for t = 1, 5 do
                    assert(g.winner == 0, "won too early")
                    assert(C.play(g, order[t]))
                    if t < 5 then
                        oi = oi + 1
                        assert(C.play(g, os[oi]))
                    end
                end
                assert(g.winner == C.X, "five should win: dir " .. k)
                assert(#g.line == 5, "the winning line")
                assert(not C.play(g, os[4] + 1), "no moves after a win")
            end
        end
    end
end

-- An overline (six) wins too (free-style); a broken five does not.
do
    local g = setup({ [7] = "..XXX.XX......." }, C.X)
    assert(C.play(g, C.idx(7, 5)) and g.winner == C.X and #g.line == 6, "six in a row wins")
    g = setup({ [7] = "..XX.XX.X......" }, C.X)
    assert(C.play(g, C.idx(7, 7)) and g.winner == 0, "X X X X . X is not a five")
end

-- A full board without a five is a draw: a pattern with no five anywhere.
do
    local g = C.new()
    local function mark(r, c) return (c + 2 * r) % 4 < 2 and C.X or C.O end
    -- Play the cells in an order that alternates the marks.
    local xs, os = {}, {}
    for r = 0, N - 1 do
        for c = 0, N - 1 do
            if mark(r, c) == C.X then xs[#xs + 1] = C.idx(r, c) else os[#os + 1] = C.idx(r, c) end
        end
    end
    assert(#xs == #os + 1 or #xs == #os, "pattern balance " .. #xs .. "/" .. #os)
    if #xs < #os then xs, os = os, xs end
    for k = 1, #xs do
        assert(C.play(g, xs[k]))
        if os[k] then assert(C.play(g, os[k])) end
    end
    assert(g.winner == -1, "draw expected, got " .. g.winner)
end

-- Take back and the saved form.
do
    local g = C.new()
    play(g, { { 7, 7 }, { 7, 8 }, { 8, 8 } })
    local s = C.encode(g)
    local h = C.decode(s)
    assert(h and h.n == 3 and h.turn == C.O and h.b[C.idx(8, 8)] == C.X, "decode")
    C.undo(g, 2)
    assert(g.n == 1 and g.turn == C.O and g.b[C.idx(7, 8)] == 0 and g.b[C.idx(8, 8)] == 0, "undo")
    assert(C.decode("zz") == nil and C.decode("7171") == nil, "bad saves refused")
    -- Undoing a win plays on.
    g = setup({ [7] = "..XXXX........." }, C.X)
    C.play(g, C.idx(7, 6))
    assert(g.winner == C.X)
    C.undo(g, 1)
    assert(g.winner == 0 and g.line == nil and g.turn == C.X)
end

-- Every level completes its own five, even with the other side's four on
-- the board, and blocks the other side's four.
do
    for level = 1, 3 do
        -- O (to move) has four; X has four too: O must win, not block.
        local g = setup({ [3] = "...XXXX........", [9] = "....OOOO......." }, C.O)
        local i = AI.choose(g, level, rand)
        assert(i == C.idx(9, 3) or i == C.idx(9, 8), "complete five, level " .. level)
        -- O to move, X has a closed four: block it.
        g = setup({ [5] = "..OXXXX........", [9] = "....OO........." }, C.O)
        i = AI.choose(g, level, rand)
        assert(i == C.idx(5, 7), "block four, level " .. level)
        -- A broken four (X X . X X) too, along a diagonal.
        g = setup({ [2] = "..X............", [3] = "...X...........", [5] = ".....X.........",
                    [6] = "......X........", [10] = "OO.............", [11] = "O.............." }, C.O)
        i = AI.choose(g, level, rand)
        assert(i == C.idx(4, 4), "block a broken four, level " .. level)
    end
    -- Normal and Hard stop an open three, Hard plays an open four.
    for level = 2, 3 do
        local g = setup({ [7] = "....XXX........", [12] = "OO............." }, C.O)
        local i = AI.choose(g, level, rand)
        assert(i == C.idx(7, 3) or i == C.idx(7, 7) or i == C.idx(7, 2) or i == C.idx(7, 8),
               "block an open three, level " .. level)
    end
    local g = setup({ [7] = "....XXX........", [1] = "OO.....O......." }, C.X)
    local i = AI.choose(g, 3, rand)
    assert(i == C.idx(7, 3) or i == C.idx(7, 7), "make an open four")
end

-- Threat search: a four-three wins by fours; a double open three by threats.
do
    local g = setup({ [7] = ".....XX........", [9] = ".......X.......", [10] = ".......X.......",
                      [0] = "O...O...O......", [14] = "..O............" }, C.X)
    local v = AI.vct(g.b, C.X, 3, 1000000)
    assert(v == C.idx(7, 7), "double three found")
    g = setup({ [4] = "....XXX........", [5] = ".......X.......", [6] = ".......X.......",
                [3] = "...O...........", [0] = "O...O...O......" }, C.X)
    local i = AI.vcf(g.b, C.X, 10)
    assert(i, "a win by fours")
    assert(AI.vcf(C.new().b, C.X, 10) == nil, "nothing on an empty board")
end

-- Games end, every move legal; Hard beats Easy most of the time (each side
-- starting half the games), and its moves stay within the node budget.
do
    local function game(lx, lo)
        local g = C.new()
        local guard = 0
        while g.winner == 0 do
            local lv = g.turn == C.X and lx or lo
            local i = AI.choose(g, lv, rand)
            assert(C.play(g, i), "illegal device move")
            guard = guard + 1
            assert(guard <= N * N, "game did not end")
        end
        return g.winner
    end
    local hardWins = 0
    for k = 1, 8 do
        local w = k % 2 == 1 and game(3, 1) or game(1, 3)
        if (k % 2 == 1 and w == C.X) or (k % 2 == 0 and w == C.O) then hardWins = hardWins + 1 end
    end
    assert(hardWins >= 6, "Hard should beat Easy, won " .. hardWins .. "/8")
    -- Normal against Hard also finishes.
    local w = game(2, 3)
    assert(w ~= 0)
end

print("caro: all tests passed")
