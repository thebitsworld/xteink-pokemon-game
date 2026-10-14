-- Backgammon for Xteink Pokemon (Lua app). The rules live in logic.lua,
-- the turns and saving in game.lua and the device's player in ai.lua; they
-- are loaded only for a move. The board screen is two modules, draw.lua and
-- input.lua, and the start menu is bgmenu.lua; only one of them is in
-- memory at a time. This file holds the state and the flow.
--
-- You play White (outlined checkers), moving from 24 down to 1 and bearing
-- off at the bottom right; the device or a friend plays Black. The dice are
-- rolled for each turn by themselves.
--
-- Buttons (X3/X4): Left/Right move over the checkers that can move, Confirm
-- picks one, Left/Right then move over where it can go and Confirm moves
-- it; when the dice are used Confirm ends the turn. Back drops the picked
-- checker, takes back a step of this turn, or opens the menu. Touch (X4
-- Pro): tap a point (or the bar), then where it goes (the tray to bear
-- off); Undo and Done buttons.

-- State shared with the modules.
local S = {
    LEVELS = { "Easy", "Normal", "Hard" },
    w = 480, h = 800, m = {}, touch = false,
    mode = 2,          -- 1..3 against the device at that level, 4 two players
    game = nil,        -- see game.lua
    R = {}, nR = 0,    -- the dice left this turn
    left = 0,          -- how many of them must still be played
    sources = {},      -- points (own numbering) with a checker that can move
    cur = 1,           -- the cursor among them
    picked = nil,      -- the point picked up
    dt = {}, ds = {},  -- where the picked checker can go, and the steps for each
    dcur = 1,
    thinking = false,
    records = {},      -- per mode: { won, lost, points, points against }
    restart = false,   -- a new game asked for, started between screens
    act = nil, arg = nil, -- a move asked for by input.lua: game.lua's function
    want = "menu",
}
local THINK_MS = 1800

local screen, screenName = nil, nil
local thinkingDrawn = false

function S.vsDevice() return S.mode <= #S.LEVELS end
function S.humanTurn()
    local g = S.game
    return g and not g.result and (not S.vsDevice() or g.turn == 1)
end
-- A side's name: "You"/"Device" against the device, else "White"/"Black".
function S.name(side)
    if S.vsDevice() then return side == 1 and "You" or "Device" end
    return side == 1 and "White" or "Black"
end

-- game.lua with the rules, loaded for the callback that needs them and
-- dropped when it returns.
local gameMod = nil
function S.G()
    if not gameMod then gameMod = smudge.dofile("game.lua")(smudge.dofile("logic.lua"), S) end
    return gameMod
end

function S.show(name)
    S.want = name
    if name == "menu" then
        S.thinking = false
        if S.game then S.act = "save" end
    end
    smudge.request_update()
end

function S.newGame(mode) S.mode, S.restart = mode, true end

local function loadScreen(name)
    if screenName == name and not S.restart then return end
    screen, screenName, gameMod = nil, nil, nil
    collectgarbage("collect")
    if S.restart then
        S.restart, S.game = false, nil
        collectgarbage("collect")
        S.G().new()
        gameMod = nil
        collectgarbage("collect")
    end
    screen = smudge.dofile(name == "menu" and "bgmenu.lua" or (name .. ".lua"))(S)
    screenName = name
end

-- The device's turn: the screen goes first to give the search its room.
local function deviceMove()
    screen, screenName, gameMod = nil, nil, nil
    collectgarbage("collect")
    local g = S.game
    local C = smudge.dofile("logic.lua")
    local AI = smudge.dofile("ai.lua")(C)
    local mv = AI.choose(g.b[2], g.b[1], g.d1, g.d2, S.mode, function(n) return math.random(1, n) end, smudge.millis,
                         THINK_MS)
    AI = nil
    collectgarbage("collect")
    gameMod = smudge.dofile("game.lua")(C, S)
    gameMod.deviceMove(mv)
    gameMod = nil
    collectgarbage("collect")
    smudge.request_update()
end

-- Callbacks ------------------------------------------------------------------

function on_init()
    S.w, S.h = smudge.get_bounds()
    S.m = smudge.get_metrics()
    S.touch = smudge.has_touch()
    math.randomseed(smudge.millis() + smudge.time())
    for i = 1, #S.LEVELS + 1 do
        local r = {}
        for v in (smudge.load("rec" .. i, "") .. ",,,,"):gmatch("([^,]*),") do r[#r + 1] = tonumber(v) or 0 end
        S.records[i] = { r[1], r[2], r[3], r[4] }
    end
    local text = smudge.load("game", "")
    if text ~= "" then
        local G = S.G()
        local ok, mode, g = pcall(G.parse, text)
        if ok and g then
            S.mode, S.game = mode, g
            G.refresh()
        end
    end
    S.show("menu")
    S.act, gameMod = nil, nil
end

function on_draw()
    smudge.clear()
    loadScreen(S.want == "menu" and "menu" or "draw")
    screen.draw()
    thinkingDrawn = S.thinking
end

function on_update()
    gameMod = nil
    if S.want == "board" and S.thinking and (S.mode < 3 or thinkingDrawn) then deviceMove() end
end

function on_exit()
    if S.game then S.G().save() end
end

-- After your input: the move it asked for is played without the screen
-- module in memory; Easy and Normal answer at once, without a "thinking"
-- picture.
local function afterInput()
    if S.act then
        screen, screenName = nil, nil
        collectgarbage("collect")
        local act = S.act
        S.act = nil
        S.G()[act](S.arg)
    end
    gameMod = nil
    if S.want == "board" and S.thinking and S.mode < 3 then deviceMove() end
end

function on_button(btn, pressed)
    if not pressed or (S.thinking and S.want == "board") then return end
    loadScreen(S.want == "menu" and "menu" or "input")
    screen.button(btn)
    afterInput()
end

function on_tap(x, y)
    if S.thinking and S.want == "board" then return end
    loadScreen(S.want == "menu" and "menu" or "input")
    screen.tap(x, y)
    afterInput()
end
