-- Klondike solitaire for Xteink Pokemon (Lua app).
-- Rules live in logic.lua; this file draws and handles input.
--
-- Buttons (X3/X4): Left/Right pick a column, Up/Down move between the top row
-- (stock, waste, foundations) and the tableau, and within a column choose
-- which face-up card to take. Confirm picks up, then places; Confirm on the
-- picked card again sends it wherever it fits. Back cancels, or opens the menu.
-- Touch (X4 Pro): tap a card to pick it up, tap where it should go; tap it
-- again to send it wherever it fits.
--
-- Red suits are drawn hollow (small symbols) and grey (centre picture), black
-- suits solid, so the two colours stay apart on a black-and-white screen.

local K = smudge.dofile("logic.lua")

local SUIT_SPRITES = { [0] = "sprites/clubs.raw", "sprites/diamonds.raw", "sprites/hearts.raw", "sprites/spades.raw" }

local w, h = 480, 800
local m = {}
local touch = false

local screen = "menu" -- "menu" | "play"
local menuIndex = 1
local game = nil
local picked = nil    -- source table for K.sourceCards, or nil
local cursor = { row = "tab", col = 1, depth = 1 }
local message = nil
local stats = { won = 0, played = 0 }
local wonCounted = false

local function rand(n) return math.random(1, n) end

-- Layout ---------------------------------------------------------------------

local L = {}

local function computeLayout()
    local gap = 4
    L.cw = (w - 16 - 6 * gap) // 7
    L.ch = L.cw * 7 // 5
    L.gap = gap
    L.left = (w - (7 * L.cw + 6 * gap)) // 2
    L.status = m.top_padding + m.header_height + 6
    L.topY = L.status + 46
    L.tabY = L.topY + L.ch + 14
    L.bottom = h - m.button_hints_height - 6
end

local function colX(col) return L.left + (col - 1) * (L.cw + L.gap) end

