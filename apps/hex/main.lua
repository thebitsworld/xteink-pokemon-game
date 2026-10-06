-- Hex for Xteink Pokemon (Lua app).
-- Rules and the computer opponent live in logic.lua and the start menu in
-- menu.lua (loaded while it is on screen); this file draws the board and
-- handles input.
--
-- Buttons (X3/X4): Left/Right and Up/Down move the cursor, Confirm places a
-- stone, Back opens the menu. Touch (X4 Pro): tap a cell.

local X = smudge.dofile("logic.lua")

local LEVELS = { "Easy", "Normal", "Hard" }
local SIZES = { 7, 9, 11 }
local THINK_MS = 1500

local w, h = 480, 800
local m = {}
local touch = false

local menu = nil
local mode = 1          -- 1..3 against the device at that level, 4 two players
local size = 2          -- index into SIZES
local game = nil
local human = X.WHITE   -- your colour against the device (it alternates; Black first)
local curC, curR = 1, 1
local thinking, thinkingDrawn = false, false
local records = {}

local function rand(n) return math.random(1, n) end
local function vsDevice() return mode <= #LEVELS end
local function colourName(p) return p == X.BLACK and "Black" or "White" end

local function newGame(newMode)
    mode = newMode
    game = X.new(SIZES[size])
    if vsDevice() then human = 3 - human end
    curC, curR = (game.n + 1) // 2, (game.n + 1) // 2
end

