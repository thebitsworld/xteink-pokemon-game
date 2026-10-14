-- Slitherlink rules. No drawing, so it can be tested on a computer
-- (test/lua_apps/logic_test.lua). Puzzles are made by slithergen.lua.
--
-- An n x n grid of cells has (n + 1) x (n + 1) dots. Edges are numbered
-- horizontal ones first, row by row (row r = 0..n, column c = 0..n-1), then
-- vertical ones (row r = 0..n-1, column c = 0..n). The clues are one string,
-- row by row: "." or "0".."3", the lines around that cell. The solution is a
-- string with "1" for each edge of the loop; the player's edges are numbers:
-- 0 empty, 1 line, 2 cross (sure to be empty).

local S = {}
S.EMPTY, S.LINE, S.CROSS = 0, 1, 2

function S.edgeCount(n) return 2 * n * (n + 1) end
function S.hEdge(n, r, c) return r * n + c + 1 end
function S.vEdge(n, r, c) return n * (n + 1) + r * (n + 1) + c + 1 end

-- The edge's ends as dots: row, column of each.
function S.ends(n, e)
    local H = n * (n + 1)
    if e <= H then
        local r, c = (e - 1) // n, (e - 1) % n
        return r, c, r, c + 1
    end
    local r, c = (e - H - 1) // (n + 1), (e - H - 1) % (n + 1)
    return r, c, r + 1, c
end

-- Lines around cell (r, c) in the edge states cur.
local function around(n, r, c, cur)
    local k = 0
    if cur[S.hEdge(n, r, c)] == 1 then k = k + 1 end
    if cur[S.hEdge(n, r + 1, c)] == 1 then k = k + 1 end
    if cur[S.vEdge(n, r, c)] == 1 then k = k + 1 end
    if cur[S.vEdge(n, r, c + 1)] == 1 then k = k + 1 end
    return k
end
function S.linesAround(p, r, c) return around(p.size, r, c, p.cur) end

function S.new(size, clues, sol)
    local p = { size = size, clues = clues, sol = sol, cur = {}, elapsedMs = 0, solved = false }
    for e = 1, S.edgeCount(size) do p.cur[e] = 0 end
    return p
end

function S.restart(p)
    for e = 1, S.edgeCount(p.size) do p.cur[e] = 0 end
    p.solved = false
end

-- The clue of cell (r, c) or nil.
function S.clue(p, r, c)
    local b = p.clues:byte(r * p.size + c + 1)
    if b == 46 then return nil end
    return b - 48
end

-- Every clue is met and the lines make one closed loop.
function S.isSolved(p)
    local n, cur = p.size, p.cur
    for r = 0, n - 1 do
        for c = 0, n - 1 do
            local k = S.clue(p, r, c)
            if k and around(n, r, c, cur) ~= k then return false end
        end
    end
    -- Every dot has 0 or 2 lines; then walk the loop from one line.
    local deg, lines, first = {}, 0, nil
    for e = 1, S.edgeCount(n) do
        if cur[e] == 1 then
            local r1, c1, r2, c2 = S.ends(n, e)
            local a, b = r1 * (n + 1) + c1 + 1, r2 * (n + 1) + c2 + 1
            deg[a], deg[b] = (deg[a] or 0) + 1, (deg[b] or 0) + 1
            lines, first = lines + 1, first or e
        end
    end
    if lines == 0 then return false end
    for _, d in pairs(deg) do
        if d ~= 2 then return false end
    end
    -- Follow the loop: from the end of the current edge take the other line.
    local walked, e, from = 1, first, nil
    local r1, c1 = S.ends(n, first)
    local start = r1 * (n + 1) + c1 + 1
    from = start
    while true do
        local a1, b1, a2, b2 = S.ends(n, e)
        local va, vb = a1 * (n + 1) + b1 + 1, a2 * (n + 1) + b2 + 1
        local at = (va == from) and vb or va
        if at == start then break end
        -- The other line at dot `at`.
        local r, c = (at - 1) // (n + 1), (at - 1) % (n + 1)
        local nextE = nil
        local cand = { c > 0 and S.hEdge(n, r, c - 1), c < n and S.hEdge(n, r, c),
                       r > 0 and S.vEdge(n, r - 1, c), r < n and S.vEdge(n, r, c) }
        for i = 1, 4 do
            local f = cand[i]
            if f and f ~= e and cur[f] == 1 then nextE = f end
        end
        e, from, walked = nextE, at, walked + 1
        if walked > lines then return false end
    end
    return walked == lines
end

-- empty -> line -> cross -> empty on edge e (or to `state` when given).
function S.cycle(p, e, state)
    if p.solved then return false end
    p.cur[e] = state or (p.cur[e] + 1) % 3
    p.solved = S.isSolved(p)
    return true
end

-- Lines off the loop and crosses on it (the solution is unique).
function S.mistakes(p)
    local n = 0
    for e = 1, S.edgeCount(p.size) do
        local s = p.sol:byte(e) - 48
        if (p.cur[e] == 1 and s == 0) or (p.cur[e] == 2 and s == 1) then n = n + 1 end
    end
    return n
end

function S.serialize(p)
    local cur = {}
    for e = 1, S.edgeCount(p.size) do cur[e] = p.cur[e] end
    return string.format("%d,%s,%s,%s,%d", p.size, p.clues, p.sol, table.concat(cur), p.elapsedMs)
end

function S.deserialize(s)
    local size, clues, sol, cur, ms = (s or ""):match("^(%d+),([%.%d]+),(%d+),(%d+),(%d+)$")
    size = tonumber(size)
    if not size or #clues ~= size * size then return nil end
    local E = S.edgeCount(size)
    if #sol ~= E or #cur ~= E then return nil end
    local p = S.new(size, clues, sol)
    for e = 1, E do p.cur[e] = cur:byte(e) - 48 end
    p.elapsedMs = tonumber(ms)
    p.solved = S.isSolved(p)
    return p
end

return S
