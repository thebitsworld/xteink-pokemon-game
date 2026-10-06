-- Sea Battle for Xteink Pokemon (Lua app).
-- Rules and the computer's shots live in logic.lua and the start menu in
-- menu.lua (loaded while it is on screen); this file draws the game and
-- handles input.
--
-- Buttons (X3/X4): while placing, Left/Right shuffle your fleet and Confirm
-- starts; in battle the arrows aim and Confirm fires. Back opens the menu.
-- Touch (X4 Pro): Shuffle and Start buttons, then tap a square to fire.

local S = smudge.dofile("logic.lua")

local LEVELS = { "Easy", "Normal", "Hard" }
local COLS = "ABCDEFGHIJ"

local w, h = 480, 800
local m = {}
local touch = false

local menu = nil
local level = 2
local game = nil
local placing = false   -- choosing the fleet before the first shot
local curC, curR = 5, 5
local thinking, thinkingDrawn = false, false
local yourNote, deviceNote = nil, nil
local records = {}

local function rand(n) return math.random(1, n) end
local function squareName(i) return COLS:sub(S.colOf(i), S.colOf(i)) .. S.rowOf(i) end

local function newGame(lv)
    level = lv
    game = S.new(rand)
    placing, thinking = true, false
    yourNote, deviceNote = nil, nil
    curC, curR = 5, 5
end

