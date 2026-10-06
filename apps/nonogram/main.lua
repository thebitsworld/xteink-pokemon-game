-- Nonogram for Xteink Pokemon (Lua app).
-- Rules live in logic.lua, the puzzle generator in generator.lua and the start
-- menu in menu.lua; this file draws the game and handles input. generator.lua
-- and menu.lua are loaded only while needed, to fit the X3's Lua memory.
--
-- Buttons (X3/X4): Left/Right and Up/Down move the cursor, Confirm fills a
-- cell, hold Confirm to mark it empty, Back opens the menu. Touch (X4 Pro):
-- tap a cell to fill it - or to mark it, with the Fill/Mark switch above the
-- grid - and hold a cell to mark it.

local N = smudge.dofile("logic.lua")

local LEVELS = {
    { name = "Easy", size = 5 },
    { name = "Medium", size = 8 },
    { name = "Hard", size = 10 },
    { name = "Expert", size = 12 },
}
local HOLD_MS = 550

local w, h = 480, 800
local m = {}
local touch = false

local menu = nil       -- the menu module while the menu is on screen
local level = 1
local puzzle = nil
local curC, curR = 1, 1
local markMode = false
local sessionStart = 0  -- millis when the current stretch of play began
local stats = {}        -- per level: { solved, bestMs }
local generateFailed = false

local holdStart, holdUsed = nil, false
local ignoreTapUntil = 0

local function rand(n) return math.random(1, n) end

