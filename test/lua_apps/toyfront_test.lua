-- Tests for apps/toyfront (run by ctest: LuaAppLogic_toyfront).
local T = dofile(APPS .. "/toyfront/logic.lua")
local AI = dofile(APPS .. "/toyfront/ai.lua")(T)

math.randomseed(8)
local function rand(n) return math.random(1, n) end

-- The boards: 16 medals of 1 to 3, walls between neighbours.
for _, b in ipairs(T.BOARDS) do
    assert(#b.medals == 16 and b.medals:match("^[123]+$"), b.name)
    for _, wl in ipairs(b.walls) do
        local d = math.abs(wl[1] - wl[2])
        assert(d == 1 or d == T.SIZE, b.name .. ": a wall between neighbours")
    end
end

-- A game on the open board with chosen hands.
local function game(hand1, hand2)
    local g = T.new(1, rand)
    g.hands = { hand1, hand2 }
    g.decks = { {}, {} }
    return g
end

-- Where toys may go.
do
    local g = game({ 3, 3, 3 }, { 2, 2, 2 })
    assert(T.canPlay(g, 1, T.idx(4, 2)) and not T.canPlay(g, 1, T.idx(3, 2)), "home row first")
    assert(T.canPlay(g, 2, T.idx(1, 4)) and not T.canPlay(g, 2, T.idx(4, 4)))
    assert(T.play(g, 1, T.idx(4, 2)) == "build" and g.owner[T.idx(4, 2)] == 1 and g.strength[T.idx(4, 2)] == 3)
    assert(g.turn == 2)
    assert(T.canPlay(g, 1, T.idx(3, 2)), "joined to a base you hold")
    assert(not T.canPlay(g, 1, T.idx(3, 3)), "not diagonally")
    -- A wall cuts the path (River: no path between 6 and 10).
    local r = T.new(2, rand)
    r.owner[10] = 1
    assert(not T.joined(r, 6, 10) and not T.canPlay(r, 1, 6))
end

-- Boosting, attacking.
do
    local g = game({ 6, 6, 1 }, { 4, 4, 4 })
    local a, b = T.idx(4, 1), T.idx(3, 1)
    g.owner[a], g.strength[a] = 1, 5
    g.owner[b], g.strength[b] = 2, 4
    assert(T.play(g, 1, a) == "boost" and g.strength[a] == T.MAX_STRENGTH, "strength stops at 9")
    g.turn = 1
    assert(T.play(g, 1, b) == "take" and g.owner[b] == 1 and g.strength[b] == 2, "6 takes a 4, keeping 2")
    g.turn, g.owner[b], g.strength[b] = 1, 2, 4
    assert(T.play(g, 1, b) == "attack" and g.owner[b] == 2 and g.strength[b] == 3, "1 wears a 4 down to 3")
    local h = game({ 4 }, { 1 })
    h.owner[a], h.strength[a] = 1, 1
    h.owner[b], h.strength[b] = 2, 4
    assert(T.play(h, 1, b) == "even" and h.owner[b] == 0 and h.strength[b] == 0, "equal toys leave it empty")
end

-- Giving up a toy only with nowhere to play; the end and the winner.
do
    local g = game({ 2 }, { 5 })
    for c = 1, T.SIZE do g.owner[T.idx(4, c)], g.strength[T.idx(4, c)] = 2, 9 end
    for c = 1, T.SIZE do g.owner[T.idx(3, c)], g.strength[T.idx(3, c)] = 2, 9 end
    assert(not T.hasMove(g, 1) and T.discard(g, 1) and #g.hands[1] == 0 and g.turn == 2)
    assert(T.play(g, 1, T.idx(2, 1)) == "build")
    assert(g.over and T.winner(g) == 2)
    local h = game({ 2 }, { 5 })
    assert(not T.discard(h, 1), "a toy that can be played cannot be given up")
end

-- Saving.
do
    local g = T.new(4, rand, 2)
    T.play(g, 1, T.idx(1, 2))
    local s = T.serialize(g)
    local h = T.deserialize(s)
    assert(h and h.board == 4 and h.turn == 1 and T.serialize(h) == s)
    assert(T.deserialize("") == nil and T.deserialize("9|1|0|||1|1") == nil)
end

-- The device against itself: every game ends, and Normal beats Easy.
do
    local normalWins, easyWins = 0, 0
    for game = 1, 200 do
        local g = T.new(rand(#T.BOARDS), rand, game % 2 + 1)
        local normalSide = game % 3 == 0 and 2 or 1
        local guard = 0
        while not g.over do
            guard = guard + 1
            assert(guard < 60, "the game ends")
            local k, i = AI.choose(g, g.turn == normalSide and 2 or 1, rand)
            if i then assert(T.play(g, k, i)) else assert(T.discard(g, k)) end
        end
        local wn = T.winner(g)
        if wn == normalSide then normalWins = normalWins + 1 elseif wn ~= 0 then easyWins = easyWins + 1 end
    end
    print(string.format("Normal won %d, Easy %d of 200", normalWins, easyWins))
    assert(normalWins > easyWins * 3)
end

print("toyfront ok")
