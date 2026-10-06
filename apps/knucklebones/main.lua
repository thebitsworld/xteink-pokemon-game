-- Knucklebones for Xteink Pokemon (Lua app).
-- Rules and the computer opponent live in logic.lua and the start menu in
-- menu.lua (loaded only while it is on screen); this file draws the game and
-- handles input.
--
-- Buttons (X3/X4): Left/Right pick a column, Confirm places the die, Back
-- opens the menu. Touch (X4 Pro): tap one of your columns.

local K = smudge.dofile("logic.lua")

local LEVELS = {
    { name = "Easy" },
    { name = "Normal" },
    { name = "Hard" },
}

local w, h = 480, 800
local m = {}
local touch = false

local menu = nil      -- the menu module while the menu is on screen
local mode = 1        -- 1..3: against the device at that level; 4: two players
local game = nil
local cursor = 2
local thinking = false
local thinkingDrawn = false
local records = {}

local function rand(n) return math.random(1, n) end
local function vsDevice() return mode <= #LEVELS end

local function loadRecords()
    for i = 1, #LEVELS do
        local wins, losses, draws = (smudge.load("rec" .. i, "0,0,0")):match("(%d+),(%d+),(%d+)")
        records[i] = { tonumber(wins) or 0, tonumber(losses) or 0, tonumber(draws) or 0 }
    end
end

local function saveRecord(i)
    local r = records[i]
    smudge.save("rec" .. i, string.format("%d,%d,%d", r[1], r[2], r[3]))
end

local function startDeviceTurnIfDue()
    if vsDevice() and game.winner == 0 and game.turn == 2 then
        thinking, thinkingDrawn = true, false
    end
end

local function newGame(newMode)
    mode = newMode
    -- Against the device a coin decides who starts; two players: bottom first.
    game = K.new(rand, vsDevice() and rand(2) or 1)
    cursor = 2
    thinking = false
end

local function saveGame()
    if game and game.winner == 0 then
        smudge.save("game", mode .. "#" .. K.serialize(game))
    else
        smudge.save("game", "")
    end
end

local function loadGame()
    local s = smudge.load("game", "")
    local savedMode, data = s:match("^(%d+)#(.+)$")
    local g = data and K.deserialize(data)
    if g and g.winner == 0 then
        mode, game = tonumber(savedMode), g
    end
end

