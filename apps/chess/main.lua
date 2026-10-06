-- Chess for Xteink Pokemon (Lua app).
-- Rules live in logic.lua and the computer opponent in ai.lua. The screens
-- are modules of their own - view.lua (the board: drawing and input) and
-- chessmenu.lua (the start menu) - and only one of them, or the computer
-- while it thinks, is in memory at a time: all of it at once does not fit
-- the X3. This file holds the game's state and its flow.
--
-- Buttons (X3/X4): the arrows move the cursor, Confirm picks a piece and
-- then its square, Back drops the piece or opens the menu. Touch (X4 Pro):
-- tap the piece, then the square. A pawn reaching the last rank asks which
-- piece it becomes.

local C = smudge.dofile("logic.lua")

-- State shared with the screen modules.
local S = {
    LEVELS = {
        { name = "Easy", depth = 1, random = 25 },
        { name = "Normal", depth = 2, random = 0 },
        { name = "Hard", depth = 4, random = 0 },
    },
    PROMOTIONS = { C.QUEEN, C.ROOK, C.BISHOP, C.KNIGHT },
    w = 480, h = 800, m = {}, touch = false,
    mode = 2,          -- 1..3 against the device at that level, 4 two players
    human = C.WHITE,   -- your side against the device
    game = nil,
    legal = {},        -- legal moves of the side to move
    picked = nil,      -- the square of the piece picked up
    promo = nil,       -- { from, to, choice } while choosing a promotion
    curF = 5, curR = 2,
    thinking = false,
    deviceMoved = false,
    note = nil,
    records = {},
    want = "menu",     -- the screen to show: "menu" or "board"
}
local THINK_MS = 3000

local screen, screenName = nil, nil
local thinkingDrawn = false

local function rand(n) return math.random(1, n) end
function S.vsDevice() return S.mode <= #S.LEVELS end

-- Shows a screen, loading its module (and dropping the other) when needed.
function S.show(name)
    S.want = name
    if name == "menu" then S.thinking = false end
    smudge.request_update()
end

local function loadScreen()
    if screenName == S.want then return end
    screen, screenName = nil, nil
    collectgarbage("collect")
    screen = smudge.dofile(S.want == "menu" and "chessmenu.lua" or "view.lua")(C, S)
    screenName = S.want
end

function S.refresh()
    S.legal = C.legalMoves(S.game)
    S.picked, S.promo = nil, nil
    S.thinking = S.vsDevice() and not S.game.result and S.game.side ~= S.human
    thinkingDrawn = false
end

function S.newGame(mode)
    S.mode = mode
    S.game = C.new()
    S.note, S.deviceMoved = nil, false
    S.curF, S.curR = 5, (S.vsDevice() and S.human == C.BLACK) and 7 or 2
    S.refresh()
end

local function finishIfOver()
    local game = S.game
    if not game.result or not S.vsDevice() then return end
    local r = S.records[S.mode]
    local i = 3
    if game.result ~= "draw" then i = ((game.result == "white") == (S.human == C.WHITE)) and 1 or 2 end
    r[i] = r[i] + 1
    smudge.save("rec" .. S.mode, string.format("%d,%d,%d", r[1], r[2], r[3]))
end

function S.play(mv)
    C.play(S.game, mv)
    S.note, S.deviceMoved = nil, false
    finishIfOver()
    S.refresh()
    smudge.request_update()
end

-- The computer's move: the screen is dropped to give the search its room.
local function deviceMove()
    screen, screenName = nil, nil
    collectgarbage("collect")
    local AI = smudge.dofile("ai.lua")(C)
    local L = S.LEVELS[S.mode]
    local mv = AI.choose(S.game, L.depth, L.random, rand, smudge.millis, THINK_MS)
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
    S.human = tonumber(smudge.load("side", "0")) == C.BLACK and C.BLACK or C.WHITE
    local savedMode, savedHuman, history = smudge.load("game", ""):match("^(%d)#(%d)#(.*)$")
    if history then
        local g = (history == "") and C.new() or smudge.dofile("history.lua")(C).replay(history)
        if g and not g.result then
            S.mode, S.human, S.game = tonumber(savedMode), tonumber(savedHuman), g
            S.refresh()
            S.thinking = false
        end
    end
    S.show("menu")
end

function on_draw()
    smudge.clear()
    loadScreen()
    screen.draw()
    if S.thinking then thinkingDrawn = true end
end

function on_update()
    if S.want == "board" and S.thinking and thinkingDrawn then deviceMove() end
end

function on_exit()
    local keep = S.game and not S.game.result
    smudge.save("game", keep and (S.mode .. "#" .. S.human .. "#" .. S.game.history) or "")
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
