-- The Bazaar table screen: drawing and input. Usage:
--   local screen = smudge.dofile("view.lua")(B, S)
-- with B the rules (logic.lua) and S the app's shared state (main.lua).
-- main.lua drops this module while the computer trades.

return function(B, S)
local V = {}

local BUTTONS = { "Take", "Sell", "Clear" }

local function contentTop() return S.m.top_padding + S.m.header_height + 6 end
local function menuButtonX() return S.w - 96 end

local function layout()
    local w = S.w
    local top = contentTop()
    local L = { top = top }
    L.piles = top + 62
    L.mcw = math.min(84, (w - 16 - 4 * 6) // 5)
    L.mch = L.mcw * 3 // 2
    L.market = L.piles + 6 * 22 + 34
    L.hcw = math.min(64, (w - 16 - 6 * 6) // 7)
    L.hch = L.hcw * 3 // 2
    L.hand = L.market + L.mch + 30
    L.buttons = L.hand + L.hch + 30
    return L
end

local function marketX(L, i) return (S.w - 5 * L.mcw - 4 * 6) // 2 + (i - 1) * (L.mcw + 6) end
local function handX(L, k) return (S.w - 7 * L.hcw - 6 * 6) // 2 + (k - 1) * (L.hcw + 6) end
local function buttonRect(L, k)
    local bw = (S.w - 32 - 2 * 10) // 3
    return 16 + (k - 1) * (bw + 10), L.buttons, bw, 44
end

-- Drawing --------------------------------------------------------------------

-- A small picture of each kind of card, in a box of side s centred at (cx, cy).
local function glyph(kind, cx, cy, s)
    local r = s // 2
    if kind == B.DIAMOND then
        smudge.triangle(cx, cy - r, cx - r, cy, cx + r, cy, true, true)
        smudge.triangle(cx - r, cy, cx + r, cy, cx, cy + r, true, true)
    elseif kind == B.GOLD then
        smudge.circle(cx, cy, r, true, true)
        smudge.circle(cx, cy, r // 2, false, 2)
        smudge.circle(cx, cy, r // 2, true, false)
    elseif kind == B.SILVER then
        smudge.circle(cx, cy, r, false, 3)
        smudge.circle(cx, cy, r // 3, true, true)
    elseif kind == B.CLOTH then
        smudge.rect_dither(cx - r, cy - r * 2 // 3, 2 * r, r * 4 // 3, true)
        smudge.rect(cx - r, cy - r * 2 // 3, 2 * r, r * 4 // 3, false, true, 2)
    elseif kind == B.SPICE then
        local q = math.max(3, r // 3)
        smudge.circle(cx, cy - q, q, true, true)
        smudge.circle(cx - q - 1, cy + q, q, true, true)
        smudge.circle(cx + q + 1, cy + q, q, true, true)
    elseif kind == B.LEATHER then
        smudge.rounded_rect(cx - r, cy - r * 3 // 4, 2 * r, r * 3 // 2, r // 3, true, true)
        smudge.rounded_rect(cx - r + 4, cy - r * 3 // 4 + 4, 2 * r - 8, r * 3 // 2 - 8, r // 4, false, false)
    else -- camel: two humps on legs
        smudge.circle(cx - r // 3, cy, r // 2, true, true)
        smudge.circle(cx + r // 3, cy, r // 2, true, true)
        smudge.rect(cx - r * 2 // 3, cy, r * 4 // 3, r // 3, true, true)
        smudge.rect(cx - r * 2 // 3, cy, 3, r * 2 // 3, true, true)
        smudge.rect(cx + r * 2 // 3 - 3, cy, 3, r * 2 // 3, true, true)
        smudge.rect(cx + r * 2 // 3, cy - r // 3, 3, r // 3 + 1, true, true)
    end
end

local function card(x, y, cw, ch, kind, selected, focused, count)
    smudge.rounded_rect(x, y, cw, ch, 6, true, false)
    smudge.rounded_rect(x, y, cw, ch, 6, false, selected and 4 or 1)
    if kind and kind > 0 then
        glyph(kind, x + cw // 2, y + ch // 2 - 6, cw // 2)
        local name = B.NAMES[kind]
        if smudge.text_width(name, "ui10", "regular") > cw - 4 then name = name:sub(1, 4) end
        smudge.text(x + cw // 2, y + ch - 22, name, "ui10", "regular", "center", true)
    end
    if count then
        -- How many: a small tag in the top-right corner.
        smudge.rect(x + cw - 20, y + 2, 18, 20, true, false)
        smudge.rect(x + cw - 20, y + 2, 18, 20, false, true, 1)
        smudge.text(x + cw - 11, y + 2, tostring(count), "ui10", "bold", "center", true)
    end
    if selected then smudge.invert_rect(x + 3, y + 3, cw - 6, 14) end
    if focused then smudge.rect(x - 4, y - 4, cw + 8, ch + 8, false, true, 3) end
end

function V.describe(last)
    if not last then return nil end
    local who = last.player == 1 and "You" or "Device"
    if last.kind == "one" then return string.format("%s took a %s", who, B.NAMES[last.good]) end
    if last.kind == "camels" then return string.format("%s took %d camel%s", who, last.count, last.count > 1 and "s" or "") end
    if last.kind == "trade" then return string.format("%s exchanged %d cards", who, last.count) end
    local s = string.format("%s sold %d %s for %d", who, last.count, B.NAMES[last.good], last.value)
    if last.bonus > 0 then s = s .. " + bonus" end
    return s
end

local function statusText()
    local game = S.game
    if game.phase == "over" then return game.winner == 1 and "You win the match!" or "The device wins the match" end
    if game.phase == "scored" then
        local a, b = game.players[1].rupees, game.players[2].rupees
        if game.roundWinner == 0 then return string.format("Round drawn, %d - %d", a, b) end
        return string.format("%s the round, %d - %d", game.roundWinner == 1 and "You win" or "The device wins", a, b)
    end
    if S.thinking then return "The device is trading..." end
    if S.note then return S.note end
    if game.last then return V.describe(game.last) end
    return "Your turn"
end

function V.draw()
    local game, w, touch = S.game, S.w, S.touch
    smudge.header("Bazaar", string.format("Round %d", game.round))
    local L = layout()
    local text = statusText()
    local maxW = (touch and menuButtonX() or w) - 24
    smudge.text(16, L.top + 2, text, smudge.text_width(text, "ui12", "bold") > maxW and "ui10" or "ui12", "bold", "left",
                true)
    if touch then
        smudge.rounded_rect(menuButtonX(), L.top - 2, 80, 32, 6, false, 2)
        smudge.text(menuButtonX() + 40, L.top + 3, "Menu", "ui12", "bold", "center", true)
    end
    local me, dev = game.players[1], game.players[2]
    smudge.text(16, L.top + 32, string.format("Device: %d cards, %d camels   You: %d rupees, %d camels   Seals %d-%d",
                                              B.handSize(dev), dev.camels, me.rupees, me.camels, game.seals[1],
                                              game.seals[2]), "ui10", "regular", "left", true)
    -- Token piles, top first, and what is left of the deck.
    for good = 1, 6 do
        local y = L.piles + (good - 1) * 22
        glyph(good, 26, y + 10, 16)
        smudge.text(44, y, B.NAMES[good], "ui10", "bold", "left", true)
        local pile = game.piles[good]
        smudge.text(150, y, #pile > 0 and table.concat(pile, " ") or "sold out", "ui10", "regular", "left", true)
    end
    smudge.text(w - 16, L.piles, string.format("Deck %d", #game.deck), "ui10", "bold", "right", true)
    smudge.text(w - 16, L.piles + 22, string.format("Bonus 3:%d 4:%d 5:%d", #game.bonus[1], #game.bonus[2],
                                                    #game.bonus[3]), "ui10", "regular", "right", true)

    smudge.text(16, L.market - 26, "Market", "ui10", "bold", "left", true)
    for i = 1, 5 do
        card(marketX(L, i), L.market, L.mcw, L.mch, game.market[i], S.selMarket[i],
             not touch and S.row == 1 and S.col == i)
    end
    smudge.text(16, L.hand - 26, string.format("Your hand (%d/7) and camels", B.handSize(me)), "ui10", "bold", "left",
                true)
    for k = 1, 7 do
        local count = (k == 7) and me.camels or me.hand[k]
        local sel = (k == 7) and S.selCamels or (S.selHand[k] or 0)
        local x = handX(L, k)
        local focused = not touch and S.row == 2 and S.col == k
        if count > 0 then
            card(x, L.hand, L.hcw, L.hch, k, sel > 0, focused, count)
            if sel > 0 then smudge.text(x + L.hcw // 2, L.hand + L.hch + 2, sel .. " picked", "ui10", "bold", "center", true) end
        else
            smudge.rounded_rect(x, L.hand, L.hcw, L.hch, 6, false, 1)
            if focused then smudge.rect(x - 4, L.hand - 4, L.hcw + 8, L.hch + 8, false, true, 3) end
        end
    end
    for k, label in ipairs(BUTTONS) do
        local x, y, bw, bh = buttonRect(L, k)
        local focused = not touch and S.row == 3 and S.col == k
        smudge.rounded_rect(x, y, bw, bh, 8, focused, focused and true or 2)
        smudge.text(x + bw // 2, y + 11, label, "ui12", "bold", "center", not focused)
    end
    if game.phase ~= "play" then smudge.popup(text) end
    if touch then
        smudge.button_hints("", "", "", "")
    elseif game.phase ~= "play" then
        smudge.button_hints("Menu", game.phase == "over" and "Again" or "Next round", "", "")
    else
        smudge.button_hints("Menu", S.row == 3 and "Press" or "Pick", "<", ">")
    end
end

-- Input ------------------------------------------------------------------------

local function say(text)
    S.note = text
    smudge.request_update()
end

-- The Take button: one good, all the camels, or an exchange.
local function take()
    local goods, camelPicked = {}, false
    for i = 1, 5 do
        if S.selMarket[i] then
            if S.game.market[i] == B.CAMEL then camelPicked = true else goods[#goods + 1] = i end
        end
    end
    local giving = S.selCamels
    for good = 1, 6 do giving = giving + (S.selHand[good] or 0) end
    if camelPicked and #goods == 0 and giving == 0 then
        S.act({ kind = "camels" })
    elseif #goods == 1 and giving == 0 and not camelPicked then
        S.act({ kind = "one", slot = goods[1] })
    elseif #goods >= 2 and not camelPicked then
        local give = {}
        for good = 1, 6 do give[good] = S.selHand[good] or 0 end
        S.act({ kind = "trade", slots = goods, give = give, camels = S.selCamels })
    else
        say("Take: one good, the camels, or a trade")
    end
end

local function sell()
    local good, kinds = nil, 0
    for k = 1, 6 do
        if (S.selHand[k] or 0) > 0 then good, kinds = k, kinds + 1 end
    end
    local market = false
    for i = 1, 5 do
        if S.selMarket[i] then market = true end
    end
    if kinds ~= 1 or market or S.selCamels > 0 then
        say("Sell: pick one kind in your hand")
        return
    end
    S.act({ kind = "sell", good = good, count = S.selHand[good] })
end

local function pickMarket(i)
    local market = S.game.market
    local c = market[i]
    if c == 0 then return end
    if c == B.CAMEL then
        -- The camels go together.
        local on = not S.selMarket[i]
        for j = 1, 5 do
            if market[j] == B.CAMEL then S.selMarket[j] = on or nil end
        end
    else
        S.selMarket[i] = (not S.selMarket[i]) or nil
    end
    S.note = nil
end

local function pickHand(k)
    local me = S.game.players[1]
    if k == 7 then
        S.selCamels = (me.camels > 0) and ((S.selCamels + 1) % (me.camels + 1)) or 0
    else
        local n, cur = me.hand[k], S.selHand[k] or 0
        if n == 0 then return end
        -- All of it first, then one fewer each time, then none.
        S.selHand[k] = (cur == 0) and n or (cur - 1)
    end
    S.note = nil
end

local function activate()
    local game = S.game
    if game.phase ~= "play" or game.turn ~= 1 or S.thinking then return end
    if S.row == 1 then
        pickMarket(S.col)
    elseif S.row == 2 then
        pickHand(S.col)
    elseif S.col == 1 then
        take()
        return
    elseif S.col == 2 then
        sell()
        return
    else
        S.clearSelection()
        S.note = nil
    end
    smudge.request_update()
end

local function rowWidth(r) return (r == 1) and 5 or ((r == 2) and 7 or 3) end

function V.button(btn)
    if btn == "back" then
        S.show("menu")
        return
    end
    if S.game.phase ~= "play" then
        if btn == "confirm" then S.continueAfterRound() end
        return
    end
    if btn == "left" then
        S.col = (S.col - 2) % rowWidth(S.row) + 1
    elseif btn == "right" then
        S.col = S.col % rowWidth(S.row) + 1
    elseif btn == "up" or btn == "page_back" or btn == "down" or btn == "page_forward" then
        local step = (btn == "up" or btn == "page_back") and -1 or 1
        S.row = (S.row - 1 + step) % 3 + 1
        S.col = math.min(S.col, rowWidth(S.row))
    elseif btn == "confirm" then
        activate()
        return
    end
    smudge.request_update()
end

function V.tap(x, y)
    local L = layout()
    if smudge.in_rect(x, y, menuButtonX(), L.top - 2, 80, 32) then
        S.show("menu")
        return
    end
    if S.game.phase ~= "play" then
        S.continueAfterRound()
        return
    end
    for i = 1, 5 do
        if smudge.in_rect(x, y, marketX(L, i), L.market, L.mcw, L.mch) then
            S.row, S.col = 1, i
            activate()
            return
        end
    end
    for k = 1, 7 do
        if smudge.in_rect(x, y, handX(L, k), L.hand, L.hcw, L.hch) then
            S.row, S.col = 2, k
            activate()
            return
        end
    end
    for k = 1, 3 do
        if smudge.in_rect(x, y, buttonRect(L, k)) then
            S.row, S.col = 3, k
            activate()
            return
        end
    end
end

return V
end
