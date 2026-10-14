-- Nine Men's Morris for Xteink Pokemon (Lua app).
-- The rules the search needs live in logic.lua, the rest of a game (set-up,
-- real moves, results, history) in game.lua and the computer in ai.lua. The
-- board screen is two modules, draw.lua and input.lua, and the start menu is
-- mmenu.lua. Only one screen module - or the computer while it thinks, or
-- game.lua for a real move - is in memory at a time. This file holds the
-- game's state and its flow.
--
-- Buttons (X3/X4): the arrows move the cursor over the points, Confirm places
-- a piece, picks one up and puts it down, or takes a piece after a mill; Back
-- drops the piece or opens the menu. Touch (X4 Pro): tap the points.

local C = smudge.dofile("logic.lua")

-- State shared with the screen modules.
local S = {
    LEVELS = {
        { name = "Easy", depth = 1, random = 25, nodes = 2000 },
        { name = "Normal", depth = 3, random = 0, nodes = 5000 },
        { name = "Hard", depth = 12, random = 0, nodes = 15000 },
    },
    w = 480, h = 800, m = {}, touch = false,
    mode = 2,          -- 1..3 against the device at that level, 4 two players
    human = C.WHITE,   -- your colour in this game against the device
    side = C.WHITE,    -- your colour in the next new game (the menu's choice)
    flying = true,     -- flying with three pieces in the next new game
    game = nil,
    legal = nil,       -- legal moves of the side to move
    picked = nil,      -- the point of the piece picked up
    pending = nil,     -- the point moved to, waiting for the piece a mill takes
    cur = 17,          -- the cursor's point
    thinking = false,
    deviceMoved = false,
    note = nil,
    records = {},
    takeBack = nil,    -- moves the menu asked to take back, done between screens
    move = nil,        -- your move, chosen on the board and played between screens
    restart = false,   -- a new game asked for, started between screens
    want = "menu",     -- the screen to show: "menu" or "board"
}
local THINK_MS = 1500

local screen, screenName = nil, nil
local thinkingDrawn = false

local function rand(n) return math.random(1, n) end
function S.vsDevice() return S.mode <= #S.LEVELS end

-- game.lua, loaded for the moment it is needed: callers keep it in a local.
function S.rules() return smudge.dofile("game.lua")(C) end

function S.show(name)
    S.want = name
    if name == "menu" then S.thinking = false end
    smudge.request_update()
end

function S.refresh()
    S.legal = C.legalMoves(S.game, S.legal)
    S.picked, S.pending = nil, nil
    S.thinking = S.vsDevice() and not S.game.result and S.game.side ~= S.human
    thinkingDrawn = false
end

-- Loads a screen module ("menu", "draw" or "input"), dropping the other first.
local function loadScreen(name)
    if screenName == name and not S.restart and not S.takeBack and not S.move then return end
    screen, screenName = nil, nil
    collectgarbage("collect")
    if S.restart then
        -- Asked for by the menu or the board; the old game goes first.
        S.restart, S.game, S.legal = nil, nil, nil
        collectgarbage("collect")
        S.game = S.rules().new(S.flying)
        S.note, S.deviceMoved = nil, false
        S.refresh()
        collectgarbage("collect")
    elseif S.takeBack then
        -- Asked for by the menu.
        local back = S.rules().takeBack(S.game, S.takeBack)
        S.takeBack = nil
        if back then S.game = back end
        S.note, S.deviceMoved = "Move taken back", false
        S.refresh()
        collectgarbage("collect")
    elseif S.move then
        -- Chosen on the board.
        local mv = S.move
        S.move = nil
        S.play(mv)
        collectgarbage("collect")
    end
    screen = smudge.dofile(name == "menu" and "mmenu.lua" or (name .. ".lua"))(C, S)
    screenName = name
end

-- A new game in `mode`: started once the current screen module is dropped.
function S.newGame(mode)
    S.mode, S.human, S.restart = mode, S.side, true
end

function S.play(mv)
    local game = S.game
    S.rules().play(game, mv)
    S.note, S.deviceMoved = nil, false
    if game.result and S.vsDevice() then
        local r = S.records[S.mode]
        local i = 3
        if game.result ~= "draw" then i = ((game.result == "white") == (S.human == C.WHITE)) and 1 or 2 end
        r[i] = r[i] + 1
        smudge.save("rec" .. S.mode, r[1] .. "," .. r[2] .. "," .. r[3])
    end
    S.refresh()
    smudge.request_update()
end

-- Your move: played once the board's input module is dropped.
function S.queue(mv)
    S.move = mv
    smudge.request_update()
end

-- The computer's move: the screen and the legal-move list are dropped to
-- give the search its room.
local function deviceMove()
    screen, screenName, S.legal = nil, nil, nil
    collectgarbage("collect")
    local AI = smudge.dofile("ai.lua")(C)
    local mv = AI.choose(S.game, S.LEVELS[S.mode], rand, smudge.millis, THINK_MS)
    AI = nil
    collectgarbage("collect")
    if mv then
        S.play(mv)
        S.deviceMoved = true -- the board shows its move in the status line
    else
        S.refresh()
    end
end

-- Callbacks ------------------------------------------------------------------

function on_init()
    S.w, S.h = smudge.get_bounds()
    S.m = smudge.get_metrics()
    S.touch = smudge.has_touch()
    math.randomseed(smudge.millis() + smudge.time())
    for i = 1, #S.LEVELS do
        local a, b, c = (smudge.load("rec" .. i, "0,0,0")):match("(%d+),(%d+),(%d+)")
        S.records[i] = { tonumber(a) or 0, tonumber(b) or 0, tonumber(c) or 0 }
    end
    S.side = tonumber(smudge.load("side", "1")) == C.BLACK and C.BLACK or C.WHITE
    S.flying = smudge.load("flying", "1") ~= "0"
    S.human = S.side
    local savedMode, savedHuman, fly, history = smudge.load("game", ""):match("^(%d)#(%d)#(%d)#(.*)$")
    if history then
        local g = S.rules().replay(history, fly == "1")
        if g and not g.result then
            S.mode, S.human, S.game = tonumber(savedMode), tonumber(savedHuman), g
            S.refresh()
            S.thinking = false
        end
    end
    S.show("menu")
    on_init = nil -- called once: its code can go
end

function on_draw()
    smudge.clear()
    loadScreen(S.want == "menu" and "menu" or "draw")
    screen.draw()
    if S.thinking then thinkingDrawn = true end
end

function on_update()
    if S.want == "board" and S.thinking and thinkingDrawn then deviceMove() end
end

function on_exit()
    local g = S.game
    local keep = g and not g.result
    smudge.save("game", keep and (S.mode .. "#" .. S.human .. "#" .. (g.flying and 1 or 0) .. "#" .. g.history) or "")
end

function on_button(btn, pressed)
    if not pressed or S.thinking then return end
    loadScreen(S.want == "menu" and "menu" or "input")
    screen.button(btn)
end

function on_tap(x, y)
    if S.thinking then return end
    loadScreen(S.want == "menu" and "menu" or "input")
    screen.tap(x, y)
end
