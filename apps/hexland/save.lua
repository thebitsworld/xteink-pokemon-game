-- Saving and restoring a Hexland game in progress. Usage:
--   local Save = smudge.dofile("save.lua")(L)   -- L: the rules (logic.lua)
-- The whole game goes into one string: fields split by "|", numbers in a
-- field by ".", log lines by "~". main.lua loads it only to save or restore.

return function(L)
local Save = {}

local PHASES = { setupS = 1, setupR = 1, roll = 1, discard = 1, robber = 1, steal = 1, main = 1, roads = 1, offer = 1,
                 over = 1 }

local function join(t) return table.concat(t, ".") end
local function split(s)
    local out = {}
    for n in s:gmatch("%d+") do out[#out + 1] = tonumber(n) end
    return out
end

function Save.serialize(g)
    local f = { "H1",
        join({ g.level, g.starter, g.human, g.first, g.turn, g.sstep, g.lastS, g.robber, g.d1, g.d2, g.lr, g.la, g.free,
               g.played and 1 or 0, g.offered and 1 or 0, g.winner }),
        g.phase, g.after, g.vict, g.res, g.ports, g.deck,
        join(g.num), join(g.v), join(g.e), join(g.bank), join(g.need), g.offer and join(g.offer) or "" }
    for p = 1, 4 do
        local pl = g.pl[p]
        local t = {}
        for i = 1, 5 do t[i], t[i + 5], t[i + 10] = pl.r[i], pl.d[i], pl.nd[i] end
        t[16], t[17], t[18], t[19], t[20] = pl.k, pl.nr, pl.ns, pl.nc, pl.len
        f[#f + 1] = join(t)
    end
    f[#f + 1] = table.concat(g.log, "~")
    return table.concat(f, "|")
end

function Save.deserialize(s)
    local f = {}
    for x in (s .. "|"):gmatch("([^|]*)|") do f[#f + 1] = x end
    if #f ~= 19 or f[1] ~= "H1" or not PHASES[f[3]] or not PHASES[f[4]] then return nil end
    local a = split(f[2])
    if #a ~= 16 then return nil end
    local g = { level = a[1], starter = a[2], human = a[3], first = a[4], turn = a[5], sstep = a[6], lastS = a[7],
                robber = a[8], d1 = a[9], d2 = a[10], lr = a[11], la = a[12], free = a[13], played = a[14] == 1,
                offered = a[15] == 1, winner = a[16],
                phase = f[3], after = f[4], vict = f[5], res = f[6], ports = f[7], deck = f[8],
                num = split(f[9]), v = split(f[10]), e = split(f[11]), bank = split(f[12]), need = split(f[13]),
                pl = {}, log = {} }
    if f[14] ~= "" then g.offer = split(f[14]) end
    if #g.res ~= 19 or #g.ports ~= 9 or #g.num ~= 19 or #g.v ~= 54 or #g.e ~= 72 or #g.bank ~= 5 or #g.need ~= 4
        or (g.level ~= 1 and g.level ~= 2) or g.turn < 1 or g.turn > 4 or (g.phase == "offer" and not g.offer) then
        return nil
    end
    for p = 1, 4 do
        local t = split(f[14 + p])
        if #t ~= 20 then return nil end
        local pl = { r = {}, d = {}, nd = {}, k = t[16], nr = t[17], ns = t[18], nc = t[19], len = t[20] }
        for i = 1, 5 do pl.r[i], pl.d[i], pl.nd[i] = t[i], t[i + 5], t[i + 10] end
        g.pl[p] = pl
    end
    for line in f[19]:gmatch("[^~]+") do g.log[#g.log + 1] = line end
    return g
end

return Save
end
