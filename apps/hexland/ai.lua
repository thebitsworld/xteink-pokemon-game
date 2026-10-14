-- Hexland device players: rule-based, one decision per call. Usage:
--   local A = smudge.dofile("ai.lua")(L)
--   A.act(g)                      -- the player who must act (L.actor) acts
--   A.accept(g, p, recv, rn, give, gn) -- does p take rn recv for gn give?
-- main.lua loads it only for the device's turns and your trade offers.
-- Level 1 (Normal) places with a little noise and is easier to trade with;
-- level 2 (Hard) aims the robber at the leader and trades more carefully.

return function(L)
local A = {}
local WEIGHT = { 1.0, 1.0, 0.8, 1.0, 0.9 }

-- Pips a player collects per resource (a city counts twice).
function A.production(g, p)
    local prod = { 0, 0, 0, 0, 0 }
    for v = 1, 54 do
        if L.owner(g, v) == p then
            local m = g.v[v] > 4 and 2 or 1
            for k = 1, 3 do
                local h = L.vh(v, k)
                if h > 0 and h ~= g.robber and L.res(g, h) > 0 then
                    local r = L.res(g, h)
                    prod[r] = prod[r] + m * L.pips(g.num[h])
                end
            end
        end
    end
    return prod
end

-- What a corner is worth to p: its pips, more for resources p lacks, ports.
local function corner(g, v, prod)
    local s = 0
    for k = 1, 3 do
        local h = L.vh(v, k)
        if h > 0 and L.res(g, h) > 0 then
            local r = L.res(g, h)
            local pip = L.pips(g.num[h])
            if h == g.robber then pip = pip * 0.4 end
            s = s + pip * WEIGHT[r] * (prod[r] == 0 and 1.4 or 1)
        end
    end
    local t = L.port(g, v)
    if t then s = s + ((t == 0 and 1) or (prod[t] >= 5 and 2.5) or 0.5) end
    return s
end

local function noise(g) return g.level == 2 and math.random() * 0.3 or math.random() * 1.5 end

local function bestSettle(g, p, prod)
    local best, bv = -1, nil
    for _, v in ipairs(L.spots(g, p, L.SETTLE)) do
        local s = corner(g, v, prod) + noise(g)
        if s > best then best, bv = s, v end
    end
    return bv
end

-- Whether p's own network already touches corner v.
local function mine(g, p, v)
    if L.owner(g, v) == p then return true end
    for k = 1, 3 do
        local e = L.ve(v, k)
        if e > 0 and g.e[e] == p then return true end
    end
    return false
end

-- A road's worth: the best free corner it leads to (one or two steps on).
local function roadScore(g, p, e, prod)
    local a, b = L.ends(e)
    local u = mine(g, p, a) and b or a
    if mine(g, p, u) then return 0 end
    local o = L.owner(g, u)
    if o ~= 0 then return 0 end
    if L.canSettle(g, p, u, true) then return corner(g, u, prod) end
    local best = 0
    for k = 1, 3 do
        local w = L.vn(u, k)
        if w > 0 and L.canSettle(g, p, w, true) then
            local s = 0.6 * corner(g, w, prod)
            if s > best then best = s end
        end
    end
    return best
end

local function bestRoad(g, p, prod)
    local best, be = 0, nil
    for _, e in ipairs(L.spots(g, p, L.ROAD)) do
        local s = roadScore(g, p, e, prod)
        if L.setup(g) then s = s + 0.01 end
        if s > best then best, be = s, e end
    end
    return be, best
end

-- The build p saves for: a settlement if there is a corner for it, else a
-- city, else a road towards a corner, else a card.
local function missing(g, p, kind)
    local r, c, n = g.pl[p].r, L.COST[kind], 0
    for i = 1, 5 do
        if r[i] < c[i] then n = n + c[i] - r[i] end
    end
    return n
end

