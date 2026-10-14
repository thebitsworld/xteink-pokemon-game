-- Hexland: the island - hexes with their resource mark, name and number
-- token, the ports, the pieces, the robber and the cursor. Usage:
--   local screen = smudge.dofile("board.lua")(L, S); screen.draw()
-- main.lua draws it, drops it and then draws panel.lua over the rest.

return function(L, S)
local V = {}
local P = smudge.dofile("pieces.lua")()

local function hexOutline(h)
    local x0, y0 = S.vxy(L.hv(h, 6))
    for k = 1, 6 do
        local x, y = S.vxy(L.hv(h, k))
        smudge.line(x0, y0, x, y, 1, true)
        x0, y0 = x, y
    end
end

local function robber(x, y)
    smudge.circle(x, y, 13, true, false)
    smudge.circle(x, y - 7, 5, true, true)
    smudge.triangle(x - 8, y + 9, x + 8, y + 9, x, y - 4, true, true)
    smudge.rect(x - 9, y + 8, 18, 3, true, true)
end

local function hex(g, h)
    local cx, cy = S.hxy(h)
    local r = S.lay.r
    local res = L.res(g, h)
    local lh = smudge.line_height("small")
    hexOutline(h)
    if res == 0 then
        smudge.text(cx, cy - math.floor(r * 0.52) - lh // 2, "Desert", "small", "regular", "center", true)
        for i = 0, 2 do smudge.circle(cx + 4 + i * 10, cy + 10 + (i % 2) * 5, 1, true, true) end
    else
        local n = g.num[h]
        smudge.text(cx, cy - math.floor(r * 0.52) - lh // 2, L.RES[res], "small", "regular", "center", true)
        local side = math.floor(r * 0.58)
        P.icon(res, cx - side, cy)
        P.icon(res, cx + side, cy)
        -- The number token, bold and double-ringed for 6 and 8; pips below.
        local tr = 15
        local hot = n == 6 or n == 8
        smudge.circle(cx, cy, tr, true, false)
        smudge.circle(cx, cy, tr, false, hot and 2 or 1, true)
        if hot then smudge.circle(cx, cy, tr - 3, false, 1, true) end
        local nh = smudge.line_height("ui10")
        smudge.text(cx, cy - nh // 2 - 3, tostring(n), "ui10", hot and "bold" or "regular", "center", true)
        local pips = L.pips(n)
        local px = cx - (pips * 4 - 2) // 2
        for i = 0, pips - 1 do smudge.rect(px + i * 4, cy + 8, 2, 2, true, true) end
    end
    if h == g.robber then robber(cx - math.floor(r * 0.58) + (res == 0 and 10 or 0), cy + (res == 0 and 4 or 0)) end
end

-- A port: a label out at sea, lines to its two corners.
local function port(g, i)
    local e = L.PE:byte(i)
    local a, b = L.ends(e)
    local ax, ay = S.vxy(a)
    local bx, by = S.vxy(b)
    local mx, my = (ax + bx) / 2, (ay + by) / 2
    local h = 0
    for k = 1, 3 do
        local ha = L.vh(a, k)
        for j = 1, 3 do
            if ha > 0 and L.vh(b, j) == ha then h = ha end
        end
    end
    local hx, hy = S.hxy(h)
    local px, py = math.floor(mx + (mx - hx) + 0.5), math.floor(my + (my - hy) + 0.5)
    px = math.max(19, math.min(S.w - 19, px))
    smudge.line(px, py, ax, ay, 1, true)
    smudge.line(px, py, bx, by, 1, true)
    local t = g.ports:byte(i) - 48
    local lh = smudge.line_height("small")
    smudge.rect(px - 19, py - lh, 38, 2 * lh, true, false)
    smudge.rect(px - 19, py - lh, 38, 2 * lh, false, 1, true)
    smudge.text(px, py - lh, t == 0 and "3:1" or "2:1", "small", "bold", "center", true)
    smudge.text(px, py, t == 0 and "any" or L.RES[t], "small", "regular", "center", true)
end

function V.draw()
    local g = S.game
    for h = 1, 19 do hex(g, h) end
    for i = 1, 9 do port(g, i) end
    for e = 1, 72 do
        local p = g.e[e]
        if p > 0 then
            local a, b = L.ends(e)
            local ax, ay = S.vxy(a)
            local bx, by = S.vxy(b)
            P.road(ax, ay, bx, by, p)
        end
    end
    for v = 1, 54 do
        local o = g.v[v]
        if o > 0 then
            local x, y = S.vxy(v)
            P.house(x, y, o > 4 and o - 4 or o, o > 4)
        end
    end
    -- Where the build may go (small rings) and the cursor (a big one).
    local U = S.ui
    if U and U.kind == "place" then
        local function at(s)
            if U.what == L.HEX then return S.hxy(s) end
            if U.what == L.ROAD then return S.exy(s) end
            return S.vxy(s)
        end
        if U.what ~= L.HEX then
            for _, s in ipairs(U.spots) do
                local x, y = at(s)
                smudge.circle(x, y, 5, true, false)
                smudge.circle(x, y, 5, false, 2, true)
            end
        end
        local s = U.spots[U.idx]
        if s and not S.touch then
            local x, y = at(s)
            local rr = U.what == L.HEX and 21 or 14
            smudge.circle(x, y, rr, false, 3, true)
            smudge.circle(x, y, rr + 3, false, 1, false)
        end
    end
end

return V
end
