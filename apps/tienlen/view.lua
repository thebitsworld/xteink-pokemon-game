-- Drawing the Tien Len table. Usage:
--   local screen = smudge.dofile("view.lua")(T, S)
-- with T the rules (logic.lua) and S the app's state (main.lua). The
-- screen's input is input.lua; main.lua holds only one of the two (or the
-- computer players while they move) at a time. The layout is S.L.

return function(T, S)
local V = {}

local SPRITES = { [0] = "sprites/spades", "sprites/clubs", "sprites/diamonds", "sprites/hearts" }
local KINDS = { "a single", "a pair", "a triple", "four of a kind" }
local L, NAMES, PLACES = S.L, S.NAMES, S.PLACES

local function drawCard(x, y, cw, ch, c)
    smudge.rounded_rect(x, y, cw, ch, 5, true, false)
    smudge.rounded_rect(x, y, cw, ch, 5, false, 1)
    local suit = c % 4
    local red = suit >= 2 -- the big diamond and heart greyed
    smudge.text(x + 4, y + 2, T.rankName(c), "ui12", "bold", "left", true)
    smudge.draw_sprite(x + 5, y + 28, 16, 16, SPRITES[suit] .. "16.raw") -- solid: the shapes tell the suits apart
    smudge.draw_sprite(x + cw - 36, y + ch - 36, 32, 32, SPRITES[suit] .. ".raw", false, red)
end

local function kindText(k, l)
    if k == T.STRAIGHT then return "a straight of " .. l end
    if k == T.PAIRS then return l .. " pairs in a row" end
    return KINDS[k]
end

local function place(g, seat)
    for i, s in ipairs(g.order) do
        if s == seat then return PLACES[i] end
    end
    return nil
end

local function statusText(g)
    if g.over and g.instant then
        return (g.instant == 1 and "You win" or NAMES[g.instant] .. " wins") .. " at once!"
    end
    if g.over then return "You finished " .. place(g, 1) end
    if S.note then return S.note end
    if g.out[1] then return "You finished " .. place(g, 1) .. "!" end
    if g.turn == 1 then
        if g.need3 then return "Your lead: play the 3 of spades" end
        if not g.cur then return "Your lead" end
        return "Your turn: beat it or pass"
    end
    return NAMES[g.turn] .. " is thinking..."
end

local function seatBox(s)
    if s == 3 then return (S.w - L.boxW) // 2, L.top + 38 end
    if s == 4 then return 10, L.top + 104 end
    return S.w - 10 - L.boxW, L.top + 104
end

