-- Logic tests for apps/hexland: logic.lua, rules.lua, setup.lua, ai.lua and save.lua (run by ctest: LuaAppLogic_hexland).
local L = dofile(APPS .. "/hexland/rules.lua")(dofile(APPS .. "/hexland/logic.lua"))
local Setup = dofile(APPS .. "/hexland/setup.lua")(L)
local A = dofile(APPS .. "/hexland/ai.lua")(L)
local Save = dofile(APPS .. "/hexland/save.lua")(L)

-- math.random, seeded: Lua here has 32-bit integers, so a hand-rolled LCG overflows.
math.randomseed(31)

local ROAD, SETTLE, CITY = L.ROAD, L.SETTLE, L.CITY

-- Geometry: 19 hexes, 54 corners, 72 edges, 30 coast edges, 9 ports.
do
    local maxV, maxE = 0, 0
    for h = 1, 19 do
        local seen = {}
        for k = 1, 6 do
            local v = L.hv(h, k)
            assert(v >= 1 and not seen[v], "six different corners")
            seen[v] = true
            if v > maxV then maxV = v end
        end
    end
    for e = 1, 72 do
        local a, b = L.ends(e)
        assert(a >= 1 and b >= 1 and a ~= b)
        maxE = e
    end
    assert(maxV == 54 and maxE == 72 and #L.EV == 144)
    local coast = 0
    for e = 1, 72 do
        local a, b = L.ends(e)
        local n = 0
        for i = 1, 3 do
            for j = 1, 3 do
                if L.vh(a, i) > 0 and L.vh(a, i) == L.vh(b, j) then n = n + 1 end
            end
        end
        assert(n == 1 or n == 2)
        if n == 1 then coast = coast + 1 end
    end
    assert(coast == 30, "30 coast edges")
    local ports = 0
    for v = 1, 54 do
        local deg = 0
        for k = 1, 3 do
            if L.vn(v, k) > 0 then deg = deg + 1 end
        end
        assert(deg == 2 or deg == 3)
        if L.VP:byte(v) > 0 then ports = ports + 1 end
    end
    assert(ports == 18, "nine ports, two corners each, none shared")
end

-- A fresh game; tests then place pieces by hand.
local function fresh(starter)
    local g = Setup.new(1, starter, 1)
    g.phase, g.sstep, g.turn = "main", 9, 1
    return g
end
local function edgeBetween(a, b)
    for k = 1, 3 do
        local e = L.ve(a, k)
        if e > 0 then
            local x, y = L.ends(e)
            if (x == a and y == b) or (x == b and y == a) then return e end
        end
    end
end
local function give(g, p, wd, br, wl, gr, ore)
    local r = g.pl[p].r
    r[1], r[2], r[3], r[4], r[5] = wd, br, wl, gr, ore
end

-- Board generation: the usual resources and numbers, no 6/8 side by side,
-- the robber on the desert, 4 general and 5 special ports.
for i = 1, 60 do
    local g = Setup.new(1, i == 1, 1)
    local count = { [0] = 0, 0, 0, 0, 0, 0 }
    local nums = {}
    for h = 1, 19 do
        local r = L.res(g, h)
        count[r] = count[r] + 1
        if r == 0 then
            assert(g.num[h] == 0 and g.robber == h, "desert: no number, the robber")
        else
            nums[#nums + 1] = g.num[h]
        end
    end
    assert(count[0] == 1 and count[1] == 4 and count[2] == 3 and count[3] == 4 and count[4] == 4 and count[5] == 3)
    table.sort(nums)
    assert(table.concat(nums, ",") == "2,3,3,4,4,5,5,6,6,8,8,9,9,10,10,11,11,12")
    assert(Setup.hotApart(g.num), "6 and 8 apart")
    local p = {}
    for k = 1, 9 do p[#p + 1] = g.ports:sub(k, k) end
    table.sort(p)
    assert(table.concat(p) == "000012345")
    assert(#g.deck == 25 and select(2, g.deck:gsub("1", "")) == 14 and select(2, g.deck:gsub("5", "")) == 5)
end
assert(not Setup.hotApart({ 6, 8, 2, 3, 4, 5, 9, 10, 11, 12, 3, 4, 5, 9, 10, 11, 0, 2, 2 }), "hexes 1 and 2 touch")

-- Distance rule: no settlement next to another; outside the set-up it
-- needs your road.
do
    local g = fresh()
    local v = L.hv(10, 1)
    assert(L.canSettle(g, 1, v, true))
    L.build(g, 1, SETTLE, v, true)
    for k = 1, 3 do
        local u = L.vn(v, k)
        if u > 0 then assert(not L.canSettle(g, 2, u, true), "too close") end
    end
    local u = L.vn(v, 1)
    local w
    for k = 1, 3 do
        local x = L.vn(u, k)
        if x > 0 and x ~= v then w = x end
    end
    assert(L.canSettle(g, 1, w, true) and not L.canSettle(g, 1, w, false), "two away: needs a road")
    L.build(g, 1, ROAD, edgeBetween(v, u), true)
    L.build(g, 1, ROAD, edgeBetween(u, w), true)
    assert(L.canSettle(g, 1, w, false) and not L.canSettle(g, 2, w, false))
end

-- Road connectivity: roads join your network, not through another's settlement.
do
    local g = fresh()
    local a = L.hv(10, 1)
    L.build(g, 1, SETTLE, a, true)
    local spots = L.spots(g, 1, ROAD)
    for _, e in ipairs(spots) do
        local x, y = L.ends(e)
        assert(x == a or y == a, "only roads at the settlement")
    end
    assert(#spots == 3)
    -- Player 1 road a-b; player 2's settlement on c beyond b blocks b-c-d.
    local b = L.hv(10, 2)
    L.build(g, 1, ROAD, edgeBetween(a, b), true)
    local c = L.hv(10, 3)
    g.v[c] = 2
    local e = edgeBetween(b, c)
    assert(L.canRoad(g, 1, e), "a road up to their settlement")
    L.build(g, 1, ROAD, e, true)
    local d = L.hv(10, 4)
    assert(not L.canRoad(g, 1, edgeBetween(c, d)), "but not through it")
    assert(L.canRoad(g, 2, edgeBetween(c, d)))
end

-- Longest road: a chain, a fork, a loop, and a road broken by a settlement.
do
    local g = fresh()
    -- The ring of the centre hex: 6 roads, all of player 2.
    local ring = {}
    for k = 1, 6 do ring[k] = L.hv(10, k) end
    for k = 1, 5 do L.build(g, 2, ROAD, edgeBetween(ring[k], ring[k + 1]), true) end
    assert(L.longest(g, 2) == 5 and g.lr == 2, "five in a row take the longest road")
    L.build(g, 2, ROAD, edgeBetween(ring[6], ring[1]), true)
    assert(L.longest(g, 2) == 6, "a closed loop of six")
    -- A tail off the loop: 6 round plus 1.
    local out
    for k = 1, 3 do
        local u = L.vn(ring[1], k)
        if u > 0 and u ~= ring[2] and u ~= ring[6] then out = u end
    end
    L.build(g, 2, ROAD, edgeBetween(ring[1], out), true)
    assert(L.longest(g, 2) == 7, "loop plus tail")
    -- A settlement of player 3 on a loop corner: the loop still counts from it.
    g.v[ring[4]] = 3
    L.updateRoads(g)
    assert(L.longest(g, 2) == 6, "cut at their settlement: the six round it, from one side to the other")
    g.v[ring[4]] = 0

    -- A fork: a trunk of 3 with two branches of 2 and 1: longest 5.
    g = fresh()
    local path = { L.hv(1, 4) }
    local used = { [path[1]] = true }
    for i = 2, 6 do
        local v = path[i - 1]
        for k = 1, 3 do
            local u = L.vn(v, k)
            if u > 0 and not used[u] and not path[i] then
                path[i], used[u] = u, true
            end
        end
    end
    for i = 1, 5 do L.build(g, 3, ROAD, edgeBetween(path[i], path[i + 1]), true) end
    assert(L.longest(g, 3) == 5 and g.lr == 3)
    local mid = path[3]
    local branch
    for k = 1, 3 do
        local u = L.vn(mid, k)
        if u > 0 and u ~= path[2] and u ~= path[4] then branch = u end
    end
    L.build(g, 3, ROAD, edgeBetween(mid, branch), true)
    assert(L.longest(g, 3) == 5, "a fork does not add up both branches")
    -- Player 4 breaks the chain at path[4]: 3 + 2 left, below 5: nobody holds it.
    g.v[path[4]] = 4
    g.pl[4].ns = 1
    L.updateRoads(g)
    assert(L.longest(g, 3) == 3 and g.lr == 0, "broken road loses the longest road")
end

-- The longest road: a tie keeps the holder, a longer road takes it.
do
    local g = fresh()
    g.lr = 2
    local L5 = L.longest
    L.longest = function(_, p) return ({ 6, 6, 3, 0 })[p] end
    L.updateRoads(g)
    assert(g.lr == 2, "holder keeps it on a tie... even with 6 vs 6")
    L.longest = function(_, p) return ({ 7, 6, 3, 0 })[p] end
    L.updateRoads(g)
    assert(g.lr == 1)
    L.longest = function(_, p) return ({ 4, 6, 6, 0 })[p] end
    L.updateRoads(g)
    assert(g.lr == 0, "holder broken below the others, who tie: nobody")
    L.longest = L5
end

-- Production: settlements 1, cities 2, nothing under the robber.
do
    local g = fresh()
    local h = 1
    while L.res(g, h) == 0 do h = h + 1 end
    local n, r = g.num[h], L.res(g, h)
    for x = 1, 19 do
        if x ~= h and g.num[x] == n then g.num[x] = 0 end -- only hex h produces
    end
    g.v[L.hv(h, 1)] = 2
    g.v[L.hv(h, 4)] = 3 + 4
    local d1 = math.max(1, n - 6)
    g.robber = 10
    L.roll(g, d1, n - d1)
    assert(g.pl[2].r[r] == 1 and g.pl[3].r[r] == 2 and g.bank[r] == 16)
    assert(g.log[#g.log]:find("Ana " .. L.SH[r]) and g.log[#g.log]:find("Ben 2" .. L.SH[r]))
    g.phase, g.robber = "roll", h
    L.roll(g, d1, n - d1)
    assert(g.pl[2].r[r] == 1 and g.pl[3].r[r] == 2, "the robber blocks the hex")
    assert(g.log[#g.log]:find("nothing"))
    -- The bank short: two players want 3, it has 2: nobody gets any.
    g.robber, g.bank[r] = 10, 2
    L.roll(g, d1, n - d1)
    assert(g.pl[2].r[r] == 1 and g.pl[3].r[r] == 2 and g.bank[r] == 2)
    -- Only one player: they get what is left.
    g.v[L.hv(h, 1)] = 0
    L.roll(g, d1, n - d1)
    assert(g.pl[3].r[r] == 4 and g.bank[r] == 0)
end

-- A 7: more than seven cards discard half; you choose, the device does not.
do
    local g = fresh()
    give(g, 1, 3, 3, 3, 0, 0)  -- 9: discard 4
    give(g, 2, 8, 0, 0, 0, 0)  -- 8: discard 4
    give(g, 3, 2, 2, 2, 1, 0)  -- 7: keeps all
    L.roll(g, 3, 4)
    assert(g.phase == "discard" and g.need[1] == 4 and L.actor(g) == 1)
    assert(L.count(g, 2) == 4 and L.count(g, 3) == 7)
    assert(not L.discardOne(g, 1, 4), "none to discard")
    for _ = 1, 4 do assert(L.discardOne(g, 1, 1) or L.discardOne(g, 1, 2)) end
    assert(L.count(g, 1) == 5 and g.phase == "robber" and L.actor(g) == 1)
    -- The robber next to two players: you pick whom to rob.
    local h
    for x = 1, 19 do
        if x ~= g.robber then h = h or x end
    end
    g.v[L.hv(h, 1)], g.v[L.hv(h, 4)] = 2, 3
    L.moveRobber(g, h)
    assert(g.phase == "steal" and (g.vict == "23" or g.vict == "32"))
    L.steal(g, 3)
    assert(g.phase == "main" and L.count(g, 3) == 6 and L.count(g, 1) == 6)
end

-- Development cards.
do
    local g = fresh()
    give(g, 1, 0, 0, 1, 1, 1)
    g.deck = g.deck:sub(1, -2) .. "1"
    assert(L.canBuy(g, 1) and L.buyCard(g, 1) == 1 and g.pl[1].d[1] == 1)
    assert(not L.canPlay(g, 1, 1), "not the turn it was bought")
    L.endTurn(g)
    g.turn, g.phase = 1, "roll"
    assert(L.canPlay(g, 1, 1), "a knight before rolling")
    L.play(g, 1, 1)
    assert(g.phase == "robber" and g.after == "roll" and g.pl[1].k == 1)
    L.moveRobber(g, g.robber == 1 and 2 or 1)
    assert(g.phase == "roll")
    L.roll(g, 2, 3)
    g.pl[1].d[3], g.pl[1].d[4], g.pl[1].d[2] = 1, 1, 1
    assert(not L.canPlay(g, 1, 3), "one card a turn")
    g.played = false
    local wool = g.pl[1].r[3]
    L.play(g, 1, 3, 3, 3)
    assert(g.pl[1].r[3] == wool + 2, "Year of Plenty")
    g.played = false
    give(g, 1, 0, 0, 0, 0, 0)
    give(g, 2, 0, 0, 0, 3, 0)
    give(g, 4, 1, 0, 0, 2, 0)
    L.play(g, 1, 4, 4)
    assert(g.pl[1].r[4] == 5 and g.pl[2].r[4] == 0 and g.pl[4].r[4] == 0 and g.pl[4].r[1] == 1, "Monopoly")
    g.played = false
    L.build(g, 1, SETTLE, L.hv(10, 1), true)
    L.play(g, 1, 2)
    assert(g.phase == "roads" and g.free == 2)
    L.freeRoad(g, L.spots(g, 1, ROAD)[1])
    assert(g.phase == "roads")
    L.freeRoad(g, L.spots(g, 1, ROAD)[1])
    assert(g.phase == "main" and g.pl[1].nr == 2 and L.count(g, 1) == 5, "two free roads")
    assert(not L.canPlay(g, 1, 5), "victory points are never played")
end

-- Largest army: three knights take it, more knights take it over.
do
    local g = fresh()
    local function knight(p)
        g.turn, g.phase, g.played = p, "main", false
        g.pl[p].d[1] = 1
        L.play(g, p, 1)
        g.phase = "main"
    end
    knight(2); knight(2)
    assert(g.la == 0)
    knight(2)
    assert(g.la == 2 and L.vp(g, 2) == 2)
    knight(3); knight(3); knight(3)
    assert(g.la == 2, "a tie does not take it")
    knight(3)
    assert(g.la == 3 and L.vp(g, 2) == 0 and L.vp(g, 3) == 2)
end

-- Trading rates: 4:1, 3:1 at a general port, 2:1 at its own port.
do
    local g = fresh()
    g.ports = "012345000"
    assert(L.rate(g, 1, 1) == 4)
    local pe = L.PE:byte(1)
    local a = L.ends(pe)
    g.v[a] = 1
    assert(L.rate(g, 1, 1) == 3 and L.rate(g, 1, 5) == 3, "general port")
    local b = L.ends(L.PE:byte(2))
    g.v[b] = 1
    assert(L.rate(g, 1, 1) == 2 and L.rate(g, 1, 2) == 3, "a wood port")
    give(g, 1, 2, 0, 0, 0, 0)
    assert(L.bankTrade(g, 1, 1, 5) and g.pl[1].r[5] == 1 and g.pl[1].r[1] == 0)
    assert(not L.bankTrade(g, 1, 2, 5), "not enough")
    give(g, 2, 0, 0, 2, 0, 0)
    give(g, 1, 1, 0, 0, 0, 0)
    assert(L.swap(g, 1, 2, 1, 1, 3, 2) and g.pl[1].r[3] == 2 and g.pl[2].r[1] == 1)
    assert(not L.swap(g, 1, 2, 1, 1, 3, 1), "they have no wool left")
end

-- Points and the end: settlements, cities, the two cards of honour, VP cards.
do
    local g = fresh()
    for k = 1, 3 do
        local v = L.hv(({ 1, 3, 17 })[k], 1)
        L.build(g, 1, SETTLE, v, true)
    end
    L.build(g, 1, CITY, L.hv(1, 1), true)
    assert(L.vp(g, 1) == 4)
    g.la = 1
    g.pl[1].d[5] = 2
    assert(L.vp(g, 1) == 6 and L.vp(g, 1, true) == 8)
    g.lr = 1
    assert(g.phase == "main")
    L.checkWin(g)
    assert(g.phase == "over" and g.winner == 1, "10 points on your turn")
    assert(L.place(g, 1) == 1 and L.place(g, 2) == 2)
end

-- The device players' trade answers.
do
    local g = fresh()
    L.build(g, 2, SETTLE, L.hv(10, 1), true)
    give(g, 2, 1, 1, 1, 0, 3)  -- wants grain for a settlement, has ore to spare
    assert(A.accept(g, 2, 4, 1, 5, 1), "grain for spare ore")
    assert(not A.accept(g, 2, 5, 1, 4, 1), "not ore for grain it does not have")
    assert(not A.accept(g, 2, 5, 1, 1, 1), "not its wood for more ore")
    g.pl[1].ns = 8
    assert(not A.accept(g, 2, 4, 1, 5, 1), "no deals with a player about to win")
end

-- Self-play: four device players (and the human seat played by the device,
-- answering offers and discarding) always finish with a 10-point winner.
local function selfPlay(human, level, starter, check)
    local g = Setup.new(level, starter, human)
    local acts, turns, last = 0, 0, g.turn
    while g.phase ~= "over" and acts < 4000 do
        local before = g.turn
        A.act(g)
        acts = acts + 1
        if g.turn ~= before then turns = turns + 1 end
        if check and acts % 7 == 0 then check(g) end
    end
    assert(g.phase == "over", "the game ends")
    assert(L.vp(g, g.winner, true) >= 10 and g.winner == g.turn)
    -- Pieces and cards add up.
    for r = 1, 5 do
        local n = g.bank[r]
        for p = 1, 4 do n = n + g.pl[p].r[r] end
        assert(n == 19, "19 of each resource")
    end
    local roads, towns = 0, 0
    for e = 1, 72 do
        if g.e[e] > 0 then roads = roads + 1 end
    end
    for v = 1, 54 do
        if g.v[v] > 0 then towns = towns + 1 end
    end
    local nr, nt = 0, 0
    for p = 1, 4 do nr, nt = nr + g.pl[p].nr, nt + g.pl[p].ns + g.pl[p].nc end
    assert(roads == nr and towns == nt)
    return g, turns
end

local wins, total = { 0, 0, 0, 0 }, 0
for i = 1, 24 do
    local g, turns = selfPlay(i % 2, i % 3 == 0 and 2 or 1, i % 4 == 0)
    wins[g.winner] = wins[g.winner] + 1
    total = total + turns
    assert(turns < 400, "a game takes a sensible number of turns")
end
print(string.format("self-play: wins %d %d %d %d, %.1f turns a game", wins[1], wins[2], wins[3], wins[4], total / 24))

-- Save and restore round-trip, mid-game, then the game plays on.
do
    local n = 0
    selfPlay(1, 2, false, function(g)
        local s = Save.serialize(g)
        local h = Save.deserialize(s)
        assert(h, "restores")
        assert(Save.serialize(h) == s, "same game after a round trip")
        n = n + 1
    end)
    assert(n > 5)
    assert(not Save.deserialize("H1|garbage"), "rejects a broken save")
    local g = Setup.new(1, false, 1)
    for _ = 1, 30 do A.act(g) end
    local h = Save.deserialize(Save.serialize(g))
    while h.phase ~= "over" do A.act(h) end
    assert(h.winner > 0)
end

print("hexland tests passed")
