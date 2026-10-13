-- Logic tests for apps/tictactoe/logic.lua (run by ctest: LuaAppLogic_tictactoe).
local T = dofile(APPS .. "/tictactoe/logic.lua")

local function play(g, cells)
    for _, i in ipairs(cells) do assert(T.play(g, i), "play failed in cell " .. i) end
end

local function copy(g)
    local c = { cells = {}, moves = g.moves, turn = g.turn, winner = g.winner, line = g.line }
    for i = 1, 9 do c.cells[i] = g.cells[i] end
    return c
end

-- X moves first, turns alternate, a taken cell refuses a mark.
do
    local g = T.new()
    assert(g.turn == 1)
    assert(T.play(g, 5) and g.turn == 2 and g.cells[5] == 1)
    assert(not T.play(g, 5), "taken cell")
    assert(not T.play(g, 0) and not T.play(g, 10), "off the board")
end

-- Every one of the eight lines wins, with the line reported.
do
    for _, l in ipairs(T.LINES) do
        local g = T.new()
        -- X on the line, O on cells away from it, interleaved.
        local others = {}
        for i = 1, 9 do
            if i ~= l[1] and i ~= l[2] and i ~= l[3] then others[#others + 1] = i end
        end
        local oPlays, found = {}, 0
        for _, i in ipairs(others) do
            -- O must not make a line of its own with two cells.
            if found < 2 then oPlays[#oPlays + 1] = i found = found + 1 end
        end
        play(g, { l[1], oPlays[1], l[2], oPlays[2], l[3] })
        assert(g.winner == 1 and g.line == l, "line " .. table.concat(l, ","))
        assert(not T.play(g, others[3]), "no moves after a win")
    end
end

-- A full board without a line is a draw.
do
    local g = T.new()
    play(g, { 1, 2, 3, 5, 4, 6, 8, 7, 9 })
    assert(g.winner == -1, "draw expected")
end

-- Perfect play: X takes a win in one, blocks a loss in one.
do
    local g = T.new()
    play(g, { 1, 4, 2, 5 }) -- X: 1, 2; O: 4, 5; X to move wins at 3
    local best = T.bestMoves(g)
    assert(#best == 1 and best[1] == 3, "win in one")

    g = T.new()
    play(g, { 1, 5, 2 }) -- O to move must block at 3
    best = T.bestMoves(g)
    assert(#best == 1 and best[1] == 3, "block")
end

-- Hard never loses, whoever starts: try every reply of the other side.
do
    local function rand(n) return 1 end
    local losses = 0
    local function explore(g, deviceMark)
        if g.winner ~= 0 then
            if g.winner > 0 and g.winner ~= deviceMark then losses = losses + 1 end
            return
        end
        if g.turn == deviceMark then
            -- every perfect choice, not only the first
            for _, i in ipairs(T.bestMoves(g)) do
                local c = copy(g)
                T.play(c, i)
                explore(c, deviceMark)
            end
        else
            for i = 1, 9 do
                if g.cells[i] == 0 then
                    local c = copy(g)
                    T.play(c, i)
                    explore(c, deviceMark)
                end
            end
        end
    end
    explore(T.new(), 1)
    explore(T.new(), 2)
    assert(losses == 0, "Hard lost " .. losses .. " games")
    -- deviceMove at Hard picks one of the perfect cells.
    local g = T.new()
    play(g, { 1, 4, 2, 5 })
    assert(T.deviceMove(g, 3, rand) == 3)
end

-- Every level returns a legal move, and Normal always wins or blocks.
do
    math.randomseed(7)
    local function rand(n) return math.random(1, n) end
    for level = 1, 3 do
        for _ = 1, 200 do
            local g = T.new()
            while g.winner == 0 do
                local i = T.deviceMove(g, level, rand)
                assert(i and T.canPlay(g, i), "illegal move at level " .. level)
                T.play(g, i)
            end
        end
    end
    local g = T.new()
    play(g, { 1, 5, 2 })
    for _ = 1, 50 do assert(T.deviceMove(g, 2, rand) == 3, "Normal blocks") end
    g = T.new()
    play(g, { 1, 4, 2, 5 })
    for _ = 1, 50 do assert(T.deviceMove(g, 2, rand) == 3, "Normal wins") end
end

print("tictactoe: all tests passed")
