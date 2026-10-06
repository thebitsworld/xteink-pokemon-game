-- Yacht dice for Xteink Pokemon (Lua app).
-- Rules live in logic.lua, the computer opponent in ai.lua and the start menu
-- in menu.lua; this file draws the game and handles input. ai.lua and menu.lua
-- are loaded only while needed, to fit the X3's Lua memory.
--
-- Buttons (X3/X4): Left/Right move through the dice, the Roll button and the
-- scorecard; Up/Down jump between them (and step through the scorecard).
-- Confirm holds a die, rolls, or scores the dice in a box. Back opens the
-- menu. Touch (X4 Pro): tap a die, Roll, or a box.

local Y = smudge.dofile("logic.lua")

local MODES = { "Solo", "vs Device", "Two players" }
local DEVICE_STEP_MS = 900

local w, h = 480, 800
local m = {}
local touch = false

local menu = nil       -- the menu module while the menu is on screen
local AI = nil         -- the opponent while it is playing a turn
local mode = 1
local game = nil
local focus = 6        -- 1..5 dice, 6 Roll, 6 + box for a scorecard row
local pendingZero = nil
local note = nil       -- what just happened, shown in the status row
local best = 0
local record = { 0, 0, 0 }
-- Device turn: one step per DEVICE_STEP_MS, each drawn before the next.
local deviceStepDue = nil
local deviceDrawn = false

local function rand(n) return math.random(1, n) end
local function deviceTurn() return mode == 2 and game and not game.over and game.turn == 2 end

local function playerName(p)
    if mode == 1 then return "You" end
    if mode == 2 then return p == 1 and "You" or "Device" end
    return "Player " .. p
end

local function saveGame()
    smudge.save("game", (game and not game.over) and (mode .. "#" .. Y.serialize(game)) or "")
end

local function scheduleDevice()
    deviceStepDue = deviceTurn() and smudge.millis() + DEVICE_STEP_MS or nil
    deviceDrawn = false
end

local function openMenu()
    deviceStepDue = nil
    collectgarbage("collect")
    menu = smudge.dofile("menu.lua")("Yacht", { "Five dice, three rolls, thirteen boxes." })
    smudge.request_update()
end

local function play()
    menu = nil
    collectgarbage("collect")
    scheduleDevice()
    smudge.request_update()
end

