-- Co Ganh (cờ gánh) rules: what the computer's search needs - the board's
-- geometry, move generation and make/unmake with conversions. Setting up a
-- game, playing a real move, the results and the history are in game.lua. No
-- drawing, so both can be tested on a computer (test/lua_apps/coganh_test.lua).
--
-- The 25 points are a 5x5 grid numbered row by row from the top (row 0) -
-- point p is row (p-1)//5, column (p-1)%5. Every point is joined to its
-- orthogonal neighbours; the "strong" points, where row + column is even,
-- are also joined diagonally (the Alquerque board: the two long diagonals
-- and the diamond through the middles of the sides).
--
-- Board values: 0 empty, 1 White (bottom, moves first), 2 Black (top). A move
-- is one integer: from + to * 32.
--
-- A move converts enemy pieces (they change colour, nothing leaves the
-- board): "ganh" - the piece moved in sits between two enemy pieces on a line,
-- and both turn; then "vay" - every enemy group (pieces joined along the
-- lines) left with no empty point next to it turns. The mover's own pieces
-- never turn on their own move.

local C = {}

C.WHITE, C.BLACK = 1, 2
C.PIECES = 8

-- NB[(p-1)*8 + d]: the neighbour of p in direction d (0: none). Directions
-- 1..8 are N, NE, E, SE, S, SW, W, NW; d and d+4 are opposite.
local DR = { -1, -1, 0, 1, 1, 1, 0, -1 }
local DC = { 0, 1, 1, 1, 0, -1, -1, -1 }
local NB = {}
for p = 1, 25 do
    local r, c = (p - 1) // 5, (p - 1) % 5
    for d = 1, 8 do
        local rr, cc = r + DR[d], c + DC[d]
        local q = 0
        if rr >= 0 and rr < 5 and cc >= 0 and cc < 5 and (d % 2 == 1 or (r + c) % 2 == 0) then
            q = rr * 5 + cc + 1
        end
        NB[(p - 1) * 8 + d] = q
    end
end
C.NB = NB

function C.rc(p) return (p - 1) // 5, (p - 1) % 5 end
function C.strong(p) return ((p - 1) // 5 + (p - 1) % 5) % 2 == 0 end

-- The starting position, top row first: Black on the top row, both ends of
-- the second row and the right end of the middle row; White the mirror image.
C.START = "22222" .. "20002" .. "10002" .. "10001" .. "11111"

-- A new game. `forced`: the "mo" rule (a player must ganh into the point
-- the opponent has just opened).
function C.new(forced)
    local g = { b = {}, side = C.WHITE, on = { 0, 0 }, forced = forced and true or false,
                cv = {}, ct = 0, lf = {}, ply = 0 }
    for p = 1, 25 do
        local v = C.START:byte(p) - 48
        g.b[p] = v
        if v > 0 then g.on[v] = g.on[v] + 1 end
    end
    return g
end

-- How many enemy pieces colour s would ganh by moving into the empty point p.
local function ganhCount(b, p, s)
    local o, base, n = 3 - s, (p - 1) * 8, 0
    for d = 1, 4 do
        local a, c = NB[base + d], NB[base + d + 4]
        if a > 0 and c > 0 and b[a] == o and b[c] == o then n = n + 2 end
    end
    return n
end
C.ganhCount = ganhCount

-- Scratch for the group search (no allocation per move).
local MK, Q = {}, {}
for p = 1, 25 do MK[p] = 0 end
local stamp = 0

-- Plays mv. The converted points go on g.cv (then their count), so unmake
-- can turn them back; C.lastConverted(g) reads them.
function C.make(g, mv)
    local from, to = mv % 32, mv // 32
    local b, s = g.b, g.side
    local o = 3 - s
    b[from], b[to] = 0, s
    local cv, ct, n = g.cv, g.ct, 0
    -- Ganh.
    local base = (to - 1) * 8
    for d = 1, 4 do
        local a, c = NB[base + d], NB[base + d + 4]
        if a > 0 and c > 0 and b[a] == o and b[c] == o then
            b[a], b[c] = s, s
            cv[ct + n + 1], cv[ct + n + 2] = a, c
            n = n + 2
        end
    end
    -- Vay: enemy groups without an empty point next to them.
    if g.on[o] - n > 0 then
        stamp = stamp + 1
        for p = 1, 25 do
            if b[p] == o and MK[p] ~= stamp then
                MK[p] = stamp
                Q[1] = p
                local head, qn, free = 1, 1, false
                while head <= qn do
                    local x = (Q[head] - 1) * 8
                    head = head + 1
                    for d = 1, 8 do
                        local q = NB[x + d]
                        if q > 0 then
                            local v = b[q]
                            if v == 0 then
                                free = true
                            elseif v == o and MK[q] ~= stamp then
                                MK[q] = stamp
                                qn = qn + 1
                                Q[qn] = q
                            end
                        end
                    end
                end
                if not free then
                    for i = 1, qn do
                        b[Q[i]] = s
                        n = n + 1
                        cv[ct + n] = Q[i]
                    end
                end
            end
        end
    end
    ct = ct + n + 1
    cv[ct] = n
    g.ct = ct
    g.on[s], g.on[o] = g.on[s] + n, g.on[o] - n
    g.ply = g.ply + 1
    g.lf[g.ply] = from
    g.side = o
    return n
end

function C.unmake(g, mv)
    local from, to = mv % 32, mv // 32
    local b, cv, ct = g.b, g.cv, g.ct
    local o = g.side
    local s = 3 - o
    local n = cv[ct]
    for i = ct - n, ct - 1 do b[cv[i]] = o end
    g.ct = ct - n - 1
    g.on[s], g.on[o] = g.on[s] - n, g.on[o] + n
    g.ply = g.ply - 1
    b[to], b[from] = 0, s
    g.side = s
end

-- The points the last move converted, as a list.
function C.lastConverted(g)
    local t, ct = {}, g.ct
    local n = g.cv[ct] or 0
    for i = ct - n, ct - 1 do t[#t + 1] = g.cv[i] end
    return t
end

-- With the "mo" rule: the point the side to move must ganh into (the one
-- the opponent has just left), or 0.
function C.forcedPoint(g)
    if not g.forced or g.ply == 0 then return 0 end
    local f, b, s = g.lf[g.ply], g.b, g.side
    if f == 0 or b[f] ~= 0 or ganhCount(b, f, s) == 0 then return 0 end
    for d = 1, 8 do
        local q = NB[(f - 1) * 8 + d]
        if q > 0 and b[q] == s then return f end
    end
    return 0
end

-- Every legal move of the side to move, written to out[base+1..]. Returns
-- how many.
function C.gen(g, out, base)
    base = base or 0
    local s, b, n = g.side, g.b, base
    local f = C.forcedPoint(g)
    if f > 0 then
        for d = 1, 8 do
            local q = NB[(f - 1) * 8 + d]
            if q > 0 and b[q] == s then
                n = n + 1
                out[n] = q + f * 32
            end
        end
        return n - base
    end
    for from = 1, 25 do
        if b[from] == s then
            local x = (from - 1) * 8
            for d = 1, 8 do
                local to = NB[x + d]
                if to > 0 and b[to] == 0 then
                    n = n + 1
                    out[n] = from + to * 32
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
