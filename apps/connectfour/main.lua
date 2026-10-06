-- Connect Four for Xteink Pokemon (Lua app).
-- Rules and the computer opponent live in logic.lua; this file draws and
-- handles input.
--
-- Buttons (X3/X4): Left/Right (or Up/Down) pick a column, Confirm drops a
-- disc, Back opens the menu. Touch (X4 Pro): tap a column to drop a disc.

local C4 = smudge.dofile("logic.lua")

local LEVELS = {
    { name = "Easy", depth = 2, randomPercent = 25 },
    { name = "Normal", depth = 4, randomPercent = 0 },
    { name = "Hard", depth = 5, randomPercent = 0 },
}

local w, h = 480, 800
local m = {}
local touch = false

local screen = "menu" -- "menu" | "play"
local menuIndex = 1
local mode = 1        -- 1..3: against the device at that level; 4: two players
local game = nil
local cursor = 4
local thinking = false
local thinkingDrawn = false
local records = {}

local function rand(n) return math.random(1, n) end

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

local function vsDevice() return mode <= #LEVELS end

local function newGame(newMode)
    mode = newMode
    game = C4.new()
    cursor = 4
    thinking = false
    screen = "play"
end

local function menuItems()
    local items = {}
    if game and game.winner == 0 and game.moves > 0 then
        items[#items + 1] = { label = "Resume", action = "resume" }
    end
    for i, L in ipairs(LEVELS) do
        local r = records[i]
        items[#items + 1] = { label = "vs Device: " .. L.name, action = "mode", mode = i,
                              note = string.format("Won %d  Lost %d  Drawn %d", r[1], r[2], r[3]) }
    end
    items[#items + 1] = { label = "Two players", action = "mode", mode = #LEVELS + 1,
                          note = "Take turns on this reader" }
    items[#items + 1] = { label = "Exit", action = "exit" }
    return items
end

-- Layout ---------------------------------------------------------------------

local function contentTop() return m.top_padding + m.header_height + 8 end
local function contentBottom() return h - m.button_hints_height - 8 end

local function menuButton(i)
    local bh, gap = 64, 10
    return { x = 24, y = contentTop() + 50 + (i - 1) * (bh + gap), w = w - 48, h = bh }
end

local function menuTouchButton() return { x = w - 16 - 80, y = contentTop(), w = 80, h = 44 } end

local function boardRect()
    local top = contentTop() + 60 -- status line, then the column marker row
    local cell = math.min((w - 16) // C4.COLS, (contentBottom() - top) // (C4.ROWS + 1))
    local bw, bh = cell * C4.COLS, cell * C4.ROWS
    return { x = (w - bw) // 2, y = top + cell, cell = cell, w = bw, h = bh, markerY = top }
end

-- Drawing --------------------------------------------------------------------

local function inLine(c, r)
    if not game.line then return false end
    for _, p in ipairs(game.line) do
        if p[1] == c and p[2] == r then return true end
    end
    return false
end

local function drawDisc(cx, cy, radius, who, highlight)
    if who == 1 then
        smudge.circle(cx, cy, radius, true, true)
        if highlight then smudge.circle(cx, cy, radius // 3, true, false) end
    elseif who == 2 then
        -- A thick ring with a centre dot: clearly not an empty (thin) hole.
        smudge.circle(cx, cy, radius, true, false)
        smudge.circle(cx, cy, radius, false, math.max(4, radius // 4))
        smudge.circle(cx, cy, math.max(3, radius // 5), true, true)
        if highlight then smudge.circle(cx, cy, radius - radius // 4 - 4, false, 3) end
    end
end

local function playerName(who)
    if vsDevice() then return who == 1 and "You" or "Device" end
    return who == 1 and "Black" or "White"
end

local function statusText()
    if game.winner == -1 then return "Draw - the board is full" end
    if game.winner > 0 then
        if vsDevice() then return game.winner == 1 and "You win!" or "The device wins" end
        return playerName(game.winner) .. " wins!"
    end
    if thinking then return "Thinking..." end
    if vsDevice() then return "Your move" end
    return playerName(game.turn) .. " to move"
end

local function drawPlay()
    smudge.header("Connect Four", vsDevice() and LEVELS[mode].name or "2 players")
    local y = contentTop()
    -- Whose disc is whose, next to the status.
    drawDisc(28, y + 22, 12, game.winner == 0 and game.turn or (game.winner > 0 and game.winner or 1), false)
    smudge.text(50, y + 10, statusText(), "ui12", "bold", "left", true)
    if touch then
        local b = menuTouchButton()
        smudge.rounded_rect(b.x, b.y, b.w, b.h, 6, false, 2)
        smudge.text(b.x + b.w // 2, b.y + 12, "Menu", "ui12", "bold", "center", true)
    end

    local rect = boardRect()
    local s = rect.cell
    -- Column marker: a triangle over the chosen column (buttons only).
    if not touch and game.winner == 0 and not thinking then
        local cx = rect.x + (cursor - 1) * s + s // 2
        smudge.triangle(cx - s // 4, rect.markerY + s // 4, cx + s // 4, rect.markerY + s // 4, cx,
                        rect.markerY + s - 6, true, true)
    end
    smudge.rect(rect.x, rect.y, rect.w, rect.h, false, true, 3)
    for r = 1, C4.ROWS do
        for c = 1, C4.COLS do
            local cx = rect.x + (c - 1) * s + s // 2
            local cy = rect.y + (C4.ROWS - r) * s + s // 2
            local who = game.cells[C4.index(c, r)]
            if who == 0 then
                smudge.circle(cx, cy, s // 2 - 5, false, 1)
            else
                drawDisc(cx, cy, s // 2 - 5, who, inLine(c, r))
            end
        end
    end
    if touch then
        smudge.button_hints("", "", "", "")
    elseif game.winner ~= 0 then
        smudge.button_hints("Menu", "Again", "", "")
    else
        smudge.button_hints("Menu", "Drop", "<", ">")
    end
end

local function drawMenu()
    smudge.header("Connect Four", "")
    smudge.centered_text(contentTop() + 10, "Line up four of your discs to win.", "ui10", "regular", true)
    local items = menuItems()
    if menuIndex > #items then menuIndex = #items end
    for i, item in ipairs(items) do
        local b = menuButton(i)
        local selected = (i == menuIndex) and not touch
        smudge.rounded_rect(b.x, b.y, b.w, b.h, 8, selected, selected and true or 2)
        local ty = item.note and b.y + 8 or b.y + (b.h - smudge.line_height("ui12")) // 2
        smudge.text(b.x + b.w // 2, ty, item.label, "ui12", "bold", "center", not selected)
        if item.note then
            smudge.text(b.x + b.w // 2, b.y + 36, item.note, "ui10", "regular", "center", not selected)
        end
    end
    if touch then smudge.button_hints("", "", "", "") else smudge.button_hints("Exit", "Select", "Up", "Down") end
end

-- Game flow ------------------------------------------------------------------

local function finishIfOver()
    if game.winner == 0 or not vsDevice() then return end
    local r = records[mode]
    if game.winner == 1 then r[1] = r[1] + 1 elseif game.winner == 2 then r[2] = r[2] + 1 else r[3] = r[3] + 1 end
    saveRecord(mode)
end

local function playerDrop(c)
    if game.winner ~= 0 or thinking or not C4.canDrop(game, c) then return end
    C4.drop(game, c)
    finishIfOver()
    if vsDevice() and game.winner == 0 then
        -- Show the player's disc first; on_update moves once this is drawn.
        thinking, thinkingDrawn = true, false
    end
    smudge.request_update()
end

local function deviceMove()
    local L = LEVELS[mode]
    local c
    if L.randomPercent > 0 and math.random(1, 100) <= L.randomPercent then
        local open = {}
        for col = 1, C4.COLS do if C4.canDrop(game, col) then open[#open + 1] = col end end
        c = open[rand(#open)]
    else
        c = C4.bestMove(game, L.depth, rand)
    end
    if c then C4.drop(game, c) end
    thinking = false
    finishIfOver()
    collectgarbage("collect")
    smudge.request_update()
end

local function runMenuItem(item)
    if item.action == "resume" then
        screen = "play"
    elseif item.action == "mode" then
        newGame(item.mode)
    elseif item.action == "exit" then
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
    loadRecords()
end

function on_draw()
    smudge.clear()
    if screen == "menu" then drawMenu() else drawPlay() end
    if thinking then thinkingDrawn = true end
end

function on_update()
    if screen == "play" and thinking and thinkingDrawn then deviceMove() end
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
            smudge.exit()
            return
        end
        smudge.request_update()
        return
    end
    if btn == "back" then
        screen, menuIndex = "menu", 1
        smudge.request_update()
        return
    end
    if game.winner ~= 0 then
        if btn == "confirm" then newGame(mode) smudge.request_update() end
        return
    end
    if thinking then return end
    if btn == "left" or btn == "up" or btn == "page_back" then
        cursor = (cursor - 2) % C4.COLS + 1
    elseif btn == "right" or btn == "down" or btn == "page_forward" then
        cursor = cursor % C4.COLS + 1
    elseif btn == "confirm" then
        playerDrop(cursor)
        return
    end
    smudge.request_update()
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
    local mb = menuTouchButton()
    if smudge.in_rect(x, y, mb.x, mb.y, mb.w, mb.h) then
        screen, menuIndex = "menu", 1
        smudge.request_update()
        return
    end
    if game.winner ~= 0 then
        newGame(mode)
        smudge.request_update()
        return
    end
    local rect = boardRect()
    if x >= rect.x and x < rect.x + rect.w and y >= rect.markerY and y < rect.y + rect.h then
        cursor = (x - rect.x) // rect.cell + 1
        playerDrop(cursor)
    end
end
