-- Hexland drawings shared by board.lua and panel.lua: the players' pieces
-- and the resource marks. Usage: local P = smudge.dofile("pieces.lua")()
-- The four players differ in 1-bit by their fill: you solid black, Ana an
-- outline, Ben hatched (dithered), Cyd dotted. Roads likewise: solid,
-- hollow, dashed and dotted.

return function()
local P = {}

-- A settlement (house) or a city (house with a tower) centred on x, y.
function P.house(x, y, p, city)
    local black = p == 1
    local rects, tris
    if city then
        rects = { { x - 11, y - 2, 22, 10 }, { x + 1, y - 11, 10, 9 } }
        tris = { { x - 12, y - 2, x + 1, y - 2, x - 5, y - 10 } }
    else
        rects = { { x - 7, y - 2, 14, 9 } }
        tris = { { x - 9, y - 2, x + 9, y - 2, x, y - 11 } }
    end
    smudge.rect(x - (city and 14 or 11), y - 13, city and 28 or 22, city and 24 or 23, true, false)
    for _, t in ipairs(tris) do smudge.triangle(t[1], t[2], t[3], t[4], t[5], t[6], true, black) end
    for _, r in ipairs(rects) do smudge.rect(r[1], r[2], r[3], r[4], true, black) end
    if black then
        if city then smudge.rect(x + 4, y - 11, 4, 3, true, false) end
        return
    end
    local th = p == 2 and 2 or 1
    if p == 3 then
        local r = rects[1]
        smudge.rect_dither(r[1] + 1, r[2] + 1, r[3] - 2, r[4] - 2, true)
        if city then smudge.rect_dither(x + 2, y - 10, 8, 8, true) end
    elseif p == 4 then
        smudge.circle(x - (city and 5 or 0), y + 3, 2, true, true)
        if city then smudge.circle(x + 6, y - 6, 2, true, true) end
    end
    for _, t in ipairs(tris) do
        smudge.line(t[1], t[2], t[5], t[6], th, true)
        smudge.line(t[3], t[4], t[5], t[6], th, true)
    end
    for _, r in ipairs(rects) do smudge.rect(r[1], r[2], r[3], r[4], false, th, true) end
    if city then smudge.rect(x + 4, y - 11, 4, 3, true, false) end
end

-- A road from (ax, ay) to (bx, by), its ends kept clear of the corners.
function P.road(ax, ay, bx, by, p)
    local dx, dy = bx - ax, by - ay
    local function at(t) return math.floor(ax + dx * t + 0.5), math.floor(ay + dy * t + 0.5) end
    local x1, y1 = at(0.2)
    local x2, y2 = at(0.8)
    smudge.line(x1, y1, x2, y2, 14, false)
    if p == 1 then
        smudge.line(x1, y1, x2, y2, 7, true)
    elseif p == 2 then
        smudge.line(x1, y1, x2, y2, 11, true)
        smudge.line(x1, y1, x2, y2, 5, false)
    elseif p == 3 then
        for _, s in ipairs({ 0.2, 0.43, 0.66 }) do
            local sx, sy = at(s)
            local ex, ey = at(s + 0.14)
            smudge.line(sx, sy, ex, ey, 8, true)
        end
    else
        for i = 0, 4 do
            local cx, cy = at(0.24 + i * 0.13)
            smudge.circle(cx, cy, 3, true, true)
        end
    end
end

-- Resource marks, about 18 px: 1 Wood (tree), 2 Brick (wall), 3 Wool
-- (fleece), 4 Grain (ear), 5 Ore (mountains).
function P.icon(r, x, y)
    if r == 1 then
        smudge.triangle(x, y - 9, x - 7, y + 4, x + 7, y + 4, true, true)
        smudge.line(x, y + 4, x, y + 9, 3, true)
    elseif r == 2 then
        smudge.rect(x - 9, y - 7, 18, 14, false, 1, true)
        smudge.line(x - 9, y, x + 9, y, 1, true)
        smudge.line(x - 3, y - 7, x - 3, y, 1, true)
        smudge.line(x + 4, y - 7, x + 4, y, 1, true)
        smudge.line(x, y, x, y + 7, 1, true)
    elseif r == 3 then
        for _, c in ipairs({ { -5, 2 }, { 5, 2 }, { 0, -4 }, { 0, 4 } }) do
            smudge.circle(x + c[1], y + c[2], 5, true, false)
            smudge.circle(x + c[1], y + c[2], 5, false, 1, true)
        end
        smudge.circle(x, y + 1, 3, true, false)
    elseif r == 4 then
        smudge.line(x, y - 9, x, y + 9, 1, true)
        for i = 0, 2 do
            local yy = y - 6 + i * 5
            smudge.line(x, yy, x - 5, yy - 4, 2, true)
            smudge.line(x, yy, x + 5, yy - 4, 2, true)
        end
    elseif r == 5 then
        smudge.triangle(x - 10, y + 7, x - 3, y - 7, x + 4, y + 7, true, true)
        smudge.triangle(x, y + 7, x + 5, y - 2, x + 10, y + 7, false, 1, true)
    end
end

return P
end
