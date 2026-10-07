-- The Tide & Paper table screen, drawing half (input.lua handles the
-- buttons and taps; layout.lua is shared). Usage:
--   local screen = smudge.dofile("view.lua")(T, S)
-- main.lua loads one half at a time, drops the screen while the device plays
-- and shows result.lua between rounds.

return function(T, S)
local V = {}
local Lay = smudge.dofile("layout.lua")(T, S)
local layout, tableItems, tableX, handPos = Lay.layout, Lay.tableItems, Lay.tableX, Lay.handPos
local buttonRect, crabCards, buttonEnabled, BUTTONS = Lay.buttonRect, Lay.crabCards, Lay.buttonEnabled, Lay.BUTTONS

-- What each kind scores, in words (shown when one card is selected).
local HELP = {
    "Pair: take a card from a discard pile", "Pair: play again", "Pair: draw a card",
    "With a shark: steal a card", "With a swimmer: steal a card",
    "1-6 shells score 0 2 4 6 8 10", "1-5 score 0 3 6 9 12", "1-3 score 1 3 5", "1-2 score 0 5",
    "1 point per boat", "1 point per fish", "2 points per penguin", "3 points per sailor",
    "Scores your commonest mark; four win the game",
}

local function mark(m, cx, cy, r)
    if m == 0 then return end
    local filled = m <= 3
    local shape = (m - 1) % 3
    if shape == 0 then
        smudge.circle(cx, cy, r, filled, filled and true or 2)
    elseif shape == 1 then
        smudge.rect(cx - r, cy - r, 2 * r, 2 * r, filled, true, 2)
    else
        if filled then
            smudge.triangle(cx, cy - r, cx - r, cy + r, cx + r, cy + r, true, true)
        else
            smudge.line(cx, cy - r, cx - r, cy + r, 2, true)
            smudge.line(cx - r, cy + r, cx + r, cy + r, 2, true)
            smudge.line(cx + r, cy + r, cx, cy - r, 2, true)
        end
    end
end

