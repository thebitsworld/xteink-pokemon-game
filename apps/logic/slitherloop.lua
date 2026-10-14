-- Slitherlink loops and the grid's layout for slithergen.lua, loaded only
-- while they are made (so this code and the solving are not in memory
-- together). Usage:
--   local L = smudge.dofile("slitherloop.lua")()
--   local sol, clues = L.make(n, rand)
--   local T = L.topology(n)
--
-- A loop is the edge of a random blob of cells: grown cell by cell from one,
-- never closing a hole or touching itself at a corner.

return function()
local G = {}

-- Edge numbering as in slither.lua.
local function hEdge(n, r, c) return r * n + c + 1 end
local function vEdge(n, r, c) return n * (n + 1) + r * (n + 1) + c + 1 end
local function ends(n, e)
    local H = n * (n + 1)
    if e <= H then
        local r, c = (e - 1) // n, (e - 1) % n
        return r, c, r, c + 1
    end
    local r, c = (e - H - 1) // (n + 1), (e - H - 1) % (n + 1)
    return r, c, r + 1, c
end

-- A random blob of cells (inside[i], i = r * n + c + 1) with a single loop as
-- its edge. `fill`: percent of the cells.
function G.blob(n, rand, fill)
    local inside, cand = {}, {}
    for i = 1, n * n do inside[i] = false end
    inside[rand(n * n)] = true
    local count, target, fails = 1, n * n * fill // 100, 0
    local function at(r, c) return r >= 0 and r < n and c >= 0 and c < n and inside[r * n + c + 1] end
    local function insideNeighbours(i)
        local r, c, k = (i - 1) // n, (i - 1) % n, 0
        if at(r - 1, c) then k = k + 1 end
        if at(r + 1, c) then k = k + 1 end
        if at(r, c - 1) then k = k + 1 end
        if at(r, c + 1) then k = k + 1 end
        return k
    end
    -- No dot where two inside cells meet only at the corner.
    local function pinched(i)
        local r0, c0 = (i - 1) // n, (i - 1) % n
        for r = r0, r0 + 1 do
            for c = c0, c0 + 1 do
                if r > 0 and r < n and c > 0 and c < n then
                    local a, b, x, d = at(r - 1, c - 1), at(r - 1, c), at(r, c - 1), at(r, c)
                    if a == d and b == x and a ~= b then return true end
                end
            end
        end
        return false
    end
    -- Every outside cell reaches the border through outside cells.
    local reach, stack = {}, {}
    local function noHoles()
        local top = 0
        for i = 1, n * n do
            reach[i] = false
            local r, c = (i - 1) // n, (i - 1) % n
            if not inside[i] and (r == 0 or c == 0 or r == n - 1 or c == n - 1) then
                reach[i], top = true, top + 1
                stack[top] = i
            end
        end
        while top > 0 do
            local i = stack[top]
            top = top - 1
            local r, c = (i - 1) // n, (i - 1) % n
            for k = 1, 4 do
                local rr, cc = r + (k == 1 and -1 or k == 2 and 1 or 0), c + (k == 3 and -1 or k == 4 and 1 or 0)
                if rr >= 0 and rr < n and cc >= 0 and cc < n then
                    local j = rr * n + cc + 1
                    if not inside[j] and not reach[j] then
                        reach[j], top = true, top + 1
                        stack[top] = j
                    end
                end
            end
        end
        for i = 1, n * n do
            if not inside[i] and not reach[i] then return false end
        end
        return true
    end
    while count < target and fails < 4 * n * n do
        local m = 0
        for i = 1, n * n do
            if not inside[i] and insideNeighbours(i) > 0 then
                m = m + 1
                cand[m] = i
            end
        end
        if m == 0 then break end
        -- Of two random cells the one touching less of the blob: a wigglier loop.
        local i, j = cand[rand(m)], cand[rand(m)]
        if insideNeighbours(j) < insideNeighbours(i) then i = j end
        inside[i] = true
        if pinched(i) or not noHoles() then
            inside[i] = false
            fails = fails + 1
        else
            count = count + 1
        end
    end
    return inside
