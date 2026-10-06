-- Hearts for Xteink Pokemon (Lua app).
-- Rules live in logic.lua, the computer players in ai.lua and saving in
-- save.lua (loaded only to save or restore); this file draws the table and
-- handles input. The start menu is the shared one from the
-- other games, kept in this file: loading it from a file mid-game would need
-- memory Hearts does not have to spare on the X3.
--
-- Buttons (X3/X4): Left/Right move along your hand (and to the Pass button),
-- Up/Down between its two rows, Confirm picks a card to pass or plays it.
-- Back opens the menu. Touch (X4 Pro): tap a card, tap Pass.

local H = smudge.dofile("logic.lua")

-- Load order matters on the X3: a saved game is restored first, with
-- save.lua dropped again, and only then are the computer players loaded.
local restored, restoredLevel = nil, nil
do
    local savedLevel, data = smudge.load("game", ""):match("^(%d)#(.+)$")
    if data then
        restored = smudge.dofile("save.lua")(H).deserialize(data)
        restoredLevel = tonumber(savedLevel)
        collectgarbage("collect")
    end
end
local AI = smudge.dofile("ai.lua")(H)

local LEVELS = { "Easy", "Normal" }
local NAMES = { "You", "West", "North", "East" }
local PASS_NAMES = { [1] = "left", [3] = "right", [2] = "across" }
local AI_STEP_MS, TRICK_PAUSE_MS = 700, 1400
local SPRITES = { [0] = "sprites/clubs", "sprites/diamonds", "sprites/spades", "sprites/hearts" }

local w, h = 480, 800
local m = {}
local touch = false

local menu = nil
local level = 2
local game = nil
local cursor = 1
local chosen = {}        -- cards picked to pass
local note = nil         -- status: what just happened, or why a card was refused
local showTrick = nil    -- a finished trick kept on the table for a moment
local stepDue, drawn = nil, false
local stats = {}         -- per level: { won, played }

local function rand(n) return math.random(1, n) end

local function schedule(ms)
    stepDue, drawn = smudge.millis() + ms, false
end

-- Starts the computer players' moves if it is their turn.
local function wake()
    if game and not game.over and game.phase == "play" and game.turn ~= 1 then
        schedule(showTrick and TRICK_PAUSE_MS or AI_STEP_MS)
    end
end

local function newGame(lv)
    level = lv
    game = H.new(rand)
    cursor, chosen, note, showTrick = 1, {}, nil, nil
end

