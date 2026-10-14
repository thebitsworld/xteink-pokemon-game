-- Caro (Gomoku) for Xteink Pokemon (Lua app).
-- The rules live in logic.lua and the computer in ai.lua. The screens are
-- modules: the start menu cmenu.lua (on the shared menu.lua) and the board
-- board.lua (drawing and input). Only one screen - or the computer while it
-- thinks - is in memory at a time: all at once does not fit the X3. This
-- file holds the game's state and its flow.
--
-- Buttons (X3/X4): the arrows move the cursor (it starts on the last move),
-- Confirm places a mark, Back opens the menu. Touch (X4 Pro): tap a cell
-- (twice if the cells are small), Menu at the top right.

local C = smudge.dofile("logic.lua")
local N = C.N
local THINK_MS = 1800

-- State shared with the screen modules.
local S = {
    LEVELS = { "Easy", "Normal", "Hard" },
    w = 480, h = 800, m = {}, touch = false,
    mode = 2,          -- 1..3 against the device at that level, 4 two players
    human = C.X,       -- your mark against the device
    youFirst = true,   -- against the device: you play X (X always moves first)
    game = nil,
    curR = N // 2, curC = N // 2,
    pending = nil,     -- touch with small cells: the cell tapped once
    thinking = false,
    records = {},
    want = "menu",     -- the screen to show: "menu" or "board"
}
local screen, screenName = nil, nil
local thinkingDrawn = false

local function rand(n) return math.random(1, n) end
function S.vsDevice() return S.mode <= #S.LEVELS end

-- The cursor on the last move, or the centre.
function S.cursorToLast()
    local i = S.game and C.last(S.game)
    if i then S.curR, S.curC = C.rc(i) else S.curR, S.curC = N // 2, N // 2 end
    S.pending = nil
end

local function saveGame()
    local g = S.game
    local keep = g and g.winner == 0 and g.n > 0
    smudge.save("game", keep and (S.mode .. "#" .. S.human .. "#" .. C.encode(g)) or "")
end

local function startThinking()
    S.thinking = S.vsDevice() and S.game.winner == 0 and S.game.turn ~= S.human
    thinkingDrawn = false
end

-- Shows "menu" or "board" (loaded at the next draw or input, once the
-- current screen module is out of memory).
function S.show(name)
    if name == "menu" then
        S.thinking = false
        saveGame()
    else
        startThinking()
    end
    S.want = name
    smudge.request_update()
end

function S.newGame(mode)
    S.mode = mode
    S.game = C.new()
    S.human = (not S.vsDevice() or S.youFirst) and C.X or C.O
    S.cursorToLast()
    S.show("board")
end

local function loadScreen()
    if screenName == S.want then return end
    screen, screenName = nil, nil
    collectgarbage("collect")
    screen = smudge.dofile(S.want == "menu" and "cmenu.lua" or "board.lua")(C, S)
    screenName = S.want
end

local function finishIfOver()
    local game = S.game
    if game.winner == 0 or not S.vsDevice() then return end
    local r = S.records[S.mode]
    local k = game.winner == S.human and 1 or (game.winner > 0 and 2 or 3)
    r[k] = r[k] + 1
    smudge.save("rec" .. S.mode, r[1] .. "," .. r[2] .. "," .. r[3])
end

-- Your mark on cell i.
function S.place(i)
    local game = S.game
    S.pending = nil
    if game.winner ~= 0 or S.thinking then return end
    if S.vsDevice() and game.turn ~= S.human then return end
    if not C.play(game, i) then return end
    finishIfOver()
    startThinking()
    smudge.request_update()
end

-- The computer's move: the screen and the rules are dropped to give the
-- search its room.
local function deviceMove()
    screen, screenName, C = nil, nil, nil
    collectgarbage("collect")
    local AI = smudge.dofile("ai.lua")()
    local i = AI.choose(S.game, S.mode, rand, smudge.millis, THINK_MS)
    AI = nil
    collectgarbage("collect")
    C = smudge.dofile("logic.lua")
    C.play(S.game, i)
    S.thinking = false
    finishIfOver()
    S.cursorToLast()
    smudge.request_update()
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
    S.youFirst = smudge.load("first", "1") ~= "0"
    local savedMode, savedHuman, data = smudge.load("game", ""):match("^(%d)#(%d)#(.+)$")
    local g = data and C.decode(data)
    if g and g.winner == 0 then
        S.mode, S.human, S.game = tonumber(savedMode), tonumber(savedHuman), g
        if S.mode < 1 or S.mode > #S.LEVELS + 1 then S.mode = 2 end
        if S.human ~= C.X and S.human ~= C.O then S.human = C.X end
        S.cursorToLast()
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
    saveGame()
end

function on_button(btn, pressed)
    if not pressed then return end
    loadScreen()
    screen.button(btn)
end

function on_tap(x, y)
    loadScreen()
    screen.tap(x, y)
end
