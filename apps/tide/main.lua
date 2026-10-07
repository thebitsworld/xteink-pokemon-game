-- Tide & Paper for Xteink Pokemon (Lua app): a set-collecting card game
-- against the device. Rules live in logic.lua, dealing in setup.lua and the
-- device player in ai.lua; the screens are modules of their own - view.lua
-- and input.lua (the table, drawn and played; layout.lua is theirs),
-- result.lua (between rounds) and tmenu.lua (the start menu) -
-- and saving is in save.lua. Only one screen, or the device player while it
-- plays, is in memory at a time: all of it at once does not fit the X3.
-- This file holds the state and the flow of the game.
--
-- Buttons (X3/X4): Up/Down move between the table, your hand and the
-- buttons, Left/Right along them, Confirm picks. Touch (X4 Pro): tap.

local T = smudge.dofile("logic.lua")

-- State shared with the screens.
local S = {
    LEVELS = { "Easy", "Normal" },
    w = 480, h = 800, m = {}, touch = false,
    level = 2,
    game = nil,
    sel = {},           -- selected hand positions (up to two)
    keepIdx = nil,      -- the drawn card chosen to keep
    row = 1, col = 1,   -- cursor: row 1 table, 2 hand, 3 buttons
    thinking = false,
    deviceLog = nil,    -- what the device did last turn
    stats = {},         -- per level: { games won, played }
    want = "menu",      -- "menu" or "table" (the table shows result.lua between rounds)
}
local STEP_MS = 900

local screen, screenName = nil, nil
local thinkingDrawn, stepDue = false, nil

local function rand(n) return math.random(1, n) end
S.rand = rand

local function startThinking()
    local g = S.game
    S.thinking = S.want == "table" and g ~= nil and g.turn == 2 and (g.phase == "draw" or g.phase == "play")
    thinkingDrawn = false
end

-- The screen module to use: for the table, view.lua draws and input.lua takes
-- buttons and taps, loaded in turn so the two halves are never in memory together.
local function wanted(forInput)
    if S.want == "menu" then return "tmenu.lua" end
    local ph = S.game.phase
    if ph == "round" or ph == "over" then return "result.lua" end
    return forInput and "input.lua" or "view.lua"
end

local function loadScreen(forInput)
    local name = wanted(forInput)
    if screenName == name then return end
    screen, screenName = nil, nil
    collectgarbage("collect")
    screen = smudge.dofile(name)(name == "tmenu.lua" and S or T, S)
    screenName = name
end

local function dropScreen()
    screen, screenName = nil, nil
    collectgarbage("collect")
end

function S.show(name)
    S.want = name
    startThinking()
    smudge.request_update()
end

local function finishIfOver()
    local g = S.game
    if g.phase ~= "over" or g.counted then return end
    g.counted = true
    local s = S.stats[S.level]
    s[2] = s[2] + 1
    if g.winner == 1 then s[1] = s[1] + 1 end
    smudge.save("st" .. S.level, s[1] .. "," .. s[2])
end

function S.after()
    S.sel, S.keepIdx = {}, nil
    finishIfOver()
    startThinking()
    smudge.request_update()
end

-- Dealing: the screen is dropped while setup.lua deals.
local function deal(newGame)
    dropScreen()
    local Setup = smudge.dofile("setup.lua")(T)
    if newGame then S.game = Setup.new(rand) else Setup.nextRound(S.game, rand) end
    Setup = nil
    collectgarbage("collect")
    S.deviceLog = nil
    S.row, S.col = 1, 1
    S.after()
end

function S.newGame(level)
    S.level = level
    S.want = "table"
    deal(true)
end

function S.continue()
    deal(S.game.phase == "over")
end

-- The device's turn: the screen is dropped to give it room.
local function deviceTurn()
    dropScreen()
    local AI = smudge.dofile("ai.lua")(T)
    S.deviceLog = AI.describe(AI.turn(S.game, S.level, rand))
    AI = nil
    collectgarbage("collect")
    S.row, S.col = 1, 1
    S.after()
end

-- Callbacks ---------------------------------------------------------------------

function on_init()
    S.w, S.h = smudge.get_bounds()
    S.m = smudge.get_metrics()
    S.touch = smudge.has_touch()
    math.randomseed(smudge.millis() + smudge.time())
    for i = 1, #S.LEVELS do
        local a, b = (smudge.load("st" .. i, "0,0")):match("(%d+),(%d+)")
        S.stats[i] = { tonumber(a) or 0, tonumber(b) or 0 }
    end
    local level, data = smudge.load("game", ""):match("^(%d)#(.+)$")
    if data then
        local g = smudge.dofile("save.lua")(T).deserialize(data)
        if g then S.level, S.game = tonumber(level), g end
        collectgarbage("collect")
    end
    S.show("menu")
end

function on_draw()
    smudge.clear()
    loadScreen()
    screen.draw()
    if S.thinking and not thinkingDrawn then
        thinkingDrawn = true
        stepDue = smudge.millis() + STEP_MS
    end
end

function on_update()
    if S.thinking and thinkingDrawn and stepDue and smudge.millis() >= stepDue then
        stepDue = nil
        deviceTurn()
    end
end

function on_exit()
    dropScreen()
    local g = S.game
    if g and g.phase == "round" then
        -- A finished round is already scored: save the next one (if it is the
        -- device's turn, it plays when the game is resumed).
        smudge.dofile("setup.lua")(T).nextRound(g, rand)
        collectgarbage("collect")
    end
    local keep = g and g.phase ~= "over"
    smudge.save("game", keep and (S.level .. "#" .. smudge.dofile("save.lua")(T).serialize(g)) or "")
end

function on_button(btn, pressed)
    if not pressed or S.thinking then return end
    loadScreen(true)
    screen.button(btn)
end

function on_tap(x, y)
    if S.thinking then return end
    loadScreen(true)
    screen.tap(x, y)
end