local function formatTime(ms)
    local s = ms // 1000
    return string.format("%d:%02d", s // 60, s % 60)
end

local function loadStats()
    for i = 1, #LEVELS do
        local solved, bestMs = (smudge.load("st" .. i, "0,0")):match("(%d+),(%d+)")
        stats[i] = { tonumber(solved) or 0, tonumber(bestMs) or 0 }
    end
end

local function levelOfSize(size)
    for i, L in ipairs(LEVELS) do
        if L.size == size then return i end
    end
    return nil
end

-- Play time: elapsedMs holds finished stretches, sessionStart the running one.
local function elapsed()
    if not puzzle then return 0 end
    if puzzle.solved or menu then return puzzle.elapsedMs end
    return puzzle.elapsedMs + (smudge.millis() - sessionStart)
end

local function pauseClock()
    if puzzle and not puzzle.solved and not menu then
        puzzle.elapsedMs = elapsed()
    end
end

local function resumeClock()
    sessionStart = smudge.millis()
end

local function saveGame()
    if puzzle and not puzzle.solved then
        smudge.save("game", N.serialize(puzzle))
    else
        smudge.save("game", "")
    end
end

local function loadGame()
    local p = N.deserialize(smudge.load("game", ""))
    if p and not p.solved and levelOfSize(p.size) then
        puzzle, level = p, levelOfSize(p.size)
        curC, curR = (p.size + 1) // 2, (p.size + 1) // 2
    end
end

local function openMenu()
    pauseClock()
    collectgarbage("collect")
    local lines = { "Fill the cells the numbers describe." }
    if generateFailed then lines[2] = "Could not make a puzzle - try again." end
    menu = smudge.dofile("menu.lua")("Nonogram", lines)
    smudge.request_update()
end

-- Makes a new puzzle and starts it. The menu, the old puzzle and afterwards
-- the generator itself are dropped, so only one of them uses memory at a time.
local function newPuzzle(lv)
    level, menu, puzzle = lv, nil, nil
    collectgarbage("collect")
    local picture = smudge.dofile("generator.lua")(N).generate(LEVELS[lv].size, rand, 400)
    collectgarbage("collect")
    local p = picture and N.newPuzzle(LEVELS[lv].size, picture)
    generateFailed = p == nil
    if not p then
        openMenu()
        return
    end
    puzzle = p
    curC, curR = (p.size + 1) // 2, (p.size + 1) // 2
    markMode = false
    resumeClock()
    smudge.request_update()
end

local function menuItems()
    local items = {}
    if puzzle and not puzzle.solved then
        items[#items + 1] = { label = "Resume", level = 0,
                              note = string.format("%s %dx%d", LEVELS[level].name, puzzle.size, puzzle.size) }
    end
    for i, L in ipairs(LEVELS) do
        local s = stats[i]
        local note = s[1] > 0 and string.format("Solved %d  Best %s", s[1], formatTime(s[2])) or "Not solved yet"
        items[#items + 1] = { label = string.format("%s  %dx%d", L.name, L.size, L.size), level = i, note = note }
    end
    items[#items + 1] = { label = "Exit", level = -1 }
    return items
end

-- Layout ---------------------------------------------------------------------

local function contentTop() return m.top_padding + m.header_height + 8 end
local function contentBottom() return h - m.button_hints_height - 8 end

local function toolbar()
    local y = contentTop()
    return { y = y, h = 44, mode = { x = w - 16 - 210, y = y, w = 120, h = 44 },
             menu = { x = w - 16 - 80, y = y, w = 80, h = 44 } }
end

local CLUE_GAP = 13

-- Width of a row clue drawn number by number with CLUE_GAP between them
-- (the font's own space is too narrow to tell "1 1" from "11").
local function rowClueWidth(clue)
    if #clue == 0 then return smudge.text_width("0", "ui10", "bold") end
    local total = 0
    for k, n in ipairs(clue) do
        total = total + smudge.text_width(tostring(n), "ui10", "bold") + (k > 1 and CLUE_GAP or 0)
    end
    return total
end

-- The grid with its clue gutters (row clues to the left, column clues above).
local function gridRect()
    local p = puzzle
    local n = p.size
    local rowGutter, colLines = 0, 1
    for i = 1, n do
        local tw = rowClueWidth(p.rowClues[i])
        if tw > rowGutter then rowGutter = tw end
        if #p.colClues[i] > colLines then colLines = #p.colClues[i] end
    end
    rowGutter = rowGutter + 12
    local lh = smudge.line_height("ui10")
    local colGutter = colLines * lh + 6
    local top = toolbar().y + 52
    local bottomRoom = touch and 0 or 30 -- the hint line under the grid
    local cell = math.min((w - 16 - rowGutter) // n, (contentBottom() - bottomRoom - top - colGutter) // n, 48)
    local total = rowGutter + n * cell
    local x0 = (w - total) // 2
    return { x = x0 + rowGutter, y = top + colGutter, cell = cell, size = n * cell, rowGutter = rowGutter,
             colGutter = colGutter, lh = lh, left = x0, top = top }
end

-- Drawing --------------------------------------------------------------------

local function drawCell(G, c, r)
    local s = G.cell
    local x, y = G.x + (c - 1) * s, G.y + (r - 1) * s
    local state = N.cell(puzzle, c, r)
    if state == N.FILLED then
        smudge.rect(x + 2, y + 2, s - 3, s - 3, true, true)
    elseif puzzle.solved then
        return -- the finished picture shows only its filled cells
    elseif state == N.MARKED then
        local k = s // 3
        smudge.line(x + k, y + k, x + s - k, y + s - k, 1, true)
        smudge.line(x + s - k, y + k, x + k, y + s - k, 1, true)
    elseif state == N.MISTAKE then
        smudge.rect_dither(x + 2, y + 2, s - 3, s - 3, false)
        local k = s // 4
        smudge.line(x + k, y + k, x + s - k, y + s - k, 3, true)
        smudge.line(x + s - k, y + k, x + k, y + s - k, 3, true)
    end
end

local function drawClues(G)
    local p = puzzle
    local n = p.size
    local s = G.cell
    for r = 1, n do
        local done = N.rowDone(p, r)
        local clue = p.rowClues[r]
        local style = done and "regular" or "bold"
        local ty = G.y + (r - 1) * s + (s - G.lh) // 2
        -- Right to left from the grid edge.
        local x = G.x - 6
        if #clue == 0 then
            smudge.text(x, ty, "0", "ui10", style, "right", true)
        else
            for k = #clue, 1, -1 do
                local text = tostring(clue[k])
                smudge.text(x, ty, text, "ui10", style, "right", true)
                x = x - smudge.text_width(text, "ui10", style) - CLUE_GAP
            end
        end
        if done then
            local left = G.x - 6 - rowClueWidth(clue)
            smudge.line(left, ty + G.lh // 2, G.x - 6, ty + G.lh // 2, 1, true)
        end
    end
    for c = 1, n do
        local clue = p.colClues[c]
        local done = N.colDone(p, c)
        local cx = G.x + (c - 1) * s + s // 2
        local count = math.max(1, #clue)
        for k = 1, count do
            local text = (#clue == 0) and "0" or tostring(clue[k])
            local ty = G.y - 4 - (count - k + 1) * G.lh
            smudge.text(cx, ty, text, "ui10", done and "regular" or "bold", "center", true)
        end
        if done then
            local top = G.y - 4 - count * G.lh
            smudge.line(cx, top + 2, cx, G.y - 6, 1, true)
        end
    end
end

local function drawGrid(G)
    local n = puzzle.size
    local s = G.cell
    for r = 1, n do
        for c = 1, n do drawCell(G, c, r) end
    end
    -- Thin lines between cells, thick ones every five and around the edge.
    for i = 0, n do
        if i % 5 == 0 or i == n then
            smudge.rect(G.x + i * s - 1, G.y - 1, 3, G.size + 3, true, true)
            smudge.rect(G.x - 1, G.y + i * s - 1, G.size + 3, 3, true, true)
        else
            smudge.line(G.x + i * s, G.y, G.x + i * s, G.y + G.size, 1, true)
            smudge.line(G.x, G.y + i * s, G.x + G.size, G.y + i * s, 1, true)
        end
    end
end

local function drawPlay()
    local L = LEVELS[level]
    smudge.header("Nonogram", string.format("%s %dx%d", L.name, puzzle.size, puzzle.size))
    local tb = toolbar()
    local status = string.format("%s   Mistakes: %d", formatTime(elapsed()), puzzle.mistakes)
    smudge.text(16, tb.y + 12, status, "ui12", "bold", "left", true)
    if touch then
        local b = tb.mode
        smudge.rounded_rect(b.x, b.y, b.w, b.h, 6, markMode, markMode and true or 2)
        smudge.text(b.x + b.w // 2, b.y + 12, markMode and "Mark" or "Fill", "ui12", "bold", "center", not markMode)
        b = tb.menu
        smudge.rounded_rect(b.x, b.y, b.w, b.h, 6, false, 2)
        smudge.text(b.x + b.w // 2, b.y + 12, "Menu", "ui12", "bold", "center", true)
    end
    local G = gridRect()
    drawClues(G)
    drawGrid(G)
    if not touch and not puzzle.solved then
        local s = G.cell
        local cx, cy = G.x + (curC - 1) * s, G.y + (curR - 1) * s
        -- A black ring between two white ones shows on filled and empty cells alike.
        smudge.rect(cx - 6, cy - 6, s + 13, s + 13, false, false, 3)
        smudge.rect(cx - 3, cy - 3, s + 7, s + 7, false, true, 4)
        smudge.rect(cx + 4, cy + 4, s - 7, s - 7, false, false, 2)
        smudge.centered_text(G.y + G.size + 10, "Hold Confirm to mark a cell empty", "ui10", "regular", true)
    end
    if puzzle.solved then
        local mistakes = puzzle.mistakes == 1 and "1 mistake" or (puzzle.mistakes .. " mistakes")
        smudge.popup(string.format("Solved in %s, %s", formatTime(puzzle.elapsedMs), mistakes))
    end
    if touch then
        smudge.button_hints("", "", "", "")
    elseif puzzle.solved then
        smudge.button_hints("Menu", "Next", "", "")
    else
        smudge.button_hints("Menu", "Fill", "<", ">")
    end
end

-- Actions --------------------------------------------------------------------

local function afterSolve()
    if not puzzle.solved then return end
    puzzle.elapsedMs = puzzle.elapsedMs + (smudge.millis() - sessionStart)
    local s = stats[level]
    s[1] = s[1] + 1
    if s[2] == 0 or puzzle.elapsedMs < s[2] then s[2] = puzzle.elapsedMs end
    smudge.save("st" .. level, string.format("%d,%d", s[1], s[2]))
    smudge.save("game", "")
end

local function fill(c, r)
    if puzzle.solved then return end
    if N.fill(puzzle, c, r) then
        afterSolve()
        smudge.request_update()
    end
end

local function mark(c, r)
    if puzzle.solved then return end
    if N.toggleMark(puzzle, c, r) then smudge.request_update() end
end

local function choose(item)
    if not item then return end
    if item == "exit" or item.level < 0 then
        smudge.exit()
    elseif item.level == 0 then
        menu = nil
        collectgarbage("collect")
        resumeClock()
        smudge.request_update()
    else
        newPuzzle(item.level)
    end
end

local function cellAt(x, y)
    local G = gridRect()
    if not smudge.in_rect(x, y, G.x, G.y, G.size, G.size) then return nil end
    return (x - G.x) // G.cell + 1, (y - G.y) // G.cell + 1
end

-- Callbacks ------------------------------------------------------------------

function on_init()
    w, h = smudge.get_bounds()
    m = smudge.get_metrics()
    touch = smudge.has_touch()
    math.randomseed(smudge.millis() + smudge.time())
    loadStats()
    loadGame()
    openMenu()
end

function on_draw()
    smudge.clear()
    if menu then menu.draw(menuItems()) else drawPlay() end
end

function on_update()
    if menu or touch or puzzle.solved then return end
    if smudge.is_button_down("confirm") then
        if not holdStart then
            holdStart, holdUsed = smudge.millis(), false
        elseif not holdUsed and smudge.millis() - holdStart >= HOLD_MS then
            holdUsed = true
            mark(curC, curR)
        end
    end
end

function on_exit()
    pauseClock()
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
    if puzzle.solved then
        if btn == "confirm" then newPuzzle(level) end
        return
    end
    local n = puzzle.size
    if btn == "left" then
        curC = (curC - 2) % n + 1
    elseif btn == "right" then
        curC = curC % n + 1
    elseif btn == "up" or btn == "page_back" then
        curR = (curR - 2) % n + 1
    elseif btn == "down" or btn == "page_forward" then
        curR = curR % n + 1
    elseif btn == "confirm" then
        local wasHold = holdUsed
        holdStart, holdUsed = nil, false
        if not wasHold then fill(curC, curR) end
        return
    end
    smudge.request_update()
end

function on_tap(x, y)
    if smudge.millis() < ignoreTapUntil then return end
    if menu then
        choose(menu.tap(x, y, menuItems()))
        return
    end
    local tb = toolbar()
    if smudge.in_rect(x, y, tb.menu.x, tb.menu.y, tb.menu.w, tb.menu.h) then
        openMenu()
        return
    end
    if smudge.in_rect(x, y, tb.mode.x, tb.mode.y, tb.mode.w, tb.mode.h) then
        markMode = not markMode
        smudge.request_update()
        return
    end
    if puzzle.solved then
        newPuzzle(level)
        return
    end
    local c, r = cellAt(x, y)
    if not c then return end
    curC, curR = c, r
    if markMode then mark(c, r) else fill(c, r) end
end

-- Firmware that reports touch long presses (on_touch(x, y, "long_press")).
function on_touch(x, y, event)
    if event ~= "long_press" or menu or puzzle.solved then return end
    local c, r = cellAt(x, y)
    if not c then return end
    ignoreTapUntil = smudge.millis() + 400
    mark(c, r)
end
