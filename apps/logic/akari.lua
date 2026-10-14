-- Light Up (Akari) rules. No drawing, so it can be tested on a computer
-- (test/lua_apps/logic_test.lua). Puzzles are made by akarigen.lua.
--
-- The grid is one string, row by row: "." for a white cell, "#" for a black
-- cell, "0".."4" for a black cell with a number. A bulb lights its cell and
-- every white cell in its row and column up to a black cell. The solution
-- is a string with "1" for each bulb. The player's cells are numbers: 0
-- empty, 1 bulb, 2 dot (sure to have no bulb).

local A = {}
A.EMPTY, A.BULB, A.DOT = 0, 1, 2

function A.isWhite(p, i) return p.grid:byte(i) == 46 end

-- The number of black cell i, or nil.
function A.number(p, i)
    local b = p.grid:byte(i)
    if b >= 48 and b <= 52 then return b - 48 end
    return nil
end

function A.new(size, grid, sol)
    local p = { size = size, grid = grid, sol = sol, cur = {}, elapsedMs = 0, solved = false }
    for i = 1, size * size do p.cur[i] = 0 end
    return p
end

function A.restart(p)
    for i = 1, p.size * p.size do p.cur[i] = 0 end
    p.solved, p.marks = false, nil
end

-- How many bulbs light each cell: lit[i] (a bulb counts itself; on a bulb,
-- more than 1 means it sees another bulb).
function A.lighting(p)
    local n, cur, lit = p.size, p.cur, {}
    for i = 1, n * n do lit[i] = 0 end
    for i = 1, n * n do
        if cur[i] == 1 then
            lit[i] = lit[i] + 1
            local r, c = (i - 1) // n, (i - 1) % n
            for d = 1, 4 do
                local dr = d == 1 and -1 or d == 2 and 1 or 0
                local dc = d == 3 and -1 or d == 4 and 1 or 0
                local rr, cc = r + dr, c + dc
                while rr >= 0 and rr < n and cc >= 0 and cc < n and A.isWhite(p, rr * n + cc + 1) do
                    local j = rr * n + cc + 1
                    lit[j] = lit[j] + 1
                    rr, cc = rr + dr, cc + dc
                end
            end
        end
    end
    return lit
end

-- Bulbs next to black cell i.
function A.bulbsAround(p, i)
    local n, k = p.size, 0
    local c = (i - 1) % n
    if i > n and p.cur[i - n] == 1 then k = k + 1 end
    if i <= n * n - n and p.cur[i + n] == 1 then k = k + 1 end
    if c > 0 and p.cur[i - 1] == 1 then k = k + 1 end
    if c < n - 1 and p.cur[i + 1] == 1 then k = k + 1 end
    return k
end

-- Every white cell lit, no bulb sees another, every number met.
function A.isSolved(p)
    local lit = A.lighting(p)
    for i = 1, p.size * p.size do
        if A.isWhite(p, i) then
            if lit[i] == 0 or (p.cur[i] == 1 and lit[i] > 1) then return false end
        else
            local k = A.number(p, i)
            if k and A.bulbsAround(p, i) ~= k then return false end
        end
    end
    return true
end

-- empty -> bulb -> dot -> empty on white cell i (or to `state` when given).
-- Returns true when it changed.
function A.cycle(p, i, state)
    if p.solved or not A.isWhite(p, i) then return false end
    p.cur[i] = state or (p.cur[i] + 1) % 3
    p.marks = nil
    p.solved = A.isSolved(p)
    return true
end

-- Bulbs off the solution and dots on it (the solution is unique); p.marks
-- lists them until the next change.
function A.mistakes(p)
    local n, marks = 0, {}
    for i = 1, p.size * p.size do
        local s, v = p.sol:byte(i) - 48, p.cur[i]
        if (v == 1 and s == 0) or (v == 2 and s == 1) then
            n = n + 1
            marks[i] = true
        end
    end
    p.marks = n > 0 and marks or nil
    return n
end

function A.serialize(p)
    return string.format("%d,%s,%s,%s,%d", p.size, p.grid, p.sol, table.concat(p.cur), p.elapsedMs)
end

function A.deserialize(s)
    local size, grid, sol, cur, ms = (s or ""):match("^(%d+),([%.#0-4]+),([01]+),([012]+),(%d+)$")
    size = tonumber(size)
    if not size or #grid ~= size * size or #sol ~= size * size or #cur ~= size * size then return nil end
    local p = A.new(size, grid, sol)
    for i = 1, size * size do
        if A.isWhite(p, i) then p.cur[i] = cur:byte(i) - 48 end
    end
    p.elapsedMs = tonumber(ms)
    p.solved = A.isSolved(p)
    return p
end

return A
