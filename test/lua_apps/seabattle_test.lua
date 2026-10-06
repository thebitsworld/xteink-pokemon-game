-- Logic tests for apps/seabattle/logic.lua (run by ctest: LuaAppLogic_seabattle).
local S = dofile(APPS .. "/seabattle/logic.lua")

-- math.random, seeded: Lua here has 32-bit integers, so a hand-rolled LCG overflows.
math.randomseed(77)
local function rand(n) return math.random(1, n) end

-- Random fleets: five ships of the right lengths, on the grid, never touching.
do
    for _ = 1, 200 do
        local f = S.randomFleet(rand)
        assert(#f.ships == 5)
        local squares = 0
        for n, ship in ipairs(f.ships) do
            assert(ship.len == S.FLEET[n])
            for _, i in ipairs(S.shipSquares(ship)) do
                assert(S.at(f.occ, i) == n)
                squares = squares + 1
                -- No square of another ship around it.
                local c, r = S.colOf(i), S.rowOf(i)
                for dr = -1, 1 do
                    for dc = -1, 1 do
                        local cc, rr = c + dc, r + dr
                        if cc >= 1 and cc <= S.N and rr >= 1 and rr <= S.N then
                            local o = S.at(f.occ, S.index(cc, rr))
                            assert(o == 0 or o == n, "ships touch")
                        end
                    end
                end
            end
        end
        local occupied = 0
        for i = 1, S.N * S.N do if S.at(f.occ, i) ~= 0 then occupied = occupied + 1 end end
        assert(squares == 17 and occupied == 17)
    end
end

-- Firing: miss, hit, sunk (its neighbours become water), turns alternate, the
-- same square cannot be shot twice, and sinking the last ship wins.
do
    local g = S.new(rand)
    local enemy = g.fleets[2]
    local water = nil
    for i = 1, S.N * S.N do
        if S.at(enemy.occ, i) == 0 and not water then water = i end
    end
    assert(S.fire(g, water) == "miss" and S.at(g.shots[1], water) == S.MISS and g.turn == 2)
    assert(S.fire(g, 1) ~= nil and g.turn == 1)
    assert(S.fire(g, water) == nil, "already shot")
    -- Sink the destroyer (slot 5) square by square.
    local squares = S.shipSquares(enemy.ships[5])
    assert(S.fire(g, squares[1]) == "hit")
    S.fire(g, (S.at(g.shots[2], 2) == S.UNKNOWN) and 2 or 3) -- the device's turn
    local result, ship = S.fire(g, squares[2])
    assert(result == "sunk" and ship == enemy.ships[5])
    for _, i in ipairs(squares) do assert(S.at(g.shots[1], i) == S.SUNK) end
    local c, r = S.colOf(squares[1]), S.rowOf(squares[1])
    for dr = -1, 1 do
        for dc = -1, 1 do
            local cc, rr = c + dc, r + dr
            if cc >= 1 and cc <= S.N and rr >= 1 and rr <= S.N then
                assert(S.at(g.shots[1], S.index(cc, rr)) ~= S.UNKNOWN, "around a sunk ship is known water")
            end
        end
    end
    local afloat = S.afloat(g, 1)
    assert(afloat[1] and not afloat[5])

    -- Player 1 sinks everything (player 2 keeps firing at its own squares).
    g = S.new(rand)
    local p2 = 0
    for _, ship in ipairs(g.fleets[2].ships) do
        for _, i in ipairs(S.shipSquares(ship)) do
            if S.at(g.shots[1], i) == S.UNKNOWN then
                S.fire(g, i)
                if g.winner == 0 then
                    p2 = p2 + 1
                    S.fire(g, p2)
                end
            end
        end
    end
    assert(g.winner == 1 and S.fire(g, 100) == nil)
end

-- The computer: never shoots the same square twice, finishes a ship it has
-- hit, and the levels get better at it (fewer shots to sink a fleet).
local function shotsToWin(level, games)
    local total = 0
    for _ = 1, games do
        local g = S.new(rand)
        local shots = 0
        while g.winner == 0 do
            g.turn = 1
            local i = S.chooseShot(g, level, rand)
            assert(S.at(g.shots[1], i) == S.UNKNOWN, "a square already shot")
            S.fire(g, i)
            shots = shots + 1
            assert(shots <= 100)
        end
        total = total + shots
    end
    return total / games
end

do
    local easy, normal, hard = shotsToWin(1, 30), shotsToWin(2, 30), shotsToWin(3, 30)
    print(string.format("shots to win: easy %.1f normal %.1f hard %.1f", easy, normal, hard))
    assert(easy > normal and normal > hard, "levels in order")
    assert(hard < 55, "the probability player is quick")

    -- After a hit the normal player fires next to it.
    local g = S.new(rand)
    local ship = g.fleets[2].ships[1]
    local first = S.shipSquares(ship)[2]
    S.fire(g, first)
    g.turn = 1
    local i = S.chooseShot(g, 2, rand)
    local dc, dr = math.abs(S.colOf(i) - S.colOf(first)), math.abs(S.rowOf(i) - S.rowOf(first))
    assert(dc + dr == 1, "fires next to the hit")
end

-- Save and restore round-trip; a corrupt save is refused.
do
    local g = S.new(rand)
    for _ = 1, 20 do S.fire(g, S.chooseShot(g, 3, rand)) end
    local s = S.serialize(g)
    local back = S.deserialize(s)
    assert(back and S.serialize(back) == s and back.turn == g.turn)
    assert(S.deserialize(s:gsub("^%d", "3")) == nil, "bad turn")
    local bad = s:gsub("(|)(%d+)%.(%d+)%.1%.5", "%11.1.1.6", 1)
    assert(S.deserialize(bad) == nil, "a ship of the wrong length")
end

print("seabattle logic ok")
