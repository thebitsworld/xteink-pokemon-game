-- Caro (Gomoku) rules: a 15x15 board, X moves first, five or more in a row
-- wins (free-style: an overline counts), a full board is a draw. No smudge
-- calls, so test/lua_apps/caro_test.lua runs it on a computer.
--
-- Cells are numbered 1..225, row by row: i = r * 15 + c + 1 with r, c from 0.
-- A game: { b = cells (0 empty, 1 X, 2 O), n = stones played, turn = 1 or 2,
--           winner = 0 playing, 1/2 a side, -1 draw, line = the winning cells,
--           hist = the cells in the order played }.

local L = { N = 15, X = 1, O = 2 }
local N = 15
L.DR = { 0, 1, 1, 1 }
L.DC = { 1, 0, 1, -1 }
local DR, DC = L.DR, L.DC

function L.idx(r, c) return r * N + c + 1 end
function L.rc(i) return (i - 1) // N, (i - 1) % N end

function L.new()
    local b = {}
    for i = 1, N * N do b[i] = 0 end
    return { b = b, n = 0, turn = 1, winner = 0, line = nil, hist = {} }
end

-- The cells of p's longest run of five or more through cell i, or nil.
function L.winLine(b, i, p)
    local r, c = (i - 1) // N, (i - 1) % N
    for k = 1, 4 do
        local dr, dc = DR[k], DC[k]
        local back, fwd = 0, 0
        while true do
            local rr, cc = r - (back + 1) * dr, c - (back + 1) * dc
            if rr < 0 or rr >= N or cc < 0 or cc >= N or b[rr * N + cc + 1] ~= p then break end
            back = back + 1
        end
        while true do
            local rr, cc = r + (fwd + 1) * dr, c + (fwd + 1) * dc
            if rr < 0 or rr >= N or cc < 0 or cc >= N or b[rr * N + cc + 1] ~= p then break end
            fwd = fwd + 1
        end
        if back + fwd + 1 >= 5 then
            local out = {}
            for t = -back, fwd do out[#out + 1] = (r + t * dr) * N + (c + t * dc) + 1 end
            return out
        end
    end
    return nil
end

-- Plays the side to move on cell i; false if the game is over or the cell
-- is taken or off the board.
function L.play(g, i)
    if g.winner ~= 0 or type(i) ~= "number" or i < 1 or i > N * N or g.b[i] ~= 0 then return false end
    local p = g.turn
    g.b[i] = p
    g.n = g.n + 1
    g.hist[g.n] = i
    local line = L.winLine(g.b, i, p)
    if line then
        g.winner, g.line = p, line
    elseif g.n == N * N then
        g.winner = -1
    end
    g.turn = 3 - p
    return true
end

-- Takes back up to `count` stones (a finished game plays on again).
function L.undo(g, count)
    for _ = 1, count do
        if g.n == 0 then break end
        local i = g.hist[g.n]
        g.turn = g.b[i]
        g.b[i] = 0
        g.hist[g.n] = nil
        g.n = g.n - 1
    end
    g.winner, g.line = 0, nil
end

function L.last(g) return g.hist[g.n] end

-- The moves as text (two hex digits a cell), and back.
function L.encode(g)
    local t = {}
    for k = 1, g.n do t[k] = string.format("%02x", g.hist[k]) end
    return table.concat(t)
end

function L.decode(s)
    if type(s) ~= "string" or #s % 2 ~= 0 then return nil end
    local g = L.new()
    for k = 1, #s, 2 do
        local i = tonumber(s:sub(k, k + 1), 16)
        if not i or not L.play(g, i) then return nil end
    end
    return g
end

return L
