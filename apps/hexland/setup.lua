-- Hexland: a new game - the island (random or the starter board), the
-- card deck and the players. Usage:
--   local g = smudge.dofile("setup.lua")(L).new(level, starter, human)
-- main.lua loads it only to start a game.

return function(L)
local S = {}

local function shuffle(t)
    for i = #t, 2, -1 do
        local j = math.random(1, i)
        t[i], t[j] = t[j], t[i]
    end
    return t
end

local function chars(s)
    local t = {}
    for i = 1, #s do t[i] = s:sub(i, i) end
    return t
end

-- Two hexes touch when they share two corners.
function S.adjacent(a, b)
    local n = 0
    for i = 1, 6 do
        for j = 1, 6 do
            if L.hv(a, i) == L.hv(b, j) then n = n + 1 end
        end
    end
    return n >= 2
end

-- No 6 or 8 next to another 6 or 8.
function S.hotApart(num)
    for a = 1, 19 do
        if num[a] == 6 or num[a] == 8 then
            for b = a + 1, 19 do
                if (num[b] == 6 or num[b] == 8) and S.adjacent(a, b) then return false end
            end
        end
    end
    return true
end

-- The starter board (fixed): every resource on a fair number.
S.STARTER_RES = "3415234140123145352"
S.STARTER_NUM = { 10, 6, 3, 5, 9, 4, 11, 8, 3, 0, 10, 6, 12, 5, 9, 2, 4, 8, 11 }
S.STARTER_PORTS = "102304050"

function S.new(level, starter, human)
    local g = {
        level = level or 1, starter = starter and 1 or 0, human = human or 1,
        v = {}, e = {}, num = {}, bank = { 19, 19, 19, 19, 19 }, need = { 0, 0, 0, 0 }, log = {}, pl = {},
        phase = "setupS", sstep = 1, lastS = 0, d1 = 0, d2 = 0, lr = 0, la = 0, free = 0,
        played = false, offered = false, after = "main", vict = "", winner = 0,
    }
    for i = 1, 54 do g.v[i] = 0 end
    for i = 1, 72 do g.e[i] = 0 end
    if starter then
        g.res, g.ports = S.STARTER_RES, S.STARTER_PORTS
        for h = 1, 19 do g.num[h] = S.STARTER_NUM[h] end
    else
        g.res = table.concat(shuffle(chars("0111122233334444555")))
        repeat
            local nums, k = shuffle({ 2, 3, 3, 4, 4, 5, 5, 6, 6, 8, 8, 9, 9, 10, 10, 11, 11, 12 }), 0
            for h = 1, 19 do
                if L.res(g, h) == 0 then
                    g.num[h] = 0
                else
                    k = k + 1
                    g.num[h] = nums[k]
                end
            end
        until S.hotApart(g.num)
        g.ports = table.concat(shuffle(chars("000012345")))
    end
    g.robber = (g.res:find("0", 1, true))
    g.deck = table.concat(shuffle(chars("1111111111111122334455555")))
    for p = 1, 4 do
        g.pl[p] = { r = { 0, 0, 0, 0, 0 }, d = { 0, 0, 0, 0, 0 }, nd = { 0, 0, 0, 0, 0 }, k = 0, nr = 0, ns = 0, nc = 0,
                    len = 0 }
    end
    g.first = math.random(1, 4)
    g.turn = g.first
    L.log(g, L.NAMES[g.first] .. (g.first == 1 and " go" or " goes") .. " first. Place a settlement and a road.")
    return g
end

return S
end
