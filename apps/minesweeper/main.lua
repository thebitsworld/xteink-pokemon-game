-- Minesweeper for Xteink Pokemon (Lua app).
-- Rules live in logic.lua; this file draws and handles input.
--
-- Buttons (X3/X4): Left/Right and Up/Down move the cursor, Confirm digs (or
-- opens the neighbours of a finished number), hold Confirm to flag, Back opens
-- the menu. Touch (X4 Pro): tap a cell to dig - or to flag, with the
-- Dig/Flag switch above the board - and hold a cell to flag it.

local Board = smudge.dofile("logic.lua")

local LEVELS = {
    { name = "Easy", cols = 9, rows = 9, mines = 10 },
    { name = "Medium", cols = 12, rows = 12, mines = 24 },
    { name = "Hard", cols = 16, rows = 16, mines = 40 },
}
local HOLD_MS = 550

local w, h = 480, 800
local m = {}
local touch = false

local screen = "menu" -- "menu" | "play"
local menuIndex = 1
local level = 1
local board = nil
local curC, curR = 1, 1
local flagMode = false
local startMs, endMs = 0, 0
local best = {}

-- Confirm hold tracking (buttons): the release after a hold must not dig.
local holdStart, holdUsed = nil, false
-- A long press on touch is followed by its own tap; ignore that one.
local ignoreTapUntil = 0

local function rand(n) return math.random(1, n) end

local function loadBest()
    for i = 1, #LEVELS do best[i] = tonumber(smudge.load("best" .. i, "0")) or 0 end
end

