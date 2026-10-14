-- Slitherlink screen: drawing and input. Usage:
--   local screen = smudge.dofile("slitherui.lua")(S, SL)   -- S: main.lua's state, SL: slither.lua
--
-- Buttons (X3/X4): the cursor sits on a dot and the arrows move it along an
-- edge. Confirm switches the pen: Move only moves; Line draws a line on the
-- edge it moves along (or rubs it out); Cross marks the edge as empty (or
-- clears the mark). Back opens the menu. Touch (X4 Pro): tap an edge to
-- cycle it: line, cross, empty.

return function(S, SL)
local M = {}
local B = smudge.dofile("board.lua")(S)
local P = S.P
local n = P.size
local PENS = { "Move", "Line", "Cross" }

local function layout()
    local bx, by, bw, bh = B.boardRoom()
    local s = math.min((bw - 32) // n, (bh - 32) // n, 80)
    local gy = by + (bh - s * n) // 2
    return bx + (bw - s * n) // 2, gy, s, gy + s * n + 14
end

local function drawEdge(gx, gy, s, e)
    local v = P.cur[e]
    if v == 0 then return end
    local r1, c1, r2, c2 = SL.ends(n, e)
    local x1, y1, x2, y2 = gx + c1 * s, gy + r1 * s, gx + c2 * s, gy + r2 * s
    if v == 1 then
        if r1 == r2 then smudge.rect(x1, y1 - 2, x2 - x1, 5, true, true) else smudge.rect(x1 - 2, y1, 5, y2 - y1, true, true) end
    else
        local mx, my = (x1 + x2) // 2, (y1 + y2) // 2
        smudge.line(mx - 5, my - 5, mx + 5, my + 5, 2, true)
        smudge.line(mx - 5, my + 5, mx + 5, my - 5, 2, true)
    end
end

function M.draw()
    if S.pen < 1 or S.pen > 3 then S.pen = 1 end
    B.drawTop(S.touch and "" or ("Pen: " .. PENS[S.pen]))
    local gx, gy, s, helpY = layout()
    local font = s >= 44 and "ui12" or "ui10"
    local lh = smudge.line_height(font)
    for r = 0, n - 1 do
        for c = 0, n - 1 do
            local k = SL.clue(P, r, c)
            if k then
                local have = SL.linesAround(P, r, c)
                local x, y = gx + c * s + s // 2, gy + r * s + s // 2
                if have > k then
                    smudge.rect(x - lh // 2 - 3, y - lh // 2 - 3, lh + 6, lh + 6, true, true)
                    smudge.text(x, y - lh // 2, tostring(k), font, "bold", "center", false)
                else
                    smudge.text(x, y - lh // 2, tostring(k), font, have == k and "regular" or "bold", "center", true)
                end
            end
        end
    end
    for e = 1, SL.edgeCount(n) do drawEdge(gx, gy, s, e) end
    for r = 0, n do
        for c = 0, n do smudge.rect(gx + c * s - 2, gy + r * s - 2, 5, 5, true, true) end
    end
    if not S.touch and not P.solved then
        local x, y = gx + S.cc * s, gy + S.cr * s
        smudge.circle(x, y, 11, false, 3, true)
        if S.pen > 1 then smudge.circle(x, y, 16, false, 2, true) end
    end
    if B.drawSolved() then return end
    if S.touch then
        B.drawHelp(helpY, "Tap an edge: line, cross, empty")
        smudge.button_hints("", "", "", "")
    else
        B.drawHelp(helpY, S.pen == 1 and "Confirm: pen to draw lines" or
            (S.pen == 2 and "Moving draws or rubs out a line" or "Moving marks or clears a cross"))
        smudge.button_hints("Menu", PENS[S.pen % 3 + 1], "<", ">")
    end
end

local function setEdge(e, state)
    if SL.cycle(P, e, state) then B.moved() end
end

function M.button(btn)
    if B.commonButton(btn) then return end
    if S.cr < 0 or S.cr > n or S.cc < 0 or S.cc > n then S.cr, S.cc = 0, 0 end
    if btn == "back" then
        S.show("pause")
        return
    end
    if btn == "confirm" then
        S.pen = S.pen % 3 + 1
        S.note = nil
        smudge.request_update()
        return
    end
    local r, c = S.cr, S.cc
    local e
    if btn == "left" and c > 0 then
        c, e = c - 1, SL.hEdge(n, r, c - 1)
    elseif btn == "right" and c < n then
        c, e = c + 1, SL.hEdge(n, r, c)
    elseif (btn == "up" or btn == "page_back") and r > 0 then
        r, e = r - 1, SL.vEdge(n, r - 1, c)
    elseif (btn == "down" or btn == "page_forward") and r < n then
        r, e = r + 1, SL.vEdge(n, r, c)
    end
    if not e then return end
    S.cr, S.cc, S.note = r, c, nil
    if S.pen == 2 then
        setEdge(e, P.cur[e] == 1 and 0 or 1)
    elseif S.pen == 3 then
        setEdge(e, P.cur[e] == 2 and 0 or 2)
    else
        smudge.request_update()
    end
end

function M.tap(x, y)
    if B.commonTap(x, y) then return end
    local gx, gy, s = layout()
    -- The nearest edge: a horizontal one on the nearest row of dots or a
    -- vertical one on the nearest column, whichever line is closer.
    local fx, fy = (x - gx) / s, (y - gy) / s
    if fx < -0.4 or fy < -0.4 or fx > n + 0.4 or fy > n + 0.4 then return end
    local hr, hc = math.floor(fy + 0.5), math.floor(fx)
    local vr, vc = math.floor(fy), math.floor(fx + 0.5)
    local dh, dv = math.abs(fy - hr), math.abs(fx - vc)
    local e
    if dh <= dv and hc >= 0 and hc < n then
        e = SL.hEdge(n, hr, hc)
    elseif vr >= 0 and vr < n then
        e = SL.vEdge(n, vr, vc)
    elseif hc >= 0 and hc < n then
        e = SL.hEdge(n, hr, hc)
    end
    if not e or math.min(dh, dv) > 0.4 then return end
    setEdge(e, (P.cur[e] + 1) % 3)
end

return M
end