local function menuItems()
    local items = {}
    if game and game.winner == 0 then items[1] = { label = "Resume", mode = 0 } end
    for i, name in ipairs(LEVELS) do
        local r = records[i]
        items[#items + 1] = { label = "vs Device: " .. name, mode = i, note = string.format("Won %d  Lost %d", r[1], r[2]) }
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
    menu = smudge.dofile("menu.lua")("Hex", { "Black joins top and bottom, White left and right.",
                                              "The first unbroken chain wins." })
    smudge.request_update()
end

local function startThinking()
    thinking = vsDevice() and game.winner == 0 and game.turn ~= human
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
-- Pointy-top hexagons of radius s; row r is shifted half a cell to the right
-- of the row above, which makes the rhombus.

local function contentTop() return m.top_padding + m.header_height + 6 end

local function geometry()
    local n = game.n
    local avail = w - 40
    local s = avail / ((n - 1) * 1.5 * 1.732 + 1.732)
    local dx, dy = s * 1.732, s * 1.5
    local boardH = (n - 1) * dy + 2 * s
    local top = contentTop() + 70
    local bottom = h - m.button_hints_height - 50
    local y0 = top + math.max(0, (bottom - top - boardH) // 2) + s
    return { s = s, dx = dx, dy = dy, x0 = 20 + dx / 2, y0 = y0, n = n }
end

local function centre(G, c, r)
    return math.floor(G.x0 + (c - 1) * G.dx + (r - 1) * G.dx / 2 + 0.5), math.floor(G.y0 + (r - 1) * G.dy + 0.5)
end

local function menuButtonX() return w - 96 end

-- Drawing --------------------------------------------------------------------

local HEX_COS = { 0, 0.866, 0.866, 0, -0.866, -0.866 }
local HEX_SIN = { -1, -0.5, 0.5, 1, 0.5, -0.5 }

local function hexagon(cx, cy, s, thick)
    for k = 1, 6 do
        local k2 = k % 6 + 1
        smudge.line(math.floor(cx + HEX_COS[k] * s + 0.5), math.floor(cy + HEX_SIN[k] * s + 0.5),
                    math.floor(cx + HEX_COS[k2] * s + 0.5), math.floor(cy + HEX_SIN[k2] * s + 0.5), thick, true)
    end
end

local function statusText()
    if game.winner ~= 0 then
        if vsDevice() then return game.winner == human and "You win!" or "The device wins" end
        return colourName(game.winner) .. " wins!"
    end
    if thinking then return "Device is thinking..." end
    if vsDevice() then
        return string.format("Your move - you are %s (%s)", colourName(human),
                             human == X.BLACK and "top-bottom" or "left-right")
    end
    return colourName(game.turn) .. " to move"
end

local function drawBoard(G)
    local n, s = G.n, G.s
    -- Edges: Black's (top, bottom) solid, White's (left, right) dotted.
    local ax, ay = centre(G, 1, 1)
    local bx, by = centre(G, n, 1)
    local cx, cy = centre(G, 1, n)
    local dxp, dyp = centre(G, n, n)
    local off = math.floor(s * 1.4)
    smudge.line(ax - off // 2, ay - off, bx + off // 2, by - off, 5, true)
    smudge.line(cx - off // 2, cy + off, dxp + off // 2, dyp + off, 5, true)
    for k = 0, 20 do
        local t = k / 20
        if k % 2 == 0 then
            local lx = ax - off + (cx - ax) * t
            local ly = ay + (cy - ay) * t
            smudge.circle(math.floor(lx), math.floor(ly), 3, true, true)
            local rx = bx + off + (dxp - bx) * t
            local ry = by + (dyp - by) * t
            smudge.circle(math.floor(rx), math.floor(ry), 3, true, true)
        end
    end
    local stone = math.floor(s * 0.68)
    for r = 1, n do
        for c = 1, n do
            local x, y = centre(G, c, r)
            hexagon(x, y, s, 1)
            local v = game.cells[X.index(game, c, r)]
            if v == X.BLACK then
                smudge.circle(x, y, stone, true, true)
            elseif v == X.WHITE then
                smudge.circle(x, y, stone, true, false)
                smudge.circle(x, y, stone, false, 3)
            end
            if game.last == X.index(game, c, r) then
                smudge.circle(x, y, math.max(2, stone // 3), true, v ~= X.BLACK)
            end
        end
    end
    if not touch and game.winner == 0 and not thinking then
        local x, y = centre(G, curC, curR)
        hexagon(x, y, s - 2, 4)
    end
end

local function drawPlay()
    local n = game.n
    smudge.header("Hex", string.format("%s  %dx%d", vsDevice() and LEVELS[mode] or "2 players", n, n))
    local top = contentTop()
    local text = statusText()
    local maxW = (touch and menuButtonX() or w) - 24
    smudge.text(16, top + 4, text, smudge.text_width(text, "ui12", "bold") > maxW and "ui10" or "ui12", "bold", "left",
                true)
    if touch then
        smudge.rounded_rect(menuButtonX(), top - 2, 80, 34, 6, false, 2)
        smudge.text(menuButtonX() + 40, top + 4, "Menu", "ui12", "bold", "center", true)
    end
    local G = geometry()
    drawBoard(G)
    if game.winner ~= 0 then smudge.popup(text) end
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
    if game.winner == human then r[1] = r[1] + 1 else r[2] = r[2] + 1 end
    smudge.save("rec" .. mode, r[1] .. "," .. r[2])
end

local function place(i)
    if game.winner ~= 0 or thinking then return end
    if vsDevice() and game.turn ~= human then return end
    if not X.play(game, i) then return end
    finishIfOver()
    startThinking()
    smudge.request_update()
end

local function deviceMove()
    local i = X.chooseMove(game, mode, rand, smudge.millis, THINK_MS)
    X.play(game, i)
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
    size = tonumber(smudge.load("size", "2")) or 2
    if size < 1 or size > #SIZES then size = 2 end
    local savedMode, savedHuman, data = smudge.load("game", ""):match("^(%d)#(%d)#(.+)$")
    local g = data and X.deserialize(data)
    if g and g.winner == 0 then
        mode, human, game = tonumber(savedMode), tonumber(savedHuman), g
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
    local keep = game and game.winner == 0
    smudge.save("game", keep and (mode .. "#" .. human .. "#" .. X.serialize(game)) or "")
end

function on_button(btn, pressed)
    if not pressed then return end
    if menu then
        choose(menu.button(btn, menuItems()))
    elseif btn == "back" then
        openMenu()
    elseif game.winner ~= 0 then
        if btn == "confirm" then choose({ mode = mode }) end
    elseif btn == "confirm" then
        place(X.index(game, curC, curR))
    else
        local n = game.n
        if btn == "left" then curC = (curC - 2) % n + 1
        elseif btn == "right" then curC = curC % n + 1
        elseif btn == "up" or btn == "page_back" then curR = (curR - 2) % n + 1
        elseif btn == "down" or btn == "page_forward" then curR = curR % n + 1 end
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
    if game.winner ~= 0 then
        choose({ mode = mode })
        return
    end
    -- The nearest cell centre, if the tap is inside its hexagon (roughly).
    local G = geometry()
    local best, bestD = nil, G.s * G.s
    for r = 1, G.n do
        for c = 1, G.n do
            local cx, cy = centre(G, c, r)
            local d = (cx - x) * (cx - x) + (cy - y) * (cy - y)
            if d < bestD then best, bestD, curC, curR = X.index(game, c, r), d, c, r end
        end
    end
    if best then place(best) end
end
