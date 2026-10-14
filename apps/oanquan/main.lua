-- O An Quan (Vietnamese mandarin-square mancala) for Xteink Pokemon (Lua app).
-- The rules live in logic.lua, the computer in ai.lua (loaded only while it
-- thinks), the board screen in board.lua and the start menu in oqmenu.lua;
-- only one of those three is in memory at a time. This file holds the game's
-- state and its flow.
--
-- Buttons (X3/X4): Left/Right move the cursor over your squares, Confirm picks
-- the square up, then Left or Right sows it that way (Back puts it down).
-- Touch (X4 Pro): tap one of your squares, then one of the two arrows.

local C = smudge.dofile("logic.lua")

local S = {
    LEVELS = { { name = "Easy" }, { name = "Normal" }, { name = "Hard" } },
    w = 480, h = 800, m = {}, touch = false,
    mode = 1,          -- 1..3 against the device at that level, 4 two players
    s = nil,           -- the game (logic.lua); the player is side 1, the bottom row
    info = {},         -- what the last move did (C.play's info)
    lastMove = nil,    -- the last move and who played it
    lastSide = 0,
    prevText = nil,    -- one line on the move before
    cursor = 2,        -- a square of the player to move
    picked = nil,      -- the square picked up, waiting for a direction
    starter = 1,       -- who opens the next game
    thinking = false,
    records = {},
    want = "menu",
}

local screen, screenName = nil, nil
local thinkingDrawn = false
local NAMES = { "Bottom", "Top" }

local function rand(n) return math.random(1, n) end
function S.vsDevice() return S.mode <= #S.LEVELS end

function S.name(side)
    if S.vsDevice() then return side == 1 and "You" or "Device" end
    return NAMES[side]
end

-- Column letter of a stone square, A..E from the screen's left.
function S.letter(p)
    local c = p <= 6 and p - 1 or 13 - p
    return ("ABCDE"):sub(c, c)
end

-- Whether sowing square p in direction d goes right on the screen.
function S.goesRight(p, d) return (p <= 6) == (d > 0) end

-- The cursor on the first non-empty square of the player to move.
local function resetCursor()
    local s = S.s
    local f = C.first(s[C.SIDE])
    S.cursor = f + 2
    if s[S.cursor] == 0 then
        for p = f, f + 4 do
            if s[p] > 0 then
                S.cursor = p
                break
            end
        end
    end
    S.picked = nil
end

function S.refresh()
    resetCursor()
    S.thinking = S.vsDevice() and S.s[C.OVER] == 0 and S.s[C.SIDE] == 2
    thinkingDrawn = false
end

function S.show(name)
    S.want = name
    smudge.request_update()
end

local function saveGame()
    local keep = S.s and S.s[C.OVER] == 0
    smudge.save("game", keep and (S.mode .. "#" .. C.serialize(S.s)) or "")
    smudge.save("starter", tostring(S.starter))
end

function S.newGame(mode)
    S.mode = mode
    S.s = C.new(S.starter)
    S.starter = 3 - S.starter
    S.lastMove, S.lastSide, S.prevText = nil, 0, nil
    S.refresh()
    saveGame()
end

-- A short line on the last move, kept when the next one is played.
local function shortText()
    local i, m = S.info, S.lastMove
    local p = m // 2
    local t = S.name(S.lastSide) .. ": " .. S.letter(p) .. (S.goesRight(p, m % 2 == 1 and 1 or -1) and " right" or " left")
    if i.capD + i.capQ == 0 then return t .. ", no capture" end
    return t .. ", took " .. (i.capD + C.QUAN_VALUE * i.capQ)
end

function S.play(m)
    local s = S.s
    if S.lastMove then S.prevText = shortText() end
    S.lastSide, S.lastMove = s[C.SIDE], m
    C.play(s, m, S.info)
    if s[C.OVER] == 1 then
        if S.vsDevice() then
            local r = S.records[S.mode]
            local w = C.winner(s)
            local i = w == 0 and 3 or (w == 1 and 1 or 2)
            r[i] = r[i] + 1
            smudge.save("rec" .. S.mode, r[1] .. "," .. r[2] .. "," .. r[3])
        end
        saveGame()
    end
    S.refresh()
    smudge.request_update()
end

local function loadScreen(name)
    if screenName == name then return end
    screen, screenName = nil, nil
    collectgarbage("collect")
    screen = smudge.dofile(name == "menu" and "oqmenu.lua" or "board.lua")(C, S)
    screenName = name
end

-- The device's move: the board screen is dropped while the search runs.
local function deviceMove()
    screen, screenName = nil, nil
    collectgarbage("collect")
    local AI = smudge.dofile("ai.lua")(C)
    local m = AI.choose(S.s, S.mode, rand)
    AI = nil
    collectgarbage("collect")
    if m then S.play(m) else S.refresh() end
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
    S.starter = tonumber(smudge.load("starter", "1")) == 2 and 2 or 1
    local mode, data = smudge.load("game", ""):match("^(%d)#(.*)$")
    local s = data and C.deserialize(data)
    if s and s[C.OVER] == 0 then
        S.mode, S.s = tonumber(mode), s
        S.refresh()
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
