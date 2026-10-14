-- Drawing the backgammon board screen. Usage:
--   local screen = smudge.dofile("draw.lua")(S)
-- with S the app's shared state (main.lua). The screen's input is
-- input.lua; drawing leaves the board's geometry in S.B for it. It needs no
-- rules (logic.lua stays out of memory while the board is drawn).
--
-- The board is the usual one turned a quarter: the 24 points are rows, 13
-- to 24 down the left side and 12 to 1 down the right, the bar across the
-- middle (between 18 and 19, 7 and 6). White's home (1-6) is at the bottom
-- right and Black's (19-24) at the bottom left, with the borne-off trays
-- under them. Checkers lie along a point from the board's edge; a stack of
-- more than five shows its count on the fifth.

return function(S)
local V = {}
local B

local function pips(s)
    local p = 25 * s[25]
    for i = 1, 24 do p = p + i * s[i] end
    return p
end

-- Text of a move in the board's numbers ("13/7 8/7", "bar/22", "4/off");
-- `white` whether White moved.
function V.describe(steps, white)
    local t = {}
    for i = 1, #steps - 1, 2 do
        local f, d = steps:byte(i) - 64, steps:byte(i + 1) - 48
        local to = f - d
        local a = f == 25 and "bar" or tostring(white and f or 25 - f)
        local b = to <= 0 and "off" or tostring(white and to or 25 - to)
        t[#t + 1] = a .. "/" .. b
    end
    return table.concat(t, " ")
end

local function layout()
    local m = S.m
    local top = m.top_padding + m.header_height
    B = S.B or {}
    B.infoY = top + 6
    B.diceY = top + 38
    B.y0 = top + 84
    B.offH = 44
    B.barH = 34
    local bottom = S.h - m.button_hints_height - B.offH - 8
    B.rowH = (bottom - B.y0 - B.barH) // 12
    B.colW = (S.w - 16) // 2
    B.lx, B.rx = 4, S.w - 4 - B.colW
    B.numW = 26
    B.barY = B.y0 + 6 * B.rowH
    B.offY = B.barY + B.barH + 6 * B.rowH + 4
    B.D = math.min(B.rowH - 2, (B.colW - B.numW - 4) // 5)
    B.bw = 76 -- the touch buttons (Menu, Undo, Done)
    S.B = B
end

-- The rectangle of a point (board numbering) and whether it is on the left.
local function pointRect(p)
    local row, x, left
    if p <= 12 then row, x, left = 13 - p, B.rx, false else row, x, left = p - 12, B.lx, true end
    local y = row <= 6 and B.y0 + (row - 1) * B.rowH or B.barY + B.barH + (row - 7) * B.rowH
    return x, y, left
end

-- The centre of the k-th checker on point p.
local function slot(p, k)
    local x, y, left = pointRect(p)
    local off = 2 + (k - 1) * B.D + B.D // 2
    return left and x + off or x + B.colW - off, y + B.rowH // 2
end

local function checker(x, y, side)
    local r = B.D // 2
    local white = side == 1
    smudge.circle(x, y, r, true, white)
    smudge.circle(x, y, r - 2, true, not white)
end

local function stack(p, n, side)
    for k = 1, math.min(n, 5) do
        local x, y = slot(p, k)
        checker(x, y, side)
        if k == 5 and n > 5 then
            smudge.text(x, y - 9, tostring(n), "ui10", "bold", "center", side == 1)
        end
    end
end

local function drawPoint(p)
    local x, y, left = pointRect(p)
    local tri = B.colW - B.numW
    local y1, y2, ym = y + 2, y + B.rowH - 2, y + B.rowH // 2
    local filled = p % 2 == 1
    if left then
        smudge.triangle(x, y1, x, y2, x + tri, ym, filled, true, 1)
        smudge.text(x + B.colW - B.numW // 2, ym - 8, tostring(p), "small", "regular", "center", true)
    else
        smudge.triangle(x + B.colW, y1, x + B.colW, y2, x + B.colW - tri, ym, filled, true, 1)
        smudge.text(x + B.numW // 2, ym - 8, tostring(p), "small", "regular", "center", true)
    end
end

-- The number box of a point: framed (a move's start) or inverted (its end).
local function markNumber(p, invert)
    local x, y, left = pointRect(p)
    local nx = left and x + B.colW - B.numW or x
    if invert then
        smudge.invert_rect(nx + 1, y + 4, B.numW - 2, B.rowH - 8)
    else
        smudge.rect(nx + 1, y + 4, B.numW - 2, B.rowH - 8, false, true, 1)
    end
end

-- Board numbering of a side's own point i.
local function abs(side, i) return side == 1 and i or 25 - i end

-- The region of a side's own point (25: its half of the bar, 0: its tray).
function V.region(side, i)
    if i == 25 then return side == 1 and 0 or S.w // 2, B.barY, S.w // 2, B.barH end
    if i <= 0 then return side == 1 and B.rx or B.lx, B.offY, B.colW, B.offH end
    local x, y = pointRect(abs(side, i))
    return x, y, B.colW, B.rowH
end

-- Pips of a die: three rows of "x" marks for the values 1..6.
local PIPS = { "    x    ", "x       x", "x   x   x", "x x   x x", "x x x x x", "x xx xx x" }

local function die(x, y, sz, v, black, used)
    smudge.rounded_rect(x, y, sz, sz, 5, true, black)
    if not black then smudge.rounded_rect(x, y, sz, sz, 5, false, true, 2) end
    local q, r, p = sz // 4, math.max(2, sz // 10), PIPS[v]
    for k = 1, 9 do
        if p:byte(k) == 120 then
            smudge.circle(x + ((k - 1) % 3 + 1) * q, y + ((k - 1) // 3 + 1) * q, r, true, not black)
        end
    end
    if used then
        smudge.line(x + 2, y + sz - 3, x + sz - 3, y + 2, 3, black and false or true)
    end
end

local function statusText()
    local g = S.game
    if g.result then
        local kind = g.pts == 3 and " a backgammon" or (g.pts == 2 and " a gammon" or "")
        local who = S.name(g.result)
        local verb = (S.vsDevice() and g.result == 1) and " win" or " wins"
        return who .. verb .. kind .. " (" .. g.pts .. (g.pts == 1 and " point)" or " points)")
    end
    if S.thinking then return "Device is thinking..." end
    if S.note then return S.note end
    local L = g.last
    if L and g.steps == "" then
        local text = L.steps == "" and "no move" or V.describe(L.steps, L.side == 1)
        return S.name(L.side) .. " " .. L.d1 .. "-" .. L.d2 .. ": " .. text
    end
    if g.opening then return S.name(g.turn) .. (S.vsDevice() and g.turn == 1 and " start" or " starts") .. " with " ..
                             g.d1 .. "-" .. g.d2 end
    return ""
end

local function prompt()
    local g = S.game
    if g.result then return "" end
    if not S.humanTurn() then return "" end
    if g.need == 0 then return "No move possible" end
    if S.left == 0 then return "Move done" end
    if S.picked then return "Where to?" end
    return S.vsDevice() and "Your move" or (g.turn == 1 and "White to move" or "Black to move")
end

function V.draw()
    local g = S.game
    layout()
    smudge.header("Backgammon", S.vsDevice() and S.LEVELS[S.mode] or "2 players")
    -- The last move or the result, and the Menu button.
    local text = statusText()
    local maxW = S.w - 24 - (S.touch and B.bw + 8 or 0)
    smudge.text(12, B.infoY + 2, text, smudge.text_width(text, "ui12", "bold") > maxW and "ui10" or "ui12", "bold",
                "left", true)
    if S.touch then
        smudge.rounded_rect(S.w - B.bw - 8, B.infoY, B.bw, 30, 6, false, 2)
        smudge.text(S.w - B.bw // 2 - 8, B.infoY + 5, "Menu", "ui10", "bold", "center", true)
    end
    -- The dice of the side to move (used ones struck through) and a prompt.
    local nd = g.d1 == g.d2 and 4 or 2
    local sz = 34
    local used = nd - S.nR
    for k = 1, nd do
        local v = k == 2 and g.d2 or g.d1
        die(12 + (k - 1) * (sz + 8), B.diceY, sz, v, g.turn == 2, k <= used and not g.result)
    end
    local px = 12 + nd * (sz + 8) + 6
    smudge.text(px, B.diceY + 7, prompt(), "ui10", "bold", "left", true)
    if S.touch and S.humanTurn() then
        local can = #g.steps > 0
        if can then
            smudge.rounded_rect(S.w - 2 * B.bw - 16, B.diceY, B.bw, sz, 6, false, 2)
            smudge.text(S.w - B.bw - B.bw // 2 - 16, B.diceY + 7, "Undo", "ui10", "bold", "center", true)
        end
        if S.left == 0 then
            smudge.rounded_rect(S.w - B.bw - 8, B.diceY, B.bw, sz, 6, true, true)
            smudge.text(S.w - B.bw // 2 - 8, B.diceY + 7, g.need == 0 and "Pass" or "Done", "ui10", "bold", "center",
                        false)
        end
    end
    -- The points and their checkers.
    local W, K = g.b[1], g.b[2]
    smudge.rect(B.lx - 2, B.y0 - 2, S.w - 4, B.offY - B.y0, false, true, 1)
    for p = 1, 24 do
        drawPoint(p)
        if W[p] > 0 then stack(p, W[p], 1) elseif K[25 - p] > 0 then stack(p, K[25 - p], 2) end
    end
    -- The bar: White's half on the left, Black's on the right.
    smudge.rect_dither(0, B.barY + 1, S.w, B.barH - 2, false)
    smudge.text(S.w // 2, B.barY + 8, "BAR", "small", "bold", "center", true)
    local cy = B.barY + B.barH // 2
    for side = 1, 2 do
        local n = g.b[side][25]
        if n > 0 then
            local cx = side == 1 and S.w // 4 or 3 * S.w // 4
            checker(cx, cy, side)
            if n > 1 then
                smudge.rect(cx + B.D // 2 + 4, cy - 12, 34, 24, true, false)
                smudge.text(cx + B.D // 2 + 8, cy - 9, "x" .. n, "ui10", "bold", "left", true)
            end
        end
    end
    -- The trays: borne off and pip counts.
    for side = 1, 2 do
        local x, y, w, h = V.region(side, 0)
        local s = g.b[side]
        smudge.rect(x, y, w, h, false, true, 1)
        checker(x + 6 + B.D // 2, y + h // 2, side)
        smudge.text(x + B.D + 14, y + 3, S.name(side) .. ": " .. s[26] .. " off", "ui10", "bold", "left", true)
        smudge.text(x + B.D + 14, y + 23, pips(s) .. " pips", "small", "regular", "left", true)
    end
    -- The last move: its starts framed, its ends inverted.
    local L = g.last
    if L and g.steps == "" and not S.picked then
        local mark = {} -- point -> true (an end) or false (a start)
        for i = 1, #L.steps - 1, 2 do
            local f = L.steps:byte(i) - 64
            local t = f - (L.steps:byte(i + 1) - 48)
            if f < 25 and mark[abs(L.side, f)] == nil then mark[abs(L.side, f)] = false end
            if t > 0 then mark[abs(L.side, t)] = true end
        end
        for p, invert in pairs(mark) do markNumber(p, invert) end
    end
    local me = g.b[g.turn]
    if S.picked then
        -- The picked checker and where it can go.
        local x, y, w, h = V.region(g.turn, S.picked)
        smudge.rect(x, y, w, h, false, true, 4)
        for i, t in ipairs(S.dt) do
            local dx, dy
            if t == 0 then
                local rx, ry, rw, rh = V.region(g.turn, 0)
                dx, dy = rx + rw - 20, ry + rh // 2
            else
                local p = abs(g.turn, t)
                local n = me[t] > 0 and me[t] + 1 or 1
                if p == abs(g.turn, S.picked) then n = n - 1 end
                dx, dy = slot(p, math.min(n, 5))
            end
            smudge.circle(dx, dy, 9, true, false)
            smudge.circle(dx, dy, 7, true, true)
            if not S.touch and i == S.dcur then
                local rx, ry, rw, rh = V.region(g.turn, t)
                smudge.rect(rx + 2, ry + 2, rw - 4, rh - 4, false, true, 3)
            end
        end
    elseif not S.touch and S.humanTurn() and #S.sources > 0 then
        local x, y, w, h = V.region(g.turn, S.sources[S.cur])
        smudge.rect(x + 1, y + 1, w - 2, h - 2, false, true, 3)
    end
    if g.result then smudge.popup(text) end
    if S.touch then
        smudge.button_hints("", "", "", "")
    elseif g.result then
        smudge.button_hints("Menu", "Again", "", "")
    elseif not S.humanTurn() then
        smudge.button_hints("Menu", "", "", "")
    else
        local back = S.picked and "Drop" or (#g.steps > 0 and "Undo" or "Menu")
        local ok = S.picked and "Move" or (S.left == 0 and (g.need == 0 and "Pass" or "Done") or "Pick")
        smudge.button_hints(back, ok, "<", ">")
    end
end

return V
end
