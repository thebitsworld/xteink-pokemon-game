-- Reversi for Xteink Pokemon (Lua app).
-- The rules and real games live in logic.lua and the computer in ai.lua,
-- loaded only while it thinks. The board screen (drawing and input) is
-- board.lua and the start menu rvmenu.lua; only one of them - or the
-- computer - is in memory at a time. This file holds the game's state and
-- its flow.
--
-- Buttons (X3/X4): with "Show moves" on, Left/Right jump the cursor to the
-- previous/next legal square and Up/Down to the nearest legal square in a
-- row above/below; with it off the arrows move freely over the board.
-- Confirm plays, Back opens the menu. Touch (X4 Pro): tap a square.

local L = smudge.dofile("logic.lua")

-- State shared with the screen modules.
local S = {
    LEVELS = { "Easy", "Normal", "Hard" },
    w = 480, h = 800, m = {}, touch = false,
    mode = 2,          -- 1..3 against the device at that level, 4 two players
    human = L.BLACK,   -- your colour against the device
    hints = true,      -- show the legal squares as dots
    game = nil,
    legal = {}, nlegal = 0, -- legal squares of the side to move
    cur = 44,          -- the cursor's square (buttons only)
    thinking = false,
    note = nil,        -- a short message in the status line
    info = nil,        -- the last move, under the board
    passInfo = nil,    -- a pass, under the board
    records = {},
    want = "menu",     -- the screen to show: "menu" or "board"
}
local THINK_MS = 2000

local screen, screenName = nil, nil
local thinkingDrawn = false

local function rand(n) return math.random(1, n) end
function S.vsDevice() return S.mode <= #S.LEVELS end

-- "You", "The device", "Black" or "White" for a side.
function S.who(side)
    if S.vsDevice() then return side == S.human and "You" or "The device" end
    return side == L.BLACK and "Black" or "White"
end

local function saveGame()
    local g = S.game
    local keep = g and not g.result and L.moveCount(g) > 0
    smudge.save("game", keep and (S.mode .. "#" .. S.human .. "#" .. g.history) or "")
end

function S.show(name)
    S.want = name
    if name == "menu" then
        S.thinking = false
        saveGame()
    end
    smudge.request_update()
end

-- Loads a screen module ("menu" or "board"), dropping the other first.
local function loadScreen(name)
    if screenName == name then return end
    screen, screenName = nil, nil
    collectgarbage("collect")
    screen = smudge.dofile(name == "menu" and "rvmenu.lua" or "board.lua")(L, S)
    screenName = name
end

-- The legal squares, whether the device is to move, and the cursor on the
-- legal square nearest to where it was.
function S.refresh()
    local g = S.game
    S.nlegal = g.result and 0 or L.moves(g.b, g.side, S.legal)
    S.thinking = S.vsDevice() and not g.result and g.side ~= S.human
    thinkingDrawn = false
    if S.hints and S.nlegal > 0 then
        local best, bestD = S.legal[1], 1000
        local r0, c0 = S.cur // 10, S.cur % 10
        for i = 1, S.nlegal do
            local sq = S.legal[i]
            local dr, dc = sq // 10 - r0, sq % 10 - c0
            local d = dr * dr + dc * dc
            if d < bestD then best, bestD = sq, d end
        end
        S.cur = best
    end
end

function S.newGame(mode)
    S.mode = mode
    S.game = L.new()
    S.note, S.info, S.passInfo = nil, nil, nil
    S.cur = 44
    S.refresh()
end

function S.play(sq)
    local g = S.game
    local mover, before = g.side, g.passed
    if not L.play(g, sq) then return false end
    S.note = nil
    S.info = S.who(mover) .. " played " .. L.name(sq)
    if g.passed then
        S.passInfo = S.who(g.passed) .. (S.who(g.passed) == "You" and " have" or " has") .. " no move and must pass"
    elseif before then
        S.passInfo = S.who(before) .. " had no move and passed"
    else
        S.passInfo = nil
    end
    if g.result then
        if S.vsDevice() then
            local r = S.records[S.mode]
            local i = 3
            if g.result ~= "draw" then i = ((g.result == "black") == (S.human == L.BLACK)) and 1 or 2 end
            r[i] = r[i] + 1
            smudge.save("rec" .. S.mode, r[1] .. "," .. r[2] .. "," .. r[3])
        end
        saveGame()
    end
    S.refresh()
    smudge.request_update()
    return true
end

-- Takes back to your last turn (one move with two players).
function S.backCount()
    local g = S.game
    if not g or g.result then return 0 end
    return L.backTo(g, S.vsDevice() and S.human or nil)
end

function S.takeBack()
    local n = S.backCount()
    if n == 0 then return end
    local g = L.takeBack(S.game, n)
    if g then S.game = g end
    S.note, S.info, S.passInfo = "Move taken back", nil, nil
    S.refresh()
end

-- The device's move: the screen is dropped to give the search its room.
local function deviceMove()
    screen, screenName = nil, nil
    collectgarbage("collect")
    local AI = smudge.dofile("ai.lua")(L)
    local sq = AI.choose(S.game, S.mode, rand, smudge.millis)
    AI = nil
    collectgarbage("collect")
    if not (sq and S.play(sq)) then S.refresh() end
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
    S.human = tonumber(smudge.load("side", "1")) == L.WHITE and L.WHITE or L.BLACK
    S.hints = smudge.load("hints", "1") ~= "0"
    local savedMode, savedHuman, history = smudge.load("game", ""):match("^(%d)#(%d)#(.*)$")
    if history then
        local g = L.replay(history)
        local mode, human = tonumber(savedMode), tonumber(savedHuman)
        if g and not g.result and mode >= 1 and mode <= 4 and (human == L.BLACK or human == L.WHITE) then
            S.mode, S.human, S.game = mode, human, g
            S.refresh()
            if g.last then S.info = "Last move: " .. L.name(g.last) end
        end
    end
    S.show("menu")
end

function on_draw()
    smudge.clear()
    loadScreen(S.want)
    screen.draw()
    if S.thinking then thinkingDrawn = true end
end

function on_update()
    if S.want == "board" and S.thinking and thinkingDrawn then deviceMove() end
end

function on_exit() saveGame() end

function on_button(btn, pressed)
    if not pressed or (S.thinking and S.want == "board") then return end
    loadScreen(S.want)
    screen.button(btn)
end

function on_tap(x, y)
    if S.thinking and S.want == "board" then return end
    loadScreen(S.want)
    screen.tap(x, y)
end
