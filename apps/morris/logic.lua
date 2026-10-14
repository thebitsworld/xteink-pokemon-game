-- Nine Men's Morris rules: what the computer's search needs - the board's
-- geometry, move generation and make/unmake. Setting up a game, playing a
-- real move, the results and the history are in game.lua. No drawing, so both
-- can be tested on a computer (test/lua_apps/morris_test.lua).
--
-- The 24 points sit on a 7x7 grid (C.xy(p): column and row, row 0 at the
-- top), numbered row by row:
--
--    1 ----------- 2 ----------- 3
--    |    4 ------ 5 ------ 6    |
--    |    |    7 - 8 - 9    |    |
--   10 - 11 - 12       13 - 14 - 15
--    |    |   16 - 17 - 18  |    |
--    |   19 ----- 20 ----- 21    |
--   22 ---------- 23 ---------- 24
--
-- The 16 lines of three (C.LINES, C.line) are the mills, and two points are joined
-- when they are next to each other on a line. Each point lies on exactly two
-- lines. Board values: 0 empty, 1 White, 2 Black. A move is one integer:
-- from + to * 32 + removed * 1024, from 0 for a placement, removed 0 when
-- the move closes no mill.

local C = {}

C.WHITE, C.BLACK = 1, 2
C.PIECES = 9

-- Columns and rows of the points, one digit each ("xy" per point).
local XY = "003060113151223242031323435363243444153555063666"
function C.xy(p) return XY:byte(p * 2 - 1) - 48, XY:byte(p * 2) - 48 end
-- The lines, three points each (two letters per point: "01" is point 1).
C.LINES = "010203040506070809101112131415161718192021222324" ..
          "011022041119071216020508172023091318061421031524"
function C.line(l, k) return tonumber(C.LINES:sub((l - 1) * 6 + k * 2 - 1, (l - 1) * 6 + k * 2)) end

