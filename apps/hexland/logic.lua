-- Hexland rules, the part kept in memory all game: the island's geometry,
-- names, points and the log. The moves (building, rolling, the robber,
-- cards, trading) are in rules.lua, loaded only to make one. No smudge.*
-- calls, so the tests run it on a computer. Usage:
--   local L = smudge.dofile("logic.lua")
--   local R = smudge.dofile("rules.lua")(L)   -- R also has all of L
--
-- The island has 19 hexes, 54 corners and 72 edges. Their links are byte
-- strings, worked out once (a table per corner would not fit the X3): HV 6
-- corners per hex, VH/VN/VE up to 3 hexes/neighbour corners/edges per corner
-- (0 = none), EV 2 corners per edge, VX/VY/HX/HY positions in half hex
-- widths (x) and quarter hex heights (y), PE the 9 port edges (30 coast
-- edges round the island, 3, 3, 4 apart), VP the port (1-9) at a corner.
-- A game g keeps the board as flat arrays: g.v[corner] 0, p (settlement) or
-- p + 4 (city); g.e[edge] 0 or p; g.res a string of 19 digits (0 desert,
-- 1-5 resources), g.num[hex] the number token, g.ports 9 digits (0 = 3:1).

local L = {}
local abs, byte = math.abs, string.byte

L.NAMES = { "You", "Ana", "Ben", "Cyd" }
L.RES = { "Wood", "Brick", "Wool", "Grain", "Ore" }
L.SH = { "Wd", "Br", "Wl", "Gr", "Or" }
L.DEV = { "Knight", "Road Building", "Year of Plenty", "Monopoly", "Victory Point" }
L.ROAD, L.SETTLE, L.CITY, L.CARD, L.HEX = 1, 2, 3, 4, 5
L.COST = { { 1, 1, 0, 0, 0 }, { 1, 1, 1, 1, 0 }, { 0, 0, 0, 2, 3 }, { 0, 0, 1, 1, 1 } }
L.WIN = 10

L.HV = "\001\002\003\004\005\006\007\008\009\010\003\002\011\012\013\014\009\008\005\004\015\016\017\018\003\010\019\020\015\004\009\014\021\022\019\010\013\023\024\025\021\014\017\016\026\027\028\029\015\020\030\031\026\016\019\022\032\033\030\020\021\025\034\035\032\022\024\036\037\038\034\025\026\031\039\040\041\027\030\033\042\043\039\031\032\035\044\045\042\033\034\038\046\047\044\035\039\043012\040\042\045340\043\044\047563\045"
L.EV = "\001\002\002\003\003\004\004\005\005\006\001\006\007\008\008\009\009\010\003\010\002\007\011\012\012\013\013\014\009\014\008\011\004\015\015\016\016\017\017\018\005\018\010\019\019\020\015\020\014\021\021\022\019\022\013\023\023\024\024\025\021\025\016\026\026\027\027\028\028\029\017\029\020\030\030\031\026\031\022\032\032\033\030\033\025\034\034\035\032\035\024\036\036\037\037\038\034\038\031\039\039\040\040\041\027\041\033\042\042\043\039\043\035\044\044\045\042\045\038\046\046\047\044\047\04300112\0402\04533404\04755636"
L.VX = "\003\004\004\003\002\002\005\006\006\005\007\008\008\007\003\002\001\001\005\004\007\006\009\009\008\002\001\000\000\004\003\006\005\008\007\010\010\009\003\002\001\005\004\007\006\009\008\004\003\002\006\005\008\007"
L.VY = "\000\001\003\004\003\001\000\001\003\004\000\001\003\004\006\007\006\004\006\007\006\007\004\006\007\009\010\009\007\009\010\009\010\009\010\007\009\010\012\013\012\012\013\012\013\012\013\015\016\015\015\016\015\016"
L.HX = "\003\005\007\002\004\006\008\001\003\005\007\009\002\004\006\008\003\005\007"
L.HY = "\002\002\002\005\005\005\005\008\008\008\008\008\011\011\011\011\014\014\014"
L.VH = "\001\000\000\001\002\000\001\002\005\001\004\005\001\004\000\001\000\000\002\000\000\002\003\000\002\003\006\002\005\006\003\000\000\003\000\000\003\007\000\003\006\007\004\005\009\004\008\009\004\008\000\004\000\000\005\006\010\005\009\010\006\007\011\006\010\011\007\000\000\007\012\000\007\011\012\008\009\013\008\013\000\008\000\000\008\000\000\009\010\014\009\013\014\010\011\015\010\014\015\011\012\016\011\015\016\012\000\000\012\000\000\012\016\000\013\014\017\013\017\000\013\000\000\014\015\018\014\017\018\015\016\019\015\018\019\016\000\000\016\019\000\017\018\000\017\000\000\017\000\000\018\019\000\018\000\000\019\000\000\019\000\000"
L.VN = "\002\006\000\001\003\007\002\004\010\003\005\015\004\006\018\005\001\000\008\002\000\007\009\011\008\010\014\009\003\019\012\008\000\011\013\000\012\014\023\013\009\021\004\016\020\015\017\026\016\018\029\017\005\000\010\020\022\019\015\030\014\022\025\021\019\032\013\024\000\023\025\036\024\021\034\016\027\031\026\028\041\027\029\000\028\017\000\020\031\033\030\026\039\022\033\035\032\030\042\025\035\038\034\032\044\024\037\000\036\038\000\037\034\046\031\040\043\039\0412\040\027\000\033\043\045\042\0390\035\045\047\044\0423\038\047\000\046\0445\0431402\0001\040\000\0454630\000\0476\00053\000"
L.VE = "\001\006\000\001\002\011\002\003\010\003\004\017\004\005\021\005\006\000\007\011\000\007\008\016\008\009\015\009\010\022\012\016\000\012\013\000\013\014\028\014\015\025\017\018\024\018\019\032\019\020\036\020\021\000\022\023\027\023\024\037\025\026\031\026\027\040\028\029\000\029\030\046\030\031\043\032\033\039\033\0345\034\035\000\035\036\000\037\038\042\038\0392\040\041\045\041\0426\043\0441\044\0459\046\047\000\0470\00001\06023834B45\00067\05978\0639\058\062\058\059C\060\061\000\061\062F\063\064E\064A\000AB\000CDHDE\000FG\000GH\000"
L.PE = "\036\005\011\013\046\060H\0644"
L.VP = "\000\003\000\000\002\002\003\000\000\000\000\004\004\000\000\000\001\000\000\000\000\000\000\005\000\000\000\000\001\000\000\000\000\000\000\005\000\006\000\009\009\000\000\000\000\006\000\008\008\000\007\000\000\007"
local HV, VH, VN, VE, EV, VP = L.HV, L.VH, L.VN, L.VE, L.EV, L.VP

