-- Kakuro rules. No drawing, so it can be tested on a computer
-- (test/lua_apps/logic_test.lua). Puzzles are made by kakurogen.lua.
--
-- The grid is one string, row by row: "#" for a black cell, "1".."9" for a
-- white cell and its digit in the solution. The first row and column are
-- black. A run is a line of white cells across or down; the black cell
-- before it holds its sum, the clue. The clues are worked out from the
-- solution: ca / cd are strings with one byte per cell, the across and down
-- sums of a black cell (0: no run). The player's digits are one number per
-- cell, 0 for empty.

local K = {}

-- t as a string of bytes, a piece at a time: unpacking all at once would
-- grow the Lua stack (by kilobytes on the X3).
local function pack(t)
    local s = ""
    for a = 1, #t, 32 do s = s .. string.char(table.unpack(t, a, math.min(#t, a + 31))) end
    return s
end

function K.new(size, grid)
    local n, ca, cd = size, {}, {}
    for i = 1, n * n do
        ca[i], cd[i] = 0, 0
        if grid:byte(i) == 35 then
            local s, j = 0, i + 1
            while (j - 1) % n > 0 and grid:byte(j) ~= 35 do
                s, j = s + grid:byte(j) - 48, j + 1
            end
            ca[i], s, j = s, 0, i + n
            while j <= n * n and grid:byte(j) ~= 35 do
                s, j = s + grid:byte(j) - 48, j + n
            end
            cd[i] = s
        end
    end
    local p = { size = n, grid = grid, ca = pack(ca), cd = pack(cd), cur = {}, elapsedMs = 0, solved = false }
    for i = 1, n * n do p.cur[i] = 0 end
    return p
end

function K.isWhite(p, i) return p.grid:byte(i) ~= 35 end

function K.isSolved(p)
    local g, cur = p.grid, p.cur
    for i = 1, p.size * p.size do
        local b = g:byte(i)
        if b ~= 35 and cur[i] ~= b - 48 then return false end
    end
    return true
end

-- Digit d (0 clears) in white cell i. Returns true when it changed.
function K.set(p, i, d)
    if p.solved or not K.isWhite(p, i) or p.cur[i] == d then return false end
    p.cur[i], p.marks = d, nil
    p.solved = K.isSolved(p)
    return true
end

function K.restart(p)
    for i = 1, p.size * p.size do p.cur[i] = 0 end
    p.solved, p.marks = false, nil
end

-- The run after black cell i (step 1 across, n down): 0 while a cell is
-- empty and nothing is wrong yet, 1 when full with the right sum and no
-- repeated digit, 2 when wrong (a repeat, a sum too high, or full and not
-- the sum).
function K.runState(p, i, step)
    local n, g, cur = p.size, p.grid, p.cur
    local want = step == 1 and p.ca:byte(i) or p.cd:byte(i)
    local sum, seen, empty, j = 0, 0, false, i + step
    while j <= n * n and (step == n or (j - 1) % n > 0) and g:byte(j) ~= 35 do
        local d = cur[j]
        if d == 0 then
            empty = true
        else
            if seen & (1 << d) ~= 0 then return 2 end
            seen, sum = seen | (1 << d), sum + d
        end
        j = j + step
    end
    if sum > want or (not empty and sum ~= want) then return 2 end
    return empty and 0 or 1
end

-- Digits that differ from the solution; p.marks lists them until the next change.
function K.mistakes(p)
    local n, marks = 0, {}
    for i = 1, p.size * p.size do
        local d = p.cur[i]
        if d > 0 and d ~= p.grid:byte(i) - 48 then
            n = n + 1
            marks[i] = true
        end
    end
    p.marks = n > 0 and marks or nil
    return n
end

function K.serialize(p)
    return string.format("%d,%s,%s,%d", p.size, p.grid, table.concat(p.cur), p.elapsedMs)
end

function K.deserialize(s)
    local size, grid, cur, ms = (s or ""):match("^(%d+),([#1-9]+),(%d+),(%d+)$")
    size = tonumber(size)
    if not size or #grid ~= size * size or #cur ~= size * size then return nil end
    local p = K.new(size, grid)
    for i = 1, size * size do
        if K.isWhite(p, i) then p.cur[i] = cur:byte(i) - 48 end
    end
    p.elapsedMs = tonumber(ms)
    p.solved = K.isSolved(p)
    return p
end

return K
