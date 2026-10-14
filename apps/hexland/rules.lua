-- Hexland moves: placing pieces (the distance rule, connected roads, the
-- longest road), the set-up, rolling and production, discards and the
-- robber, development cards, trading and the end of a turn. Loaded only to
-- make a move (by input.lua, ui.lua and ai.lua). Usage:
--   local R = smudge.dofile("rules.lua")(L)   -- L: logic.lua; R has all of L too

return function(L)
local R = setmetatable({}, { __index = L })
local byte = string.byte
local VE, VN = L.VE, L.VN

local function short(counts)
    local out = {}
    for r = 1, 5 do
        local n = counts[r]
        if n > 0 then out[#out + 1] = (n > 1 and n or "") .. L.SH[r] end
    end
    return table.concat(out, " ")
end
R.short = short

function R.afford(g, p, kind)
    local r, c = g.pl[p].r, L.COST[kind]
    for i = 1, 5 do
        if r[i] < c[i] then return false end
    end
    return true
end
local function pay(g, p, kind)
    local r, c, b = g.pl[p].r, L.COST[kind], g.bank
    for i = 1, 5 do
        r[i] = r[i] - c[i]
        b[i] = b[i] + c[i]
    end
end
local function take(g, p, res, n)
    n = math.min(n, g.bank[res])
    g.bank[res] = g.bank[res] - n
    g.pl[p].r[res] = g.pl[p].r[res] + n
    return n
end

-- The distance rule; outside the set-up the corner must touch your road.
function R.canSettle(g, p, v, anywhere)
    if g.v[v] ~= 0 then return false end
    for k = 1, 3 do
        local u = byte(VN, v * 3 - 3 + k)
        if u > 0 and g.v[u] ~= 0 then return false end
    end
    if anywhere then return true end
    for k = 1, 3 do
        local e = byte(VE, v * 3 - 3 + k)
        if e > 0 and g.e[e] == p then return true end
    end
    return false
end

-- Your network reaches corner v: your building, or your road with no
-- other player's building on the corner.
local function reaches(g, p, v)
    local o = L.owner(g, v)
    if o ~= 0 then return o == p end
    for k = 1, 3 do
        local e = byte(VE, v * 3 - 3 + k)
        if e > 0 and g.e[e] == p then return true end
    end
    return false
end

function R.canRoad(g, p, e)
    if g.e[e] ~= 0 then return false end
    local a, b = L.ends(e)
    if g.phase == "setupR" then return a == g.lastS or b == g.lastS end
    return reaches(g, p, a) or reaches(g, p, b)
end

-- Where p may put a road, settlement or city (or the robber) now.
function R.spots(g, p, kind)
    local out = {}
    if kind == L.HEX then
        for h = 1, 19 do
            if h ~= g.robber then out[#out + 1] = h end
        end
    elseif L.left(g, p, kind) <= 0 then
        return out
    elseif kind == L.ROAD then
        for e = 1, 72 do
            if R.canRoad(g, p, e) then out[#out + 1] = e end
        end
    else
        local setup = L.setup(g)
        for v = 1, 54 do
            if (kind == L.SETTLE and R.canSettle(g, p, v, setup)) or (kind == L.CITY and g.v[v] == p) then
                out[#out + 1] = v
            end
        end
    end
    return out
end

-- Longest road: a depth-first walk over p's roads; a used road is marked by
-- turning its owner negative. A walk may end on, but not pass, another
-- player's building.
local function walk(g, p, v, len)
    if len > 0 then
        local o = L.owner(g, v)
        if o ~= 0 and o ~= p then return len end
    end
    local best = len
    for k = 1, 3 do
        local e = byte(VE, v * 3 - 3 + k)
        if e > 0 and g.e[e] == p then
            g.e[e] = -p
            local a, b = L.ends(e)
            local n = walk(g, p, a == v and b or a, len + 1)
            if n > best then best = n end
            g.e[e] = p
        end
    end
    return best
end

function R.longest(g, p)
    local best = 0
    for v = 1, 54 do
        local n = walk(g, p, v, 0)
        if n > best then best = n end
    end
    return best
end

-- Longest road (5+): the holder keeps it on a tie; if it is broken, the one
-- player then longest takes it, or nobody does on a tie.
function R.updateRoads(g)
    local best, who, n = 0, 0, 0
    for p = 1, 4 do
        local l = R.longest(g, p)
        g.pl[p].len = l
        if l > best then
            best, who, n = l, p, 1
        elseif l == best then
            n = n + 1
        end
    end
    local h = g.lr
    if h > 0 and g.pl[h].len >= 5 and g.pl[h].len >= best then return end
    g.lr = (best >= 5 and n == 1) and who or 0
end

function R.checkWin(g)
    local p = g.turn
    if g.phase ~= "over" and not L.setup(g) and L.vp(g, p, true) >= L.WIN then
        g.phase, g.winner = "over", p
        L.log(g, L.NAMES[p] .. (p == 1 and " win" or " wins") .. " with " .. L.vp(g, p, true) .. " points!")
    end
end

-- "Ana built: road, city, card" - one line per turn, added to.
local WHAT = { "road", "settlement", "city", "card" }
local function built(g, p, kind)
    local head, t = L.NAMES[p] .. " built: ", g.log
    local last = t[#t]
    if last and last:sub(1, #head) == head and #last < 48 then
        t[#t] = last .. ", " .. WHAT[kind]
    else
        L.log(g, head .. WHAT[kind])
    end
end

-- Builds (paid unless free) and updates the roads and points.
function R.build(g, p, kind, spot, free)
    local pl = g.pl[p]
    if not free then pay(g, p, kind) end
    built(g, p, kind)
    if kind == L.ROAD then
        g.e[spot] = p
        pl.nr = pl.nr + 1
    elseif kind == L.SETTLE then
        g.v[spot] = p
        pl.ns = pl.ns + 1
    else
        g.v[spot] = p + 4
        pl.ns, pl.nc = pl.ns - 1, pl.nc + 1
    end
    if kind ~= L.CITY then R.updateRoads(g) end
    R.checkWin(g)
end

-- Set-up: each player places a settlement and a road, in turn order and then
-- back again; the second settlement gives one of each resource around it.
function R.setupPlayer(g, step)
    local i = step <= 4 and step - 1 or 8 - step
    return (g.first - 1 + i) % 4 + 1
end

function R.setupSettle(g, v)
    local p = g.turn
    g.v[v] = p
    g.pl[p].ns = g.pl[p].ns + 1
    g.lastS = v
    g.phase = "setupR"
    if g.sstep > 4 then
        local got = { 0, 0, 0, 0, 0 }
        for k = 1, 3 do
            local h = L.vh(v, k)
            if h > 0 and L.res(g, h) > 0 then
                local r = L.res(g, h)
                got[r] = got[r] + take(g, p, r, 1)
            end
        end
        L.log(g, L.NAMES[p] .. " start" .. (p == 1 and "" or "s") .. " with " .. short(got))
    end
end

function R.setupRoad(g, e)
    g.e[e] = g.turn
    g.pl[g.turn].nr = g.pl[g.turn].nr + 1
    g.sstep = g.sstep + 1
    if g.sstep > 8 then
        g.phase, g.turn = "roll", g.first
        R.updateRoads(g)
    else
        g.phase, g.turn = "setupS", R.setupPlayer(g, g.sstep)
    end
end

-- Rolling ---------------------------------------------------------------------

-- The device's discard: always from its biggest pile.
function R.autoDiscard(g, p, n)
    local r = g.pl[p].r
    for _ = 1, n do
        local best = 1
        for i = 2, 5 do
            if r[i] >= r[best] then best = i end
        end
        r[best] = r[best] - 1
        g.bank[best] = g.bank[best] + 1
    end
end

-- Production of a roll: settlements 1, cities 2; when the bank cannot pay
-- everyone a resource, only a single player is paid (what is left).
local function produce(g, n)
    local got = {}
    for i = 1, 20 do got[i] = 0 end
    for h = 1, 19 do
        local r = L.res(g, h)
        if g.num[h] == n and h ~= g.robber and r > 0 then
            for k = 1, 6 do
                local o = g.v[L.hv(h, k)]
                if o > 0 then
                    local p = o > 4 and o - 4 or o
                    got[p * 5 - 5 + r] = got[p * 5 - 5 + r] + (o > 4 and 2 or 1)
                end
            end
        end
    end
    for r = 1, 5 do
        local total, who = 0, 0
        for p = 1, 4 do
            if got[p * 5 - 5 + r] > 0 then total, who = total + got[p * 5 - 5 + r], who + 1 end
        end
        if total > g.bank[r] and who > 1 then
            for p = 1, 4 do got[p * 5 - 5 + r] = 0 end
        end
    end
    local parts = {}
    for p = 1, 4 do
        local mine = {}
        for r = 1, 5 do mine[r] = take(g, p, r, got[p * 5 - 5 + r]) end
        local s = short(mine)
        if s ~= "" then parts[#parts + 1] = L.NAMES[p] .. " " .. s end
    end
    return #parts > 0 and table.concat(parts, ", ") or "nothing"
end

function R.roll(g, d1, d2)
    local n = d1 + d2
    g.d1, g.d2 = d1, d2
    local head = L.NAMES[g.turn] .. " rolled " .. n .. ": "
    if n ~= 7 then
        L.log(g, head .. produce(g, n))
        g.phase = "main"
        return
    end
    local parts = {}
    for p = 1, 4 do
        local c = L.count(g, p)
        g.need[p] = c > 7 and c // 2 or 0
        if g.need[p] > 0 then
            parts[#parts + 1] = L.NAMES[p] .. " -" .. g.need[p]
            if p ~= g.human then
                R.autoDiscard(g, p, g.need[p])
                g.need[p] = 0
            end
        end
    end
    L.log(g, head .. "robber" .. (#parts > 0 and (", discard: " .. table.concat(parts, " ")) or ""))
    g.after = "main"
    g.phase = (g.human > 0 and g.need[g.human] > 0) and "discard" or "robber"
end

function R.discardOne(g, p, r)
    local pl = g.pl[p]
    if pl.r[r] <= 0 or g.need[p] <= 0 then return false end
    pl.r[r] = pl.r[r] - 1
    g.bank[r] = g.bank[r] + 1
    g.need[p] = g.need[p] - 1
    if g.need[p] == 0 then g.phase = "robber" end
    return true
end

-- The robber ------------------------------------------------------------------

-- The players (other than p) with a building on hex h and cards, as a string of digits.
function R.victims(g, h, p)
    local s = ""
    for k = 1, 6 do
        local o = L.owner(g, L.hv(h, k))
        if o > 0 and o ~= p and L.count(g, o) > 0 and not s:find(o, 1, true) then s = s .. o end
    end
    return s
end

function R.steal(g, from)
    local p = g.turn
    local n = L.count(g, from)
    g.phase, g.vict = g.after, ""
    if n == 0 then return end
    local k = math.random(1, n)
    local r = g.pl[from].r
    for i = 1, 5 do
        k = k - r[i]
        if k <= 0 then
            r[i] = r[i] - 1
            g.pl[p].r[i] = g.pl[p].r[i] + 1
            local what = (p == g.human or from == g.human) and (" " .. L.RES[i]) or " a card"
            L.log(g, L.NAMES[p] .. (p == 1 and " take" or " takes") .. what .. " from " .. L.NAMES[from])
            return
        end
    end
end

-- Moves the robber; steals at once from a single victim, otherwise the
-- phase becomes "steal" (g.vict: the choices).
function R.moveRobber(g, h)
    g.robber = h
    local v = R.victims(g, h, g.turn)
    if #v == 0 then
        g.phase = g.after
        L.log(g, L.NAMES[g.turn] .. " moved the robber")
    elseif #v == 1 then
        R.steal(g, byte(v) - 48)
    else
        g.phase, g.vict = "steal", v
    end
end

-- Development cards -----------------------------------------------------------
function R.buyCard(g, p)
    pay(g, p, L.CARD)
    built(g, p, L.CARD)
    local c = byte(g.deck, #g.deck) - 48
    g.deck = g.deck:sub(1, -2)
    local pl = g.pl[p]
    pl.d[c], pl.nd[c] = pl.d[c] + 1, pl.nd[c] + 1
    R.checkWin(g)
    return c
end

function R.canBuy(g, p) return #g.deck > 0 and R.afford(g, p, L.CARD) end

-- A card bought this turn waits; one card a turn; only a knight before the roll.
function R.canPlay(g, p, c)
    local pl = g.pl[p]
    if c == 5 or g.played or g.turn ~= p or pl.d[c] - pl.nd[c] <= 0 then return false end
    if g.phase == "roll" then return c == 1 end
    if g.phase ~= "main" then return false end
    if c == 2 then return #R.spots(g, p, L.ROAD) > 0 end
    return true
end

-- Plays card c (a, b: the resources for Year of Plenty, a for Monopoly).
function R.play(g, p, c, a, b)
    local pl = g.pl[p]
    g.played = true
    pl.d[c] = pl.d[c] - 1
    local who = L.NAMES[p] .. (p == 1 and " play " or " plays ")
    if c == 1 then
        pl.k = pl.k + 1
        if pl.k >= 3 and g.la ~= p and (g.la == 0 or pl.k > g.pl[g.la].k) then g.la = p end
        L.log(g, who .. "a Knight")
        g.after, g.phase = g.phase, "robber"
        R.checkWin(g)
    elseif c == 2 then
        g.free, g.phase = math.min(2, L.left(g, p, L.ROAD)), "roads"
        L.log(g, who .. "Road Building")
    elseif c == 3 then
        local got = { 0, 0, 0, 0, 0 }
        got[a] = got[a] + take(g, p, a, 1)
        got[b] = got[b] + take(g, p, b, 1)
        L.log(g, who .. "Year of Plenty: " .. short(got))
    else
        local n = 0
        for q = 1, 4 do
            if q ~= p then
                n = n + g.pl[q].r[a]
                g.pl[q].r[a] = 0
            end
        end
        pl.r[a] = pl.r[a] + n
        L.log(g, who .. "Monopoly: " .. n .. " " .. L.RES[a])
    end
end

-- A free road of Road Building; the phase ends after two or when none fits.
function R.freeRoad(g, e)
    R.build(g, g.turn, L.ROAD, e, true)
    g.free = g.free - 1
    if g.phase == "roads" and (g.free <= 0 or #R.spots(g, g.turn, L.ROAD) == 0) then
        g.free, g.phase = 0, "main"
    end
end

-- Trading ---------------------------------------------------------------------

function R.bankTrade(g, p, give, get)
    local n = L.rate(g, p, give)
    local r = g.pl[p].r
    if r[give] < n or g.bank[get] < 1 or give == get then return false end
    r[give], g.bank[give] = r[give] - n, g.bank[give] + n
    take(g, p, get, 1)
    L.log(g, L.NAMES[p] .. (p == 1 and " trade " or " trades ") .. n .. " " .. L.SH[give] .. " for 1 " .. L.SH[get] ..
        " with the " .. (n == 4 and "bank" or "port"))
    return true
end

-- a gives gn of resource give to b and gets bn of resource get.
function R.swap(g, a, b, give, gn, get, bn)
    local ra, rb = g.pl[a].r, g.pl[b].r
    if ra[give] < gn or rb[get] < bn then return false end
    ra[give], rb[give] = ra[give] - gn, rb[give] + gn
    rb[get], ra[get] = rb[get] - bn, ra[get] + bn
    return true
end

-- The human's answer to a device's offer (g.offer: from, give, gn, get, bn).
function R.answer(g, yes)
    local o = g.offer
    g.phase, g.offered, g.offer = "main", true, nil
    if yes and R.swap(g, o[1], g.human, o[2], o[3], o[4], o[5]) then
        L.log(g, "You trade " .. o[5] .. " " .. L.SH[o[4]] .. " for " .. o[3] .. " " .. L.SH[o[2]] .. " with " ..
            L.NAMES[o[1]])
    else
        L.log(g, "You decline " .. L.NAMES[o[1]] .. "'s offer")
    end
end

function R.endTurn(g)
    local pl = g.pl[g.turn]
    for i = 1, 5 do pl.nd[i] = 0 end
    g.played, g.offered = false, false
    g.turn = g.turn % 4 + 1
    g.phase = "roll"
    R.checkWin(g)
end

return R
end
