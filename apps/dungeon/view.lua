-- The Dungeon Map play screen: layout and drawing. Usage:
--   local view = smudge.dofile("view.lua")(D, S)
-- with D the rules (logic.lua) and S the game state (main.lua). Loaded while
-- a map is on screen and dropped while one is being made.

return function(D, S)
local V = {}
local w, h, m, touch = S.w, S.h, S.m, S.touch

-- Layout ---------------------------------------------------------------------

local function contentTop() return m.top_padding + m.header_height + 8 end
local function contentBottom() return h - m.button_hints_height - 8 end

function V.toolbar()
    local y = contentTop()
    return { y = y, mode = { x = w - 16 - 210, y = y, w = 120, h = 44 }, menu = { x = w - 16 - 80, y = y, w = 80, h = 44 } }
end

function V.gridRect()
    local n = S.puzzle.size
    local top = V.toolbar().y + 56
    local gutter = 30
    local bottomRoom = touch and 0 or 30
    local cell = math.min((w - 32 - gutter) // n, (contentBottom() - bottomRoom - top - gutter) // n, 56)
    local total = gutter + n * cell
    local x0 = (w - total) // 2 + gutter
    return { x = x0, y = top + gutter, cell = cell, size = n * cell }
end

-- Drawing --------------------------------------------------------------------

local function drawMonster(x, y, s)
    local cx, cy, r = x + s // 2, y + s // 2, s * 3 // 10
    smudge.circle(cx, cy, r, true, true)
    smudge.triangle(cx - r, cy - r // 2, cx - r // 2, cy - r - r // 2, cx - r // 4, cy - r, true, true)
    smudge.triangle(cx + r, cy - r // 2, cx + r // 2, cy - r - r // 2, cx + r // 4, cy - r, true, true)
    local e = math.max(2, r // 3)
    smudge.circle(cx - r // 2, cy - r // 6, e, true, false)
    smudge.circle(cx + r // 2, cy - r // 6, e, true, false)
    smudge.rect(cx - r // 2, cy + r // 3, r, math.max(2, r // 5), true, false)
end

local function drawChest(x, y, s)
    local bw, bh = s * 3 // 4, s * 11 // 20
    local bx, by = x + (s - bw) // 2, y + (s - bh) // 2 + s // 20
    smudge.rect(bx, by, bw, bh, true, true)
    smudge.rect(bx + 2, by + bh // 3, bw - 4, 2, true, false)
    local k = math.max(4, s // 8)
    smudge.rect(bx + (bw - k) // 2, by + bh // 3 - k // 3, k, k, true, false)
end

function V.draw()
    local L = D.LEVELS[S.level]
    smudge.header("Dungeon Map", string.format("%s %dx%d", L.name, S.puzzle.size, S.puzzle.size))
    local tb = V.toolbar()
    smudge.text(16, tb.y + 12, S.formatTime(S.elapsed()), "ui12", "bold", "left", true)
    if touch then
        local b = tb.mode
        smudge.rounded_rect(b.x, b.y, b.w, b.h, 6, S.openMode, S.openMode and true or 2)
        smudge.text(b.x + b.w // 2, b.y + 12, S.openMode and "Open" or "Wall", "ui12", "bold", "center", not S.openMode)
        b = tb.menu
        smudge.rounded_rect(b.x, b.y, b.w, b.h, 6, false, 2)
        smudge.text(b.x + b.w // 2, b.y + 12, "Menu", "ui12", "bold", "center", true)
    end
    local G = V.gridRect()
    local n, s = S.puzzle.size, G.cell
    local lh = smudge.line_height("ui12")
    -- The numbers: plain once that row or column has its walls, bold until then.
    local colWalls = {}
    for c = 1, n do colWalls[c] = 0 end
    for r = 1, n do
        local count = D.POP[S.marks.wall[r]]
        for c = 1, n do
            if S.marks.wall[r] & D.bit(c) ~= 0 then colWalls[c] = colWalls[c] + 1 end
        end
        local clue = S.puzzle.rowClue[r]
        smudge.text(G.x - 10, G.y + (r - 1) * s + (s - lh) // 2, tostring(clue), "ui12",
                    count == clue and "regular" or "bold", "right", true)
    end
    for c = 1, n do
        local clue = S.puzzle.colClue[c]
        smudge.text(G.x + (c - 1) * s + s // 2, G.y - lh - 6, tostring(clue), "ui12",
                    colWalls[c] == clue and "regular" or "bold", "center", true)
    end
    -- Cells.
    for r = 1, n do
        for c = 1, n do
            local x, y, b = G.x + (c - 1) * s, G.y + (r - 1) * s, D.bit(c)
            if S.puzzle.monsters[r] & b ~= 0 then
                drawMonster(x, y, s)
            elseif S.puzzle.chests[r] & b ~= 0 then
                drawChest(x, y, s)
            elseif S.marks.wall[r] & b ~= 0 then
                smudge.rect(x + 1, y + 1, s - 1, s - 1, true, true)
            elseif S.marks.open[r] & b ~= 0 and not S.solved then
                smudge.circle(x + s // 2, y + s // 2, math.max(2, s // 10), true, true)
            end
        end
    end
    for i = 0, n do
        smudge.line(G.x + i * s, G.y, G.x + i * s, G.y + G.size, 1, true)
        smudge.line(G.x, G.y + i * s, G.x + G.size, G.y + i * s, 1, true)
    end
    smudge.rect(G.x - 1, G.y - 1, G.size + 3, G.size + 3, false, true, 2)
    if not touch and not S.solved then
        local cx, cy = G.x + (S.curC - 1) * s, G.y + (S.curR - 1) * s
        smudge.rect(cx - 5, cy - 5, s + 11, s + 11, false, false, 3)
        smudge.rect(cx - 2, cy - 2, s + 5, s + 5, false, true, 4)
        smudge.rect(cx + 3, cy + 3, s - 5, s - 5, false, false, 2)
        smudge.centered_text(G.y + G.size + 10, "Confirm: wall, open, unknown", "ui10", "regular", true)
    end
    if S.solved then smudge.popup("Map complete in " .. S.formatTime(S.puzzle.elapsedMs) .. "!") end
    if touch then
        smudge.button_hints("", "", "", "")
    elseif S.solved then
        smudge.button_hints("Menu", "Next", "", "")
    else
        smudge.button_hints("Menu", "Mark", "<", ">")
    end
end

-- The cell under (x, y), or nil.
function V.cellAt(x, y)
    local G = V.gridRect()
    if not smudge.in_rect(x, y, G.x, G.y, G.size, G.size) then return nil end
    return (x - G.x) // G.cell + 1, (y - G.y) // G.cell + 1
end

return V
end
