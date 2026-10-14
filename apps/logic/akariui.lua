-- Light Up screen: drawing and input. Usage:
--   local screen = smudge.dofile("akariui.lua")(S, A)   -- S: main.lua's state, A: akari.lua
--
-- Buttons (X3/X4): the arrows move the cursor, Confirm switches the white
-- cell under it: bulb, dot (no bulb), empty. Back opens the menu. Touch
-- (X4 Pro): tap a white cell to switch it the same way. Lit cells are
-- shaded; a bulb that sees another gets a ring, a number with too many
-- bulbs turns white.

return function(S, A)
local M = {}
local B = smudge.dofile("board.lua")(S)
local P = S.P
local n = P.size
local NEXT = { "Bulb", "Dot", "Clear" }

local function layout()
    local bx, by, bw, bh = B.boardRoom()
    local s = math.min(bw // n, bh // n, 72)
    local gy = by + (bh - s * n) // 2
    return bx + (bw - s * n) // 2, gy, s, gy + s * n + 12
end

function M.draw()
    if S.cr < 0 or S.cr >= n or S.cc < 0 or S.cc >= n then S.cr, S.cc = 0, 0 end
    local lit = A.lighting(P)
    local bulbs, dark = 0, 0
    for i = 1, n * n do
        if A.isWhite(P, i) then
            if P.cur[i] == 1 then bulbs = bulbs + 1 end
            if lit[i] == 0 then dark = dark + 1 end
        end
    end
    B.drawTop("Bulbs " .. bulbs .. "   Unlit " .. dark)
    local gx, gy, s, helpY = layout()
    local font = s >= 44 and "ui12" or "ui10"
    local lh = smudge.line_height(font)
    local rad = s * 3 // 10
    for i = 1, n * n do
        local r, c = (i - 1) // n, (i - 1) % n
        local x, y = gx + c * s, gy + r * s
        local cx, cy = x + s // 2, y + s // 2
        if A.isWhite(P, i) then
            if lit[i] > 0 then smudge.rect_dither(x + 1, y + 1, s - 1, s - 1, false) end
            smudge.rect(x, y, s + 1, s + 1, false, 1, true)
            local v = P.cur[i]
            if v == 1 then
                smudge.circle(cx, cy, rad + 2, true, false)
                smudge.circle(cx, cy, rad, true, true)
                if lit[i] > 1 then smudge.circle(cx, cy, s // 2 - 3, false, 3, true) end
            elseif v == 2 then
                smudge.circle(cx, cy, 8, true, false)
                smudge.circle(cx, cy, 5, true, true)
            end
            if P.marks and P.marks[i] then
                smudge.line(x + 4, y + 4, x + s - 4, y + s - 4, 3, true)
                smudge.line(x + 4, y + s - 4, x + s - 4, y + 4, 3, true)
            end
        else
            smudge.rect(x, y, s + 1, s + 1, true, true)
            local k = A.number(P, i)
            if k then
                local have = A.bulbsAround(P, i)
                if have > k then
                    smudge.rect(x + 4, y + 4, s - 7, s - 7, true, false)
                    smudge.text(cx, cy - lh // 2, tostring(k), font, "bold", "center", true)
                else
                    smudge.text(cx, cy - lh // 2, tostring(k), font, have == k and "regular" or "bold", "center", false)
                end
            end
        end
    end
    if not S.touch and not P.solved then
        local x, y = gx + S.cc * s, gy + S.cr * s
        -- A black ring between two white ones shows on any cell.
        smudge.rect(x - 2, y - 2, s + 5, s + 5, false, 2, false)
        smudge.rect(x, y, s + 1, s + 1, false, 4, true)
        smudge.rect(x + 4, y + 4, s - 7, s - 7, false, 2, false)
    end
    if B.drawSolved() then return end
    if S.touch then
        B.drawHelp(helpY, "Tap a cell: bulb, dot, empty")
        smudge.button_hints("", "", "", "")
    else
        local i = S.cr * n + S.cc + 1
        B.drawHelp(helpY, "Light every cell; bulbs must not see each other")
        smudge.button_hints("Menu", A.isWhite(P, i) and NEXT[P.cur[i] + 1] or "", "<", ">")
    end
end

local function cycle(r, c)
    if A.cycle(P, r * n + c + 1) then B.moved() end
end

function M.button(btn)
    if B.commonButton(btn) then return end
    if btn == "back" then
        S.show("pause")
        return
    end
    if btn == "confirm" then
        cycle(S.cr, S.cc)
        return
    end
    if btn == "left" then
        S.cc = (S.cc - 1) % n
    elseif btn == "right" then
        S.cc = (S.cc + 1) % n
    elseif btn == "up" or btn == "page_back" then
        S.cr = (S.cr - 1) % n
    elseif btn == "down" or btn == "page_forward" then
        S.cr = (S.cr + 1) % n
    else
        return
    end
    S.note = nil
    smudge.request_update()
end

function M.tap(x, y)
    if B.commonTap(x, y) then return end
    local gx, gy, s = layout()
    if not smudge.in_rect(x, y, gx, gy, s * n, s * n) then return end
    S.cr, S.cc = (y - gy) // s, (x - gx) // s
    cycle(S.cr, S.cc)
end

return M
end
