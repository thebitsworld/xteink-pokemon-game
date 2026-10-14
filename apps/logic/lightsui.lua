-- Lights Out screen: drawing and input. Usage:
--   local screen = smudge.dofile("lightsui.lua")(S, L)   -- S: main.lua's state, L: lights.lua
--
-- Buttons (X3/X4): the arrows move the cursor, Confirm presses the light
-- (it and its four neighbours switch), Back opens the menu. Touch (X4 Pro):
-- tap a light to press it.

return function(S, L)
local M = {}
local B = smudge.dofile("board.lua")(S)
local P = S.P
local n = P.size

local function layout()
    local bx, by, bw, bh = B.boardRoom()
    local s = math.min(bw // n, bh // n, 84)
    local gy = by + (bh - s * n) // 2
    return bx + (bw - s * n) // 2, gy, s, gy + s * n + 12
end

function M.draw()
    local on = 0
    for i = 1, n * n do on = on + P.cells[i] end
    B.drawTop("Moves " .. P.moves .. "   Lit " .. on)
    local gx, gy, s, helpY = layout()
    for r = 0, n - 1 do
        for c = 0, n - 1 do
            local x, y = gx + c * s, gy + r * s
            if P.cells[r * n + c + 1] == 1 then
                smudge.rounded_rect(x + 4, y + 4, s - 8, s - 8, 8, true, true)
            else
                smudge.rounded_rect(x + 4, y + 4, s - 8, s - 8, 8, false, 2, true)
            end
        end
    end
    if not S.touch and not P.solved then
        local x, y = gx + S.cc * s, gy + S.cr * s
        -- A black ring between two white ones shows on lit and dark lights alike.
        smudge.rect(x - 1, y - 1, s + 2, s + 2, false, 3, true)
        smudge.rect(x + 6, y + 6, s - 12, s - 12, false, 3, false)
        smudge.rect(x + 9, y + 9, s - 18, s - 18, false, 2, true)
    end
    if B.drawSolved() then return end
    if S.touch then
        B.drawHelp(helpY, "Tap a light: it and its neighbours switch")
        smudge.button_hints("", "", "", "")
    else
        B.drawHelp(helpY, "Confirm: the light and its neighbours switch")
        smudge.button_hints("Menu", "Press", "<", ">")
    end
end

local function press(r, c)
    if L.play(P, r, c) then B.moved() end
end

function M.button(btn)
    if B.commonButton(btn) then return end
    if btn == "back" then
        S.show("pause")
        return
    end
    if btn == "confirm" then
        press(S.cr, S.cc)
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
    press(S.cr, S.cc)
end

return M
end