-- PL1[p], PL2[p]: the two lines through p. ADJ[p]: the points joined to p,
-- five bits each (read with % 32 and // 32 until it is 0).
local PL1, PL2, ADJ = {}, {}, {}
for p = 1, 24 do ADJ[p] = 0 end
local function join(a, b)
    local v, mul = ADJ[a], 1
    while v >= mul do mul = mul * 32 end
    ADJ[a] = v + b * mul
end
for l = 1, 16 do
    local a, b, c = C.line(l, 1), C.line(l, 2), C.line(l, 3)
    for _, p in ipairs({ a, b, c }) do
        if PL1[p] then PL2[p] = l else PL1[p] = l end
    end
    join(a, b); join(b, a); join(b, c); join(c, b)
end
C.PL1, C.PL2, C.ADJ = PL1, PL2, ADJ

-- An empty board, White to place first. `flying`: a player down to three
-- pieces may move to any empty point.
function C.new(flying)
    local g = { b = {}, lc = {}, side = C.WHITE, hand = { C.PIECES, C.PIECES }, on = { 0, 0 },
                flying = flying ~= false }
    for p = 1, 24 do g.b[p] = 0 end
    -- lc[l*2-1], lc[l*2]: White's and Black's pieces on line l.
    for i = 1, 32 do g.lc[i] = 0 end
    return g
end

local function put(g, p, c)
    g.b[p] = c
    local lc = g.lc
    local i = PL1[p] * 2 - 2 + c
    lc[i] = lc[i] + 1
    i = PL2[p] * 2 - 2 + c
    lc[i] = lc[i] + 1
    g.on[c] = g.on[c] + 1
end

local function take(g, p)
    local c = g.b[p]
    g.b[p] = 0
    local lc = g.lc
    local i = PL1[p] * 2 - 2 + c
    lc[i] = lc[i] - 1
    i = PL2[p] * 2 - 2 + c
    lc[i] = lc[i] - 1
    g.on[c] = g.on[c] - 1
end
C.put, C.take = put, take

-- Whether the piece on p (colour c) is in a mill.
local function inMill(g, p, c)
    local lc = g.lc
    return lc[PL1[p] * 2 - 2 + c] == 3 or lc[PL2[p] * 2 - 2 + c] == 3
end
function C.inMill(g, p) return g.b[p] ~= 0 and inMill(g, p, g.b[p]) end

-- Whether colour c moving from `from` (0: from the hand) to `to` closes a mill.
local function closes(g, from, to, c)
    local lc = g.lc
    local l = PL1[to]
    local k = lc[l * 2 - 2 + c] + 1
    if from > 0 and (PL1[from] == l or PL2[from] == l) then k = k - 1 end
    if k == 3 then return true end
    l = PL2[to]
    k = lc[l * 2 - 2 + c] + 1
    if from > 0 and (PL1[from] == l or PL2[from] == l) then k = k - 1 end
    return k == 3
end
C.closes = closes

-- 1 placing, 2 moving, 3 flying: the phase of colour c.
function C.phase(g, c)
    if g.hand[c] > 0 then return 1 end
    if g.flying and g.on[c] == 3 then return 3 end
    return 2
end

-- Whether the side to move has lost on pieces (fewer than three left).
function C.lost(g)
    local s = g.side
    return g.on[s] + g.hand[s] < 3
end

function C.make(g, mv)
    local from, to, rem = mv % 32, (mv // 32) % 32, mv // 1024
    local s = g.side
    if from == 0 then g.hand[s] = g.hand[s] - 1 else take(g, from) end
    put(g, to, s)
    if rem > 0 then take(g, rem) end
    g.side = 3 - s
end

function C.unmake(g, mv)
    local from, to, rem = mv % 32, (mv // 32) % 32, mv // 1024
    local s = 3 - g.side
    g.side = s
    if rem > 0 then put(g, rem, 3 - s) end
    take(g, to)
    if from == 0 then g.hand[s] = g.hand[s] + 1 else put(g, from, s) end
end

-- The opponent's pieces a mill may take, in REM[1..nr]: those not in a mill,
-- or all of them when every one is in a mill. Scratch shared by every call
-- (it is used up before the search goes deeper).
local REM = {}
local function removable(g, o)
    local b, nr = g.b, 0
    for p = 1, 24 do
        if b[p] == o and not inMill(g, p, o) then
            nr = nr + 1
            REM[nr] = p
        end
    end
    if nr == 0 then
        for p = 1, 24 do
            if b[p] == o then
                nr = nr + 1
                REM[nr] = p
            end
        end
    end
    return nr
end

local function emit(g, out, n, from, to, s, nr)
    local mv = from + to * 32
    if nr > 0 and closes(g, from, to, s) then
        for i = 1, nr do
            n = n + 1
            out[n] = mv + REM[i] * 1024
        end
    else
        n = n + 1
        out[n] = mv
    end
    return n
end

-- Every legal move of the side to move, written to out[base+1..]. Returns
-- how many. (A side that has lost on pieces still gets its moves: callers
-- check C.lost first.)
function C.gen(g, out, base)
    base = base or 0
    local s = g.side
    local b = g.b
    local nr = removable(g, 3 - s)
    local n = base
    local ph = C.phase(g, s)
    if ph == 1 then
        for to = 1, 24 do
            if b[to] == 0 then n = emit(g, out, n, 0, to, s, nr) end
        end
    elseif ph == 3 then
        for from = 1, 24 do
            if b[from] == s then
                for to = 1, 24 do
                    if b[to] == 0 then n = emit(g, out, n, from, to, s, nr) end
                end
            end
        end
    else
        for from = 1, 24 do
            if b[from] == s then
                local a = ADJ[from]
                while a > 0 do
                    local to = a % 32
                    a = a // 32
                    if b[to] == 0 then n = emit(g, out, n, from, to, s, nr) end
                end
            end
        end
    end
    return n - base
end

-- The legal moves as a list (`out` reused if given).
function C.legalMoves(g, out)
    out = out or {}
    local n = C.gen(g, out, 0)
    for i = #out, n + 1, -1 do out[i] = nil end
    return out
end

return C