function L.ends(e) return byte(EV, 2 * e - 1), byte(EV, 2 * e) end
function L.vn(v, k) return byte(VN, v * 3 - 3 + k) end
function L.ve(v, k) return byte(VE, v * 3 - 3 + k) end
function L.vh(v, k) return byte(VH, v * 3 - 3 + k) end
function L.hv(h, k) return byte(HV, h * 6 - 6 + k) end
function L.res(g, h) return byte(g.res, h) - 48 end
function L.pips(n)
    if not n or n < 2 or n == 7 or n > 12 then return 0 end
    return 6 - abs(7 - n)
end
function L.owner(g, v)
    local o = g.v[v]
    return o > 4 and o - 4 or o
end
-- The port at a corner: nil, 0 (3:1) or a resource (2:1).
function L.port(g, v)
    local s = byte(VP, v)
    if s > 0 then return byte(g.ports, s) - 48 end
end
function L.setup(g) return g.phase == "setupS" or g.phase == "setupR" end

-- Who must act now: the human discarding or answering an offer, or the
-- player whose turn it is.
function L.actor(g)
    if g.phase == "discard" or g.phase == "offer" then return g.human end
    return g.turn
end

function L.log(g, s)
    local t = g.log
    t[#t + 1] = s
    if #t > 6 then table.remove(t, 1) end
end

function L.count(g, p)
    local r, n = g.pl[p].r, 0
    for i = 1, 5 do n = n + r[i] end
    return n
end

-- Pieces left: 15 roads, 5 settlements, 4 cities.
function L.left(g, p, kind)
    local pl = g.pl[p]
    if kind == L.ROAD then return 15 - pl.nr end
    if kind == L.SETTLE then return 5 - pl.ns end
    return 4 - pl.nc
end

-- The bank's rate for resource r: 4, 3 with a 3:1 port, 2 with its own port.
function L.rate(g, p, r)
    local best = 4
    for v = 1, 54 do
        if L.owner(g, v) == p then
            local t = L.port(g, v)
            if t == r then return 2 end
            if t == 0 then best = 3 end
        end
    end
    return best
end

function L.vp(g, p, hidden)
    local pl = g.pl[p]
    local n = pl.ns + 2 * pl.nc
    if g.lr == p then n = n + 2 end
    if g.la == p then n = n + 2 end
    if hidden then n = n + pl.d[5] end
    return n
end

-- 1 + the number of players with more points (VP cards included).
function L.place(g, p)
    local n = 1
    for q = 1, 4 do
        if L.vp(g, q, true) > L.vp(g, p, true) then n = n + 1 end
    end
    return n
end

return L
