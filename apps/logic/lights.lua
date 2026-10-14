-- Lights Out rules. No drawing, so it can be tested on a computer
-- (test/lua_apps/logic_test.lua).
--
-- Pressing a light toggles it and its four neighbours; the goal is all lights
-- off. A puzzle is made by pressing random lights of a dark board, so it can
-- always be solved (pressing the same lights again undoes it). Lights are one
-- number per cell, row by row, 1 = on.

local L = {}

function L.press(cells, n, r, c)
    for k = 0, 4 do
        local rr = r + (k == 1 and -1 or k == 2 and 1 or 0)
        local cc = c + (k == 3 and -1 or k == 4 and 1 or 0)
        if rr >= 0 and rr < n and cc >= 0 and cc < n then
            local i = rr * n + cc + 1
            cells[i] = 1 - cells[i]
        end
    end
end

local function anyOn(cells)
    for i = 1, #cells do
        if cells[i] == 1 then return true end
    end
    return false
end

-- A new n x n puzzle; `presses` (a list of cell indexes) is filled with the
-- lights pressed to make it, a solution.
function L.new(n, rand, presses)
    local p = { size = n, cells = {}, moves = 0, elapsedMs = 0, solved = false }
    presses = presses or {}
    repeat
        for i = 1, n * n do p.cells[i] = 0 end
        local order = {}
        for i = 1, n * n do order[i] = i end
        local count = n * n // 3 + rand(n)
        for k = 1, count do
            local j = rand(n * n - k + 1) + k - 1
            order[k], order[j] = order[j], order[k]
            local i = order[k]
            presses[k] = i
            L.press(p.cells, n, (i - 1) // n, (i - 1) % n)
        end
        for k = count + 1, #presses do presses[k] = nil end
    until anyOn(p.cells)
    p.start = table.concat(p.cells)
    return p
end

-- The player presses light (r, c). Returns true when it changed.
function L.play(p, r, c)
    if p.solved then return false end
    L.press(p.cells, p.size, r, c)
    p.moves = p.moves + 1
    p.solved = not anyOn(p.cells)
    return true
end

function L.restart(p)
    for i = 1, p.size * p.size do p.cells[i] = p.start:byte(i) - 48 end
    p.moves, p.solved = 0, false
end

function L.serialize(p)
    return string.format("%d,%s,%s,%d,%d", p.size, p.start, table.concat(p.cells), p.moves, p.elapsedMs)
end

function L.deserialize(s)
    local size, start, cells, moves, ms = (s or ""):match("^(%d+),([01]+),([01]+),(%d+),(%d+)$")
    size = tonumber(size)
    if not size or #start ~= size * size or #cells ~= size * size then return nil end
    local p = { size = size, start = start, cells = {}, moves = tonumber(moves), elapsedMs = tonumber(ms) }
    for i = 1, size * size do p.cells[i] = cells:byte(i) - 48 end
    p.solved = not anyOn(p.cells)
    return p
end

return L