-- Vertical step between the cards of a column, shrunk to fit the screen.
local function columnSteps(pile)
    local downStep = 9
    local upStep = L.ch * 2 // 5
    local up = #pile.cards - pile.down
    if up > 1 then
        local avail = L.bottom - L.tabY - L.ch - pile.down * downStep
        upStep = math.max(16, math.min(upStep, avail // (up - 1)))
    end
    return downStep, upStep
end

local function cardY(pile, i)
    local downStep, upStep = columnSteps(pile)
    if i <= pile.down then return L.tabY + (i - 1) * downStep end
    return L.tabY + pile.down * downStep + (i - pile.down - 1) * upStep
end

-- Top row: column 1 stock, 2 waste, 4-7 foundations 1-4.
local function topSlot(col)
    if col == 1 then return "stock" end
    if col == 2 then return "waste" end
    if col >= 4 then return "foundation" end
    return nil
end

local function touchButtons()
    return { undo = { x = w - 16 - 196, y = L.status, w = 90, h = 40 },
             menu = { x = w - 16 - 96, y = L.status, w = 96, h = 40 } }
end

-- Drawing --------------------------------------------------------------------

local function filledSuit(x, y, s, suit, color)
    local r = s // 4
    if suit == 1 then -- diamond
        smudge.triangle(x + s // 2, y, x, y + s // 2, x + s, y + s // 2, true, color)
        smudge.triangle(x + s // 2, y + s, x, y + s // 2, x + s, y + s // 2, true, color)
    elseif suit == 2 then -- heart
        smudge.circle(x + r, y + r + 1, r + 1, true, color)
        smudge.circle(x + s - r, y + r + 1, r + 1, true, color)
        smudge.triangle(x - 1, y + r + 2, x + s + 1, y + r + 2, x + s // 2, y + s, true, color)
    elseif suit == 3 then -- spade
        smudge.triangle(x + s // 2, y, x - 1, y + s * 3 // 5, x + s + 1, y + s * 3 // 5, true, color)
        smudge.circle(x + r, y + s * 3 // 5, r + 1, true, color)
        smudge.circle(x + s - r, y + s * 3 // 5, r + 1, true, color)
        smudge.triangle(x + s // 2, y + s // 2, x + s // 4, y + s, x + s * 3 // 4, y + s, true, color)
    else -- club: three separate lobes and a narrow stem, so it never reads as a spade
        local lobe = math.max(2, s // 5)
        smudge.circle(x + s // 2, y + lobe, lobe, true, color)
        smudge.circle(x + lobe, y + s // 2 + 1, lobe, true, color)
        smudge.circle(x + s - lobe, y + s // 2 + 1, lobe, true, color)
        smudge.triangle(x + s // 2, y + s // 2, x + s // 2 - 2, y + s, x + s // 2 + 2, y + s, true, color)
    end
end

local function drawSuit(x, y, s, suit)
    filledSuit(x, y, s, suit, true)
    if suit == 1 or suit == 2 then filledSuit(x + 2, y + 2, s - 4, suit, false) end -- hollow red
end

local function drawCardFace(x, y, card)
    smudge.rounded_rect(x, y, L.cw, L.ch, 5, true, false)
    smudge.rounded_rect(x, y, L.cw, L.ch, 5, false, 1)
    local rankText = K.RANK_NAMES[K.rank(card)]
    smudge.text(x + 4, y + 2, rankText, "ui12", "bold", "left", true)
    local tw = smudge.text_width(rankText, "ui12", "bold")
    drawSuit(x + 6 + tw, y + 6, 14, K.suit(card))
    if L.ch >= 70 then
        local red = K.isRed(card)
        smudge.draw_sprite(x + (L.cw - 32) // 2, y + L.ch - 32 - 10, 32, 32, SUIT_SPRITES[K.suit(card)], false, red)
    end
end

local function drawCardBack(x, y)
    smudge.rounded_rect(x, y, L.cw, L.ch, 5, true, false)
    smudge.rounded_rect(x, y, L.cw, L.ch, 5, false, 1)
    smudge.rect_dither(x + 4, y + 4, L.cw - 8, L.ch - 8, true)
end

local function drawSlot(x, y, label)
    smudge.rounded_rect(x, y, L.cw, L.ch, 5, false, 1)
    if label then
        smudge.text(x + L.cw // 2, y + L.ch // 2 - 10, label, "ui10", "regular", "center", true)
    end
end

-- The rectangle covered by a source: one card, or a column run down to the bottom.
local function sourceRect(src)
    if src.kind == "waste" then return colX(2), L.topY, L.cw, L.ch end
    if src.kind == "foundation" then return colX(src.index + 3), L.topY, L.cw, L.ch end
    local pile = game.tableau[src.col]
    local y = cardY(pile, src.index)
    return colX(src.col), y, L.cw, cardY(pile, #pile.cards) + L.ch - y
end

local function cursorRect()
    if cursor.row == "top" then return colX(cursor.col), L.topY, L.cw, L.ch end
    local pile = game.tableau[cursor.col]
    if #pile.cards == 0 then return colX(cursor.col), L.tabY, L.cw, L.ch end
    local y = cardY(pile, cursor.depth)
    return colX(cursor.col), y, L.cw, cardY(pile, #pile.cards) + L.ch - y
end

local function drawPlay()
    smudge.header("Solitaire", game.drawCount == 3 and "Draw 3" or "Draw 1")
    smudge.text(16, L.status + 8, message or ("Moves: " .. game.moves), "ui12", "bold", "left", true)
    if touch then
        local tb = touchButtons()
        for key, label in pairs({ undo = "Undo", menu = "Menu" }) do
            local b = tb[key]
            smudge.rounded_rect(b.x, b.y, b.w, b.h, 6, false, 2)
            smudge.text(b.x + b.w // 2, b.y + 9, label, "ui12", "bold", "center", true)
        end
    end

    -- Top row.
    if #game.stock > 0 then drawCardBack(colX(1), L.topY) else drawSlot(colX(1), L.topY, "Turn") end
    if #game.waste > 0 then drawCardFace(colX(2), L.topY, game.waste[#game.waste]) else drawSlot(colX(2), L.topY) end
    for i = 1, 4 do
        local f = game.foundations[i]
        if #f > 0 then drawCardFace(colX(i + 3), L.topY, f[#f]) else drawSlot(colX(i + 3), L.topY, "A") end
    end

    -- Tableau.
    for col = 1, 7 do
        local pile = game.tableau[col]
        if #pile.cards == 0 then
            drawSlot(colX(col), L.tabY, "K")
        else
            for i = 1, #pile.cards do
                local y = cardY(pile, i)
                if i <= pile.down then drawCardBack(colX(col), y) else drawCardFace(colX(col), y, pile.cards[i]) end
            end
        end
    end

    if picked then
        local x, y, rw, rh = sourceRect(picked)
        smudge.invert_rect(x, y, rw, rh)
    end
    if not touch then
        local x, y, rw, rh = cursorRect()
        smudge.rounded_rect(x - 3, y - 3, rw + 6, rh + 6, 7, false, 3)
    end
    if K.isWon(game) then smudge.popup("You won in " .. game.moves .. " moves!") end

    if touch then
        smudge.button_hints("", "", "", "")
    elseif picked then
        smudge.button_hints("Cancel", "Place", "<", ">")
    else
        local onStock = cursor.row == "top" and cursor.col == 1
        smudge.button_hints("Menu", onStock and "Draw" or "Pick", "<", ">")
    end
end

local function menuItems()
    local items = {}
    if game and not K.isWon(game) then items[#items + 1] = { label = "Resume", action = "resume" } end
    items[#items + 1] = { label = "New game: draw 1", action = "new", draw = 1 }
    items[#items + 1] = { label = "New game: draw 3", action = "new", draw = 3 }
    if game and K.canUndo(game) and not touch then items[#items + 1] = { label = "Undo last move", action = "undo" } end
    items[#items + 1] = { label = "Exit", action = "exit" }
    return items
end

local function menuButton(i)
    local bh, gap = 60, 10
    return { x = 24, y = L.status + 60 + (i - 1) * (bh + gap), w = w - 48, h = bh }
end

local function drawMenu()
    smudge.header("Solitaire", "")
    smudge.centered_text(L.status + 6, "Build each suit from ace to king.", "ui10", "regular", true)
    smudge.centered_text(L.status + 28, string.format("Won %d of %d games", stats.won, stats.played), "ui10",
                         "regular", true)
    local items = menuItems()
    if menuIndex > #items then menuIndex = #items end
    for i, item in ipairs(items) do
        local b = menuButton(i)
        local selected = (i == menuIndex) and not touch
        smudge.rounded_rect(b.x, b.y, b.w, b.h, 8, selected, selected and true or 2)
        smudge.text(b.x + b.w // 2, b.y + (b.h - smudge.line_height("ui12")) // 2, item.label, "ui12", "bold",
                    "center", not selected)
    end
    if touch then smudge.button_hints("", "", "", "") else smudge.button_hints("Exit", "Select", "Up", "Down") end
end

-- Game flow ------------------------------------------------------------------

local function saveGame()
    if game and not K.isWon(game) then smudge.save("game", K.serialize(game)) else smudge.save("game", "") end
    smudge.save("stats", stats.won .. "," .. stats.played)
end

local function newGame(drawCount)
    game = K.new(rand, drawCount)
    picked, message, wonCounted = nil, nil, false
    cursor = { row = "tab", col = 1, depth = 1 }
    stats.played = stats.played + 1
    screen = "play"
    saveGame()
end

local function checkWin()
    -- Once every card is face up the rest is mechanical: finish it.
    while K.canAutoFinish(game) do
        if not K.autoFinishStep(game) then break end
    end
    if K.isWon(game) and not wonCounted then
        wonCounted = true
        stats.won = stats.won + 1
        saveGame()
    end
end

local function clampCursor()
    if cursor.row == "tab" then
        local pile = game.tableau[cursor.col]
        local first = math.max(1, pile.down + 1)
        cursor.depth = math.max(first, math.min(#pile.cards, cursor.depth))
        if #pile.cards == 0 then cursor.depth = 1 end
    elseif cursor.col == 3 then
        cursor.col = 2
    end
end

-- The source/destination under a position (row, col, depth).
local function sourceAt(pos)
    if pos.row == "top" then
        local slot = topSlot(pos.col)
        if slot == "waste" then return { kind = "waste" } end
        if slot == "foundation" then return { kind = "foundation", index = pos.col - 3 } end
        return nil
    end
    local pile = game.tableau[pos.col]
    if #pile.cards == 0 or pos.depth <= pile.down then return nil end
    return { kind = "tableau", col = pos.col, index = pos.depth }
end

local function destinationAt(pos)
    if pos.row == "top" then
        if topSlot(pos.col) == "foundation" then return { kind = "foundation", index = pos.col - 3 } end
        return nil
    end
    return { kind = "tableau", col = pos.col }
end

local function sameSource(a, b)
    return a and b and a.kind == b.kind and a.col == b.col and a.index == b.index
end

-- Confirm / tap at `pos`.
local function activate(pos)
    message = nil
    if K.isWon(game) then return end
    if not picked then
        if pos.row == "top" and pos.col == 1 then
            K.draw(game)
        else
            local src = sourceAt(pos)
            if src and K.sourceCards(game, src) then picked = src end
        end
    else
        local here = sourceAt(pos)
        if sameSource(here, picked) then
            local dst = K.findDestination(game, picked)
            if not (dst and K.move(game, picked, dst)) then message = "No place for that card" end
        else
            local dst = destinationAt(pos)
            if not (dst and K.move(game, picked, dst)) then message = "Can't go there" end
        end
        picked = nil
    end
    checkWin()
    clampCursor()
    smudge.request_update()
end

local function runMenuItem(item)
    if item.action == "resume" then
        screen = "play"
    elseif item.action == "new" then
        newGame(item.draw)
    elseif item.action == "undo" then
        K.undo(game)
        picked = nil
        clampCursor()
        screen = "play"
    elseif item.action == "exit" then
        saveGame()
        smudge.exit()
        return
    end
    smudge.request_update()
end

-- Callbacks ------------------------------------------------------------------

function on_init()
    w, h = smudge.get_bounds()
    m = smudge.get_metrics()
    touch = smudge.has_touch()
    math.randomseed(smudge.millis() + smudge.time())
    computeLayout()
    local won, played = (smudge.load("stats", "0,0")):match("(%d+),(%d+)")
    stats.won, stats.played = tonumber(won) or 0, tonumber(played) or 0
    local saved = smudge.load("game", "")
    if saved ~= "" then game = K.deserialize(saved) end
end

function on_exit()
    saveGame()
end

function on_draw()
    smudge.clear()
    if screen == "menu" then drawMenu() else drawPlay() end
end

function on_button(btn, pressed)
    if not pressed then return end
    if screen == "menu" then
        local items = menuItems()
        if btn == "up" or btn == "left" or btn == "page_back" then
            menuIndex = (menuIndex - 2) % #items + 1
        elseif btn == "down" or btn == "right" or btn == "page_forward" then
            menuIndex = menuIndex % #items + 1
        elseif btn == "confirm" then
            runMenuItem(items[menuIndex])
            return
        elseif btn == "back" then
            saveGame()
            smudge.exit()
            return
        end
        smudge.request_update()
        return
    end

    if btn == "back" then
        if picked then
            picked = nil
        else
            saveGame()
            screen, menuIndex = "menu", 1
        end
        smudge.request_update()
        return
    end
    if K.isWon(game) then
        if btn == "confirm" then screen, menuIndex = "menu", 1 smudge.request_update() end
        return
    end
    if btn == "left" or btn == "right" then
        local step = btn == "left" and -1 or 1
        cursor.col = (cursor.col - 1 + step) % 7 + 1
        if cursor.row == "top" and cursor.col == 3 then cursor.col = cursor.col + step end
        if cursor.row == "tab" then cursor.depth = #game.tableau[cursor.col].cards end
    elseif btn == "up" or btn == "page_back" then
        if cursor.row == "tab" then
            local pile = game.tableau[cursor.col]
            if #pile.cards > 0 and cursor.depth > pile.down + 1 then
                cursor.depth = cursor.depth - 1
            else
                cursor.row = "top"
            end
        else
            cursor.row = "tab"
            cursor.depth = #game.tableau[cursor.col].cards
        end
    elseif btn == "down" or btn == "page_forward" then
        if cursor.row == "top" then
            cursor.row = "tab"
            cursor.depth = #game.tableau[cursor.col].cards
        else
            local pile = game.tableau[cursor.col]
            if cursor.depth < #pile.cards then cursor.depth = cursor.depth + 1 else cursor.row = "top" end
        end
    elseif btn == "confirm" then
        activate(cursor)
        return
    end
    clampCursor()
    smudge.request_update()
end

-- Which position (row, col, depth) a tap lands on, or nil.
local function positionAt(x, y)
    local col = nil
    for c = 1, 7 do
        if x >= colX(c) and x < colX(c) + L.cw then col = c end
    end
    if not col then return nil end
    if y >= L.topY and y < L.topY + L.ch then
        if topSlot(col) then return { row = "top", col = col } end
        return nil
    end
    if y < L.tabY then return nil end
    local pile = game.tableau[col]
    if #pile.cards == 0 then return { row = "tab", col = col, depth = 1 } end
    if y > cardY(pile, #pile.cards) + L.ch then return { row = "tab", col = col, depth = #pile.cards } end
    local depth = 1
    for i = 1, #pile.cards do
        if y >= cardY(pile, i) then depth = i end
    end
    return { row = "tab", col = col, depth = depth }
end

function on_tap(x, y)
    if screen == "menu" then
        local items = menuItems()
        for i, item in ipairs(items) do
            local b = menuButton(i)
            if smudge.in_rect(x, y, b.x, b.y, b.w, b.h) then
                runMenuItem(item)
                return
            end
        end
        return
    end
    local tb = touchButtons()
    if smudge.in_rect(x, y, tb.menu.x, tb.menu.y, tb.menu.w, tb.menu.h) then
        picked = nil
        saveGame()
        screen, menuIndex = "menu", 1
        smudge.request_update()
        return
    end
    if smudge.in_rect(x, y, tb.undo.x, tb.undo.y, tb.undo.w, tb.undo.h) then
        picked, message = nil, nil
        if not K.undo(game) then message = "Nothing to undo" end
        smudge.request_update()
        return
    end
    if K.isWon(game) then
        screen, menuIndex = "menu", 1
        smudge.request_update()
        return
    end
    local pos = positionAt(x, y)
    if not pos then
        if picked then picked = nil smudge.request_update() end
        return
    end
    cursor = { row = pos.row, col = pos.col, depth = pos.depth or 1 }
    activate(pos)
end