end

-- The loop around a blob as a solution string, and the clue of every cell.
function G.loopOf(n, inside)
    local function at(r, c) return r >= 0 and r < n and c >= 0 and c < n and inside[r * n + c + 1] end
    local sol = {}
    for r = 0, n do
        for c = 0, n - 1 do sol[hEdge(n, r, c)] = at(r - 1, c) ~= at(r, c) and 1 or 0 end
    end
    for r = 0, n - 1 do
        for c = 0, n do sol[vEdge(n, r, c)] = at(r, c - 1) ~= at(r, c) and 1 or 0 end
    end
    local clues = {}
    for r = 0, n - 1 do
        for c = 0, n - 1 do
            clues[r * n + c + 1] = sol[hEdge(n, r, c)] + sol[hEdge(n, r + 1, c)] + sol[vEdge(n, r, c)]
                + sol[vEdge(n, r, c + 1)]
        end
    end
    return table.concat(sol), clues
end

-- A random loop: the solution string and every cell's clue (a table).
function G.make(n, rand)
    return G.loopOf(n, G.blob(n, rand, 45 + rand(20)))
end

-- The grid's layout as strings, one byte per number (a table slot costs
-- 8-16 bytes, which the X3 cannot spare): the dots at each end of an edge
-- (ev1, ev2), the cells beside it (ec1, ec2; 0 none), four edges per dot
-- (vinc; 0 none) and the four sides of each cell (cedge).
function G.topology(n)
    local V, E, C, W = (n + 1) * (n + 1), 2 * n * (n + 1), n * n, n + 1
    -- A string of f(1..count), made 32 bytes at a time (a whole table of
    -- them would cost more than the strings).
    local buf = {}
    local function bytes(count, f)
        local parts = {}
        for i = 1, count, 32 do
            local m = math.min(32, count - i + 1)
            for j = 1, m do buf[j] = f(i + j - 1) end
            parts[#parts + 1] = string.char(table.unpack(buf, 1, m))
        end
        return table.concat(parts)
    end
    local function cellsBeside(e)
        local r1, c1, r2 = ends(n, e)
        local x, y
        if r1 == r2 then
            x, y = r1 > 0 and (r1 - 1) * n + c1 + 1 or 0, r1 < n and r1 * n + c1 + 1 or 0
        else
            x, y = c1 > 0 and r1 * n + c1 or 0, c1 < n and r1 * n + c1 + 1 or 0
        end
        if x == 0 then return y, 0 end
        return x, y
    end
    -- Four edges per dot (0: none), the four sides of a cell, by slot k.
    local function dotEdge(k)
        local v, s = (k - 1) // 4, (k - 1) % 4
        local r, c = v // W, v % W
        if s == 0 then return c > 0 and hEdge(n, r, c - 1) or 0 end
        if s == 1 then return c < n and hEdge(n, r, c) or 0 end
        if s == 2 then return r > 0 and vEdge(n, r - 1, c) or 0 end
        return r < n and vEdge(n, r, c) or 0
    end
    local function side(k)
        local i, s = (k - 1) // 4, (k - 1) % 4
        local r, c = i // n, i % n
        if s == 0 then return hEdge(n, r, c) end
        if s == 1 then return hEdge(n, r + 1, c) end
        if s == 2 then return vEdge(n, r, c) end
        return vEdge(n, r, c + 1)
    end
    return {
        ev1 = bytes(E, function(e)
            local r1, c1 = ends(n, e)
            return r1 * W + c1 + 1
        end),
        ev2 = bytes(E, function(e)
            local _, _, r2, c2 = ends(n, e)
            return r2 * W + c2 + 1
        end),
        ec1 = bytes(E, cellsBeside),
        ec2 = bytes(E, function(e)
            local _, y = cellsBeside(e)
            return y
        end),
        vinc = bytes(4 * V, dotEdge),
        cedge = bytes(4 * C, side),
    }
end

return G
end
