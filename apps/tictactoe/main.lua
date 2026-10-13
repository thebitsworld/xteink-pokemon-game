-- Tic-Tac-Toe for Xteink Pokemon (Lua app).
-- Rules and the computer opponent live in logic.lua; this file draws and
-- handles input.
--
-- Buttons (X3/X4): Left/Right/Up/Down move over the grid, Confirm plays,
-- Back opens the menu. Touch (X4 Pro): tap a square to play.
-- X always moves first; against the device, who starts alternates game by
-- game, and whoever starts plays X.

local T = smudge.dofile("logic.lua")

local LEVELS = {
    { name = "Easy" },
    { name = "Normal" },
    { name = "Hard" },
}

local w, h = 480, 800
local m = {}
local touch = false

local screen = "menu" -- "menu" | "play"
local menuIndex = 1
local mode = 1        -- 1..3: against the device at that level; 4: two players
local game = nil
local cursor = 5
local playerMark = 1  -- against the device: 1 = the player is X, 2 = O
local playerStarts = true
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
    if newMode ~= mode then playerStarts = true end
    mode = newMode
    game = T.new()
    cursor = 5
    playerMark = playerStarts and 1 or 2
    thinking = false
    screen = "play"
    -- The device opens when it is its turn to start.
    if vsDevice() and game.turn ~= playerMark then thinking, thinkingDrawn = true, false end
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
    local top = contentTop() + 70
    local size = math.min(w - 48, contentBottom() - top - 16)
    local cell = size // 3
    size = cell * 3
    return { x = (w - size) // 2, y = top, cell = cell, size = size }
end

local function cellRect(rect, i)
    local c, r = (i - 1) % 3, (i - 1) // 3
    return rect.x + c * rect.cell, rect.y + r * rect.cell
end

-- Drawing --------------------------------------------------------------------

local function drawMark(cx, cy, size, who)
    local half = size // 2
    if who == 1 then
        smudge.thick_line(cx - half, cy - half, cx + half, cy + half, 8, true)
        smudge.thick_line(cx - half, cy + half, cx + half, cy - half, 8, true)
    elseif who == 2 then
        smudge.circle(cx, cy, half, false, 8)
    end
end

local function markName(who) return who == 1 and "X" or "O" end

local function statusText()
    if game.winner == -1 then return "Draw" end
    if game.winner > 0 then
        if vsDevice() then return game.winner == playerMark and "You win!" or "The device wins" end
        return markName(game.winner) .. " wins!"
    end
    if thinking then return "Thinking..." end
    if vsDevice() then return "Your move (" .. markName(playerMark) .. ")" end
    return markName(game.turn) .. " to move"
end

local function drawPlay()
    smudge.header("Tic-Tac-Toe", vsDevice() and LEVELS[mode].name or "2 players")
    local y = contentTop()
    smudge.text(24, y + 10, statusText(), "ui12", "bold", "left", true)
    if touch then
        local b = menuTouchButton()
        smudge.rounded_rect(b.x, b.y, b.w, b.h, 6, false, 2)
        smudge.text(b.x + b.w // 2, b.y + 12, "Menu", "ui12", "bold", "center", true)
    end

    local rect = boardRect()
    local s = rect.cell
    -- The grid: two bars each way (filled, so both directions are as thick),
    -- no outer frame.
    for k = 1, 2 do
        smudge.rect(rect.x + k * s - 2, rect.y + 6, 4, rect.size - 12, true)
        smudge.rect(rect.x + 6, rect.y + k * s - 2, rect.size - 12, 4, true)
    end
    for i = 1, 9 do
        local cx, cy = cellRect(rect, i)
        if game.cells[i] ~= 0 then drawMark(cx + s // 2, cy + s // 2, s * 3 // 5, game.cells[i]) end
    end
    -- The cursor (buttons only): a frame inside the square.
    if not touch and game.winner == 0 and not thinking then
        local cx, cy = cellRect(rect, cursor)
        smudge.rounded_rect(cx + 10, cy + 10, s - 20, s - 20, 10, false, 3)
    end
    -- The winning line, through the centres of its three squares.
    if game.line then
        local ax, ay = cellRect(rect, game.line[1])
        local bx, by = cellRect(rect, game.line[3])
        smudge.thick_line(ax + s // 2, ay + s // 2, bx + s // 2, by + s // 2, 12, true)
    end
    if game.winner ~= 0 then
        smudge.centered_text(rect.y + rect.size + 24, touch and "Tap the board to play again" or "Confirm to play again",
                             "ui10", "regular", true)
    end
    if touch then
        smudge.button_hints("", "", "", "")
    elseif game.winner ~= 0 then
        smudge.button_hints("Menu", "Again", "", "")
    else
        smudge.button_hints("Menu", "Play", "<", ">")
    end
end

local function drawMenu()
    smudge.header("Tic-Tac-Toe", "")
    smudge.centered_text(contentTop() + 10, "Three in a row wins.", "ui10", "regular", true)
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
    if game.winner == 0 then return end
    if vsDevice() then
        local r = records[mode]
        if game.winner == playerMark then
            r[1] = r[1] + 1
        elseif game.winner > 0 then
            r[2] = r[2] + 1
        else
            r[3] = r[3] + 1
        end
        saveRecord(mode)
    end
    playerStarts = not playerStarts -- the other side opens the next game
end

local function playerPlay(i)
    if game.winner ~= 0 or thinking or not T.canPlay(game, i) then return end
    T.play(game, i)
    finishIfOver()
    if vsDevice() and game.winner == 0 then
        -- Show the player's mark first; on_update moves once this is drawn.
        thinking, thinkingDrawn = true, false
    end
    smudge.request_update()
end

local function deviceMove()
    local i = T.deviceMove(game, mode, rand)
    if i then T.play(game, i) end
    thinking = false
    finishIfOver()
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
    local c, r = (cursor - 1) % 3, (cursor - 1) // 3
    if btn == "left" or btn == "page_back" then
        c = (c + 2) % 3
    elseif btn == "right" or btn == "page_forward" then
        c = (c + 1) % 3
    elseif btn == "up" then
        r = (r + 2) % 3
    elseif btn == "down" then
        r = (r + 1) % 3
    elseif btn == "confirm" then
        playerPlay(cursor)
        return
    end
    cursor = r * 3 + c + 1
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
    local rect = boardRect()
    if game.winner ~= 0 then
        if smudge.in_rect(x, y, rect.x, rect.y, rect.size, rect.size + 60) then
            newGame(mode)
            smudge.request_update()
        end
        return
    end
    if smudge.in_rect(x, y, rect.x, rect.y, rect.size, rect.size) then
        cursor = ((y - rect.y) // rect.cell) * 3 + (x - rect.x) // rect.cell + 1
        playerPlay(cursor)
    end
end