local function menuItems()
    local items = {}
    if game and not game.over then items[1] = { label = "Resume", mode = 0 } end
    items[#items + 1] = { label = "Solo", mode = 1,
                          note = best > 0 and ("Best score " .. best) or "Score as much as you can" }
    items[#items + 1] = { label = "vs Device", mode = 2,
                          note = string.format("Won %d  Lost %d  Drawn %d", record[1], record[2], record[3]) }
    items[#items + 1] = { label = "Two players", mode = 3, note = "Take turns on this reader" }
    items[#items + 1] = { label = "Exit", mode = -1 }
    return items
end

local function choose(item)
    if not item then return end
    if item == "exit" or item.mode < 0 then
        smudge.exit()
        return
    end
    if item.mode > 0 then
        mode = item.mode
        game = Y.new(mode == 1 and 1 or 2)
        focus, pendingZero, note = 6, nil, nil
    end
    play()
end

-- Layout ---------------------------------------------------------------------
-- Status row, five dice, the Roll button, column heads, then the scorecard:
-- 6 upper rows, the bonus row, 7 lower rows and the total.

local function contentTop() return m.top_padding + m.header_height + 6 end

local function layout()
    local top = contentTop()
    local s = math.min(56, (w - 80) // 5)
    local diceY = top + 36
    local roll = { x = 24, y = diceY + s + 10, w = w - 48, h = 40 }
    -- 16 rows: the column heads and the 15 of the card.
    local rowH = math.min(36, (h - m.button_hints_height - 6 - roll.y - roll.h - 6) // 16)
    local cardTop = roll.y + roll.h + 6 + rowH
    return { s = s, diceX = (w - 5 * s - 48) // 2, diceY = diceY, roll = roll, cardTop = cardTop, rowH = rowH }
end

-- Top of the scorecard row of `box` (the bonus row sits after Sixes).
local function boxRowY(L, box) return L.cardTop + (box <= 6 and box - 1 or box) * L.rowH end
local function dieX(L, i) return L.diceX + (i - 1) * (L.s + 12) end
local function menuButtonX() return w - 96 end

-- Drawing --------------------------------------------------------------------

-- A die; a held one is inverted (black with white pips), a stale one (the
-- last player's, before this player rolls) greyed.
local function drawDie(x, y, s, v, held, stale)
    local r = math.max(4, s // 8)
    if held then
        smudge.rounded_rect(x, y, s, s, r, true, true)
    else
        if stale then smudge.rect_dither(x + 3, y + 3, s - 6, s - 6, false) end
        smudge.rounded_rect(x, y, s, s, r, false, 2)
    end
    local cx, cy, d = x + s // 2, y + s // 2, s // 4
    local pr = math.max(3, s // 11)
    local function pip(dx, dy)
        if stale and not held then smudge.circle(cx + dx * d, cy + dy * d, pr + 2, true, false) end
        smudge.circle(cx + dx * d, cy + dy * d, pr, true, not held)
    end
    if v % 2 == 1 then pip(0, 0) end
    if v >= 2 then pip(-1, -1) pip(1, 1) end
    if v >= 4 then pip(1, -1) pip(-1, 1) end
    if v == 6 then pip(-1, 0) pip(1, 0) end
end

local function statusText()
    if game.over then
        if mode == 1 then return "Final score " .. Y.total(game.cards[1]) end
        local win = Y.winner(game)
        if win == -1 then return "A tie!" end
        if mode == 2 then return win == 1 and "You win!" or "The device wins" end
        return playerName(win) .. " wins!"
    end
    if note then return note end
    local who = (mode == 1) and "" or (playerName(game.turn) .. ": ")
    if game.rolls == 0 then return who .. "roll the dice" end
    return who .. string.format("roll %d of %d", game.rolls, Y.ROLLS)
end

local function drawCard(L)
    local players = game.players
    local colW = players == 2 and 66 or 90
    local card = game.cards[game.turn]
    local live = game.rolls > 0 and not deviceTurn()
    local function row(y, label, values, focused, bold)
        if focused then smudge.rect(14, y, w - 28, L.rowH, true, true) end
        local ty = y + (L.rowH - 20) // 2
        smudge.text(20, ty, label, "ui10", bold and "bold" or "regular", "left", not focused)
        for p = 1, players do
            if values[p] then
                smudge.text(w - 20 - (players - p + 1) * colW + colW // 2, ty, values[p], "ui10", "bold", "center",
                            not focused)
            end
        end
        smudge.line(14, y + L.rowH - 1, w - 14, y + L.rowH - 1, 1, true)
    end
    local heads = {}
    for p = 1, players do
        heads[p] = mode == 1 and "Score" or (mode == 2 and (p == 1 and "You" or "Dev") or ("P" .. p))
    end
    row(L.cardTop - L.rowH, "", heads, false, false)
    for box = 1, Y.BOXES do
        local values = {}
        for p = 1, players do
            local c = game.cards[p]
            if c[box] ~= Y.UNSCORED then
                values[p] = tostring(c[box])
            elseif live and p == game.turn and Y.canTake(card, game.dice, box) then
                values[p] = "(" .. Y.boxScore(card, game.dice, box) .. ")"
            end
        end
        local label = pendingZero == box and "Score 0 here? Confirm again" or Y.BOX_NAMES[box]
        row(boxRowY(L, box), label, values, not touch and focus == 6 + box, false)
        if box == 6 then
            local bonus = {}
            for p = 1, players do
                local up = Y.upperTotal(game.cards[p])
                bonus[p] = up >= Y.UPPER_BONUS_AT and tostring(Y.UPPER_BONUS) or (up .. "/63")
            end
            row(L.cardTop + 6 * L.rowH, "Upper bonus", bonus, false, true)
        end
    end
    local totals = {}
    for p = 1, players do
        local c = game.cards[p]
        totals[p] = Y.total(c) .. (c.yachtBonus > 0 and "*" or "")
    end
    row(L.cardTop + 14 * L.rowH, "Total", totals, false, true)
end

local function drawPlay()
    smudge.header("Yacht", MODES[mode])
    local L = layout()
    local top = contentTop()
    -- Status, in the smaller font when it would run into the Menu button.
    local text = statusText()
    local maxW = (touch and menuButtonX() or w) - 24
    smudge.text(16, top + 4, text, smudge.text_width(text, "ui12", "bold") > maxW and "ui10" or "ui12", "bold",
                "left", true)
    if touch then
        smudge.rounded_rect(menuButtonX(), top - 2, 80, 34, 6, false, 2)
        smudge.text(menuButtonX() + 40, top + 4, "Menu", "ui12", "bold", "center", true)
    end
    for i = 1, Y.DICE do
        local x = dieX(L, i)
        drawDie(x, L.diceY, L.s, game.dice[i], game.held[i], game.rolls == 0)
        if not touch and focus == i then smudge.rect(x - 4, L.diceY - 4, L.s + 8, L.s + 8, false, true, 3) end
    end
    local b = L.roll
    local focused = not touch and focus == 6
    smudge.rounded_rect(b.x, b.y, b.w, b.h, 8, focused, focused and true or 2)
    local label
    if game.over then
        label = "Game over"
    elseif deviceTurn() then
        label = "Device is playing..."
    elseif not Y.canRoll(game) then
        label = "Pick a box to score"
    else
        label = string.format("Roll  (%d left)", Y.ROLLS - game.rolls)
    end
    smudge.text(b.x + b.w // 2, b.y + 9, label, "ui12", "bold", "center", not focused)
    drawCard(L)
    if game.over then smudge.popup(text) end
    if touch then
        smudge.button_hints("", "", "", "")
    elseif game.over then
        smudge.button_hints("Menu", "Again", "", "")
    else
        smudge.button_hints("Menu", focus <= 5 and "Hold" or (focus == 6 and "Roll" or "Score"), "<", ">")
    end
end

-- Game flow ------------------------------------------------------------------

local function finishIfOver()
    if not game.over then return end
    if mode == 1 then
        if Y.total(game.cards[1]) > best then
            best = Y.total(game.cards[1])
            smudge.save("best", tostring(best))
        end
    elseif mode == 2 then
        local win = Y.winner(game)
        local i = win == 1 and 1 or (win == 2 and 2 or 3)
        record[i] = record[i] + 1
        smudge.save("rec", string.format("%d,%d,%d", record[1], record[2], record[3]))
    end
end

local function scored(who, points, box)
    note = (mode == 1) and string.format("%d in %s", points, Y.BOX_NAMES[box])
        or string.format("%s scored %d in %s", playerName(who), points, Y.BOX_NAMES[box])
    finishIfOver()
end

local function activate()
    if deviceTurn() or game.over then return end
    if focus <= 5 then
        Y.toggleHold(game, focus)
    elseif focus == 6 then
        if not Y.roll(game, rand) then return end
        note, pendingZero = nil, nil
        if not Y.canRoll(game) then
            -- No rolls left: move to the first box that can be taken.
            for box = Y.BOXES, 1, -1 do
                if Y.canTake(game.cards[game.turn], game.dice, box) then focus = 6 + box end
            end
        end
    else
        local box = focus - 6
        local card = game.cards[game.turn]
        if game.rolls == 0 or not Y.canTake(card, game.dice, box) then return end
        if Y.boxScore(card, game.dice, box) == 0 and pendingZero ~= box then
            pendingZero = box -- a zero needs a second press
        else
            local who = game.turn
            scored(who, Y.take(game, box), box)
            pendingZero, focus = nil, 6
            scheduleDevice()
        end
    end
    smudge.request_update()
end

-- One step of the device's turn: roll, then keep and re-roll, then score.
-- The opponent is loaded for the turn and dropped once it has scored.
local function deviceStep()
    local card = game.cards[2]
    if game.rolls == 0 then
        Y.roll(game, rand)
        note = "Device rolls"
    else
        if not AI then
            collectgarbage("collect")
            AI = smudge.dofile("ai.lua")(Y)
        end
        local held = Y.canRoll(game) and AI.chooseHold(card, game.dice, Y.ROLLS - game.rolls)
        local keepAll = held and held[1] and held[2] and held[3] and held[4] and held[5]
        if held and not keepAll then
            game.held = held
            Y.roll(game, rand)
            note = "Device rolls again"
        else
            local box = AI.chooseBox(card, game.dice)
            scored(2, Y.take(game, box), box)
            AI = nil
            collectgarbage("collect")
        end
    end
    scheduleDevice()
    smudge.request_update()
end

-- Left/Right: every stop in order. Up/Down: dice row and Roll as one stop
-- each, single steps through the scorecard.
local function moveFocus(step, jump)
    pendingZero = nil
    local last = 6 + Y.BOXES
    if jump and focus <= 5 then
        focus = step > 0 and 6 or last
    elseif jump and focus == 6 then
        focus = step > 0 and 7 or 1
    elseif jump and focus == 7 and step < 0 then
        focus = 6
    elseif jump and focus == last and step > 0 then
        focus = 1
    else
        focus = (focus - 1 + step) % last + 1
    end
    smudge.request_update()
end

-- Callbacks ------------------------------------------------------------------

function on_init()
    w, h = smudge.get_bounds()
    m = smudge.get_metrics()
    touch = smudge.has_touch()
    math.randomseed(smudge.millis() + smudge.time())
    best = tonumber(smudge.load("best", "0")) or 0
    local a, b, c = (smudge.load("rec", "0,0,0")):match("(%d+),(%d+),(%d+)")
    record = { tonumber(a) or 0, tonumber(b) or 0, tonumber(c) or 0 }
    local savedMode, data = smudge.load("game", ""):match("^(%d+)#(.+)$")
    local g = data and Y.deserialize(data)
    if g and not g.over then mode, game = tonumber(savedMode), g end
    openMenu()
end

function on_draw()
    smudge.clear()
    if menu then menu.draw(menuItems()) else drawPlay() end
    if deviceStepDue then deviceDrawn = true end
end

function on_update()
    if not menu and deviceStepDue and deviceDrawn and smudge.millis() >= deviceStepDue then
        deviceStepDue = nil
        deviceStep()
    end
end

function on_exit()
    saveGame()
end

function on_button(btn, pressed)
    if not pressed then return end
    if menu then
        choose(menu.button(btn, menuItems()))
    elseif btn == "back" then
        openMenu()
    elseif game.over then
        if btn == "confirm" then choose({ mode = mode }) end
    elseif btn == "left" or btn == "right" then
        moveFocus(btn == "left" and -1 or 1, false)
    elseif btn == "up" or btn == "page_back" or btn == "down" or btn == "page_forward" then
        moveFocus((btn == "up" or btn == "page_back") and -1 or 1, true)
    elseif btn == "confirm" then
        activate()
    end
end

function on_tap(x, y)
    if menu then
        choose(menu.tap(x, y, menuItems()))
        return
    end
    local L = layout()
    if smudge.in_rect(x, y, menuButtonX(), contentTop() - 2, 80, 34) then
        openMenu()
    elseif game.over then
        choose({ mode = mode })
    elseif smudge.in_rect(x, y, L.roll.x, L.roll.y, L.roll.w, L.roll.h) then
        focus = 6
        activate()
    elseif y >= L.diceY and y < L.diceY + L.s then
        for i = 1, Y.DICE do
            local dx = dieX(L, i)
            if x >= dx and x < dx + L.s then
                focus = i
                activate()
            end
        end
    else
        for box = 1, Y.BOXES do
            local by = boxRowY(L, box)
            if y >= by and y < by + L.rowH then
                if focus ~= 6 + box then pendingZero = nil end
                focus = 6 + box
                activate()
            end
        end
    end
end
