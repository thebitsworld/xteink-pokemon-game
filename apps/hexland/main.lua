-- Hexland for Xteink Pokemon (Lua app): a hex-island trading and building
-- game, you against three device players. The island, names and points live
-- in logic.lua, the moves in rules.lua (loaded only to make one), a new game
-- is dealt by setup.lua and the device players are ai.lua. The board screen
-- is drawn by board.lua (the island) and panel.lua (players, log, hand,
-- buttons and pop-ups) and played through input.lua; ui.lua works out what
-- you can do now (cursor spots, pop-up items) and the start menu is
-- hxmenu.lua. One screen module - or the device players while they play -
-- is in memory at a time. Saving is in save.lua. This file holds the state
-- and the flow of the game.
--
-- X4 Pro only (manifest "devices"): four players' state, the full rules and
-- the device players need more than the X3's 75 KB of Lua heap - its
-- simulator runs out at the first turn.
--
-- Buttons: Left/Right move along the buttons, the spots on the board or a
-- pop-up's items, Up/Down jump a row of spots, Confirm picks, Back cancels
-- or opens the menu. Touch: tap a button, a corner, an edge or a hex, a
-- pop-up item; Menu at the top right.

local L = smudge.dofile("logic.lua")
local floor, byte = math.floor, string.byte

-- State shared with the screen modules.
local S = {
    LEVELS = { "Normal", "Hard" },
    w = 480, h = 800, m = {}, touch = false,
    level = 1, starter = false,
    game = nil,
    ui = nil,       -- what you can do now (ui.lua), rebuilt after each change
    pop = nil,      -- a pop-up you opened: "build", "trade", "dev", ...
    place = nil,    -- a build you are placing: L.ROAD, L.SETTLE or L.CITY
    tr = {},        -- choices so far in a trade or card pop-up
    bar = 1,        -- the button under the cursor
    cur = nil,      -- the spot under the cursor
    req = nil,      -- your trade offer, answered by the device players
    records = {},   -- per level: { won, played, sum of places }
    thinking = false,
    want = "menu",  -- "menu" or "board"
}
local STEP_MS = 900

local screen, screenName = nil, nil
local thinkingDrawn, stepDue = false, nil

local function dropScreen()
    screen, screenName = nil, nil
    collectgarbage("collect")
end

local function loadScreen(name)
    if screenName == name then return end
    dropScreen()
    screen = smudge.dofile(name .. ".lua")(L, S)
    screenName = name
end

-- Layout: the island on top, then players, log, your hand and the buttons.
local function layout()
    local w, h = S.w, S.h
    local Y = { top = S.m.top_padding or 0, lh = smudge.line_height("small") }
    Y.barH = 44
    Y.barY = h - (S.m.button_hints_height or 40) - 8 - Y.barH
    Y.handY = Y.barY - 66
    Y.logY = Y.handY - 6 - 5 * Y.lh
    Y.plY = Y.logY - 64
    Y.bTop = Y.top + 28
    local avail = Y.plY - 4 - Y.bTop
    local r = math.min((w - 96) / (5 * 1.732), (avail - 80) / 8)
    Y.r, Y.ux, Y.uy = r, r * 0.866, r / 2
    Y.ox, Y.oy = (w - 10 * Y.ux) / 2, Y.bTop + (avail - 16 * Y.uy) / 2
    S.lay = Y
end

-- Pixel positions of a corner, a hex centre and an edge's middle.
function S.vxy(v)
    local Y = S.lay
    return floor(Y.ox + byte(L.VX, v) * Y.ux + 0.5), floor(Y.oy + byte(L.VY, v) * Y.uy + 0.5)
end
function S.hxy(h)
    local Y = S.lay
    return floor(Y.ox + byte(L.HX, h) * Y.ux + 0.5), floor(Y.oy + byte(L.HY, h) * Y.uy + 0.5)
end
function S.exy(e)
    local a, b = L.ends(e)
    local ax, ay = S.vxy(a)
    local bx, by = S.vxy(b)
    return (ax + bx) // 2, (ay + by) // 2
end

-- The five buttons (Roll, Build, Trade, Cards, End) and the pop-up box.
function S.barRect(i)
    local bw = (S.w - 16) // 5
    return 8 + (i - 1) * bw + 2, S.lay.barY, bw - 4, S.lay.barH
