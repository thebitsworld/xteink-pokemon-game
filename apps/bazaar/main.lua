-- Bazaar for Xteink Pokemon (Lua app): a two-player trading card game
-- against the device. Rules live in logic.lua and the computer trader in
-- ai.lua; the screens are modules of their own - view.lua (the table:
-- drawing and input) and bazaarmenu.lua (the start menu) - and saving is in
-- save.lua. Only one screen, or the computer while it trades, is in memory at
-- a time: all of it at once does not fit the X3. This file holds the state
-- and the flow of the game.
--
-- Pick cards, then a button: select one market good and Take to take it, a
-- camel and Take for all the camels, two or more goods plus as many of your
-- cards (or camels) and Take to exchange, or a kind in your hand and Sell.
-- Tapping a kind in your hand selects all of it; each further tap selects one
-- fewer. Buttons (X3/X4): Up/Down change row, Left/Right move along it,
-- Confirm selects or presses. Touch (X4 Pro): tap.

local B = smudge.dofile("logic.lua")

-- State shared with the screen modules.
local S = {
    LEVELS = { "Easy", "Normal" },
    w = 480, h = 800, m = {}, touch = false,
    level = 2,
    game = nil,
    selMarket = {},     -- market slot -> true
    selHand = {},       -- good -> count selected
    selCamels = 0,
    row = 1, col = 1,   -- cursor: row 1 market, 2 hand, 3 buttons
    note = nil,
    thinking = false,
    stats = {},         -- per level: { matches won, played }
    want = "menu",      -- the screen to show: "menu" or "table"
}
local STEP_MS = 900

local screen, screenName = nil, nil
local thinkingDrawn, stepDue = false, nil

local function rand(n) return math.random(1, n) end

function S.clearSelection()
    S.selMarket, S.selHand, S.selCamels = {}, {}, 0
end

local function startThinking()
    S.thinking = S.game ~= nil and S.game.phase == "play" and S.game.turn == 2
    thinkingDrawn = false
end

function S.show(name)
    S.want = name
    if name == "menu" then S.thinking = false else startThinking() end
    smudge.request_update()
end

local function loadScreen()
    if screenName == S.want then return end
    screen, screenName = nil, nil
    collectgarbage("collect")
    screen = smudge.dofile(S.want == "menu" and "bazaarmenu.lua" or "view.lua")(B, S)
    screenName = S.want
end

function S.newGame(level)
    S.level = level
    S.game = B.new(rand)
    S.clearSelection()
    S.note = nil
    S.row, S.col = 1, 1
end

local function finishIfOver()
    if S.game.phase ~= "over" then return end
    local s = S.stats[S.level]
    s[2] = s[2] + 1
    if S.game.winner == 1 then s[1] = s[1] + 1 end
    smudge.save("st" .. S.level, s[1] .. "," .. s[2])
end

-- Plays your move, or says why it is not allowed.
function S.act(mv)
    local why = B.refusal(S.game, mv)
    if why then
        S.note = why
        smudge.request_update()
        return
    end
    B.play(S.game, mv, rand)
    S.note = nil
    S.clearSelection()
    finishIfOver()
    startThinking()
    smudge.request_update()
end

function S.continueAfterRound()
    if S.game.phase == "over" then
        S.newGame(S.level)
    else
        B.nextRound(S.game, rand)
        S.clearSelection()
        S.note = nil
    end
    startThinking()
    smudge.request_update()
end

-- The computer's turn: the screen is dropped to give it room.
local function deviceTurn()
    screen, screenName = nil, nil
    collectgarbage("collect")
    local AI = smudge.dofile("ai.lua")(B)
    local mv = AI.choose(S.game, S.level, rand)
    AI = nil
    collectgarbage("collect")
    if mv then B.play(S.game, mv, rand) end
    S.note = nil
    finishIfOver()
    startThinking()
    smudge.request_update()
end

-- Callbacks ------------------------------------------------------------------

function on_init()
    S.w, S.h = smudge.get_bounds()
    S.m = smudge.get_metrics()
    S.touch = smudge.has_touch()
    math.randomseed(smudge.millis() + smudge.time())
    for i = 1, #S.LEVELS do
        local a, b = (smudge.load("st" .. i, "0,0")):match("(%d+),(%d+)")
        S.stats[i] = { tonumber(a) or 0, tonumber(b) or 0 }
    end
    local savedLevel, data = smudge.load("game", ""):match("^(%d)#(.+)$")
    if data then
        local g = smudge.dofile("save.lua")(B).deserialize(data)
        if g then S.level, S.game = tonumber(savedLevel), g end
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
    if S.want == "table" and S.thinking and thinkingDrawn and stepDue and smudge.millis() >= stepDue then
        stepDue = nil
        deviceTurn()
    end
end

function on_exit()
    screen, screenName = nil, nil
    collectgarbage("collect")
    local keep = S.game and S.game.phase == "play"
    smudge.save("game", keep and (S.level .. "#" .. smudge.dofile("save.lua")(B).serialize(S.game)) or "")
end

function on_button(btn, pressed)
    if not pressed or S.thinking then return end
    loadScreen()
    screen.button(btn)
end

function on_tap(x, y)
    if S.thinking then return end
    loadScreen()
    screen.tap(x, y)
end