local function menuItems()
    local items = {}
    if game and game.winner == 0 then
        items[#items + 1] = { label = "Resume", mode = 0 }
    end
    for i, L in ipairs(LEVELS) do
        local r = records[i]
        items[#items + 1] = { label = "vs Device: " .. L.name, mode = i,
                              note = string.format("Won %d  Lost %d  Drawn %d", r[1], r[2], r[3]) }
    end
    items[#items + 1] = { label = "Two players", mode = #LEVELS + 1,
                          note = "Take turns on this reader" }
    items[#items + 1] = { label = "Exit", mode = -1 }
    return items
end

-- Layout ---------------------------------------------------------------------
-- Top to bottom: status row, the top player's column scores and grid, a band
-- with the die to place and both totals, then the bottom player's grid and
-- column scores. Player 2 (device) is on top; each grid fills from the band
-- outwards.

local STATUS_H, SCORE_H, BAND_H = 44, 26, 96

local function contentTop() return m.top_padding + m.header_height + 8 end
local function contentBottom() return h - m.button_hints_height - 8 end

local function layout()
    local top = contentTop()
    local avail = contentBottom() - top - STATUS_H - 2 * SCORE_H - BAND_H
    local cell = math.min(avail // 6, (w - 32) // 3, 96)
    local gw = cell * 3
    local x = (w - gw) // 2
    local topGrid = top + STATUS_H + SCORE_H
    local band = topGrid + 3 * cell
    local bottomGrid = band + BAND_H
    return { cell = cell, x = x, gw = gw, topGrid = topGrid, band = band, bottomGrid = bottomGrid,
             topScores = top + STATUS_H, bottomScores = bottomGrid + 3 * cell + 4 }
end

-- Top-left corner of slot `i` (1 = next to the band) in column `c` of player `p`.
local function slotPos(L, p, c, i)
    local x = L.x + (c - 1) * L.cell
    if p == 1 then return x, L.bottomGrid + (i - 1) * L.cell end
    return x, L.topGrid + (3 - i) * L.cell
end

local function menuTouchButton() return { x = w - 16 - 80, y = contentTop(), w = 80, h = 40 } end

-- Drawing --------------------------------------------------------------------

-- The status line in bold, dropping to the smaller font when it would run
-- into the Menu button (touch) or off the screen.
local function statusLine(x, y, text, maxW)
    local font = smudge.text_width(text, "ui12", "bold") > maxW and "ui10" or "ui12"
    smudge.text(x, y, text, font, "bold", "left", true)
end

local PIPS = {
    { { 0, 0 } },
    { { -1, -1 }, { 1, 1 } },
    { { -1, -1 }, { 0, 0 }, { 1, 1 } },
    { { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 } },
    { { -1, -1 }, { 1, -1 }, { 0, 0 }, { -1, 1 }, { 1, 1 } },
    { { -1, -1 }, { 1, -1 }, { -1, 0 }, { 1, 0 }, { -1, 1 }, { 1, 1 } },
}

-- A die face of side `s` at (x, y). `marked` greys the face (about to be
-- removed); `bold` draws a heavy border (the die just placed).
local function drawDie(x, y, s, value, marked, bold)
    local r = math.max(4, s // 8)
    if marked then smudge.rect_dither(x + 2, y + 2, s - 4, s - 4, false) end
    smudge.rounded_rect(x, y, s, s, r, false, bold and 5 or 2)
    local cx, cy, step = x + s // 2, y + s // 2, s // 4
    local pr = math.max(3, s // 11)
    for _, p in ipairs(PIPS[value]) do
        local px, py = cx + p[1] * step, cy + p[2] * step
        if marked then smudge.circle(px, py, pr + 2, true, false) end
        smudge.circle(px, py, pr, true, true)
    end
end

local function playerName(p)
    if vsDevice() then return p == 1 and "You" or "Device" end
    return p == 1 and "Bottom" or "Top"
end

local function statusText()
    if game.winner ~= 0 then
        if game.winner == -1 then return "A draw!" end
        if vsDevice() then return game.winner == 1 and "You win!" or "The device wins" end
        return playerName(game.winner) .. " wins!"
    end
    if thinking then return "Device is thinking..." end
    local last = game.last
    if last and vsDevice() and last.player == 2 then
        local s = string.format("Device: %d in column %d", last.value, last.col)
        if last.removed > 0 then s = s .. string.format(" (removed %d)", last.removed) end
        return s
    end
    if vsDevice() then return "Your turn" end
    return playerName(game.turn) .. " to move"
end

local function drawGrid(L, p)
    local active = game.winner == 0 and game.turn == p and not thinking
    local humanTurn = active and (not vsDevice() or p == 1)
    for c = 1, K.COLS do
        local x = L.x + (c - 1) * L.cell
        local gy = (p == 1) and L.bottomGrid or L.topGrid
        smudge.rect(x, gy, L.cell, 3 * L.cell, false, true, 1)
        local col = game.grid[p][c]
        -- Dice the chosen column would remove from this (the waiting) grid.
        local marking = game.winner == 0 and game.turn ~= p and not thinking and cursor == c
                        and (not vsDevice() or game.turn == 1)
        for i, v in ipairs(col) do
            local dx, dy = slotPos(L, p, c, i)
            local last = game.last
            local bold = last and last.player == p and last.col == c and i == #col and last.value == v
            drawDie(dx + 6, dy + 6, L.cell - 12, v, marking and v == game.die, bold)
        end
        -- Column score, on the outer side of the grid.
        local sy = (p == 1) and L.bottomScores or (L.topScores + 2)
        smudge.text(x + L.cell // 2, sy, tostring(K.columnScore(col)), "ui10", "bold", "center", true)
        if humanTurn and cursor == c then
            smudge.rect(x - 1, gy - 1, L.cell + 2, 3 * L.cell + 2, false, true, touch and 2 or 5)
        end
    end
end

local function drawBand(L)
    local y = L.band
    smudge.line(16, y + 2, w - 16, y + 2, 1, true)
    smudge.line(16, y + BAND_H - 3, w - 16, y + BAND_H - 3, 1, true)
    -- Totals on either side.
    local midY = y + BAND_H // 2
    smudge.text(16, midY - 34, playerName(2), "ui10", "regular", "left", true)
    smudge.text(16, midY - 14, tostring(K.score(game, 2)), "ui12", "bold", "left", true)
    smudge.text(w - 16, midY - 4, playerName(1), "ui10", "regular", "right", true)
    smudge.text(w - 16, midY + 16, tostring(K.score(game, 1)), "ui12", "bold", "right", true)
    -- The die to place, with an arrow towards the grid it goes in.
    if game.winner == 0 and game.die > 0 then
        local s = BAND_H - 24
        local dx = (w - s) // 2
        drawDie(dx, y + 12, s, game.die, false, true)
        local ax = dx + s + 20
        if game.turn == 1 then
            smudge.triangle(ax - 8, midY - 4, ax + 8, midY - 4, ax, midY + 10, true, true)
        else
            smudge.triangle(ax - 8, midY + 4, ax + 8, midY + 4, ax, midY - 10, true, true)
        end
    end
end

local function drawPlay()
    smudge.header("Knucklebones", vsDevice() and LEVELS[mode].name or "2 players")
    local L = layout()
    statusLine(16, contentTop() + 10, statusText(), (touch and menuTouchButton().x or w) - 16 - 8)
    if touch then
        local b = menuTouchButton()
        smudge.rounded_rect(b.x, b.y, b.w, b.h, 6, false, 2)
        smudge.text(b.x + b.w // 2, b.y + 10, "Menu", "ui12", "bold", "center", true)
    end
    drawGrid(L, 2)
    drawBand(L)
    drawGrid(L, 1)
    if game.winner ~= 0 then
        smudge.popup(string.format("%s   %d - %d", statusText(), K.score(game, 1), K.score(game, 2)))
    end
    if touch then
        smudge.button_hints("", "", "", "")
    elseif game.winner ~= 0 then
        smudge.button_hints("Menu", "Again", "", "")
    else
        smudge.button_hints("Menu", "Place", "<", ">")
    end
end

-- Game flow ------------------------------------------------------------------

local function finishIfOver()
    if game.winner == 0 or not vsDevice() then return end
    local r = records[mode]
    if game.winner == 1 then r[1] = r[1] + 1 elseif game.winner == 2 then r[2] = r[2] + 1 else r[3] = r[3] + 1 end
    saveRecord(mode)
end

-- Keeps the cursor on a column the player to move can still use.
local function fixCursor()
    if game.winner ~= 0 or K.canPlace(game, cursor) then return end
    for c = 1, K.COLS do
        if K.canPlace(game, c) then cursor = c return end
    end
end

local function humanPlace(c)
    if game.winner ~= 0 or thinking or not K.canPlace(game, c) then return end
    if vsDevice() and game.turn ~= 1 then return end
    K.place(game, c, rand)
    finishIfOver()
    startDeviceTurnIfDue()
    fixCursor()
    smudge.request_update()
end

local function deviceMove()
    local c = K.chooseColumn(game, mode, rand)
    if c then K.place(game, c, rand) end
    thinking = false
    finishIfOver()
    fixCursor()
    smudge.request_update()
end

local function moveCursor(step)
    for _ = 1, K.COLS do
        cursor = (cursor - 1 + step) % K.COLS + 1
        if K.canPlace(game, cursor) then return end
    end
end

local function openMenu()
    thinking = false
    collectgarbage("collect")
    menu = smudge.dofile("menu.lua")("Knucklebones",
                                     { "Roll, place, and knock out matching dice.", "Matching dice in a column multiply." })
    smudge.request_update()
end

local function choose(item)
    if not item then return end
    if item == "exit" or item.mode < 0 then
        smudge.exit()
        return
    end
    if item.mode > 0 then newGame(item.mode) end
    menu = nil
    collectgarbage("collect")
    startDeviceTurnIfDue()
    fixCursor()
    smudge.request_update()
end

-- Callbacks ------------------------------------------------------------------

function on_init()
    w, h = smudge.get_bounds()
    m = smudge.get_metrics()
    touch = smudge.has_touch()
    math.randomseed(smudge.millis() + smudge.time())
    loadRecords()
    loadGame()
    openMenu()
end

function on_draw()
    smudge.clear()
    if menu then menu.draw(menuItems()) else drawPlay() end
    if thinking then thinkingDrawn = true end
end

function on_update()
    if not menu and thinking and thinkingDrawn then deviceMove() end
end

function on_exit()
    saveGame()
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
    if game.winner ~= 0 then
        if btn == "confirm" then choose({ mode = mode }) end
        return
    end
    if thinking then return end
    if btn == "left" or btn == "up" or btn == "page_back" then
        moveCursor(-1)
    elseif btn == "right" or btn == "down" or btn == "page_forward" then
        moveCursor(1)
    elseif btn == "confirm" then
        humanPlace(cursor)
        return
    end
    smudge.request_update()
end

function on_tap(x, y)
    if menu then
        choose(menu.tap(x, y, menuItems()))
        return
    end
    local mb = menuTouchButton()
    if smudge.in_rect(x, y, mb.x, mb.y, mb.w, mb.h) then
        openMenu()
        return
    end
    if game.winner ~= 0 then
        choose({ mode = mode })
        return
    end
    local L = layout()
    local gy = (game.turn == 1) and L.bottomGrid or L.topGrid
    if smudge.in_rect(x, y, L.x, gy, L.gw, 3 * L.cell) then
        cursor = (x - L.x) // L.cell + 1
        humanPlace(cursor)
    end
end