end
-- Returns x, y, width, height, the first row's y and the row height.
function S.popRect(n, sub)
    local Y = S.lay
    local head = sub and 58 or 40
    local rowH = math.min(38, (Y.plY - 8 - Y.bTop - head - 8) // n)
    local y = Y.bTop + 4
    return 16, y, S.w - 32, head + n * rowH + 8, y + head, rowH
end

local function save()
    if not S.game then return end
    local keep = S.game.phase ~= "over"
    smudge.save("game", keep and smudge.dofile("save.lua")(L).serialize(S.game) or "")
end

local function finishIfOver()
    local g = S.game
    if g.phase ~= "over" or g.counted then return end
    g.counted = true
    local r = S.records[g.level]
    r[2], r[3] = r[2] + 1, r[3] + L.place(g, 1)
    if g.winner == 1 then r[1] = r[1] + 1 end
    smudge.save("rec" .. g.level, r[1] .. "," .. r[2] .. "," .. r[3])
end

-- After any move: the pop-ups and cursor are worked out again.
function S.changed()
    S.ui = nil
    local g = S.game
    if g then
        finishIfOver()
        S.thinking = S.want == "board" and g.phase ~= "over" and L.actor(g) ~= g.human
    end
    thinkingDrawn = false
    smudge.request_update()
end

function S.show(name)
    S.want = name
    S.pop, S.place = nil, nil
    S.changed()
end

function S.newGame(level)
    dropScreen()
    S.game = nil
    collectgarbage("collect")
    S.level = level
    S.game = smudge.dofile("setup.lua")(L).new(level, S.starter, 1)
    collectgarbage("collect")
    S.pop, S.place, S.cur, S.bar = nil, nil, nil, 1
    S.want = "board"
    S.changed()
end

local function ensureUI()
    if S.ui then return end
    dropScreen()
    S.ui = smudge.dofile("ui.lua")(L, S)
    collectgarbage("collect")
end

-- The device players: one of them plays its whole turn (or until you must
-- answer); the screen is dropped to give them room.
local function deviceStep()
    dropScreen()
    S.ui = nil
    local g = S.game
    local R = smudge.dofile("rules.lua")(L)
    local A = smudge.dofile("ai.lua")(R)
    local p, turn, n = L.actor(g), g.turn, 0
    repeat
        A.act(g)
        n = n + 1
    until g.phase == "over" or L.actor(g) ~= p or g.turn ~= turn or n > 80
    A, R = nil, nil
    collectgarbage("collect")
    if g.turn == g.human and g.phase == "roll" then save() end
    S.changed()
end

-- Your offer to one device player (or all): the first that likes it takes it.
local function answerOffer()
    local q, g = S.req, S.game
    S.req = nil
    dropScreen()
    local R = smudge.dofile("rules.lua")(L)
    local A = smudge.dofile("ai.lua")(R)
    local what = q.gn .. " " .. L.SH[q.give] .. " for 1 " .. L.SH[q.get]
    local done = false
    for p = 2, 4 do
        if (q.who == 0 or q.who == p) and not done and A.accept(g, p, q.give, q.gn, q.get, 1) then
            R.swap(g, 1, p, q.give, q.gn, q.get, 1)
            L.log(g, L.NAMES[p] .. " takes your " .. what)
            done = true
        end
    end
    if not done then L.log(g, (q.who == 0 and "No one" or L.NAMES[q.who]) .. " takes your " .. what) end
    A, R = nil, nil
    collectgarbage("collect")
    S.changed()
end

-- Callbacks ------------------------------------------------------------------

function on_init()
    S.w, S.h = smudge.get_bounds()
    S.m = smudge.get_metrics()
    S.touch = smudge.has_touch()
    math.randomseed(smudge.millis() + smudge.time())
    layout()
    for i = 1, #S.LEVELS do
        local a, b, c = (smudge.load("rec" .. i, "0,0,0")):match("(%d+),(%d+),(%d+)")
        S.records[i] = { tonumber(a) or 0, tonumber(b) or 0, tonumber(c) or 0 }
    end
    S.starter = smudge.load("board", "0") == "1"
    local data = smudge.load("game", "")
    if data ~= "" then
        local g = smudge.dofile("save.lua")(L).deserialize(data)
        if g and g.phase ~= "over" then S.game, S.level = g, g.level end
        collectgarbage("collect")
    end
    S.show("menu")
end

function on_draw()
    smudge.clear()
    if S.want == "menu" then
        loadScreen("hxmenu")
        screen.draw()
        return
    end
    ensureUI()
    loadScreen("board")
    screen.draw()
    loadScreen("panel")
    screen.draw()
    if S.thinking and not thinkingDrawn then
        thinkingDrawn = true
        stepDue = smudge.millis() + STEP_MS
    end
end

function on_update()
    if S.want ~= "board" then return end
    if S.req then
        answerOffer()
    elseif S.thinking and thinkingDrawn and stepDue and smudge.millis() >= stepDue then
        stepDue = nil
        deviceStep()
    end
end

function on_exit()
    dropScreen()
    S.ui = nil
    collectgarbage("collect")
    save()
end

function on_button(btn, pressed)
    if not pressed then return end
    if S.want == "menu" then
        loadScreen("hxmenu")
        screen.button(btn)
        return
    end
    if S.thinking then
        if btn == "back" then S.show("menu") end
        return
    end
    ensureUI()
    loadScreen("input")
    screen.button(btn)
end

function on_tap(x, y)
    if S.want == "menu" then
        loadScreen("hxmenu")
        screen.tap(x, y)
        return
    end
    if S.thinking then
        if y < S.lay.bTop and x > S.w - 90 then S.show("menu") end
        return
    end
    ensureUI()
    loadScreen("input")
    screen.tap(x, y)
end