local function drawSeat(g, s)
    local x, y = seatBox(s)
    local active = not g.over and g.turn == s
    smudge.rounded_rect(x, y, L.boxW, L.boxH, 8, active, active and true or 2)
    smudge.text(x + L.boxW // 2, y + 4, NAMES[s], "ui12", "bold", "center", not active)
    local info
    if g.out[s] then
        info = "Out: " .. place(g, s)
    else
        local n = #g.hands[s]
        info = n .. (n == 1 and " card" or " cards")
        if g.passed[s] then info = info .. ", passed" end
    end
    smudge.text(x + L.boxW // 2, y + 31, info, "ui10", "regular", "center", not active)
end

local function drawTable(g)
    local y = L.tableY
    local cards = g.cur
    if cards then
        local by = g.curBy
        local verb = g.chop and " chop with " or " play "
        if by ~= 1 then verb = g.chop and " chops with " or " plays " end
        smudge.centered_text(y, NAMES[by] .. verb .. kindText(g.ck, g.cl), "ui12", "bold", true)
        local n = #cards
        local step = S.rowStep(n, L.tcw, S.w - 40)
        local x0 = (S.w - (step * (n - 1) + L.tcw)) // 2
        for i, c in ipairs(cards) do drawCard(x0 + (i - 1) * step, y + 30, L.tcw, L.tch, c) end
    else
        local who = g.turn == 1 and "You lead" or (NAMES[g.turn] .. " leads")
        smudge.centered_text(y + 50, g.newRound and ("All passed. " .. who .. " a new round") or who, "ui12", "bold",
                             true)
    end
    if g.lastSeat and g.lastPass and not g.newRound then
        smudge.centered_text(y + 40 + L.tch, NAMES[g.lastSeat] .. (g.lastSeat == 1 and " pass" or " passes"), "ui10",
                             "regular", true)
    end
end

-- The result of a finished hand, over the table.
local function drawResult(g)
    local x, y, bw, lh = 20, L.tableY - 6, S.w - 40, 30
    smudge.rounded_rect(x, y, bw, 52 + 5 * lh, 10, true, false)
    smudge.rounded_rect(x, y, bw, 52 + 5 * lh, 10, false, 3)
    local title = "Hand over"
    if g.instant then title = NAMES[g.instant] .. (g.instant == 1 and " win" or " wins") .. " at once: " .. g.instantWhy end
    smudge.centered_text(y + 12, title, "ui12", "bold", true)
    for i, s in ipairs(g.order) do
        local ty = y + 12 + i * lh
        -- Points of this hand: 3/2/1/0 by place, the winner's 3 alone after an instant win.
        local pts = g.instant and 3 or 4 - i
        smudge.text(x + 20, ty, g.instant and "Instant" or PLACES[i], "ui12", "bold", "left", true)
        smudge.text(x + 120, ty, NAMES[s], "ui12", "regular", "left", true)
        smudge.text(x + bw - 20, ty, string.format("+%d   total %d", pts, S.points[s]), "ui12", "regular", "right", true)
    end
    smudge.centered_text(y + 12 + 5 * lh, S.touch and "Tap for the next hand" or "Confirm: next hand", "ui10",
                         "regular", true)
end

-- Whether the picked cards can be played now (game.lua's refusal, which
-- the drawing does not keep in memory, has the reasons).
local function canPlay(g)
    if g.turn ~= 1 then return false end
    local cards = S.selected()
    local k, l, t = T.classify(cards)
    if not k or (g.need3 and cards[1] ~= T.THREE_SPADES) then return false end
    return T.beats(k, l, t, g.ck, g.cl, g.ct)
end

local function drawButton(x, label, enabled, focused)
    local y = L.btnY
    -- Black when it can be used now, a thin outline when not.
    smudge.rounded_rect(x, y, L.btnW, L.btnH, 8, enabled, enabled and true or 1)
    smudge.text(x + L.btnW // 2, y + (L.btnH - smudge.line_height("ui12")) // 2, label, "ui12",
                enabled and "bold" or "regular", "center", not enabled)
    if focused then smudge.rect(x - 5, y - 5, L.btnW + 10, L.btnH + 10, false, true, 3) end
end

function V.draw()
    local g, touch, w = S.game, S.touch, S.w
    smudge.header("Tien Len", S.LEVELS[S.level])
    local top = L.top
    local text = statusText(g)
    local maxW = (touch and w - 96 or w) - 24
    smudge.text(16, top + 4, text, smudge.text_width(text, "ui12", "bold") > maxW and "ui10" or "ui12", "bold", "left",
                true)
    if touch then
        smudge.rounded_rect(w - 96, top - 2, 80, 34, 6, false, 2)
        smudge.text(w - 56, top + 4, "Menu", "ui12", "bold", "center", true)
    end
    for s = 2, 4 do drawSeat(g, s) end
    if g.over then drawResult(g) else drawTable(g) end

    local hand = g.hands[1]
    local n = #hand
    local cursor = S.cursor
    if not g.over then
        drawButton(L.playX, "Play", canPlay(g), not touch and cursor == n + 1)
        drawButton(L.passX, "Pass", g.turn == 1 and g.cur ~= nil, not touch and cursor == n + 2)
    end
    for k, c in ipairs(hand) do
        local x, step = S.handX(k, n)
        drawCard(x, S.sel[c] and L.handY - L.lift or L.handY, L.cw, L.ch, c)
        if not touch and k == cursor then
            smudge.rect(x + 1, L.handY + L.ch + 3, (k == n and L.cw or step) - 3, 6, true, true)
        end
    end

    if touch then
        smudge.button_hints("", "", "", "")
    elseif g.over then
        smudge.button_hints("Menu", "Next", "", "")
    else
        local label = "Pick"
        if cursor == n + 1 then label = "Play"
        elseif cursor == n + 2 then label = "Pass"
        elseif hand[cursor] and S.sel[hand[cursor]] then label = "Drop" end
        smudge.button_hints("Menu", label, "<", ">")
    end
end

return V
end
