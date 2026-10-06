-- Go for Xteink Pokemon (Lua app).
-- Rules and the computer opponent live in logic.lua and the start menu in
-- menu.lua (loaded while it is on screen); this file draws the board and
-- handles input.
--
-- Buttons (X3/X4): Left/Right and Up/Down move the cursor (below the bottom
-- row is the Pass button), Confirm plays a stone or passes, Back opens the
-- menu. Touch (X4 Pro): tap a point, or Pass.

local G = smudge.dofile("logic.lua")

local LEVELS = { "Easy", "Normal", "Hard" }
local SIZES = { 9, 13 }

local w, h = 480, 800
local m = {}
local touch = false

local menu = nil
local mode = 2         -- 1..3 against the device at that level, 4 two players
local size = 1
local game = nil
local curC, curR = 5, 5  -- curR == n + 1 is the Pass button
local thinking, thinkingDrawn = false, false
local note = nil
local records = {}

local function rand(n) return math.random(1, n) end
local function vsDevice() return mode <= #LEVELS end
local function colourName(p) return p == G.BLACK and "Black" or "White" end

local function newGame(newMode)
    mode = newMode
    game = G.new(SIZES[size])
    curC, curR = (game.n + 1) // 2, (game.n + 1) // 2
    note = nil
end

local function menuItems()
    local items = {}
    if game and not game.over then items[1] = { label = "Resume", mode = 0 } end
    for i, name in ipairs(LEVELS) do
        local r = records[i]
        items[#items + 1] = { label = "vs Device: " .. name, mode = i,
                              note = string.format("You play Black.  Won %d  Lost %d", r[1], r[2]) }
    end
    items[#items + 1] = { label = "Two players", mode = #LEVELS + 1, note = "Take turns on this reader" }
    local n = SIZES[size]
    items[#items + 1] = { label = string.format("Board: %dx%d", n, n), mode = -2, note = "Tap to change size" }
    items[#items + 1] = { label = "Exit", mode = -1 }
    return items
end

local function openMenu()
    thinking = false
    collectgarbage("collect")
    menu = smudge.dofile("menu.lua")("Go", { "Surround more of the board than your opponent.",
                                             "Capture dead stones, then both pass to count." })
    smudge.request_update()
end

local function startThinking()
    thinking = vsDevice() and not game.over and game.turn == G.WHITE
    thinkingDrawn = false
end

local function choose(item)
    if not item then return end
    if item == "exit" or item.mode == -1 then
        smudge.exit()
        return
    end
    if item.mode == -2 then
        size = size % #SIZES + 1
        smudge.save("size", tostring(size))
        smudge.request_update()
        return
    end
    if item.mode > 0 then newGame(item.mode) end
    menu = nil
    collectgarbage("collect")
    startThinking()
    smudge.request_update()
end

-- Layout ---------------------------------------------------------------------

local function contentTop() return m.top_padding + m.header_height + 6 end
local function menuButtonX() return w - 96 end

-- The grid, leaving half a step around it for the edge stones, and the
-- Pass button under it.
local function geometry()
    local n = game.n
    local top = contentTop() + 56
    local bottom = h - m.button_hints_height - 70
    local step = math.min((w - 32) // n, (bottom - top) // n)
    local size = step * (n - 1)
    local y0 = top + step // 2
    return { n = n, step = step, x0 = (w - size) // 2, y0 = y0, size = size,
             pass = { x = w // 2 - 80, y = y0 + size + step // 2 + 12, w = 160, h = 44 } }
end

local function point(B, c, r) return B.x0 + (c - 1) * B.step, B.y0 + (r - 1) * B.step end

-- Drawing --------------------------------------------------------------------

local STARS = { [9] = { { 3, 3 }, { 7, 3 }, { 5, 5 }, { 3, 7 }, { 7, 7 } },
                [13] = { { 4, 4 }, { 10, 4 }, { 7, 7 }, { 4, 10 }, { 10, 10 } } }

local function statusText()
    if game.over then
        local b, wh = G.score(game)
        local text = string.format("Black %g, White %g", b, wh)
        if vsDevice() then
            return text .. (G.winner(game) == G.BLACK and " - you win!" or " - the device wins")
        end
        return text .. " - " .. colourName(G.winner(game)) .. " wins"
    end
    if thinking then return "Device is thinking..." end
    if note then return note end
    if vsDevice() then return "Your move (Black)" end
    return colourName(game.turn) .. " to move"
end

local function drawBoard(B)
    local n, s = B.n, B.step
    for k = 1, n do
        local x, y = point(B, k, 1)
        smudge.line(x, B.y0, x, B.y0 + B.size, (k == 1 or k == n) and 2 or 1, true)
        local x2, y2 = point(B, 1, k)
        smudge.line(B.x0, y2, B.x0 + B.size, y2, (k == 1 or k == n) and 2 or 1, true)
    end
    for _, p in ipairs(STARS[n] or {}) do
        local x, y = point(B, p[1], p[2])
        smudge.circle(x, y, 3, true, true)
    end
    local owner = game.over and G.territory(game) or nil
    local radius = s * 46 // 100
    for i = 1, n * n do
        local c, r = G.colOf(game, i), G.rowOf(game, i)
        local x, y = point(B, c, r)
        local v = game.cells[i]
        if v == G.BLACK then
            smudge.circle(x, y, radius, true, true)
        elseif v == G.WHITE then
            smudge.circle(x, y, radius, true, false)
            smudge.circle(x, y, radius, false, 2)
        elseif owner and owner[i] ~= 0 then
            -- Counted area: a small square of the owner's colour.
            local q = math.max(3, s // 6)
            smudge.rect(x - q, y - q, 2 * q + 1, 2 * q + 1, owner[i] == G.BLACK, true, 2)
        end
        if game.last == i then
            smudge.circle(x, y, math.max(3, radius // 3), true, v ~= G.BLACK)
        end
    end
    if not touch and not game.over and not thinking and curR <= n then
        local x, y = point(B, curC, curR)
        local q = s // 2
        smudge.rect(x - q - 2, y - q - 2, 2 * q + 5, 2 * q + 5, false, false, 2)
        smudge.rect(x - q, y - q, 2 * q + 1, 2 * q + 1, false, true, 3)
    end
    -- The Pass button.
    if not game.over then
        local b = B.pass
        local focused = not touch and curR > n
        smudge.rounded_rect(b.x, b.y, b.w, b.h, 8, focused, focused and true or 2)
        smudge.text(b.x + b.w // 2, b.y + 11, "Pass", "ui12", "bold", "center", not focused)
    end
end

local function drawPlay()
    local n = game.n
    smudge.header("Go", string.format("%s  %dx%d", vsDevice() and LEVELS[mode] or "2 players", n, n))
    local top = contentTop()
    local text = statusText()
    local maxW = (touch and menuButtonX() or w) - 24
    smudge.text(16, top + 4, text, smudge.text_width(text, "ui12", "bold") > maxW and "ui10" or "ui12", "bold", "left",
                true)
    smudge.text(16, top + 32, string.format("Captured: Black %d, White %d   Komi %g", game.captured[1],
                                            game.captured[2], G.KOMI), "ui10", "regular", "left", true)
    if touch then
        smudge.rounded_rect(menuButtonX(), top - 2, 80, 34, 6, false, 2)
        smudge.text(menuButtonX() + 40, top + 4, "Menu", "ui12", "bold", "center", true)
    end
    drawBoard(geometry())
    if game.over then smudge.popup(text) end
    if touch then
        smudge.button_hints("", "", "", "")
    elseif game.over then
        smudge.button_hints("Menu", "Again", "", "")
    else
        smudge.button_hints("Menu", curR > n and "Pass" or "Play", "<", ">")
    end
end

-- Game flow ------------------------------------------------------------------

local function finishIfOver()
    if not game.over or not vsDevice() then return end
    local r = records[mode]
    if G.winner(game) == G.BLACK then r[1] = r[1] + 1 else r[2] = r[2] + 1 end
    smudge.save("rec" .. mode, r[1] .. "," .. r[2])
end

local function humanTurn() return not game.over and not thinking and (not vsDevice() or game.turn == G.BLACK) end

local function playAt(i)
    if not humanTurn() then return end
    if not G.play(game, i) then
        note = (i == game.ko) and "Ko: you cannot retake at once" or "Not allowed there"
        smudge.request_update()
        return
    end
    note = nil
    finishIfOver()
    startThinking()
    smudge.request_update()
end

local function passTurn()
    if not humanTurn() then return end
    local who = game.turn
    G.pass(game)
    note = colourName(who) .. " passed"
    finishIfOver()
    startThinking()
    smudge.request_update()
end

local function deviceMove()
    local i = G.chooseMove(game, mode, rand)
    if i then
        G.play(game, i)
        note = nil
    else
        G.pass(game)
        note = "The device passed - pass too to end the game"
    end
    thinking = false
    finishIfOver()
    collectgarbage("collect")
    smudge.request_update()
end

-- Callbacks ------------------------------------------------------------------

function on_init()
    w, h = smudge.get_bounds()
    m = smudge.get_metrics()
    touch = smudge.has_touch()
    math.randomseed(smudge.millis() + smudge.time())
    for i = 1, #LEVELS do
        local a, b = (smudge.load("rec" .. i, "0,0")):match("(%d+),(%d+)")
        records[i] = { tonumber(a) or 0, tonumber(b) or 0 }
    end
    size = tonumber(smudge.load("size", "1")) or 1
    if size < 1 or size > #SIZES then size = 1 end
    local savedMode, data = smudge.load("game", ""):match("^(%d)#(.+)$")
    local g = data and G.deserialize(data)
    if g and not g.over then
        mode, game = tonumber(savedMode), g
        curC, curR = (g.n + 1) // 2, (g.n + 1) // 2
    end
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
    smudge.save("game", (game and not game.over) and (mode .. "#" .. G.serialize(game)) or "")
end

function on_button(btn, pressed)
    if not pressed then return end
    if menu then
        choose(menu.button(btn, menuItems()))
    elseif btn == "back" then
        openMenu()
    elseif game.over then
        if btn == "confirm" then choose({ mode = mode }) end
    elseif btn == "confirm" then
        if curR > game.n then passTurn() else playAt(G.index(game, curC, curR)) end
    else
        local n = game.n
        if btn == "left" then curC = (curC - 2) % n + 1
        elseif btn == "right" then curC = curC % n + 1
        elseif btn == "up" or btn == "page_back" then curR = (curR - 2) % (n + 1) + 1
        elseif btn == "down" or btn == "page_forward" then curR = curR % (n + 1) + 1 end
        smudge.request_update()
    end
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
    if game.over then
        choose({ mode = mode })
        return
    end
    local B = geometry()
    if smudge.in_rect(x, y, B.pass.x, B.pass.y, B.pass.w, B.pass.h) then
        passTurn()
        return
    end
    local c = (x - B.x0 + B.step // 2) // B.step + 1
    local r = (y - B.y0 + B.step // 2) // B.step + 1
    if c >= 1 and c <= B.n and r >= 1 and r <= B.n then
        curC, curR = c, r
        playAt(G.index(game, c, r))
    end
end
