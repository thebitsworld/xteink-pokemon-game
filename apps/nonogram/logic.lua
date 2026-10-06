-- Nonogram rules. No drawing, so it can be tested on a computer
-- (test/lua_apps/nonogram_test.lua). Puzzles are made by generator.lua.
--
-- The numbers beside each row and above each column are the lengths of its
-- runs of filled cells, in order.

local N = {}

-- Player cell states.
N.UNKNOWN, N.FILLED, N.MARKED, N.MISTAKE = 0, 1, 2, 3

-- Runs of true values in a line (a list of booleans), e.g. {3, 1}.
function N.runs(line)
    local out, run = {}, 0
    for i = 1, #line do
        if line[i] then
            run = run + 1
        elseif run > 0 then
            out[#out + 1] = run
            run = 0
        end
    end
    if run > 0 then out[#out + 1] = run end
    return out
end

-- Puzzles --------------------------------------------------------------------
-- A puzzle keeps its picture as one string of "0"/"1" (row by row) and the
-- player's cells as one string per row of state digits: a 12x12 puzzle as Lua
-- tables would take several KB of the X3's 75 KB.

-- One scratch line for cluesFor (a puzzle is made of many tries).
local scratch = {}

-- Clues of a picture given as a list of booleans or a "0"/"1" string.
function N.cluesFor(size, picture)
    local isString = type(picture) == "string"
    local function solid(i)
        if isString then return picture:byte(i) == 49 end
        return picture[i]
    end
    local rows, cols = {}, {}
    for i = size + 1, #scratch do scratch[i] = nil end
    for r = 1, size do
        for c = 1, size do scratch[c] = solid((r - 1) * size + c) end
        rows[r] = N.runs(scratch)
    end
    for c = 1, size do
        for r = 1, size do scratch[r] = solid((r - 1) * size + c) end
        cols[c] = N.runs(scratch)
    end
    return rows, cols
end

-- `solution` is the picture as a "0"/"1" string, row by row.
function N.newPuzzle(size, solution)
    local p = { size = size, solution = solution, cells = {}, mistakes = 0, solved = false, elapsedMs = 0,
                filled = 0 }
    p.rowClues, p.colClues = N.cluesFor(size, solution)
    local empty = string.rep(tostring(N.UNKNOWN), size)
    for r = 1, size do p.cells[r] = empty end
    local _, count = solution:gsub("1", "")
    p.toFill = count
    return p
end

function N.solid(p, c, r) return p.solution:byte((r - 1) * p.size + c) == 49 end
function N.cell(p, c, r) return p.cells[r]:byte(c) - 48 end

local function setCell(p, c, r, v)
    local row = p.cells[r]
    p.cells[r] = row:sub(1, c - 1) .. v .. row:sub(c + 1)
end

-- Fills a cell. A cell that is not in the picture becomes a locked mistake
-- instead, so a filled cell is always a correct one. Returns "filled",
-- "mistake" or nil (nothing changed).
function N.fill(p, c, r)
    if p.solved or N.cell(p, c, r) ~= N.UNKNOWN then return nil end
    if N.solid(p, c, r) then
        setCell(p, c, r, N.FILLED)
        p.filled = p.filled + 1
        if p.filled == p.toFill then p.solved = true end
        return "filled"
    end
    setCell(p, c, r, N.MISTAKE)
    p.mistakes = p.mistakes + 1
    return "mistake"
end

-- Marks a cell as empty, or clears the mark. Never a mistake.
function N.toggleMark(p, c, r)
    if p.solved then return false end
    local v = N.cell(p, c, r)
    if v == N.UNKNOWN then
        setCell(p, c, r, N.MARKED)
    elseif v == N.MARKED then
        setCell(p, c, r, N.UNKNOWN)
    else
        return false
    end
    return true
end

-- A line is done when its filled cells add up to its clue (only correct cells
-- can be filled, so this means it is solved).
local function lineDone(p, clue, filledCount)
    local total = 0
    for _, n in ipairs(clue) do total = total + n end
    return filledCount == total
end

function N.rowDone(p, r)
    local _, n = p.cells[r]:gsub(tostring(N.FILLED), "")
    return lineDone(p, p.rowClues[r], n)
end

function N.colDone(p, c)
    local n = 0
    for r = 1, p.size do
        if N.cell(p, c, r) == N.FILLED then n = n + 1 end
    end
    return lineDone(p, p.colClues[c], n)
end

-- Save/restore: size | mistakes | elapsed | picture bits | cell states.
function N.serialize(p)
    return table.concat({ p.size, p.mistakes, p.elapsedMs, p.solution, table.concat(p.cells) }, "|")
end

function N.deserialize(s)
    local size, mistakes, elapsed, bits, cells = s:match("^(%d+)|(%d+)|(%d+)|([01]+)|([0-3]+)$")
    size = tonumber(size)
    if not size or size < 2 or size > 20 or #bits ~= size * size or #cells ~= size * size then return nil end
    local p = N.newPuzzle(size, bits)
    p.mistakes, p.elapsedMs = tonumber(mistakes), tonumber(elapsed)
    for i = 1, size * size do
        local v, solid = cells:byte(i) - 48, bits:byte(i) == 49
        -- The states must agree with the picture.
        if (v == N.FILLED and not solid) or (v == N.MISTAKE and solid) then return nil end
        if v == N.FILLED then p.filled = p.filled + 1 end
    end
    for r = 1, size do p.cells[r] = cells:sub((r - 1) * size + 1, r * size) end
    p.solved = p.filled == p.toFill
    return p
end

return N
