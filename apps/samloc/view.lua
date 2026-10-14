-- Drawing the Sam Loc table. Usage:
--   local screen = smudge.dofile("view.lua")(T, S)
-- with T the rules (logic.lua) and S the app's state (main.lua). The
-- screen's input is input.lua; main.lua holds only one of the two (or the
-- computer players while they move) at a time. The layout is S.L.

return function(T, S)
local V = {}

local SPRITES = { [0] = "sprites/spades", "sprites/clubs", "sprites/diamonds", "sprites/hearts" }
local KINDS = { "a single", "a pair", "a triple", "four of a kind" }
local L, NAMES = S.L, S.NAMES

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
    return KINDS[k]
end

local function wins(s) return s == 1 and "You win" or NAMES[s] .. " wins" end

local function title(g)
    if g.instant then return wins(g.winner) .. " at once: " .. g.instant end
    if g.samFail then return (g.sam == 1 and "Your" or NAMES[g.sam] .. "'s") .. " Sam is beaten" end
    if g.sam then return wins(g.sam) .. " the Sam!" end
    return wins(g.winner) .. " the hand"
end

local function statusText(g)
    if g.over then return title(g) end
    if S.note then return S.note end
    if g.ask then
        if g.turn == 1 then return "Declare Sam? Shed all, never beaten" end
        return NAMES[g.turn] .. " is deciding on Sam..."
    end
    if g.turn ~= 1 then return NAMES[g.turn] .. " is thinking..." end
    if g.sam and g.sam ~= 1 then return "Beat " .. NAMES[g.sam] .. " to stop the Sam, or pass" end
    if g.sam == 1 and not g.cur then return "Your Sam: lead and never be beaten" end
    local text = g.cur and "Your turn: beat it or pass" or "Your lead"
    if T.mustTop(g, 1) then text = text .. " (next on one)" end
    return text
end

local function seatBox(s)
    if s == 3 then return (S.w - L.boxW) // 2, L.top + 38 end
    if s == 4 then return 10, L.top + 104 end
    return S.w - 10 - L.boxW, L.top + 104
end

local function drawSeat(g, s)
    local x, y = seatBox(s)
    local active = not g.over and g.turn == s
    local n = #g.hands[s]
    smudge.rounded_rect(x, y, L.boxW, L.boxH, 8, active, active and true or (n == 1 and 4 or 2))
    smudge.text(x + L.boxW // 2, y + 4, g.sam == s and NAMES[s] .. " - SAM" or NAMES[s], "ui12", "bold", "center",
                not active)
    local info = n == 1 and "Bao 1!" or n .. " cards"
    if g.passed[s] and not g.over then info = info .. " - pass" end
    smudge.text(x + L.boxW // 2, y + 31, info, "ui10", n == 1 and "bold" or "regular", "center", not active)
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
    elseif not g.ask then
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
    local x, y, bw, lh = 20, L.tableY - 30, S.w - 40, 30
    local bh = 60 + 5 * lh
    smudge.rounded_rect(x, y, bw, bh, 10, true, false)
    smudge.rounded_rect(x, y, bw, bh, 10, false, 3)
    smudge.centered_text(y + 12, title(g), "ui12", "bold", true)
    local pts = g.pts or { 0, 0, 0, 0 }
    for s = 1, 4 do
        local ty = y + 18 + s * lh
        local hand, info = g.hands[s], nil
        if s == g.winner then
            info = g.samFail and "stopped it" or (g.sam and "Sam" or (g.instant and "instant" or "out"))
        elseif g.samFail and s == g.sam then
            info = "Sam beaten"
        elseif g.sam or g.instant then
            info = ""
        else
            local twos = 0
            for _, c in ipairs(hand) do
                if T.rank(c) == T.TWO then twos = twos + 1 end
            end
            info = #hand .. " left"
            if twos > 0 then info = info .. ", " .. twos .. (twos == 1 and " two" or " twos") end
            if not g.played[s] then info = info .. ", cong" end
        end
        smudge.text(x + 16, ty, NAMES[s], "ui12", "bold", "left", true)
        smudge.text(x + 100, ty + 3, info, "ui10", "regular", "left", true)
        smudge.text(x + bw - 16, ty, string.format("%+d   total %d", pts[s], S.points[s]), "ui12", "regular", "right",
                    true)
    end
    smudge.centered_text(y + 24 + 5 * lh, S.touch and "Tap for the next hand" or "Confirm: next hand", "ui10",
                         "regular", true)
end

-- Whether the picked cards can be played now (game.lua's refusal, which
-- the drawing does not keep in memory, has the reasons).
local function canPlay(g)
    if g.turn ~= 1 then return false end
    local cards = S.selected()
    local k, l, t = T.classify(cards)
    if not k then return false end
    local hand = g.hands[1]
    if k == T.SINGLE and T.mustTop(g, 1) and t < T.rank(hand[#hand]) then return false end
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
    smudge.header("Sam Loc", S.LEVELS[S.level])
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
    local ask = g.ask and g.turn == 1
    if not g.over then
        drawButton(L.playX, ask and "Sam" or "Play", ask or canPlay(g), not touch and cursor == n + 1)
        drawButton(L.passX, ask and "No" or "Pass", ask or g.turn == 1 and g.cur ~= nil, not touch and cursor == n + 2)
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
        if cursor == n + 1 then label = ask and "Sam" or "Play"
        elseif cursor == n + 2 then label = ask and "No" or "Pass"
        elseif hand[cursor] and S.sel[hand[cursor]] then label = "Drop" end
        smudge.button_hints("Menu", label, "<", ">")
    end
end

return V
end
