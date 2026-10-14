-- Hashi (bridges) rules. No drawing, so it can be tested on a computer
-- (test/lua_apps/logic_test.lua). Puzzles are made by hashigen.lua.
--
-- The grid is one string, row by row: "." for water, "1".."8" for an island
-- and the bridges it needs. Two islands are neighbours when nothing lies
-- between them on a row or a column; each pair of neighbours is an edge that
-- carries 0, 1 or 2 bridges. A puzzle keeps the edges as flat arrays (ea/eb
-- the islands, horiz the direction) and the bridges as one number per edge.

local H = {}

-- Islands and edges of a grid string.
function H.build(size, grid)
    local ir, ic, num, at, ea, eb, hz = {}, {}, {}, {}, {}, {}, {}
    local n = 0
    for i = 1, size * size do
        at[i] = 0
        local b = grid:byte(i)
        if b ~= 46 then
            n = n + 1
            ir[n], ic[n], num[n], at[i] = (i - 1) // size, (i - 1) % size, b - 48, n
        end
    end
    local E = 0
    for k = 1, n do
        local r, c = ir[k], ic[k]
        for cc = c + 1, size - 1 do
            local j = at[r * size + cc + 1]
            if j > 0 then
                E = E + 1
                ea[E], eb[E], hz[E] = k, j, 1
                break
            end
        end
        for rr = r + 1, size - 1 do
            local j = at[rr * size + c + 1]
            if j > 0 then
                E = E + 1
                ea[E], eb[E], hz[E] = k, j, 0
                break
            end
        end
    end
    local char, unpack = string.char, table.unpack
    return { size = size, grid = grid, islands = n, edges = E, ir = char(unpack(ir, 1, n)), ic = char(unpack(ic, 1, n)),
             num = char(unpack(num, 1, n)), at = char(unpack(at)), ea = char(unpack(ea, 1, E)),
             eb = char(unpack(eb, 1, E)), horiz = char(unpack(hz, 1, E)) }
end

-- Whether edges e and f cross (one across the other, not at an island).
function H.crosses(p, e, f)
    if p.horiz:byte(e) == p.horiz:byte(f) then return false end
    if p.horiz:byte(e) ~= 1 then e, f = f, e end
    local r, c1, c2 = p.ir:byte(p.ea:byte(e)), p.ic:byte(p.ea:byte(e)), p.ic:byte(p.eb:byte(e))
    local c, r1, r2 = p.ic:byte(p.ea:byte(f)), p.ir:byte(p.ea:byte(f)), p.ir:byte(p.eb:byte(f))
    return c1 < c and c < c2 and r1 < r and r < r2
end

-- The edge between islands a and b, or nil.
function H.edgeBetween(p, a, b)
    for e = 1, p.edges do
        if (p.ea:byte(e) == a and p.eb:byte(e) == b) or (p.ea:byte(e) == b and p.eb:byte(e) == a) then return e end
    end
    return nil
end

-- The island's neighbour in direction d (1 up, 2 down, 3 left, 4 right) and
-- the edge to it, or nil.
function H.neighbour(p, k, d)
    for e = 1, p.edges do
        local a, b = p.ea:byte(e), p.eb:byte(e)
        if (p.horiz:byte(e) == 1) == (d >= 3) then
            -- ea is always above / left of eb.
            if (d == 2 or d == 4) and a == k then return b, e end
            if (d == 1 or d == 3) and b == k then return a, e end
        end
    end
    return nil
end

-- Bridges at island k.
function H.sum(p, k)
    local s = 0
    for e = 1, p.edges do
        if p.ea:byte(e) == k or p.eb:byte(e) == k then s = s + p.cur[e] end
    end
    return s
end

-- A new puzzle: the grid and its solution, one digit per edge.
function H.new(size, grid, sol)
    local p = H.build(size, grid)
    p.sol, p.cur, p.elapsedMs, p.solved = sol, {}, 0, false
    for e = 1, p.edges do p.cur[e] = 0 end
    return p
end

function H.restart(p)
    for e = 1, p.edges do p.cur[e] = 0 end
    p.solved = false
end

-- Every island has its number of bridges and they join all islands.
function H.isSolved(p)
    for k = 1, p.islands do
        if H.sum(p, k) ~= p.num:byte(k) then return false end
    end
    local seen, stack, top, count = { [1] = true }, { 1 }, 1, 1
    while top > 0 do
        local k = stack[top]
        top = top - 1
        for e = 1, p.edges do
            if p.cur[e] > 0 then
                local o = (p.ea:byte(e) == k and p.eb:byte(e)) or (p.eb:byte(e) == k and p.ea:byte(e)) or nil
                if o and not seen[o] then
                    seen[o], count, top = true, count + 1, top + 1
                    stack[top] = o
                end
            end
        end
    end
    return count == p.islands
end

-- 0 -> 1 -> 2 -> 0 bridges on edge e. Refused (false, "cross") when a bridge
-- would cross another. Returns true when it changed; p.solved is updated.
function H.cycle(p, e)
    if p.solved then return false end
    local v = (p.cur[e] + 1) % 3
    if v > 0 then
        for f = 1, p.edges do
            if p.cur[f] > 0 and H.crosses(p, e, f) then return false, "cross" end
        end
    end
    p.cur[e] = v
    p.solved = H.isSolved(p)
    return true
end

-- Edges with more bridges than the solution has (the solution is unique).
function H.mistakes(p)
    local n = 0
    for e = 1, p.edges do
        if p.cur[e] > p.sol:byte(e) - 48 then n = n + 1 end
    end
    return n
end

function H.serialize(p)
    local cur = {}
    for e = 1, p.edges do cur[e] = p.cur[e] end
    return string.format("%d,%s,%s,%s,%d", p.size, p.grid, p.sol, table.concat(cur), p.elapsedMs)
end

function H.deserialize(s)
    local size, grid, sol, cur, ms = (s or ""):match("^(%d+),([%.%d]+),(%d*),(%d*),(%d+)$")
    size = tonumber(size)
    if not size or #grid ~= size * size then return nil end
    local p = H.new(size, grid, sol)
    if #sol ~= p.edges or #cur ~= p.edges then return nil end
    for e = 1, p.edges do p.cur[e] = cur:byte(e) - 48 end
    p.elapsedMs = tonumber(ms)
    p.solved = H.isSolved(p)
    return p
end

return H