local function target(g, p, prod)
    local pl = g.pl[p]
    local settle = pl.ns < 5 and #L.spots(g, p, L.SETTLE) > 0
    local city = pl.ns > 0 and pl.nc < 4
    if settle and not (city and missing(g, p, L.CITY) < missing(g, p, L.SETTLE)) then return L.SETTLE end
    if city then return L.CITY end
    if pl.ns < 5 and pl.nr < 15 then
        local e = bestRoad(g, p, prod)
        if e then return L.ROAD end
    end
    if #g.deck > 0 then return L.CARD end
    return nil
end

-- How much p wants one of each resource now.
local function values(g, p, prod)
    local t = target(g, p, prod)
    local c, r, v = L.COST[t or L.CARD], g.pl[p].r, {}
    for i = 1, 5 do
        v[i] = 1
        if r[i] < c[i] then v[i] = v[i] + 2 end
        if r[i] > c[i] + 1 then v[i] = v[i] - 0.5 end
        if prod[i] == 0 then v[i] = v[i] + 0.5 end
    end
    return v
end

-- p (a device player) gets rn of recv for gn of give from the human.
function A.accept(g, p, recv, rn, give, gn)
    if g.pl[p].r[give] < gn or recv == give then return false end
    if L.vp(g, g.human, false) >= (g.level == 2 and 7 or 8) then return false end
    local v = values(g, p, A.production(g, p))
    return v[recv] * rn > v[give] * gn + (g.level == 2 and 0.5 or 0)
end

-- The robber: on the hex that hurts the others most (the leader most on
-- Hard), never on p's own.
local function robber(g, p)
    local best, bh = -1, nil
    for h = 1, 19 do
        if h ~= g.robber then
            local s, own = 0, false
            for k = 1, 6 do
                local v = L.hv(h, k)
                local o = L.owner(g, v)
                if o == p then
                    own = true
                elseif o > 0 then
                    local vp = L.vp(g, o, false)
                    local w = g.level == 2 and (1 + vp * vp / 16) or (1 + vp / 5)
                    s = s + L.pips(g.num[h]) * (g.v[v] > 4 and 2 or 1) * w + (L.count(g, o) > 0 and 0.5 or 0)
                end
            end
            if own then s = s - 100 end
            s = s + noise(g) * 0.5
            if s > best then best, bh = s, h end
        end
    end
    L.moveRobber(g, bh)
end

local function steal(g)
    local best, who = -1, nil
    for i = 1, #g.vict do
        local o = g.vict:byte(i) - 48
        local s = L.vp(g, o, false) * 10 + L.count(g, o)
        if s > best then best, who = s, o end
    end
    L.steal(g, who)
end

-- A knight when the robber sits on p's own hex, or for the largest army.
local function wantKnight(g, p)
    if not L.canPlay(g, p, 1) then return false end
    for k = 1, 6 do
        if L.owner(g, L.hv(g.robber, k)) == p and L.pips(g.num[g.robber]) >= 2 then return true end
    end
    local pl = g.pl[p]
    return pl.k + 1 >= 3 and g.la ~= p and (g.la == 0 or pl.k + 1 > g.pl[g.la].k)
end

