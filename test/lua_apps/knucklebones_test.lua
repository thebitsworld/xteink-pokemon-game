-- Logic tests for apps/knucklebones/logic.lua (run by ctest: LuaAppLogic_knucklebones).
local K = dofile(APPS .. "/knucklebones/logic.lua")

-- A die stream from a list, so every roll is known.
local function stream(list)
    local i = 0
    return function(n)
        i = i + 1
        local v = list[(i - 1) % #list + 1]
        assert(v >= 1 and v <= n)
        return v
    end
end

-- Column scores: each value counts value * count * count, wherever it sits.
assert(K.columnScore({}) == 0)
assert(K.columnScore({ 4, 4, 4 }) == 36)
assert(K.columnScore({ 3, 5, 3 }) == 17)
assert(K.columnScore({ 2, 2, 6 }) == 14)
assert(K.columnScore({ 1, 2, 3 }) == 6)

-- Placing removes every matching die in the facing column, only there.
do
    local g = K.new(stream({ 5 }), 1)
    g.grid[2][1] = { 5, 2, 5 }
    g.grid[2][2] = { 5 }
    assert(g.die == 5)
    assert(K.wouldRemove(g, 1, 5) == 2)
    assert(K.place(g, 1, stream({ 3 })))
    assert(#g.grid[1][1] == 1 and g.grid[1][1][1] == 5)
    assert(#g.grid[2][1] == 1 and g.grid[2][1][1] == 2, "both 5s removed, the 2 closes up")
    assert(#g.grid[2][2] == 1, "other columns untouched")
    assert(g.last.removed == 2 and g.turn == 2 and g.die == 3)
end

-- A full column refuses another die; the game ends the moment a grid fills.
do
    local g = K.new(stream({ 1 }), 1)
    g.grid[1] = { { 6, 6, 6 }, { 6, 6, 6 }, { 6, 6 } }
    assert(not K.canPlace(g, 1))
    assert(not K.place(g, 1, stream({ 1 })))
    assert(K.place(g, 3, stream({ 1 })))
    assert(g.winner == 1, "player 1 filled the grid while ahead")
    assert(not K.place(g, 2, stream({ 1 })), "no moves after the end")

    -- Filling your own grid while behind loses.
    g = K.new(stream({ 1 }), 1)
    g.grid[1] = { { 1, 2, 3 }, { 1, 2, 3 }, { 1, 2 } }
    g.grid[2] = { { 6, 6, 6 }, {}, {} }
    K.place(g, 3, stream({ 1 }))
    assert(g.winner == 2)

    -- Equal totals are a draw.
    g = K.new(stream({ 2 }), 1)
    g.grid[1] = { { 1, 1, 1 }, { 1, 1, 1 }, { 1, 1 } }
    g.grid[2] = { { 3, 3 }, { 1, 1 }, { 2 } }
    -- player 1: 9 + 9 + (1,1,2: 4 + 2 = 6) = 24; player 2: 12 + 4 + 0 (the 2 is removed) = 16
    K.place(g, 3, stream({ 1 }))
    assert(g.winner == 1 and K.score(g, 1) == 24 and K.score(g, 2) == 16)
    g = K.new(stream({ 2 }), 1)
    g.grid[1] = { { 1, 1, 1 }, { 1, 1, 1 }, { 1, 1 } }
    g.grid[2] = { { 3, 3 }, { 3, 3 }, {} }
    -- player 1: 9 + 9 + 6 = 24; player 2: 12 + 12 = 24
    K.place(g, 3, stream({ 1 }))
    assert(g.winner == -1, "equal totals draw")
end

-- The greedy opponent knocks out a big stack; the deep one also does.
do
    for level = 2, 3 do
        local g = K.new(stream({ 6 }), 2)
        g.grid[1] = { { 1 }, { 6, 6 }, { 2 } }
        g.grid[2] = { {}, {}, {} }
        assert(K.chooseColumn(g, level, stream({ 1 })) == 2, "level " .. level .. " removes the two 6s")
    end
    -- It stacks onto a matching die when nothing can be removed.
    local g = K.new(stream({ 4 }), 2)
    g.grid[1] = { { 1 }, { 2 }, { 3 } }
    g.grid[2] = { {}, { 4 }, {} }
    assert(K.chooseColumn(g, 2, stream({ 1 })) == 2, "stack the 4s")
end

-- The deep opponent avoids filling its grid while behind.
do
    local g = K.new(stream({ 1 }), 2)
    g.grid[2] = { { 1, 2, 3 }, { 1, 2, 3 }, { 1, 2 } }
    g.grid[1] = { { 6, 6 }, { 5 }, {} }
    -- Its only open column is 3, so it must play there and lose.
    assert(K.chooseColumn(g, 3, stream({ 1 })) == 3)
end

-- The opponent never looks at the die stream: two different futures, same move.
do
    for level = 2, 3 do
        local a = K.new(stream({ 3 }), 2)
        a.grid[1] = { { 3 }, { 5, 5 }, {} }
        a.grid[2] = { { 3 }, {}, { 1 } }
        local b = K.new(stream({ 3 }), 2)
        b.grid = a.grid
        assert(K.chooseColumn(a, level, stream({ 1 })) == K.chooseColumn(b, level, stream({ 6 })))
    end
end

-- A whole game against itself always ends with a sensible result.
do
    -- math.random, seeded: Lua here has 32-bit integers, so a hand-rolled LCG overflows.
    math.randomseed(11)
    local function rand(n) return math.random(1, n) end
    for _ = 1, 50 do
        local g = K.new(rand, 1)
        local moves = 0
        while g.winner == 0 do
            local c = K.chooseColumn(g, g.turn == 1 and 3 or 1, rand)
            assert(c and K.place(g, c, rand))
            moves = moves + 1
            assert(moves < 200)
        end
        assert(g.winner == -1 or g.winner == 1 or g.winner == 2)
    end
end

-- Save and restore round-trip; a corrupt save is refused.
do
    local g = K.new(stream({ 4, 2, 6 }), 1)
    K.place(g, 1, stream({ 2 }))
    K.place(g, 3, stream({ 6 }))
    local s = K.serialize(g)
    local back = K.deserialize(s)
    assert(back and K.serialize(back) == s and back.turn == g.turn and back.die == g.die)
    assert(K.deserialize("1|4|0|1.2.3.4|||||") == nil, "four dice in a column")
    assert(K.deserialize("3|4|0||||||") == nil, "bad turn")
    assert(K.deserialize("1|9|0||||||") == nil, "bad die")
end

print("knucklebones logic ok")
