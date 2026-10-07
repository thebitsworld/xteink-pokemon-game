-- Saving and restoring a Bazaar round in play. Usage:
--   local Save = smudge.dofile("save.lua")(B)   -- B: the rules (logic.lua)
-- The app loads it only when it starts and when it closes.

return function(B)
local Save = {}

-- Save/restore: everything as numbers joined by separators.
local function join(t) return table.concat(t, ".") end
local function split(s)
    local out = {}
    for n in s:gmatch("%d+") do out[#out + 1] = tonumber(n) end
    return out
end

function Save.serialize(g)
    local f = { g.round, g.turn, g.starter, g.seals[1], g.seals[2], g.phase == "play" and 1 or 0, join(g.deck),
                join(g.market) }
    for p = 1, 2 do
        local pl = g.players[p]
        f[#f + 1] = join({ pl.camels, pl.rupees, pl.bonuses, pl.goodsTokens, table.unpack(pl.hand) })
    end
    for good = 1, 6 do f[#f + 1] = join(g.piles[good]) end
    for k = 1, 3 do f[#f + 1] = join(g.bonus[k]) end
    return table.concat(f, "|")
end

function Save.deserialize(s)
    local f = {}
    for x in (s .. "|"):gmatch("([^|]*)|") do f[#f + 1] = x end
    if #f ~= 19 or f[6] ~= "1" then return nil end -- only rounds in play are saved
    local g = { round = tonumber(f[1]), turn = tonumber(f[2]), starter = tonumber(f[3]),
                seals = { tonumber(f[4]), tonumber(f[5]) }, winner = 0, phase = "play" }
    if (g.turn ~= 1 and g.turn ~= 2) or not g.round then return nil end
    g.deck, g.market = split(f[7]), split(f[8])
    if #g.market ~= 5 then return nil end
    g.players = {}
    local cards = { 0, 0, 0, 0, 0, 0, 0 }
    local function count(kind, n)
        if kind < 1 or kind > 7 then return false end
        cards[kind] = cards[kind] + n
        return true
    end
    for _, c in ipairs(g.deck) do
        if not count(c, 1) then return nil end
    end
    for _, c in ipairs(g.market) do
        if c ~= 0 and not count(c, 1) then return nil end
    end
    for p = 1, 2 do
        local v = split(f[8 + p])
        if #v ~= 10 then return nil end
        local pl = { camels = v[1], rupees = v[2], bonuses = v[3], goodsTokens = v[4],
                     hand = { v[5], v[6], v[7], v[8], v[9], v[10] } }
        count(B.CAMEL, pl.camels)
        for good = 1, 6 do count(good, pl.hand[good]) end
        g.players[p] = pl
    end
    for kind = 1, 7 do
        if cards[kind] > B.COUNTS[kind] then return nil end -- the rest were sold
    end
    g.piles, g.bonus = {}, {}
    for good = 1, 6 do g.piles[good] = split(f[10 + good]) end
    for k = 1, 3 do g.bonus[k] = split(f[16 + k]) end
    return g
end

return Save
end