local function card(x, y, cw, ch, id, selected, focused)
    smudge.rounded_rect(x, y, cw, ch, 6, true, false)
    smudge.rounded_rect(x, y, cw, ch, 6, false, selected and 4 or 1)
    if focused then smudge.rect(x - 4, y - 4, cw + 8, ch + 8, false, true, 2) end
    if not id then return end
    local kind = T.KIND[id]
    if kind == T.MERMAID then
        -- A mermaid: a dithered band along the bottom of the card.
        smudge.rect_dither(x + 6, y + ch - 18, cw - 12, 10, true)
    end
    mark(T.MARK[id], x + 12, y + 12, 6)
    local name = T.NAMES[kind]
    smudge.text(x + cw // 2, y + ch // 2 - 8, name, "ui10", "bold", "center", true)
    if T.isDuo(kind) then smudge.text(x + cw // 2, y + ch - 24, "pair", "ui10", "regular", "center", true) end
end

local function back(x, y, cw, ch, count, focused)
    smudge.rounded_rect(x, y, cw, ch, 6, true, false)
    smudge.rect_dither(x + 4, y + 4, cw - 8, ch - 8, true)
    smudge.rounded_rect(x, y, cw, ch, 6, false, 2)
    smudge.rounded_rect(x + cw // 2 - 22, y + ch // 2 - 14, 44, 28, 6, true, false)
    smudge.text(x + cw // 2, y + ch // 2 - 10, tostring(count), "ui12", "bold", "center", true)
    if focused then smudge.rect(x - 4, y - 4, cw + 8, ch + 8, false, true, 2) end
end

local function emptySlot(x, y, cw, ch, focused)
    smudge.rounded_rect(x, y, cw, ch, 6, false, 1)
    smudge.text(x + cw // 2, y + ch // 2 - 8, "empty", "ui10", "regular", "center", true)
    if focused then smudge.rect(x - 4, y - 4, cw + 8, ch + 8, false, true, 2) end
end

-- "Crab x2, Boat x2" for a player's played pairs.
local function playedText(g, p)
    local counts, order = {}, {}
    for _, id in ipairs(g.played[p]) do
        local k = T.KIND[id]
        if not counts[k] then order[#order + 1] = k end
        counts[k] = (counts[k] or 0) + 1
    end
    if #order == 0 then return "nothing yet" end
    local parts = {}
    for _, k in ipairs(order) do parts[#parts + 1] = T.NAMES[k] .. " x" .. counts[k] end
    return table.concat(parts, ", ")
end

local function prompt(g)
    if S.thinking then return "The device is playing..." end
    if g.turn ~= 1 then return "" end
    if g.phase == "draw" then return "Draw two cards, or take a pile's top card." end
    if g.phase == "keep" then
        if not S.keepIdx then return "Choose the card to keep." end
        return "Choose the pile for the other card."
    end
    if #S.sel == 1 then
        local kind = T.KIND[g.hands[1][S.sel[1]]]
        return T.NAMES[kind] .. ": " .. HELP[kind]
    end
    if g.caller == 2 then return "Last chance: your final turn. Then End turn." end
    if T.canEnd(g) then return "7+ points: Stop, or bet with Last chance." end
    return "Pick two cards for a pair, or End turn."
end

function V.draw()
    local g, L = S.game, layout()
    smudge.header("Tide & Paper", S.LEVELS[S.level])
    smudge.text(16, L.top, string.format("You %d   Device %d   (to %d)", g.scores[1], g.scores[2], T.TARGET), "ui12",
                "bold", "left", true)
    if S.touch then
        smudge.rounded_rect(S.w - 96, L.top - 4, 80, 36, 6, false, 2)
        smudge.text(S.w - 56, L.top + 2, "Menu", "ui12", "bold", "center", true)
    end
    local dev = string.format("Device: %d in hand; played %s", #g.hands[2], playedText(g, 2))
    if g.caller == 2 and g.phase ~= "round" and g.phase ~= "over" then dev = "Device bet LAST CHANCE. " .. dev end
    local res = smudge.wrapped_text(dev, S.w - 32, "ui10", "regular")
    for k = 1, math.min(2, #res.lines) do smudge.text(16, L.top + 30 + (k - 1) * 20, res.lines[k], "ui10", "regular", "left", true) end

    if g.phase == "crab" and g.turn == 1 then
        smudge.text(16, L.table - 4, "Crab pair: take any card from a discard pile.", "ui10", "bold", "left", true)
        for k, id in ipairs(crabCards(g)) do
            local x, y = handPos(L, k)
            card(x, y + (L.table - L.hand) + 24, L.hw, L.hh, id, false, not S.touch and S.row == 2 and S.col == k)
        end
    else
        local items = tableItems(g)
        for k, it in ipairs(items) do
            local x = tableX(L, #items, k)
            local focused = not S.touch and S.row == 1 and S.col == k and not S.thinking
            if it.kind == "deck" then
                back(x, L.table, L.tw, L.th, #g.deck, focused)
                smudge.text(x + L.tw // 2, L.table + L.th + 4, "Deck", "ui10", "regular", "center", true)
            elseif it.kind == "drawn" then
                card(x, L.table, L.tw, L.th, g.drawn[it.n], S.keepIdx == it.n, focused)
                smudge.text(x + L.tw // 2, L.table + L.th + 4, "Drawn", "ui10", "regular", "center", true)
            else
                local pile = g.piles[it.n]
                if #pile == 0 then emptySlot(x, L.table, L.tw, L.th, focused) else
                    card(x, L.table, L.tw, L.th, pile[#pile], false, focused)
                end
                smudge.text(x + L.tw // 2, L.table + L.th + 4, string.format("Pile %d (%d)", it.n, #pile), "ui10",
                            "regular", "center", true)
            end
        end
        -- Two lines: what to do, then what the device just did (or the rest of
        -- a long hint).
        local res2 = smudge.wrapped_text(prompt(g), S.w - 32, "ui10", "bold")
        local second = res2.lines[2]
        if S.deviceLog and g.turn == 1 and g.phase == "draw" then
            second = smudge.wrapped_text(S.deviceLog, S.w - 32, "ui10", "regular").lines[1]
        end
        if res2.lines[1] then smudge.text(16, L.status, res2.lines[1], "ui10", "bold", "left", true) end
        if second then smudge.text(16, L.status + 20, second, "ui10", "regular", "left", true) end
        smudge.text(16, L.played, "You played: " .. playedText(g, 1), "ui10", "regular", "left", true)
        for k, id in ipairs(g.hands[1]) do
            local x, y = handPos(L, k)
            if y + L.hh <= L.points then
                local selected = false
                for _, s in ipairs(S.sel) do
                    if s == k then selected = true end
                end
                card(x, y, L.hw, L.hh, id, selected, not S.touch and S.row == 2 and S.col == k and not S.thinking)
            end
        end
    end
    smudge.text(16, L.points, string.format("Your cards: %d points, mark bonus %d", T.cardPoints(g, 1),
                                            T.markBonus(g, 1)), "ui10", "bold", "left", true)
    for k, label in ipairs(BUTTONS) do
        local x, y, bw, bh = buttonRect(L, k)
        local on = buttonEnabled(g, k)
        local focused = not S.touch and S.row == 3 and S.col == k and not S.thinking
        if on then
            smudge.rounded_rect(x, y, bw, bh, 8, focused, focused and true or 2)
        else
            smudge.rounded_rect(x, y, bw, bh, 8, false, 1)
        end
        local font = smudge.text_width(label, "ui12", "bold") > bw - 6 and "ui10" or "ui12"
        smudge.text(x + bw // 2, y + 12, label, font, on and "bold" or "regular", "center", not (on and focused))
        if focused and not on then smudge.rect(x - 3, y - 3, bw + 6, bh + 6, false, true, 2) end
    end
    if S.touch then
        smudge.button_hints("", "", "", "")
    else
        smudge.button_hints("Menu", "Select", "<", ">")
    end
end

return V
end
