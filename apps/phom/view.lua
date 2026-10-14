-- Drawing the Phom table. Usage:
--   local screen = smudge.dofile("view.lua")(S)
-- with S the app's state (main.lua). The screen's input is input.lua;
-- main.lua holds only one of the two (or the computer players while they
-- move) at a time. The layout is S.L. It needs no rules: game.lua leaves the
-- melds and what you may do in the hand.

return function(S)
local V = {}

local RANKS = { "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K" }
local SUITS = { [0] = "spades", "clubs", "diamonds", "hearts" }
local function rankName(c) return RANKS[c // 4 + 1] end
local function cardName(c) return RANKS[c // 4 + 1] .. " of " .. SUITS[c % 4] end
local function has(list, c)
    for _, x in ipairs(list) do
        if x == c then return true end
    end
    return false
end

local SPRITES = { [0] = "sprites/spades", "sprites/clubs", "sprites/diamonds", "sprites/hearts" }
local L, NAMES, PLACES = S.L, S.NAMES, S.PLACES

local function drawCard(x, y, cw, ch, c)
    smudge.rounded_rect(x, y, cw, ch, 5, true, false)
    smudge.rounded_rect(x, y, cw, ch, 5, false, 1)
    local suit = c % 4
    smudge.text(x + 4, y + 2, rankName(c), "ui12", "bold", "left", true)
    smudge.draw_sprite(x + 5, y + 28, 16, 16, SPRITES[suit] .. "16.raw")
    smudge.draw_sprite(x + cw - 36, y + ch - 36, 32, 32, SPRITES[suit] .. ".raw", false, suit >= 2)
end

-- A small card; eaten ones get a thick frame.
local function drawMini(x, y, c, eaten)
    smudge.rounded_rect(x, y, L.mw, L.mh, 4, true, false)
    smudge.rounded_rect(x, y, L.mw, L.mh, 4, false, eaten and 3 or 1)
    smudge.text(x + 3, y + 2, rankName(c), "ui10", "bold", "left", true)
    smudge.draw_sprite(x + 4, y + 26, 16, 16, SPRITES[c % 4] .. "16.raw")
end

-- A row of small cards from x; returns the x after it.
local function miniRow(x, y, cards, eaten, step)
    for i, c in ipairs(cards) do drawMini(x + (i - 1) * step, y, c, eaten) end
    if #cards == 0 then return x end
    return x + (#cards - 1) * step + L.mw
end

local function statusText(g)
    if g.over then
        if g.u then return (g.u == 1 and "You go" or NAMES[g.u] .. " goes") .. " U!" end
        return "Hand over"
    end
    if S.note then return S.note end
    if g.turn ~= 1 then return NAMES[g.turn] .. " is thinking..." end
    if g.phase == "take" then
        if g.eat1 then return "Eat the " .. cardName(g.top) .. " or draw" end
        return "Your turn: draw a card"
    end
    if g.got then return (g.ate and "You ate the " or "You drew the ") .. cardName(g.got) .. ". Throw one" end
    return "You have 10 cards: throw one"
end

local function seatPos(s)
    if s == 3 then return (S.w - L.pw) // 2, L.seatY end
    if s == 4 then return 8, L.seatY + L.ph + 8 end
    return S.w - 8 - L.pw, L.seatY + L.ph + 8
end

local function drawSeat(g, s)
    local x, y = seatPos(s)
    local active = not g.over and g.turn == s
    smudge.rounded_rect(x, y, L.pw, L.ph, 8, false, active and 4 or 2)
    if active then smudge.rect(x + 4, y + 4, L.pw - 8, 24, true, true) end
    local n = #g.hands[s]
    smudge.text(x + 10, y + 5, NAMES[s] .. "  " .. n .. " cards", "ui10", "bold", "left", not active)
    if #g.eaten[s] > 0 then smudge.text(x + L.pw - 10, y + 5, "ate", "ui10", "regular", "right", not active) end
    miniRow(x + 6, y + 32, g.disc[s], false, L.mstep)
    local e = g.eaten[s]
    if #e > 0 then miniRow(x + L.pw - 6 - (#e - 1) * L.mstep - L.mw, y + 32, e, true, L.mstep) end
end

local function drawTable(g)
    local y, w = L.tableY, S.w
    if g.lastSeat then
        local who = NAMES[g.lastSeat]
        local t = g.lastAte and (who .. " ate the " .. cardName(g.lastAte) .. " and threw") or (who .. " threw")
        smudge.centered_text(y, t, "ui10", "bold", true)
    end
    local x = w // 2 - 80
    if g.thrown then
        drawCard(x, y + 26, 60, 86, g.thrown)
        if g.thrownEaten then
            smudge.rect(x - 4, y + 66, 68, 24, true, true)
            smudge.text(x + 30, y + 67, "eaten", "ui10", "bold", "center", false)
        end
    else
        smudge.rounded_rect(x, y + 26, 60, 86, 5, false, 1)
    end
    -- The stock: a card back.
    local sx = w // 2 + 20
    smudge.rounded_rect(sx, y + 26, 60, 86, 5, false, 2)
    smudge.rect_dither(sx + 5, y + 31, 50, 76, false)
    smudge.text(sx + 70, y + 40, "Stock " .. #g.stock, "ui10", "bold", "left", true)
    local r = g.over and 4 or math.min(4, g.nd[g.turn] + 1)
    smudge.text(sx + 70, y + 70, "Round " .. r .. "/4", "ui10", "bold", "left", true)
    -- Your cards thrown.
    local ty = y + 126
    smudge.text(16, ty + 14, "You threw", "ui10", "regular", "left", true)
    miniRow(120, ty, g.disc[1], false, L.mstep)
end

-- The result of a finished hand: everyone's melds and points.
local function drawResult(g)
    local x, y, bw = 10, L.seatY, S.w - 20
    local rh = L.mh + 34
    local hgt = 46 + 4 * rh + 30
    smudge.rounded_rect(x, y, bw, hgt, 10, true, false)
    smudge.rounded_rect(x, y, bw, hgt, 10, false, 3)
    smudge.centered_text(y + 10, g.u and statusText(g) or "Hand over: fewest points wins", "ui12", "bold", true)
    for i, s in ipairs(g.order) do
        local ry = y + 44 + (i - 1) * rh
        local what
        if g.u == s then what = "U"
        elseif g.u then what = g.left[s] .. " pts"
        elseif g.mom[s] then what = "Mom (no meld), " .. g.left[s] .. " pts"
        else what = g.left[s] .. " pts" end
        smudge.text(x + 12, ry, (g.u and (g.u == s and "1st" or "-") or PLACES[i]) .. "  " .. NAMES[s], "ui10",
                    "bold", "left", true)
        smudge.text(x + 124, ry, what, "ui10", "regular", "left", true)
        smudge.text(x + bw - 12, ry, string.format("+%d  total %d", g.pts[s], S.points[s]), "ui10", "bold", "right",
                    true)
        -- The hand: melds, then the cards left over.
        local hand, sizes, cx, k = g.hands[s], g.mlen[s], x + 12, 0
        for _, n in ipairs(sizes) do
            for j = 1, n do
                local c = hand[k + j]
                drawMini(cx + (j - 1) * L.mstep, ry + 24, c, has(g.eaten[s], c))
            end
            k = k + n
            cx = cx + (n - 1) * L.mstep + L.mw + 8
        end
        if k < #hand then
            if k > 0 then cx = cx + 8 end
            for j = k + 1, #hand do
                drawMini(cx, ry + 24, hand[j], false)
                cx = cx + L.mstep
            end
        end
    end
    smudge.centered_text(y + hgt - 28, S.touch and "Tap for the next hand" or "Confirm: next hand", "ui10",
                         "regular", true)
end

-- What may be done now: eat, draw, throw the picked card.
local function canDo(g)
    if g.over or g.turn ~= 1 then return false, false, false end
    if g.phase == "take" then return g.eat1, #g.stock > 0, false end
    return false, false, S.sel ~= nil and g.okd ~= nil and g.okd[S.sel] == true
end

local function drawButton(i, label, enabled, focused)
    local x, y = S.btnX(i), L.btnY
    smudge.rounded_rect(x, y, L.btnW, L.btnH, 8, enabled, enabled and true or 1)
    smudge.text(x + L.btnW // 2, y + (L.btnH - smudge.line_height("ui12")) // 2, label, "ui12",
                enabled and "bold" or "regular", "center", not enabled)
    if focused then smudge.rect(x - 5, y - 5, L.btnW + 10, L.btnH + 10, false, true, 3) end
end

function V.draw()
    local g, touch, w = S.game, S.touch, S.w
    smudge.header("Phom", S.LEVELS[S.level])
    local top = L.top
    local text = statusText(g)
    local maxW = (touch and w - 96 or w) - 24
    smudge.text(16, top + 4, text, smudge.text_width(text, "ui12", "bold") > maxW and "ui10" or "ui12", "bold", "left",
                true)
    if touch then
        smudge.rounded_rect(w - 96, top - 2, 80, 34, 6, false, 2)
        smudge.text(w - 56, top + 4, "Menu", "ui12", "bold", "center", true)
    end
    local hand = g.hands[1]
    local n = #hand
    local cursor = S.cursor
    if g.over then
        drawResult(g)
    else
        for s = 2, 4 do drawSeat(g, s) end
        drawTable(g)
        local e, d, t = canDo(g)
        drawButton(1, "Eat", e, not touch and cursor == n + 1)
        drawButton(2, "Draw", d, not touch and cursor == n + 2)
        drawButton(3, "Discard", t, not touch and cursor == n + 3)
    end
    -- Your hand, melds first (a line under each).
    local k = 0
    for _, size in ipairs(g.mlen[1] or {}) do
        local x1 = S.handX(k + 1, n)
        local x2 = S.handX(k + size, n)
        smudge.rect(x1 + 2, L.handY + L.ch + 3, x2 + L.cw - x1 - 4, 2, true, true)
        k = k + size
    end
    local eaten = g.eaten[1]
    for i, c in ipairs(hand) do
        local x, step = S.handX(i, n)
        local y = S.sel == c and L.handY - L.lift or L.handY
        drawCard(x, y, L.cw, L.ch, c)
        if has(eaten, c) then smudge.circle(x + 11, y + 60, 5, true) end
        if not touch and i == cursor then
            smudge.rect(x + 1, L.handY + L.ch + 9, (i == n and L.cw or step) - 3, 6, true, true)
        end
    end

    if touch then
        smudge.button_hints("", "", "", "")
    elseif g.over then
        smudge.button_hints("Menu", "Next", "", "")
    else
        local label = "Pick"
        if cursor == n + 1 then label = "Eat"
        elseif cursor == n + 2 then label = "Draw"
        elseif cursor == n + 3 then label = "Discard"
        elseif hand[cursor] and S.sel == hand[cursor] then label = "Drop" end
        smudge.button_hints("Menu", label, "<", ">")
    end
end

return V
end
