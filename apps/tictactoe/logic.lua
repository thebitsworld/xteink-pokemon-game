-- Tic-Tac-Toe rules and computer opponent. No drawing, so it can be tested on
-- a computer (test/lua_apps/tictactoe_test.lua).
--
-- The board is a flat array of 9 cells, row by row from the top left:
-- 0 empty, 1 X (always moves first), 2 O.

local T = {}

local LINES = {
    { 1, 2, 3 }, { 4, 5, 6 }, { 7, 8, 9 }, -- rows
    { 1, 4, 7 }, { 2, 5, 8 }, { 3, 6, 9 }, -- columns
    { 1, 5, 9 }, { 3, 5, 7 },              -- diagonals
}
T.LINES = LINES

function T.new()
    local g = { cells = {}, moves = 0, turn = 1, winner = 0, line = nil }
    for i = 1, 9 do g.cells[i] = 0 end
    return g
end

function T.canPlay(g, i)
    return g.winner == 0 and i >= 1 and i <= 9 and g.cells[i] == 0
end

-- The completed line of `who`, or nil.
local function lineOf(cells, who)
    for _, l in ipairs(LINES) do
        if cells[l[1]] == who and cells[l[2]] == who and cells[l[3]] == who then return l end
    end
    return nil
end

-- Plays the current player's mark in cell i. Returns true if it was legal.
function T.play(g, i)
    if not T.canPlay(g, i) then return false end
    local who = g.turn
    g.cells[i] = who
    g.moves = g.moves + 1
    local line = lineOf(g.cells, who)
    if line then
        g.winner, g.line = who, line
    elseif g.moves == 9 then
        g.winner = -1 -- draw
    end
    g.turn = 3 - who
    return true
end

-- Search ---------------------------------------------------------------------

-- Score for `who` to move: +10 - depth for a win (sooner is better), 0 draw.
local function negamax(cells, who, depth, alpha, beta)
    local other = 3 - who
    local best, any = -100, false
    for i = 1, 9 do
        if cells[i] == 0 then
            any = true
            cells[i] = who
            local value
            if lineOf(cells, who) then
                value = 10 - depth
            else
                value = -negamax(cells, other, depth + 1, -beta, -alpha)
            end
            cells[i] = 0
            if value > best then best = value end
            if value > alpha then alpha = value end
            if alpha >= beta then break end
        end
    end
    if not any then return 0 end
    return best
end

-- The cells the player to move scores best with (perfect play).
function T.bestMoves(g)
    local who, cells = g.turn, g.cells
    local bestScore, choices = -100, {}
    for i = 1, 9 do
        if cells[i] == 0 then
            cells[i] = who
            local value
            if lineOf(cells, who) then
                value = 10
            else
                value = -negamax(cells, 3 - who, 1, -100, 100)
            end
            cells[i] = 0
            if value > bestScore then
                bestScore, choices = value, { i }
            elseif value == bestScore then
                choices[#choices + 1] = i
            end
        end
    end
    return choices
end

local function openCells(g)
    local open = {}
    for i = 1, 9 do if g.cells[i] == 0 then open[#open + 1] = i end end
    return open
end

-- A cell that completes a line for `who` right now, or nil.
local function finishingCell(g, who)
    for i = 1, 9 do
        if g.cells[i] == 0 then
            g.cells[i] = who
            local wins = lineOf(g.cells, who) ~= nil
            g.cells[i] = 0
            if wins then return i end
        end
    end
    return nil
end

-- The device's move at `level`: 1 Easy (often random, but takes a win), 2
-- Normal (wins and blocks, otherwise perfect three times in four), 3 Hard
-- (perfect: it never loses). `rand(n)` returns 1..n.
function T.deviceMove(g, level, rand)
    local open = openCells(g)
    if #open == 0 then return nil end
    local win = finishingCell(g, g.turn)
    if level == 1 then
        if win and rand(4) > 1 then return win end
        if rand(10) <= 6 then return open[rand(#open)] end
    elseif level == 2 then
        if win then return win end
        local block = finishingCell(g, 3 - g.turn)
        if block then return block end
        if rand(4) == 1 then return open[rand(#open)] end
    end
    -- On an empty board every cell draws with perfect play: skip the search
    -- (the slowest one on the reader).
    if g.moves == 0 then return open[rand(#open)] end
    local best = T.bestMoves(g)
    return best[rand(#best)]
end

return T
