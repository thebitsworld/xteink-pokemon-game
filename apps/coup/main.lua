-- Co Up (cờ úp, hidden xiangqi) for Xteink Pokemon (Lua app). Xiangqi where
-- only the generals start face up: the other pieces are shuffled face down
-- onto the starting points and turn up when they first move.
-- The search's rules live in logic.lua, the rest of a game (set-up, real
-- moves, results, history) in game.lua and the computer in ai.lua. The board
-- screen is two modules, draw.lua and input.lua, and the start menu is
-- cumenu.lua. Only one screen module - or the computer while it thinks - is
-- in memory at a time, and game.lua only for a real move: all of it at once
-- does not fit the X3. This file holds the game's state and its flow.
--
-- Buttons (X3/X4): the arrows move the cursor over the intersections,
-- Confirm picks a piece and then its point, Back drops the piece or opens
-- the menu. Touch (X4 Pro): tap the piece, then the point.

local C = smudge.dofile("logic.lua")

-- State shared with the screen modules.
local S = {
    LEVELS = {
        { name = "Easy", depth = 1, random = 25 },
        { name = "Normal", depth = 2, random = 0 },
        { name = "Hard", depth = 3, random = 0 },
    },
    w = 480, h = 800, m = {}, touch = false,
    mode = 2,          -- 1..3 against the device at that level, 4 two players
    human = C.RED,     -- your side in this game against the device
    side = C.RED,      -- your side in the next new game (the menu's choice)
    game = nil,
    legal = nil,       -- legal moves of the side to move
    picked = nil,      -- the point of the piece picked up
    curF = 5, curR = 1,
    thinking = false,
    deviceMoved = false,
    note = nil,
    records = {},
    takeBack = nil,    -- moves the menu asked to take back, done between screens
    restart = false,   -- a new game asked for, started between screens
    want = "menu",     -- the screen to show: "menu" or "board"
}
local THINK_MS = 4000

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

-- Loads a screen module ("menu", "draw" or "input"), dropping the other first.
local function loadScreen(name)
    if screenName == name and not S.restart and not S.takeBack then return end
    screen, screenName = nil, nil
    collectgarbage("collect")
    if S.restart then
        -- Asked for by the menu or the board; the old game goes first.
        S.restart, S.game, S.legal = nil, nil, nil
        collectgarbage("collect")
        local G = S.rules()
        S.game = G.new(G.shuffle(rand))
        G = nil
        S.note, S.deviceMoved = nil, false
        S.curF, S.curR = 5, (S.vsDevice() and S.human == C.BLACK) and 10 or 1
        S.refresh()
        collectgarbage("collect")
    elseif S.takeBack then
        -- Asked for by the menu. The game is replayed from its deal; the
        -- old one goes first (two games at once are too much for the X3).
        local old = S.game
        local deal, history, count = old.deal, old.history, C.moveCount(old) - S.takeBack
        S.takeBack, S.game, S.legal, old = nil, nil, nil, nil
        collectgarbage("collect")
        S.game = S.rules().replay(deal, history, count) or S.rules().replay(deal, history)
        S.note, S.deviceMoved = "Move taken back", false
        S.refresh()
        collectgarbage("collect")
    end
    screen = smudge.dofile(name == "menu" and "cumenu.lua" or (name .. ".lua"))(C, S)
    screenName = name
end

function S.refresh()
    S.legal = C.legalMoves(S.game, S.legal)
    S.picked = nil
    S.thinking = S.vsDevice() and not S.game.result and S.game.side ~= S.human
    thinkingDrawn = false
end

-- A new game in `mode`: started once the current screen module is dropped.
function S.newGame(mode)
    S.mode, S.human, S.restart = mode, S.side, true
end

-- Plays a real move. game.lua, the board's screen module and the legal-move
-- list do not fit in memory together on the X3, so the screen is dropped
-- first: a move from input.lua waits for the next on_draw (S.play).
local function playNow(mv)
    screen, screenName, S.legal = nil, nil, nil
    collectgarbage("collect")
    local game = S.game
    S.rules().play(game, mv)
    collectgarbage("collect") -- game.lua goes before the legal moves are listed
    S.note, S.deviceMoved = nil, false
    if game.result and S.vsDevice() then
        local r = S.records[S.mode]
        local i = 3
        if game.result ~= "draw" then i = ((game.result == "red") == (S.human == C.RED)) and 1 or 2 end
        r[i] = r[i] + 1
        smudge.save("rec" .. S.mode, r[1] .. "," .. r[2] .. "," .. r[3])
    end
    S.refresh()
end

function S.play(mv)
    S.pending = mv
    smudge.request_update()
end

-- The computer's move: the screen and the legal-move list are dropped to
-- give the search its room.
local function deviceMove()
    screen, screenName, S.legal = nil, nil, nil
    collectgarbage("collect")
    local AI = smudge.dofile("ai.lua")(C)
    local L = S.LEVELS[S.mode]
    -- What the face-down pieces are is put out of the computer's reach.
    local game, secret = S.game, S.game.secret
    game.secret = nil
    local mv = AI.choose(game, L.depth, L.random, rand, smudge.millis, THINK_MS)
    game.secret = secret
    AI = nil
    collectgarbage("collect")
    if mv then
        playNow(mv)
        smudge.request_update()
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
    S.side = tonumber(smudge.load("side", "0")) == C.BLACK and C.BLACK or C.RED
    S.human = S.side
    local savedMode, savedHuman, deal, history = smudge.load("game", ""):match("^(%d)#(%d)#(%w+)#(.*)$")
    if history then
        local g = S.rules().replay(deal, history)
        if g and not g.result then
            S.mode, S.human, S.game = tonumber(savedMode), tonumber(savedHuman), g
            S.refresh()
            S.thinking = false
        end
    end
    S.show("menu")
end

function on_draw()
    if S.pending then
        local mv = S.pending
        S.pending = nil
        playNow(mv)
    end
    smudge.clear()
    loadScreen(S.want == "menu" and "menu" or "draw")
    screen.draw()
    if S.thinking then thinkingDrawn = true end
end

function on_update()
    if S.want == "board" and S.thinking and thinkingDrawn then deviceMove() end
end

function on_exit()
    local keep = S.game and not S.game.result
    smudge.save("game", keep and (S.mode .. "#" .. S.human .. "#" .. S.game.deal .. "#" .. S.game.history) or "")
end

function on_button(btn, pressed)
    if not pressed or S.thinking or S.pending then return end
    loadScreen(S.want == "menu" and "menu" or "input")
    screen.button(btn)
end

function on_tap(x, y)
    if S.thinking or S.pending then return end
    loadScreen(S.want == "menu" and "menu" or "input")
    screen.tap(x, y)
end