local function playCards(g, p, prod, t)
    if g.played then return false end
    if wantKnight(g, p) then
        L.play(g, p, 1)
        return true
    end
    if L.canPlay(g, p, 2) and t == L.ROAD then
        L.play(g, p, 2)
        return true
    end
    local r = g.pl[p].r
    if L.canPlay(g, p, 3) and t then
        local c, want = L.COST[t], {}
        for i = 1, 5 do
            for _ = 1, c[i] - r[i] do want[#want + 1] = i end
        end
        if #want >= 1 and #want <= 2 then
            L.play(g, p, 3, want[1], want[2] or want[1])
            return true
        end
    end
    if L.canPlay(g, p, 4) and t then
        -- The needed resource the others produce most.
        local c, best, br = L.COST[t], 0, nil
        for i = 1, 5 do
            if c[i] > r[i] then
                local n = 0
                for q = 1, 4 do
                    if q ~= p then n = n + A.production(g, q)[i] end
                end
                if n > best then best, br = n, i end
            end
        end
        if br and best >= 10 then
            L.play(g, p, 4, br)
            return true
        end
    end
    return false
end

local function build(g, p, prod, t)
    local pl = g.pl[p]
    if L.afford(g, p, L.CITY) and pl.ns > 0 and pl.nc < 4 then
        local best, bv = -1, nil
        for _, v in ipairs(L.spots(g, p, L.CITY)) do
            local s = corner(g, v, { 1, 1, 1, 1, 1 })
            if s > best then best, bv = s, v end
        end
        L.build(g, p, L.CITY, bv)
        return true
    end
    if L.afford(g, p, L.SETTLE) then
        local v = bestSettle(g, p, prod)
        if v then
            L.build(g, p, L.SETTLE, v)
            return true
        end
    end
    if L.afford(g, p, L.ROAD) and t ~= L.SETTLE and pl.nr < 15 then
        local e, s = bestRoad(g, p, prod)
        -- With nowhere to settle, or chasing the longest road, or to spend a big hand.
        local chase = g.level == 2 and pl.len >= 4 and g.lr ~= p
        if e and (t == L.ROAD or chase or (L.count(g, p) >= 8 and s > 0)) then
            L.build(g, p, L.ROAD, e)
            return true
        end
    end
    if L.canBuy(g, p) and (t == L.CARD or t == nil or L.count(g, p) >= 7) then
        L.buyCard(g, p)
        return true
    end
    return false
end

-- Bank or port trades towards the target; now and then an offer to you.
local function trade(g, p, prod, t)
    if not t then return false end
    local r, c = g.pl[p].r, L.COST[t]
    local short = 0
    for i = 1, 5 do
        if r[i] < c[i] then short = short + c[i] - r[i] end
    end
    if short == 0 then return false end
    for need = 1, 5 do
        if r[need] < c[need] and g.bank[need] > 0 then
            for give = 1, 5 do
                if give ~= need and r[give] - c[give] >= L.rate(g, p, give) then
                    return L.bankTrade(g, p, give, need)
                end
            end
        end
    end
    local h = g.human
    if short == 1 and h > 0 and h ~= p and not g.offered then
        g.offered = true
        if math.random() < 0.5 then
            local need, give, most = 0, 0, 0
            for i = 1, 5 do
                if r[i] < c[i] then need = i end
                if r[i] - c[i] > most then give, most = i, r[i] - c[i] end
            end
            if give > 0 and g.pl[h].r[need] > 0 then
                g.offer, g.phase = { p, give, 1, need, 1 }, "offer"
                return true
            end
        end
    end
    return false
end

local function main(g, p)
    for _ = 1, 30 do
        if g.phase ~= "main" then return end
        local prod = A.production(g, p)
        local t = target(g, p, prod)
        if not (playCards(g, p, prod, t) or build(g, p, prod, t) or trade(g, p, prod, t)) then break end
    end
    if g.phase == "main" then L.endTurn(g) end
end

function A.act(g)
    local p = L.actor(g)
    local ph = g.phase
    if ph == "setupS" then
        L.setupSettle(g, bestSettle(g, p, A.production(g, p)))
    elseif ph == "setupR" then
        L.setupRoad(g, (bestRoad(g, p, A.production(g, p))) or L.spots(g, p, L.ROAD)[1])
    elseif ph == "roll" then
        if wantKnight(g, p) then
            L.play(g, p, 1)
        else
            L.roll(g, math.random(1, 6), math.random(1, 6))
        end
    elseif ph == "discard" then
        L.autoDiscard(g, p, g.need[p])
        g.need[p], g.phase = 0, "robber"
    elseif ph == "robber" then
        robber(g, p)
    elseif ph == "steal" then
        steal(g)
    elseif ph == "roads" then
        local e = bestRoad(g, p, A.production(g, p)) or L.spots(g, p, L.ROAD)[1]
        if e then L.freeRoad(g, e) else g.free, g.phase = 0, "main" end
    elseif ph == "offer" then
        local o = g.offer
        L.answer(g, A.accept(g, p, o[2], o[3], o[4], o[5]))
    elseif ph == "main" then
        main(g, p)
    end
end

return A
end
