-- Mancala (Kalah rules) for Xteink Pokemon (Lua app).
-- The rules live in logic.lua, the computer in ai.lua (loaded only while it
-- thinks), the board screen in board.lua and the start menu in mkmenu.lua;
-- only one of those three is in memory at a time. This file holds the game's
-- state and its flow.
--
-- Buttons (X3/X4): Left/Right move the pointer over your non-empty pits,
-- Confirm sows that pit. Touch (X4 Pro): tap one of your pits.

local C = smudge.dofile("logic.lua")

local S = {
    LEVELS = { { name = "Easy" }, { name = "Normal" }, { name = "Hard" } },
    w = 480, h = 800, m = {}, touch = false,
    mode = 1,          -- 1..3 against the device at that level, 4 two players
    s = nil,           -- the game (logic.lua); you are player 1, the bottom row
    hist = "",         -- the moves so far, one character each (C.moveChar)
    g = { 1, 4, 0 },   -- this game's starter, seeds per pit, empty capture
    info = {},         -- what the last move did (C.play's info)
    lastSide = 0,      -- who played it (0: no move yet)
    prev = {},         -- short lines on the moves before, oldest first
    note = nil,        -- a line instead of the last move's summary
    cursor = 1,        -- a pit of the player to move
    first = 1,         -- next game vs device: 1 you open, 2 the device opens
    seeds = 4,         -- next game: seeds per pit
    ec = 0,            -- next game: empty capture on
    thinking = false,
    records = {},
    want = "menu",
}
local THINK_MS = 1500

local screen, screenName = nil, nil
local thinkingDrawn = false

local function rand(n) return math.random(1, n) end
function S.vsDevice() return S.mode <= #S.LEVELS end

function S.name(side)
    if S.vsDevice() then return side == 1 and "You" or "Device" end
    return side == 1 and "Bottom" or "Top"
end

-- Pit number on screen, 1..6 from the left in both rows.
function S.col(p) return p <= 6 and p or 14 - p end

-- The pointer on the player's pit nearest the store that has seeds.
local function resetCursor()
    local s = S.s
    local f = C.first(s[C.SIDE])
    S.cursor = f + 5
    for p = f + 5, f, -1 do
        if s[p] > 0 then
            S.cursor = p
            break
        end
    end
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
    smudge.save("game", keep and table.concat({ S.mode, S.g[1], S.g[2], S.g[3], S.hist }, "#") or "")
end
S.save = saveGame

local function saveOptions()
    smudge.save("opts", S.first .. "," .. S.seeds .. "," .. S.ec)
end
S.saveOptions = saveOptions

function S.newGame(mode)
    S.mode = mode
    local starter = (mode <= #S.LEVELS) and S.first or 1
    S.g = { starter, S.seeds, S.ec }
    S.s = C.new(starter, S.seeds, S.ec == 1)
    S.hist, S.lastSide, S.prev, S.note = "", 0, {}, nil
    S.refresh()
    saveGame()
end

-- A short line on the last move, kept when the next ones are played.
local function shortText()
    local i = S.info
    local t = S.name(S.lastSide) .. ": pit " .. S.col(i.from)
    if i.cap > 0 then return t .. ", captured " .. i.cap end
    if i.extra then return t .. ", extra turn" end
    return t
end

function S.play(p)
    local s = S.s
    if S.lastSide > 0 then
        local prev = S.prev
        prev[#prev + 1] = shortText()
        if #prev > 2 then table.remove(prev, 1) end
    end
    S.lastSide, S.note = s[C.SIDE], nil
    C.play(s, p, S.info)
    S.hist = S.hist .. C.moveChar(p)
    if s[C.OVER] == 1 and S.vsDevice() then
        local r = S.records[S.mode]
        local w = C.winner(s)
        local i = w == 0 and 3 or (w == 1 and 1 or 2)
        r[i] = r[i] + 1
        smudge.save("rec" .. S.mode, r[1] .. "," .. r[2] .. "," .. r[3])
    end
    saveGame()
    S.refresh()
    smudge.request_update()
end

local function loadScreen(name)
    if screenName == name then return end
    screen, screenName = nil, nil
    collectgarbage("collect")
    screen = smudge.dofile(name == "menu" and "mkmenu.lua" or "board.lua")(C, S)
    screenName = name
end

-- The device's move: the board screen is dropped while the search runs.
local function deviceMove()
    screen, screenName = nil, nil
    collectgarbage("collect")
    local AI = smudge.dofile("ai.lua")(C)
    local m = AI.choose(S.s, S.mode, rand, smudge.millis, THINK_MS)
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
    local f, n, e = smudge.load("opts", "1,4,0"):match("^(%d),(%d),(%d)$")
    S.first = f == "2" and 2 or 1
    n = tonumber(n) or 4
    S.seeds = (n >= 3 and n <= 6) and n or 4
    S.ec = e == "1" and 1 or 0
    -- A saved game is its options and moves, replayed.
    local mode, st, sd, ec, hist = smudge.load("game", ""):match("^([1-4])#([12])#([3-6])#([01])#(%u*)$")
    local s = mode and C.replay(tonumber(st), tonumber(sd), ec == "1", hist, nil, S.info)
    if s and s[C.OVER] == 0 then
        S.mode, S.g, S.hist, S.s = tonumber(mode), { tonumber(st), tonumber(sd), tonumber(ec) }, hist, s
        local n = #hist
        S.lastSide = n == 0 and 0 or (hist:byte(n) - 64 <= 6 and 1 or 2)
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