local function formatTime(ms)
    local s = ms // 1000
    return string.format("%d:%02d", s // 60, s % 60)
end

local function newGame(lv)
    level = lv
    local L = LEVELS[lv]
    board = Board.new(L.cols, L.rows, L.mines)
    curC, curR = (L.cols + 1) // 2, (L.rows + 1) // 2
    flagMode = false
    startMs, endMs = 0, 0
    screen = "play"
end

-- Menu entries: Resume (when a game is under way), the three levels, Exit.
local function menuItems()
    local items = {}
    if board and board.state == "playing" and board.generated then
        items[#items + 1] = { label = "Resume", action = "resume" }
    end
    for i, L in ipairs(LEVELS) do
        local label = string.format("%s  %dx%d, %d mines", L.name, L.cols, L.rows, L.mines)
        items[#items + 1] = { label = label, action = "level", level = i,
                              note = best[i] > 0 and ("Best " .. formatTime(best[i])) or nil }
    end
    items[#items + 1] = { label = "Exit", action = "exit" }
    return items
end

-- Layout ---------------------------------------------------------------------

local function contentTop() return m.top_padding + m.header_height + 8 end
local function contentBottom() return h - m.button_hints_height - 8 end

local function toolbar()
    -- One row above the board: status on the left; on touch, the Dig/Flag
    -- switch and a Menu button on the right.
    local y = contentTop()
    return { y = y, h = 44,
             mode = { x = w - 16 - 210, y = y, w = 120, h = 44 },
             menu = { x = w - 16 - 80, y = y, w = 80, h = 44 } }
end

local function boardRect()
    local L = LEVELS[level]
    local top = toolbar().y + 52
    local availW = w - 16
    local availH = contentBottom() - top
    local cell = math.min(availW // L.cols, availH // L.rows)
    local bw, bh = cell * L.cols, cell * L.rows
    return { x = (w - bw) // 2, y = top, cell = cell, w = bw, h = bh }
end

local function menuButton(i, count)
    local bh, gap = 64, 12
    local top = contentTop() + 70
    return { x = 24, y = top + (i - 1) * (bh + gap), w = w - 48, h = bh }
end

-- Drawing --------------------------------------------------------------------

local function drawFlag(x, y, s)
    local px = x + s // 2 - 2
    smudge.line(px, y + s // 5, px, y + s - s // 5, 2, true)
    smudge.triangle(px, y + s // 5, px, y + s // 2, x + s - s // 4, y + s // 3, true, true)
end

local function drawCell(c, r, rect)
    local i = board:index(c, r)
    local s = rect.cell
    local x, y = rect.x + (c - 1) * s, rect.y + (r - 1) * s
    local vis = board.vis[i]
    local over = board.state ~= "playing"
    if vis == Board.OPEN then
        if board.mine[i] then
            smudge.rect(x, y, s, s, true, true)
            smudge.circle(x + s // 2, y + s // 2, s // 4, true, false)
        else
            smudge.rect(x, y, s, s, false, true, 1)
            local n = board.num[i]
            if n > 0 then
                local font = s >= 30 and "ui12" or "ui10"
                local lh = smudge.line_height(font)
                smudge.text(x + s // 2, y + (s - lh) // 2, tostring(n), font, "bold", "center", true)
            end
        end
    else
        smudge.rect_dither(x + 1, y + 1, s - 2, s - 2, false)
        smudge.rect(x, y, s, s, false, true, 1)
        if vis == Board.FLAGGED then
            smudge.rect(x + 3, y + 3, s - 6, s - 6, true, false)
            drawFlag(x, y, s)
            if over and not board.mine[i] then -- wrong flag
                smudge.line(x + 4, y + 4, x + s - 4, y + s - 4, 2, true)
            end
        elseif over and board.mine[i] then
            smudge.rect(x + 3, y + 3, s - 6, s - 6, true, false)
            smudge.circle(x + s // 2, y + s // 2, s // 4, true, true)
        end
    end
end

local function drawPlay()
    local L = LEVELS[level]
    smudge.header("Minesweeper", L.name)
    local tb = toolbar()
    local left = board.mines - board.flags
    local status
    if board.state == "won" then
        status = "Cleared in " .. formatTime(endMs - startMs)
    elseif board.state == "lost" then
        status = "Boom!"
    else
        status = "Mines left: " .. left
    end
    smudge.text(16, tb.y + 12, status, "ui12", "bold", "left", true)
    if touch then
        local b = tb.mode
        smudge.rounded_rect(b.x, b.y, b.w, b.h, 6, flagMode, flagMode and true or 2)
        smudge.text(b.x + b.w // 2, b.y + 12, flagMode and "Flag" or "Dig", "ui12", "bold", "center", not flagMode)
        b = tb.menu
        smudge.rounded_rect(b.x, b.y, b.w, b.h, 6, false, 2)
        smudge.text(b.x + b.w // 2, b.y + 12, "Menu", "ui12", "bold", "center", true)
    end

    local rect = boardRect()
    for r = 1, L.rows do
        for c = 1, L.cols do drawCell(c, r, rect) end
    end
    if not touch and board.state == "playing" then
        smudge.centered_text(rect.y + rect.h + 12, "Hold Confirm to place or remove a flag", "ui10", "regular", true)
        local s = rect.cell
        smudge.rect(rect.x + (curC - 1) * s - 1, rect.y + (curR - 1) * s - 1, s + 2, s + 2, false, true, 4)
    end
    if board.state ~= "playing" then
        smudge.popup(board.state == "won" and "You cleared the field!" or "You hit a mine.")
    end
    if touch then
        smudge.button_hints("", "", "", "")
    elseif board.state == "playing" then
        smudge.button_hints("Menu", "Dig", "<", ">")
    else
        smudge.button_hints("Menu", "New game", "", "")
    end
end

local function drawMenu()
    smudge.header("Minesweeper", "")
    smudge.centered_text(contentTop() + 20, "Clear the field without digging up a mine.", "ui10", "regular", true)
    local items = menuItems()
    if menuIndex > #items then menuIndex = #items end
    for i, item in ipairs(items) do
        local b = menuButton(i, #items)
        local selected = (i == menuIndex) and not touch
        smudge.rounded_rect(b.x, b.y, b.w, b.h, 8, selected, selected and true or 2)
        local ty = item.note and b.y + 8 or b.y + (b.h - smudge.line_height("ui12")) // 2
        smudge.text(b.x + b.w // 2, ty, item.label, "ui12", "bold", "center", not selected)
        if item.note then
            smudge.text(b.x + b.w // 2, b.y + 36, item.note, "ui10", "regular", "center", not selected)
        end
    end
    if touch then
        smudge.button_hints("", "", "", "")
    else
        smudge.button_hints("Exit", "Select", "Up", "Down")
    end
end

-- Actions --------------------------------------------------------------------

local function afterMove()
    if board.state ~= "playing" and endMs == 0 then
        endMs = smudge.millis()
        if board.state == "won" then
            local t = endMs - startMs
            if best[level] == 0 or t < best[level] then
                best[level] = t
                smudge.save("best" .. level, tostring(t))
            end
        end
    end
    smudge.request_update()
end

local function dig(c, r)
    if board.state ~= "playing" then return end
    if not board.generated then startMs = smudge.millis() end
    board:activate(c, r, rand)
    afterMove()
end

local function flag(c, r)
    if board.state ~= "playing" or not board.generated then return end
    board:toggleFlag(c, r)
    smudge.request_update()
end

local function runMenuItem(item)
    if item.action == "resume" then
        screen = "play"
    elseif item.action == "level" then
        newGame(item.level)
    elseif item.action == "exit" then
        smudge.exit()
        return
    end
    smudge.request_update()
end

local function cellAt(x, y)
    local rect = boardRect()
    if not smudge.in_rect(x, y, rect.x, rect.y, rect.w, rect.h) then return nil end
    return (x - rect.x) // rect.cell + 1, (y - rect.y) // rect.cell + 1
end

-- Callbacks ------------------------------------------------------------------

function on_init()
    w, h = smudge.get_bounds()
    m = smudge.get_metrics()
    touch = smudge.has_touch()
    math.randomseed(smudge.millis() + smudge.time())
    loadBest()
end

function on_draw()
    smudge.clear()
    if screen == "menu" then drawMenu() else drawPlay() end
end

function on_update()
    -- Hold Confirm to flag (buttons only).
    if screen ~= "play" or touch then return end
    if smudge.is_button_down("confirm") then
        if not holdStart then
            holdStart, holdUsed = smudge.millis(), false
        elseif not holdUsed and smudge.millis() - holdStart >= HOLD_MS then
            holdUsed = true
            flag(curC, curR)
        end
    end
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
    if board.state ~= "playing" then
        if btn == "confirm" then newGame(level) smudge.request_update() end
        return
    end
    local L = LEVELS[level]
    if btn == "left" then
        curC = (curC - 2) % L.cols + 1
    elseif btn == "right" then
        curC = curC % L.cols + 1
    elseif btn == "up" or btn == "page_back" then
        curR = (curR - 2) % L.rows + 1
    elseif btn == "down" or btn == "page_forward" then
        curR = curR % L.rows + 1
    elseif btn == "confirm" then
        local wasHold = holdUsed
        holdStart, holdUsed = nil, false
        if not wasHold then dig(curC, curR) end
        return
    end
    smudge.request_update()
end

function on_tap(x, y)
    if smudge.millis() < ignoreTapUntil then return end
    if screen == "menu" then
        local items = menuItems()
        for i, item in ipairs(items) do
            local b = menuButton(i, #items)
            if smudge.in_rect(x, y, b.x, b.y, b.w, b.h) then
                runMenuItem(item)
                return
            end
        end
        return
    end
    local tb = toolbar()
    if smudge.in_rect(x, y, tb.menu.x, tb.menu.y, tb.menu.w, tb.menu.h) then
        screen, menuIndex = "menu", 1
        smudge.request_update()
        return
    end
    if smudge.in_rect(x, y, tb.mode.x, tb.mode.y, tb.mode.w, tb.mode.h) then
        flagMode = not flagMode
        smudge.request_update()
        return
    end
    if board.state ~= "playing" then
        newGame(level)
        smudge.request_update()
        return
    end
    local c, r = cellAt(x, y)
    if not c then return end
    curC, curR = c, r
    if flagMode then flag(c, r) else dig(c, r) end
end

-- Firmware that reports touch long presses (on_touch(x, y, "long_press")).
function on_touch(x, y, event)
    if event ~= "long_press" or screen ~= "play" then return end
    local c, r = cellAt(x, y)
    if not c then return end
    ignoreTapUntil = smudge.millis() + 400
    flag(c, r)
end
