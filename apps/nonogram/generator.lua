-- Nonogram line solver and puzzle generator. Usage:
--   local gen = smudge.dofile("generator.lua")(N)   -- N: the rules (logic.lua)
--   local picture = gen.generate(size, rand, maxTries)
--   local puzzle = N.newPuzzle(size, picture)
-- Only needed while a puzzle is being made, so the app drops it afterwards to
-- keep its memory for playing (the X3 gives a Lua app about 75 KB).
--
-- Puzzles are random pictures, mirrored left to right so they look like
-- sprites, kept only if the clues have exactly one solution that can be
-- reached one line at a time (no guessing ever needed).

return function(N)
local G = {}

-- Line solver ----------------------------------------------------------------
-- Works on module-level scratch arrays and plain functions (no closures or
-- tables per call), since generating a 12x12 puzzle solves thousands of lines.

local len, clue, known, any = 0, nil, nil, false
local canFill, canEmpty, starts, need = {}, {}, {}, {}

local function emptyOK(from, to)
    for i = from, to do
        if known[i] == 1 then return false end
    end
    return true
end

local function record()
    any = true
    local pos = 1
    for k = 1, #clue do
        for i = pos, starts[k] - 1 do canEmpty[i] = true end
        for i = starts[k], starts[k] + clue[k] - 1 do canFill[i] = true end
        pos = starts[k] + clue[k]
    end
    for i = pos, len do canEmpty[i] = true end
end

-- Place run k at or after `pos` (cells before `pos` are already decided).
local function place(k, pos)
    if k > #clue then
        if emptyOK(pos, len) then record() end
        return
    end
    local run = clue[k]
    for s = pos, len - need[k] + 1 do
        -- Cells pos..s-1 stay empty; once one of them is filled, stop.
        if s > pos and known[s - 1] == 1 then return end
        local fits = true
        for i = s, s + run - 1 do
            if known[i] == 2 then fits = false break end
        end
        local after = s + run
        if fits and after <= len and known[after] == 1 then fits = false end
        if fits then
            starts[k] = s
            place(k + 1, after + 1)
        end
    end
end

-- `line[i]` is 0 (unknown), 1 (filled) or 2 (empty). Updates `line` in place
-- with every cell all valid placements of `runs` agree on. Returns whether
-- anything changed, or nil when no placement fits.
function G.solveLine(n, runs, line)
    len, clue, known, any = n, runs, line, false
    for i = 1, n do canFill[i], canEmpty[i] = false, false end
    if #runs == 0 then
        if not emptyOK(1, n) then return nil end
        record()
    else
        need[#runs + 1] = 0
        for k = #runs, 1, -1 do need[k] = runs[k] + need[k + 1] + (k < #runs and 1 or 0) end
        place(1, 1)
    end
    known = nil
    if not any then return nil end
    local changed = false
    for i = 1, n do
        local v = line[i]
        if canFill[i] and not canEmpty[i] then
            v = 1
        elseif canEmpty[i] and not canFill[i] then
            v = 2
        end
        if v ~= line[i] then
            line[i] = v
            changed = true
        end
    end
    return changed
end

-- Solves the whole grid line by line. Returns true when every cell is decided
-- (the solution is unique and needs no guessing), false when the line solver
-- gets stuck, nil on a contradiction.
function G.lineSolvable(size, rowClues, colClues)
    local grid, line = {}, {}
    for i = 1, size * size do grid[i] = 0 end
    local dirty = true
    while dirty do
        dirty = false
        for r = 1, size do
            for c = 1, size do line[c] = grid[(r - 1) * size + c] end
            local changed = G.solveLine(size, rowClues[r], line)
            if changed == nil then return nil end
            if changed then
                dirty = true
                for c = 1, size do grid[(r - 1) * size + c] = line[c] end
            end
        end
        for c = 1, size do
            for r = 1, size do line[r] = grid[(r - 1) * size + c] end
            local changed = G.solveLine(size, colClues[c], line)
            if changed == nil then return nil end
            if changed then
                dirty = true
                for r = 1, size do grid[(r - 1) * size + c] = line[r] end
            end
        end
    end
    for i = 1, size * size do
        if grid[i] == 0 then return false end
    end
    return true
end

-- A random mirrored picture with about `density` of its cells filled, as a
-- "0"/"1" string row by row.
local function randomPicture(size, rand, density)
    local threshold = math.floor(density * 1000)
    local rows = {}
    for r = 1, size do
        local left = ""
        for _ = 1, (size + 1) // 2 do left = left .. (rand(1000) <= threshold and "1" or "0") end
        -- Mirror: an odd size shares the middle column.
        rows[r] = left .. left:sub(1, size // 2):reverse()
    end
    return table.concat(rows)
end

-- Generates a picture; `rand(n)` returns 1..n. Returns it as a "0"/"1"
-- string for N.newPuzzle() and how many pictures were tried, or nil after
-- `maxTries`.
function G.generate(size, rand, maxTries)
    for try = 1, maxTries or 400 do
        local picture = randomPicture(size, rand, 0.58)
        local rows, cols = N.cluesFor(size, picture)
        -- No blank rows or columns: they make dull pictures.
        local blank = false
        for i = 1, size do
            if #rows[i] == 0 or #cols[i] == 0 then blank = true end
        end
        if not blank and G.lineSolvable(size, rows, cols) then return picture, try end
        rows, cols = nil, nil
        collectgarbage("collect") -- the X3 has no room for many tries' worth of garbage
    end
    return nil
end

return G
end
