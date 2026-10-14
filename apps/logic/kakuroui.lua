-- Kakuro screen: drawing and input. Usage:
--   local screen = smudge.dofile("kakuroui.lua")(S, K)   -- S: main.lua's state, K: kakuro.lua
--
-- Buttons (X3/X4): the arrows move the cursor from white cell to white
-- cell. Confirm picks the cell: then Left/Right (and Up/Down) step its digit
-- down or up (empty, 1..9, empty), and Confirm or Back puts it down. Back
-- opens the menu. Touch (X4 Pro): tap a cell, then a digit on the pad
-- under the board (X clears it).

return function(S, K)
local M = {}
local B = smudge.dofile("board.lua")(S)
local P = S.P
local n = P.size
local PAD = 56 -- the touch pad's height

-- The cursor on a white cell (the first one if it is not).
local function fixCursor()
    if S.cr < 0 or S.cr >= n or S.cc < 0 or S.cc >= n or not K.isWhite(P, S.cr * n + S.cc + 1) then
        for i = 1, n * n do
            if K.isWhite(P, i) then
                S.cr, S.cc = (i - 1) // n, (i - 1) % n
                break
            end
        end
    end
end
fixCursor()

-- The grid: origin x, y and cell size; the help line's y; the pad's y.
local function layout()
    local bx, by, bw, bh = B.boardRoom()
    if S.touch then bh = bh - PAD - 8 end
    local s = math.min(bw // n, bh // n, 64)
    local gy = by + (bh - s * n) // 2
    return bx + (bw - s * n) // 2, gy, s, gy + s * n + 12, by + bh + 8
end

-- A clue at x, y (align: "left" or "right"); state as K.runState.
local function drawClue(x, y, v, font, align, state)
    local t = tostring(v)
    if state == 2 then
        local w, h = smudge.text_width(t, font, "bold"), smudge.line_height(font)
        smudge.rect(align == "right" and x - w - 2 or x - 2, y, w + 4, h, true, false)
        smudge.text(x, y, t, font, "bold", align, true)
    else
        smudge.text(x, y, t, font, state == 1 and "regular" or "bold", align, false)
    end
end

function M.draw()
    fixCursor()
    local empty = 0
    for i = 1, n * n do
        if K.isWhite(P, i) and P.cur[i] == 0 then empty = empty + 1 end
    end
    B.drawTop("Empty " .. empty)
    local gx, gy, s, helpY, padY = layout()
    local cf = s >= 54 and "ui10" or "small"
    local ch = smudge.line_height(cf)
    local df = s >= 40 and "ui12" or "ui10"
    local dh = smudge.line_height(df)
    local editing = S.sel and not S.touch
    for i = 1, n * n do
        local r, c = (i - 1) // n, (i - 1) % n
        local x, y = gx + c * s, gy + r * s
        if K.isWhite(P, i) then
            local here = r == S.cr and c == S.cc and not P.solved
            smudge.rect(x, y, s + 1, s + 1, false, 1, true)
            if here and editing then smudge.rect(x + 2, y + 2, s - 3, s - 3, true, true) end
            local d = P.cur[i]
            if d > 0 then smudge.text(x + s // 2, y + (s - dh) // 2, tostring(d), df, "bold", "center", not (here and editing)) end
            if P.marks and P.marks[i] then
                smudge.line(x + 4, y + s - 4, x + s - 4, y + 4, 2, not (here and editing))
            end
            if here and not editing then smudge.rect(x + 2, y + 2, s - 3, s - 3, false, 3, true) end
        else
            smudge.rect(x, y, s + 1, s + 1, true, true)
            local a, dn = P.ca:byte(i), P.cd:byte(i)
            if a > 0 or dn > 0 then
                smudge.line(x + 1, y + 1, x + s - 1, y + s - 1, 1, false)
                if a > 0 then drawClue(x + s - 3, y + 2, a, cf, "right", K.runState(P, i, 1)) end
                if dn > 0 then drawClue(x + 3, y + s - ch - 1, dn, cf, "left", K.runState(P, i, n)) end
            end
        end
    end
    if B.drawSolved() then return end
    if S.touch then
        -- The pad: 1..9 and X (clear).
        local bx, _, bw = B.boardRoom()
        local kw = bw // 10
        for k = 1, 10 do
            local x = bx + (bw - kw * 10) // 2 + (k - 1) * kw
            smudge.rounded_rect(x + 2, padY, kw - 4, PAD, 6, false, 2, true)
            smudge.text(x + kw // 2, padY + (PAD - smudge.line_height("ui12")) // 2, k == 10 and "X" or tostring(k),
                "ui12", "bold", "center", true)
        end
        B.drawHelp(helpY, "Tap a cell, then a digit")
        smudge.button_hints("", "", "", "")
    elseif editing then
        B.drawHelp(helpY, "Left/Right: digit down/up   Confirm: done")
        smudge.button_hints("Done", "Done", "-", "+")
    else
        B.drawHelp(helpY, "Arrows: move   Confirm: enter a digit")
        smudge.button_hints("Menu", "Digit", "<", ">")
    end
end

local function setDigit(d)
    if K.set(P, S.cr * n + S.cc + 1, d) then
        if P.solved then S.sel = nil end
        B.moved()
    end
end

-- The next white cell from the cursor in direction dr, dc, wrapping
-- round the row or column (and on to the next one).
local function step(dr, dc)
    local r, c = S.cr, S.cc
    for _ = 1, n * n do
        r, c = r + dr, c + dc
        if c >= n then r, c = (r + 1) % n, 0 elseif c < 0 then r, c = (r - 1) % n, n - 1 end
        if r >= n then r, c = 0, (c + 1) % n elseif r < 0 then r, c = n - 1, (c - 1) % n end
        if K.isWhite(P, r * n + c + 1) then
            S.cr, S.cc = r, c
            return
        end
    end
end

function M.button(btn)
    if B.commonButton(btn) then return end
    local up, down = btn == "up" or btn == "page_back", btn == "down" or btn == "page_forward"
    if S.sel then
        local d = P.cur[S.cr * n + S.cc + 1]
        if btn == "confirm" or btn == "back" then
            S.sel = nil
        elseif btn == "right" or up then
            setDigit((d + 1) % 10)
            return
        elseif btn == "left" or down then
            setDigit((d + 9) % 10)
            return
        end
    elseif btn == "back" then
        S.show("pause")
        return
    elseif btn == "confirm" then
        S.sel = true
    elseif btn == "left" then
        step(0, -1)
    elseif btn == "right" then
        step(0, 1)
    elseif up then
        step(-1, 0)
    elseif down then
        step(1, 0)
    else
        return
    end
    S.note = nil
    smudge.request_update()
end

function M.tap(x, y)
    if B.commonTap(x, y) then return end
    local gx, gy, s, _, padY = layout()
    if smudge.in_rect(x, y, gx, gy, s * n, s * n) then
        local r, c = (y - gy) // s, (x - gx) // s
        if K.isWhite(P, r * n + c + 1) then
            S.cr, S.cc, S.note = r, c, nil
            smudge.request_update()
        end
        return
    end
    local bx, _, bw = B.boardRoom()
    local kw = bw // 10
    local px = bx + (bw - kw * 10) // 2
    if smudge.in_rect(x, y, px, padY, kw * 10, PAD) then setDigit(((x - px) // kw + 1) % 10) end
end

return M
end
