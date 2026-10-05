-- Connect Four rules and computer opponent. No drawing, so it can be tested on
-- a computer (test/lua_apps/connectfour_test.lua).
--
-- The board is a flat array of COLS * ROWS cells, row 1 at the bottom:
-- 0 empty, 1 first player, 2 second player.

local C4 = {}
C4.COLS, C4.ROWS = 7, 6
local COLS, ROWS = C4.COLS, C4.ROWS

local function idx(c, r) return (r - 1) * COLS + c end
C4.index = idx

function C4.new()
    local g = { cells = {}, heights = {}, moves = 0, turn = 1, winner = 0, line = nil }
    for i = 1, COLS * ROWS do g.cells[i] = 0 end
    for c = 1, COLS do g.heights[c] = 0 end
    return g
end

function C4.canDrop(g, c)
    return g.winner == 0 and c >= 1 and c <= COLS and g.heights[c] < ROWS
end

local DIRS = { { 1, 0 }, { 0, 1 }, { 1, 1 }, { 1, -1 } }

-- The four-in-a-row through (c, r) for `who`, as a list of {c, r}, or nil.
local function lineThrough(g, c, r, who)
    for _, d in ipairs(DIRS) do
        local cells = { { c, r } }
        for _, sign in ipairs({ 1, -1 }) do
            local nc, nr = c + d[1] * sign, r + d[2] * sign
            while nc >= 1 and nc <= COLS and nr >= 1 and nr <= ROWS and g.cells[idx(nc, nr)] == who do
                cells[#cells + 1] = { nc, nr }
                nc, nr = nc + d[1] * sign, nr + d[2] * sign
            end
        end
        if #cells >= 4 then return cells end
    end
    return nil
end

-- Drops the current player's disc in column c. Returns the row, or nil.
function C4.drop(g, c)
    if not C4.canDrop(g, c) then return nil end
    local r = g.heights[c] + 1
    local who = g.turn
    g.heights[c] = r
    g.cells[idx(c, r)] = who
    g.moves = g.moves + 1
    local line = lineThrough(g, c, r, who)
    if line then
        g.winner, g.line = who, line
    elseif g.moves == COLS * ROWS then
        g.winner = -1 -- draw
    end
    g.turn = 3 - who
    return r
end

-- Search (negamax with alpha-beta) ------------------------------------------

-- Score of one 4-cell window for `who`: favours open threes and twos, and
-- blocks the opponent's.
local function windowScore(mine, theirs, empty)
    if mine == 4 then return 1000 end
    if mine == 3 and empty == 1 then return 5 end
    if mine == 2 and empty == 2 then return 2 end
    if theirs == 3 and empty == 1 then return -4 end
    return 0
end

local function evaluate(cells, who)
    local other = 3 - who
    local score = 0
    for r = 1, ROWS do -- centre column control
        if cells[idx(4, r)] == who then score = score + 3 end
    end
    for r = 1, ROWS do
        for c = 1, COLS do
            for _, d in ipairs(DIRS) do
                local ec, er = c + d[1] * 3, r + d[2] * 3
                if ec >= 1 and ec <= COLS and er >= 1 and er <= ROWS then
                    local mine, theirs, empty = 0, 0, 0
                    for k = 0, 3 do
                        local v = cells[idx(c + d[1] * k, r + d[2] * k)]
                        if v == who then mine = mine + 1 elseif v == other then theirs = theirs + 1 else empty = empty + 1 end
                    end
                    score = score + windowScore(mine, theirs, empty)
                end
            end
        end
    end
    return score
end

local ORDER = { 4, 3, 5, 2, 6, 1, 7 } -- centre first: better pruning
local WIN = 100000

local function negamax(g, depth, alpha, beta, who)
    local best = -math.huge
    local any = false
    for _, c in ipairs(ORDER) do
        if g.heights[c] < ROWS then
            any = true
            local r = g.heights[c] + 1
            g.heights[c] = r
            g.cells[idx(c, r)] = who
            local value
            if lineThrough(g, c, r, who) then
                value = WIN + depth -- win sooner rather than later
            elseif depth <= 1 then
                value = evaluate(g.cells, who)
            else
                value = -negamax(g, depth - 1, -beta, -alpha, 3 - who)
            end
            g.cells[idx(c, r)] = 0
            g.heights[c] = r - 1
            if value > best then best = value end
            if value > alpha then alpha = value end
            if alpha >= beta then break end
        end
    end
    if not any then return 0 end -- full board: draw
    return best
end

-- The column the player to move should play, searching `depth` plies.
-- `rand(n)` (1..n) breaks ties between equally good columns.
function C4.bestMove(g, depth, rand)
    local who = g.turn
    local bestScore, choices = -math.huge, {}
    for _, c in ipairs(ORDER) do
        if g.heights[c] < ROWS then
            local r = g.heights[c] + 1
            g.heights[c] = r
            g.cells[idx(c, r)] = who
            local value
            if lineThrough(g, c, r, who) then
                value = WIN * 2
            elseif depth <= 1 then
                value = evaluate(g.cells, who)
            else
                value = -negamax(g, depth - 1, -math.huge, math.huge, 3 - who)
            end
            g.cells[idx(c, r)] = 0
            g.heights[c] = r - 1
            if value > bestScore then
                bestScore, choices = value, { c }
            elseif value == bestScore then
                choices[#choices + 1] = c
            end
        end
    end
    if #choices == 0 then return nil end
    return choices[rand and rand(#choices) or 1]
end

return C4