local function menuItems()
    local items = {}
    if game and game.winner == 0 then items[1] = { label = "Resume", level = 0 } end
    for i, name in ipairs(LEVELS) do
        local r = records[i]
        items[#items + 1] = { label = "vs Device: " .. name, level = i, note = string.format("Won %d  Lost %d", r[1], r[2]) }
    end
    items[#items + 1] = { label = "Exit", level = -1 }
    return items
end

local function openMenu()
    thinking = false
    collectgarbage("collect")
    menu = smudge.dofile("menu.lua")("Sea Battle", { "Sink the hidden fleet before it sinks yours.",
                                                     "Ships never touch, not even at a corner." })
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
    thinking = not placing and game.winner == 0 and game.turn == 2
    thinkingDrawn = false
    smudge.request_update()
end

-- Layout ---------------------------------------------------------------------
-- Status (two lines), the big grid - the enemy's in battle, yours while
-- placing - with letters above and numbers to the left, then your own grid
-- small with the enemy fleet's state beside it.

local GUTTER = 26

local function contentTop() return m.top_padding + m.header_height + 6 end
local function contentBottom() return h - m.button_hints_height - 6 end

local function layout()
    local top = contentTop()
    local small = 15
    local bigTop = top + 50 + GUTTER
    local cell = math.min((w - 16 - GUTTER) // S.N, (contentBottom() - bigTop - 14 - S.N * small) // S.N)
    local bx = (w - GUTTER - S.N * cell) // 2 + GUTTER
    return { cell = cell, bx = bx, by = bigTop, small = small, sx = 16, sy = bigTop + S.N * cell + 14 }
end

local function menuButtonX() return w - 96 end

-- Drawing --------------------------------------------------------------------

-- One square of a grid. `mine` draws ship squares (your own fleet).
local function drawSquare(x, y, s, shot, ship)
    if ship then smudge.rect_dither(x + 1, y + 1, s - 1, s - 1, true) end
    if shot == S.MISS then
        smudge.circle(x + s // 2, y + s // 2, math.max(1, s // 10), true, true)
    elseif shot == S.HIT then
        local k = s // 5
        smudge.line(x + k, y + k, x + s - k, y + s - k, s >= 20 and 3 or 2, true)
        smudge.line(x + s - k, y + k, x + k, y + s - k, s >= 20 and 3 or 2, true)
    elseif shot == S.SUNK then
        smudge.rect(x + 1, y + 1, s - 1, s - 1, true, true)
    end
end

local function drawGrid(x, y, s, shots, fleet, labels)
    for i = 1, S.N * S.N do
        local c, r = S.colOf(i), S.rowOf(i)
        local ship = fleet and S.at(fleet.occ, i) ~= 0
        local shot = shots and S.at(shots, i) or S.UNKNOWN
        -- A hit on your own ship shows as a black square in the small grid.
        if fleet and shot == S.HIT and s < 20 then shot = S.SUNK end
        drawSquare(x + (c - 1) * s, y + (r - 1) * s, s, shot, ship)
    end
    for k = 0, S.N do
        smudge.line(x + k * s, y, x + k * s, y + S.N * s, 1, true)
        smudge.line(x, y + k * s, x + S.N * s, y + k * s, 1, true)
    end
    smudge.rect(x - 1, y - 1, S.N * s + 3, S.N * s + 3, false, true, 2)
    if labels then
        for k = 1, S.N do
            smudge.text(x + (k - 1) * s + s // 2, y - GUTTER + 2, COLS:sub(k, k), "ui10", "bold", "center", true)
            smudge.text(x - 5, y + (k - 1) * s + (s - 20) // 2, tostring(k), "ui10", "bold", "right", true)
        end
    end
end

local function drawPlay()
    smudge.header("Sea Battle", LEVELS[level])
    local top = contentTop()
    local L = layout()
    local line1, line2
    if placing then
        line1, line2 = "Your fleet", touch and "Shuffle until you like it, then Start" or "Left/Right: shuffle   Confirm: start"
    elseif game.winner ~= 0 then
        line1 = game.winner == 1 and "You sank the whole fleet!" or "Your fleet is sunk"
    else
        line1 = yourNote or "Fire at the enemy grid"
        line2 = thinking and "Device is aiming..." or deviceNote
    end
    smudge.text(16, top, line1, "ui12", "bold", "left", true)
    if line2 then smudge.text(16, top + 24, line2, "ui10", "regular", "left", true) end
    if touch then
        smudge.rounded_rect(menuButtonX(), top - 2, 80, 36, 6, false, 2)
        smudge.text(menuButtonX() + 40, top + 5, "Menu", "ui12", "bold", "center", true)
    end

    if placing then
        drawGrid(L.bx, L.by, L.cell, nil, game.fleets[1], true)
        if touch then
            local y = L.sy + 10
            for k, label in ipairs({ "Shuffle", "Start" }) do
                local x = (k == 1) and 24 or (w // 2 + 8)
                smudge.rounded_rect(x, y, w // 2 - 32, 56, 8, k == 2, k == 2 and true or 2)
                smudge.text(x + (w // 2 - 32) // 2, y + 16, label, "ui12", "bold", "center", k ~= 2)
            end
        end
    else
        drawGrid(L.bx, L.by, L.cell, game.shots[1], game.winner == 2 and game.fleets[2] or nil, true)
        if not touch and game.winner == 0 then
            local x, y = L.bx + (curC - 1) * L.cell, L.by + (curR - 1) * L.cell
            smudge.rect(x - 3, y - 3, L.cell + 7, L.cell + 7, false, false, 3)
            smudge.rect(x, y, L.cell + 1, L.cell + 1, false, true, 4)
        end
        -- Your fleet, small, and the enemy ships still afloat.
        drawGrid(L.sx, L.sy, L.small, game.shots[2], game.fleets[1], false)
        local tx = L.sx + S.N * L.small + 20
        smudge.text(tx, L.sy, "Enemy fleet", "ui10", "bold", "left", true)
        local afloat = S.afloat(game, 1)
        for n, name in ipairs(S.NAMES) do
            local y = L.sy + 4 + n * 24
            local text = string.format("%s (%d)", name, S.FLEET[n])
            smudge.text(tx, y, text, "ui10", "regular", "left", true)
            if not afloat[n] then
                smudge.line(tx, y + 11, tx + smudge.text_width(text, "ui10", "regular"), y + 11, 2, true)
            end
        end
    end
    if game.winner ~= 0 then smudge.popup(line1) end
    if touch then
        smudge.button_hints("", "", "", "")
    elseif game.winner ~= 0 then
        smudge.button_hints("Menu", "Again", "", "")
    elseif placing then
        smudge.button_hints("Menu", "Start", "Shuffle", "Shuffle")
    else
        smudge.button_hints("Menu", "Fire", "<", ">")
    end
end

-- Game flow ------------------------------------------------------------------

local function finishIfOver()
    if game.winner == 0 then return end
    local r = records[level]
    r[game.winner] = r[game.winner] + 1
    smudge.save("rec" .. level, r[1] .. "," .. r[2])
end

local function describe(who, i, result, ship)
    if result == "sunk" then return string.format("%s %s: sunk the %s!", who, squareName(i), S.NAMES[game.last.ship]) end
    return string.format("%s %s: %s", who, squareName(i), result == "hit" and "hit!" or "miss")
end

local function fire(i)
    if placing or thinking or game.winner ~= 0 or game.turn ~= 1 then return end
    local result = S.fire(game, i)
    if not result then return end
    yourNote = describe("You", i, result)
    finishIfOver()
    thinking, thinkingDrawn = game.winner == 0, false
    smudge.request_update()
end

local function deviceShot()
    local i = S.chooseShot(game, level, rand)
    local result = S.fire(game, i)
    deviceNote = describe("Device", i, result)
    thinking = false
    finishIfOver()
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
    local savedLevel, data = smudge.load("game", ""):match("^(%d)#(.+)$")
    local g = data and S.deserialize(data)
    if g then level, game = tonumber(savedLevel), g end
    openMenu()
end

function on_draw()
    smudge.clear()
    if menu then menu.draw(menuItems()) else drawPlay() end
    if thinking then thinkingDrawn = true end
end

function on_update()
    if not menu and thinking and thinkingDrawn then deviceShot() end
end

function on_exit()
    local keep = game and game.winner == 0 and not placing
    smudge.save("game", keep and (level .. "#" .. S.serialize(game)) or "")
end

function on_button(btn, pressed)
    if not pressed then return end
    if menu then
        choose(menu.button(btn, menuItems()))
    elseif btn == "back" then
        openMenu()
    elseif game.winner ~= 0 then
        if btn == "confirm" then choose({ level = level }) end
    elseif placing then
        if btn == "confirm" then
            placing = false
        else
            game.fleets[1] = S.randomFleet(rand)
        end
        smudge.request_update()
    elseif btn == "confirm" then
        fire(S.index(curC, curR))
    else
        if btn == "left" then curC = (curC - 2) % S.N + 1
        elseif btn == "right" then curC = curC % S.N + 1
        elseif btn == "up" or btn == "page_back" then curR = (curR - 2) % S.N + 1
        elseif btn == "down" or btn == "page_forward" then curR = curR % S.N + 1 end
        smudge.request_update()
    end
end

function on_tap(x, y)
    if menu then
        choose(menu.tap(x, y, menuItems()))
        return
    end
    if smudge.in_rect(x, y, menuButtonX(), contentTop() - 2, 80, 36) then
        openMenu()
        return
    end
    if game.winner ~= 0 then
        choose({ level = level })
        return
    end
    local L = layout()
    if placing then
        if y >= L.sy + 10 and y < L.sy + 66 then
            if x < w // 2 then game.fleets[1] = S.randomFleet(rand) else placing = false end
            smudge.request_update()
        end
        return
    end
    if smudge.in_rect(x, y, L.bx, L.by, S.N * L.cell, S.N * L.cell) then
        curC, curR = (x - L.bx) // L.cell + 1, (y - L.by) // L.cell + 1
        fire(S.index(curC, curR))
    end
end
