-- Hexland: everything under and over the island - the prompt line, the four
-- players, the log, your hand, the buttons and any pop-up. Usage:
--   local screen = smudge.dofile("panel.lua")(L, S); screen.draw()
-- main.lua draws it after board.lua (S.ui says what you can do now).

return function(L, S)
local V = {}
local P = smudge.dofile("pieces.lua")()
local BAR = { "Roll", "Build", "Trade", "Cards", "End" }

local function fit(s, max, font)
    while #s > 4 and smudge.text_width(s, font, "regular") > max do s = s:sub(1, -3) .. "." end
    return s
end

local function topBar(g, U)
    local Y = S.lay
    local y = Y.top
    smudge.rect(0, y, S.w, Y.bTop - y - 2, true, false)
    smudge.text(8, y + 4, fit(U.prompt or "", S.w - (S.touch and 170 or 100), "ui10"), "ui10", "bold", "left", true)
    local right = S.w - 8
    if S.touch then
        smudge.rounded_rect(S.w - 76, y + 2, 68, 22, 6, false, 1, true)
        smudge.text(S.w - 42, y + 4, "Menu", "ui10", "bold", "center", true)
        right = S.w - 84
    end
    if g.d1 > 0 then smudge.text(right, y + 4, "Dice " .. g.d1 .. "+" .. g.d2, "ui10", "regular", "right", true) end
    smudge.line(0, Y.bTop - 2, S.w, Y.bTop - 2, 1, true)
end

local function badge(x, y, s)
    local tw = smudge.text_width(s, "small", "bold") + 6
    smudge.rect(x - tw, y, tw, S.lay.lh, true, true)
    smudge.text(x - tw + 3, y, s, "small", "bold", "left", false)
    return x - tw - 3
end

local function players(g)
    local Y = S.lay
    local bw = (S.w - 16) // 4
    smudge.line(0, Y.plY - 3, S.w, Y.plY - 3, 1, true)
    for p = 1, 4 do
        local x, y = 8 + (p - 1) * bw + 2, Y.plY + 1
        local pl = g.pl[p]
        local cur = g.turn == p and g.phase ~= "over"
        smudge.rect(x, y, bw - 4, 58, false, cur and 3 or 1, true)
        P.house(x + 14, y + 15, p, false)
        smudge.text(x + 27, y + 3, L.NAMES[p], "ui10", "bold", "left", true)
        smudge.text(x + bw - 9, y + 5, "VP " .. L.vp(g, p, p == g.human), "small", "bold", "right", true)
        local dev = 0
        for c = 1, 5 do dev = dev + pl.d[c] end
        smudge.text(x + 6, y + 22, L.count(g, p) .. " cards", "small", "regular", "left", true)
        local bx = x + bw - 9
        if g.lr == p then bx = badge(bx, y + 22, "LR") end
        if g.la == p then badge(bx, y + 22, "LA") end
        smudge.text(x + 6, y + 39, "Dev " .. dev .. " K" .. pl.k .. " R" .. pl.len, "small", "regular", "left", true)
    end
end

local function log(g)
    local Y = S.lay
    local t = g.log
    local first = math.max(1, #t - 4)
    for i = first, #t do
        smudge.text(8, Y.logY + (i - first) * Y.lh, fit(t[i], S.w - 16, "small"), "small", i == #t and "bold" or "regular",
            "left", true)
    end
end

local function hand(g)
    local Y = S.lay
    local me = g.human
    local pl = g.pl[me]
    local bw = (S.w - 16) // 5
    smudge.line(0, Y.handY - 4, S.w, Y.handY - 4, 1, true)
    for r = 1, 5 do
        local x, y = 8 + (r - 1) * bw + 2, Y.handY
        smudge.rect(x, y, bw - 4, 42, false, 1, true)
        P.icon(r, x + 14, y + 21)
        smudge.text(x + 29, y + 2, L.RES[r], "small", "regular", "left", true)
        smudge.text(x + 29, y + 16, tostring(pl.r[r]), "ui12", "bold", "left", true)
        smudge.text(x + bw - 9, y + 24, L.rate(g, me, r) .. ":1", "small", "regular", "right", true)
    end
    local parts = {}
    for c = 1, 5 do
        if pl.d[c] > 0 then parts[#parts + 1] = (c == 5 and "VP" or L.DEV[c]) .. " " .. pl.d[c] end
    end
    local s = #parts > 0 and ("Cards: " .. table.concat(parts, ", ")) or "No development cards"
    smudge.text(8, Y.handY + 45, fit(s .. "   (deck " .. #g.deck .. ")", S.w - 16, "small"), "small", "regular", "left",
        true)
end

local function bar(U)
    local lh = smudge.line_height("ui10")
    for i = 1, 5 do
        local x, y, bw, bh = S.barRect(i)
        local on = U.kind == "bar" and U.on[i]
        local focus = on and i == S.bar and not S.touch
        if focus then
            smudge.rounded_rect(x, y, bw, bh, 8, true, true)
        else
            smudge.rounded_rect(x, y, bw, bh, 8, false, on and 2 or 1, true)
        end
        smudge.text(x + bw // 2, y + (bh - lh) // 2, BAR[i], "ui10", on and "bold" or "regular", "center", not focus)
        if not on then smudge.line(x + 10, y + bh // 2 + 1, x + bw - 10, y + bh // 2 + 1, 1, true) end
    end
end

local function popup(U)
    local items = U.items
    local x, y, pw, ph, ry, rh = S.popRect(#items, U.sub)
    local lh = smudge.line_height("ui10")
    smudge.rect(x - 4, y - 4, pw + 8, ph + 8, true, false)
    smudge.rect(x, y, pw, ph, false, 2, true)
    smudge.text(x + pw // 2, y + 10, fit(U.title, pw - 16, "ui10"), "ui10", "bold", "center", true)
    if U.sub then smudge.text(x + pw // 2, y + 32, U.sub, "ui10", "regular", "center", true) end
    for i, it in ipairs(items) do
        local iy = ry + (i - 1) * rh
        local sel = i == U.idx and not S.touch
        if sel then
            smudge.rect(x + 8, iy, pw - 16, rh - 4, true, true)
        else
            smudge.rect(x + 8, iy, pw - 16, rh - 4, false, 1, true)
        end
        local ty = iy + (rh - 4 - lh) // 2
        smudge.text(x + 18, ty, it.label, "ui10", it.off and "regular" or "bold", "left", not sel)
        if it.note then smudge.text(x + pw - 18, ty + 2, it.note, "small", "regular", "right", not sel) end
        if it.off then
            local tw = smudge.text_width(it.label, "ui10", "regular")
            smudge.line(x + 16, ty + lh // 2 + 1, x + 20 + tw, ty + lh // 2 + 1, 1, not sel)
        end
    end
end

function V.draw()
    local g, U = S.game, S.ui
    topBar(g, U)
    players(g)
    log(g)
    hand(g)
    bar(U)
    if U.kind == "pop" then popup(U) end
    if S.touch then
        smudge.button_hints("", "", "", "")
    elseif U.kind == "bar" then
        smudge.button_hints("Menu", "Press", "<", ">")
    elseif U.kind == "place" then
        smudge.button_hints(U.forced and (g.phase == "roads" and "Stop" or "Menu") or "Cancel", "Place", "<", ">")
    elseif U.kind == "pop" then
        smudge.button_hints(U.forced and "Menu" or "Cancel", "Pick", "Up", "Down")
    else
        smudge.button_hints("Menu", "", "", "")
    end
end

return V
end
