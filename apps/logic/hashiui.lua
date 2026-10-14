-- Hashi screen: drawing and input. Usage:
--   local screen = smudge.dofile("hashiui.lua")(S, H)   -- S: main.lua's state, H: hashi.lua
--
-- Buttons (X3/X4): the arrows move the cursor from island to island;
-- Confirm picks the island, then an arrow adds a bridge that way (one, two,
-- then none again); Confirm or Back puts the island down. Back opens the
-- menu. Touch (X4 Pro): tap an island, then a neighbour, to cycle the
-- bridges between them; tap a bridge's water to cycle that bridge.

return function(S, H)
local M = {}
local B = smudge.dofile("board.lua")(S)
local P = S.P
local n = P.size
if S.cr < 1 or S.cr > P.islands then S.cr = 1 end

-- The grid: origin x, y and cell size; the help line's y.
local function layout()
    local bx, by, bw, bh = B.boardRoom()
    local s = math.min(bw // n, bh // n, 72)
    local gy = by + (bh - s * n) // 2
    return bx + (bw - s * n) // 2, gy, s, gy + s * n + 12
end

local function centre(gx, gy, s, k)
    return gx + P.ic:byte(k) * s + s // 2, gy + P.ir:byte(k) * s + s // 2
end

local function drawBridge(gx, gy, s, e, rad)
    local v = P.cur[e]
    if v == 0 then return end
    local x1, y1 = centre(gx, gy, s, P.ea:byte(e))
    local x2, y2 = centre(gx, gy, s, P.eb:byte(e))
    if P.horiz:byte(e) == 1 then
        x1, x2 = x1 + rad, x2 - rad
        if v == 1 then
            smudge.rect(x1, y1 - 1, x2 - x1, 3, true, true)
        else
            smudge.rect(x1, y1 - 6, x2 - x1, 3, true, true)
            smudge.rect(x1, y1 + 4, x2 - x1, 3, true, true)
        end
    else
        y1, y2 = y1 + rad, y2 - rad
        if v == 1 then
            smudge.rect(x1 - 1, y1, 3, y2 - y1, true, true)
        else
            smudge.rect(x1 - 6, y1, 3, y2 - y1, true, true)
            smudge.rect(x1 + 4, y1, 3, y2 - y1, true, true)
        end
    end
end

function M.draw()
    local placed = 0
    for e = 1, P.edges do placed = placed + P.cur[e] end
    B.drawTop("Bridges: " .. placed)
    local gx, gy, s, helpY = layout()
    local rad = s * 2 // 5
    -- Faint dots on the water so rows and columns can be followed.
    for r = 0, n - 1 do
        for c = 0, n - 1 do
            if P.at:byte(r * n + c + 1) == 0 then smudge.rect(gx + c * s + s // 2 - 1, gy + r * s + s // 2 - 1, 2, 2, true, true) end
        end
    end
    for e = 1, P.edges do drawBridge(gx, gy, s, e, rad) end
    local font = s >= 44 and "ui12" or "ui10"
    local lh = smudge.line_height(font)
    for k = 1, P.islands do
        local x, y = centre(gx, gy, s, k)
        local have = H.sum(P, k)
        local full = have == P.num:byte(k)
        smudge.circle(x, y, rad, true, 1, false)
        if full then
            smudge.circle(x, y, rad, true, 1, true)
        else
            smudge.circle(x, y, rad, false, have > P.num:byte(k) and 5 or 2, true)
        end
        smudge.text(x, y - lh // 2, tostring(P.num:byte(k)), font, "bold", "center", not full)
        if k == S.sel then
            smudge.rect(x - s // 2 + 1, y - s // 2 + 1, s - 2, s - 2, false, 4, true)
        elseif k == S.cr and not S.touch then
            smudge.rect(x - s // 2 + 2, y - s // 2 + 2, s - 4, s - 4, false, 2, true)
        end
    end
    if B.drawSolved() then return end
    if S.touch then
        B.drawHelp(helpY, S.sel and "Tap a neighbour to add a bridge" or "Tap an island, then a neighbour")
        smudge.button_hints("", "", "", "")
    elseif S.sel then
        B.drawHelp(helpY, "Arrows: bridge that way (1, 2, none)")
        smudge.button_hints("Done", "Done", "<", ">")
    else
        B.drawHelp(helpY, "Arrows: move   Confirm: pick island")
        smudge.button_hints("Menu", "Pick", "<", ">")
    end
end

local function cycle(e)
    local ok, why = H.cycle(P, e)
    if ok then
        B.moved()
    else
        S.note = why == "cross" and "Bridges cannot cross." or nil
        smudge.request_update()
    end
end

-- The nearest island in direction d (1 up, 2 down, 3 left, 4 right) from k:
-- straight along the line first, else the closest one on that side.
local function nextIsland(k, d)
    local o = H.neighbour(P, k, d)
    if o then return o end
    local best, bestD = nil, 1e9
    for j = 1, P.islands do
        local dr, dc = P.ir:byte(j) - P.ir:byte(k), P.ic:byte(j) - P.ic:byte(k)
        local along, across = 0, 0
        if d == 1 then along, across = -dr, dc elseif d == 2 then along, across = dr, dc
        elseif d == 3 then along, across = -dc, dr else along, across = dc, dr end
        if along > 0 then
            local dist = along + 2 * math.abs(across)
            if dist < bestD then best, bestD = j, dist end
        end
    end
    return best
end

local DIRS = { up = 1, page_back = 1, down = 2, page_forward = 2, left = 3, right = 4 }

function M.button(btn)
    if B.commonButton(btn) then return end
    local d = DIRS[btn]
    if S.sel then
        if btn == "confirm" or btn == "back" then
            S.sel = nil
        elseif d then
            local o, e = H.neighbour(P, S.sel, d)
            if e then
                cycle(e)
                return
            end
            S.note = "No island that way."
        end
    elseif btn == "back" then
        S.show("pause")
        return
    elseif btn == "confirm" then
        S.sel, S.note = S.cr, nil
    elseif d then
        S.cr = nextIsland(S.cr, d) or S.cr
        S.note = nil
    end
    smudge.request_update()
end

function M.tap(x, y)
    if B.commonTap(x, y) then return end
    local gx, gy, s = layout()
    if not smudge.in_rect(x, y, gx, gy, s * n, s * n) then return end
    local r, c = (y - gy) // s, (x - gx) // s
    local k = P.at:byte(r * n + c + 1)
    if k == 0 then k = nil end
    if k then
        local e = S.sel and S.sel ~= k and H.edgeBetween(P, S.sel, k)
        if e then
            S.sel = nil
            cycle(e)
            return
        end
        S.sel = (S.sel ~= k) and k or nil
        S.note = nil
    else
        -- Water: the bridge passing over it, if any (one with bridges first).
        local pick = nil
        for e = 1, P.edges do
            local a, b = P.ea:byte(e), P.eb:byte(e)
            local over
            if P.horiz:byte(e) == 1 then
                over = P.ir:byte(a) == r and P.ic:byte(a) < c and c < P.ic:byte(b)
            else
                over = P.ic:byte(a) == c and P.ir:byte(a) < r and r < P.ir:byte(b)
            end
            if over and (not pick or P.cur[e] > 0) then pick = e end
        end
        S.sel = nil
        if pick then
            cycle(pick)
            return
        end
    end
    smudge.request_update()
end

return M
end