local function menuItems()
    local items = {}
    if game and not game.over then items[1] = { label = "Resume", level = 0 } end
    for i, name in ipairs(LEVELS) do
        local s = stats[i]
        items[#items + 1] = { label = "New game: " .. name .. " players", level = i,
                              note = string.format("Won %d of %d", s[1], s[2]) }
    end
    items[#items + 1] = { label = "Exit", level = -1 }
    return items
end

-- Start menu (the same as the other games' menu.lua) ---------------------------
local function makeMenu(title, lines)
    local M = { index = 1 }
    local w, h = smudge.get_bounds()
    local m = smudge.get_metrics()
    local touch = smudge.has_touch()
    local top = m.top_padding + m.header_height + 8
    local first = top + 16 + #lines * 20

    local function rect(i) return 24, first + (i - 1) * 74, w - 48, 64 end

    function M.draw(items)
        smudge.header(title, "")
        for i, line in ipairs(lines) do
            smudge.centered_text(top + (i - 1) * 20, line, "ui10", "regular", true)
        end
        if M.index > #items then M.index = #items end
        for i, item in ipairs(items) do
            local x, y, bw, bh = rect(i)
            local selected = i == M.index and not touch
            smudge.rounded_rect(x, y, bw, bh, 8, selected, selected and true or 2)
            local ty = item.note and y + 8 or y + (bh - smudge.line_height("ui12")) // 2
            smudge.text(x + bw // 2, ty, item.label, "ui12", "bold", "center", not selected)
            if item.note then smudge.text(x + bw // 2, y + 36, item.note, "ui10", "regular", "center", not selected) end
        end
        if touch then smudge.button_hints("", "", "", "") else smudge.button_hints("Exit", "Select", "Up", "Down") end
    end

    function M.button(btn, items)
        if btn == "confirm" then return items[M.index] end
        if btn == "back" then return "exit" end
        if btn == "up" or btn == "left" or btn == "page_back" then
            M.index = (M.index - 2) % #items + 1
        elseif btn == "down" or btn == "right" or btn == "page_forward" then
            M.index = M.index % #items + 1
        end
        smudge.request_update()
        return nil
    end

    function M.tap(x, y, items)
        for i, item in ipairs(items) do
            local rx, ry, rw, rh = rect(i)
            if smudge.in_rect(x, y, rx, ry, rw, rh) then return item end
        end
        return nil
    end

    return M
end

local function openMenu()
    stepDue = nil
    collectgarbage("collect")
    menu = makeMenu("Hearts", { "Avoid hearts and the queen of spades.",
                                                 "Lowest score when someone reaches 100 wins." })
    smudge.request_update()
end

local function choose(item)
    if not item then return end
    if item == "exit" or item.level < 0 then
        smudge.exit()
        return
    end
    if item.level > 0 then newGame(item.level) end
    menu = nil
    collectgarbage("collect")
    wake()
    smudge.request_update()
end

-- Layout ---------------------------------------------------------------------

local function contentTop() return m.top_padding + m.header_height + 6 end
local function menuButtonX() return w - 96 end

local function layout()
    local cw = math.min(64, (w - 16 - 6 * 6) // 7)
    local ch = cw * 7 // 5
    local handTop = h - m.button_hints_height - 8 - 2 * ch - 12
    local top = contentTop() + 34
    return { cw = cw, ch = ch, handTop = handTop, cx = w // 2, cy = top + (handTop - top) // 2 }
end

local function handPos(L, k)
    local row, col = (k - 1) // 7, (k - 1) % 7
    local count = math.min(7, #game.hands[1] - row * 7)
    local x0 = (w - count * L.cw - (count - 1) * 6) // 2
    return x0 + col * (L.cw + 6), L.handTop + row * (L.ch + 12)
end

-- Where each seat's card lies on the table.
local function tablePos(L, seat)
    if seat == 1 then return L.cx - L.cw // 2, L.cy + 8 end
    if seat == 3 then return L.cx - L.cw // 2, L.cy - L.ch - 8 end
    if seat == 2 then return L.cx - L.cw * 3 // 2 - 56, L.cy - L.ch // 2 end
    return L.cx + L.cw // 2 + 56, L.cy - L.ch // 2
end

local function passButton(L) return { x = L.cx - 100, y = L.cy - 28, w = 200, h = 56 } end

-- Drawing --------------------------------------------------------------------

local function drawCard(L, x, y, c, dim)
    smudge.rounded_rect(x, y, L.cw, L.ch, 5, true, false)
    smudge.rounded_rect(x, y, L.cw, L.ch, 5, false, 1)
    local suit = H.suit(c)
    local red = suit == H.HEARTS or suit == H.DIAMONDS
    local text = H.rankName(c)
    smudge.text(x + 4, y + 2, text, "ui12", "bold", "left", true)
    -- The suit beside the rank and large in the middle; red suits greyed.
    smudge.draw_sprite(x + 6 + smudge.text_width(text, "ui12", "bold"), y + 5, 16, 16, SPRITES[suit] .. "16.raw", false,
                       red)
    smudge.draw_sprite(x + (L.cw - 32) // 2, y + L.ch - 40, 32, 32, SPRITES[suit] .. ".raw", false, red)
    -- A card the rules do not allow now is greyed.
    if dim then smudge.rect_dither(x + 2, y + 22, L.cw - 4, L.ch - 24, false) end
end

local function seatLabel(s)
    local text = string.format("%s %d", NAMES[s], game.scores[s])
    if game.taken[s] > 0 then text = text .. string.format(" (+%d)", game.taken[s]) end
    return text
end

local function statusText()
    if game.over then
        local lead = H.leaders(game)
        if #lead == 1 then return lead[1] == 1 and "You win the game!" or (NAMES[lead[1]] .. " wins the game") end
        return "A shared win"
    end
    if game.phase == "scored" then
        return game.moon and (NAMES[game.moon] .. " shot the moon!") or "Hand over"
    end
    if note then return note end
    if game.phase == "pass" then
        return string.format("Pick 3 cards to pass %s (%d chosen)", PASS_NAMES[H.passOffset(game.hand)], #chosen)
    end
    if game.turn == 1 then
        if game.tricks == 0 and #game.trick == 0 then return "Lead the two of clubs" end
        return "Your turn"
    end
    return NAMES[game.turn] .. " is playing..."
end

local function isChosen(c)
    for _, x in ipairs(chosen) do
        if x == c then return true end
    end
    return false
end

local function drawPlay()
    smudge.header("Hearts", string.format("Hand %d", game.hand))
    local top = contentTop()
    local L = layout()
    local text = statusText()
    local maxW = (touch and menuButtonX() or w) - 24
    smudge.text(16, top + 4, text, smudge.text_width(text, "ui12", "bold") > maxW and "ui10" or "ui12", "bold", "left",
                true)
    if touch then
        smudge.rounded_rect(menuButtonX(), top - 2, 80, 34, 6, false, 2)
        smudge.text(menuButtonX() + 40, top + 4, "Menu", "ui12", "bold", "center", true)
    end

    -- The table: seat names with scores (points taken this hand in brackets),
    -- and the trick in front of each player.
    for s = 1, 4 do
        local x, y = tablePos(L, s)
        local label = seatLabel(s)
        local ly = (s == 3) and (y - 24) or (y + L.ch + 4)
        local bold = game.phase == "play" and game.turn == s and not showTrick
        smudge.text(x + L.cw // 2, ly, label, "ui10", bold and "bold" or "regular", "center", true)
    end
    local cards = showTrick and showTrick.cards or game.trick
    for _, t in ipairs(cards) do
        local x, y = tablePos(L, t.seat)
        drawCard(L, x, y, t.card, false)
        if showTrick and t.seat == showTrick.winner then
            smudge.rounded_rect(x - 3, y - 3, L.cw + 6, L.ch + 6, 7, false, 3)
        end
    end
    if game.phase == "pass" then
        local b = passButton(L)
        local ready = #chosen == 3
        local focused = not touch and cursor > #game.hands[1]
        smudge.rounded_rect(b.x, b.y, b.w, b.h, 8, ready, ready and true or 2)
        smudge.text(b.x + b.w // 2, b.y + 16, "Pass " .. PASS_NAMES[H.passOffset(game.hand)], "ui12", "bold", "center",
                    not ready)
        if focused then smudge.rect(b.x - 5, b.y - 5, b.w + 10, b.h + 10, false, true, 3) end
    end

    -- Your hand, in two rows; cards picked to pass sit raised.
    local myTurn = game.phase == "play" and game.turn == 1 and not showTrick
    for k, c in ipairs(game.hands[1]) do
        local x, y = handPos(L, k)
        if isChosen(c) then y = y - 10 end
        local dim = game.phase == "play" and H.refusal(game, 1, c) ~= nil and (myTurn or #game.trick > 0)
        drawCard(L, x, y, c, dim)
        if isChosen(c) then smudge.rounded_rect(x - 2, y - 2, L.cw + 4, L.ch + 4, 6, false, 3) end
        if not touch and k == cursor then smudge.rect(x - 4, y - 4, L.cw + 8, L.ch + 8, false, true, 3) end
    end

    if game.phase == "scored" then
        local parts = {}
        for s = 1, 4 do parts[s] = string.format("%s +%d", NAMES[s], game.handScores[s]) end
        smudge.popup(game.over and text or table.concat(parts, "  "))
    end
    if touch then
        smudge.button_hints("", "", "", "")
    elseif game.phase == "scored" then
        smudge.button_hints("Menu", game.over and "New" or "Next", "", "")
    else
        smudge.button_hints("Menu", game.phase == "pass" and (cursor > #game.hands[1] and "Pass" or "Pick") or "Play",
                            "<", ">")
    end
end

-- Game flow ------------------------------------------------------------------

local function finishGame()
    local s = stats[level]
    s[2] = s[2] + 1
    local lead = H.leaders(game)
    if #lead == 1 and lead[1] == 1 then s[1] = s[1] + 1 end
    smudge.save("st" .. level, s[1] .. "," .. s[2])
end

local function afterPlay(seat, c)
    if game.tricks > 0 and #game.trick == 0 then
        showTrick = game.lastTrick
        local pts = 0
        for _, t in ipairs(showTrick.cards) do pts = pts + H.points(t.card) end
        note = string.format("%s %s the trick%s", NAMES[showTrick.winner], showTrick.winner == 1 and "take" or "takes",
                             pts > 0 and string.format(" (+%d)", pts) or "")
        if game.phase == "scored" and game.over then finishGame() end
    else
        note = nil
    end
    if cursor > #game.hands[1] then cursor = math.max(1, #game.hands[1]) end
    wake()
    smudge.request_update()
end

local function activate()
    if game.phase == "pass" then
        local hand = game.hands[1]
        if cursor > #hand then
            if #chosen ~= 3 then return end
            local picks = { chosen }
            for s = 2, 4 do picks[s] = AI.choosePass(game, s, level, rand) end
            H.pass(game, picks)
            chosen, note, cursor = {}, nil, 1
            wake()
        else
            local c = hand[cursor]
            if isChosen(c) then
                for k, x in ipairs(chosen) do
                    if x == c then table.remove(chosen, k) break end
                end
            elseif #chosen < 3 then
                chosen[#chosen + 1] = c
            end
        end
        smudge.request_update()
    elseif game.phase == "play" then
        if game.turn ~= 1 then return end
        showTrick = nil
        local c = game.hands[1][cursor]
        if not c then return end
        local why = H.refusal(game, 1, c)
        if why then
            note = why
            smudge.request_update()
            return
        end
        H.play(game, c)
        afterPlay(1, c)
    elseif game.phase == "scored" then
        if game.over then
            choose({ level = level })
        else
            H.nextHand(game, rand)
            showTrick, note, cursor = nil, nil, 1
            wake()
            smudge.request_update()
        end
    end
end

-- One computer move (or the end of a trick's pause).
local function step()
    if game.phase ~= "play" or game.turn == 1 then
        showTrick = (game.phase == "play") and showTrick or nil
        smudge.request_update()
        return
    end
    showTrick = nil
    local seat = game.turn
    local c = AI.choosePlay(game, level, rand)
    H.play(game, c)
    afterPlay(seat, c)
end

-- Callbacks ------------------------------------------------------------------

function on_init()
    w, h = smudge.get_bounds()
    m = smudge.get_metrics()
    touch = smudge.has_touch()
    math.randomseed(smudge.millis() + smudge.time())
    for i = 1, #LEVELS do
        local a, b = (smudge.load("st" .. i, "0,0")):match("(%d+),(%d+)")
        stats[i] = { tonumber(a) or 0, tonumber(b) or 0 }
    end
    if restored then level, game = restoredLevel, restored end
    restored = nil
    openMenu()
end

function on_draw()
    smudge.clear()
    if menu then menu.draw(menuItems()) else drawPlay() end
    if stepDue then drawn = true end
end

function on_update()
    if not menu and stepDue and drawn and smudge.millis() >= stepDue then
        stepDue = nil
        step()
    end
end

function on_exit()
    -- The app is closing: make room for save.lua by dropping the players.
    AI, menu = nil, nil
    collectgarbage("collect")
    local keep = game and not game.over and (game.phase == "play" or game.phase == "pass")
    smudge.save("game", keep and (level .. "#" .. smudge.dofile("save.lua")(H).serialize(game)) or "")
end

function on_button(btn, pressed)
    if not pressed then return end
    if menu then
        choose(menu.button(btn, menuItems()))
        return
    end
    if btn == "back" then
        openMenu()
        return
    end
    local n = #game.hands[1] + (game.phase == "pass" and 1 or 0)
    if btn == "confirm" then
        activate()
        return
    elseif btn == "left" then
        cursor = (cursor - 2) % math.max(1, n) + 1
    elseif btn == "right" then
        cursor = cursor % math.max(1, n) + 1
    elseif btn == "up" or btn == "page_back" then
        cursor = cursor > 7 and cursor - 7 or cursor
    elseif btn == "down" or btn == "page_forward" then
        cursor = math.min(n, cursor + 7)
    end
    smudge.request_update()
end

function on_tap(x, y)
    if menu then
        choose(menu.tap(x, y, menuItems()))
        return
    end
    if smudge.in_rect(x, y, menuButtonX(), contentTop() - 2, 80, 34) then
        openMenu()
        return
    end
    local L = layout()
    if game.phase == "scored" then
        activate()
        return
    end
    if game.phase == "pass" then
        local b = passButton(L)
        if smudge.in_rect(x, y, b.x, b.y, b.w, b.h) then
            cursor = #game.hands[1] + 1
            activate()
            return
        end
    end
    -- The last card drawn is on top, so test from the end.
    for k = #game.hands[1], 1, -1 do
        local cx, cy = handPos(L, k)
        if isChosen(game.hands[1][k]) then cy = cy - 10 end
        if smudge.in_rect(x, y, cx, cy, L.cw, L.ch) then
            cursor = k
            activate()
            return
        end
    end
end
