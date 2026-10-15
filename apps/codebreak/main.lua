-- Code Breaker for Xteink Pokemon (Lua app): find a hidden code of symbols
-- from black and white peg scores, or let the device find yours.
-- The rules are logic.lua and the device's guessing ai.lua (loaded only while
-- it thinks). The board screen (drawing and input) is board.lua, the start
-- menu cmenu.lua and the options opts.lua (both on the shared menu.lua).
-- Only one screen - or the device while it thinks - is in memory at a time:
-- this file keeps the state and the flow.
--
-- Buttons (X3/X4): Left/Right move along the positions to the Submit button,
-- Up/Down change the symbol, Confirm submits, Back opens the menu.
-- Touch (X4 Pro): tap a position, then a symbol; tap Submit; Menu at the top.

local L = smudge.dofile("logic.lua")
local THINK_MS = 1500

-- State shared with the screen modules.
local S = {
    w = 480, h = 800, m = {}, touch = false,
    game = nil,
    -- Options: the custom game's size (and the device game's), the guesses
    -- every game allows, and whether the app scores the device's guesses.
    n = 4, k = 6, rep = true, max = 10, auto = true,
    stats = {},      -- by key (see statKey): { games, wins, guesses, best }
    row = {},        -- the guess (or secret) being entered: symbols, 0 = empty
    cur = 1,         -- the cursor: a position, or n + 1 = the Submit button
    sb = 0, sw = 0,  -- your score of the device's guess (black, white)
    field = 1,       -- the score field under the cursor: 1 black, 2 white, 3 Submit
    left = nil,      -- codes that fit the scores, after the device's guess
    note = nil,
    thinking = false,
    want = "cmenu",  -- the screen to show: "cmenu", "opts" or "board"
}

local screen, screenName = nil, nil
local thinkingDrawn = false

local function rand(n) return math.random(1, n) end

-- The size of preset p (4: the options').
function S.size(p)
    local P = L.PRESETS[p]
    if P then return P.n, P.k, P.rep end
    return S.n, S.k, S.rep
end

function S.statKey(mode, n, k, rep)
    return (mode == "you" and "y" or "d") .. n .. k .. (rep and "r" or "u")
end

function S.stat(key)
    local s = S.stats[key]
    if not s then
        local a, b, c, d = smudge.load("st_" .. key, "0,0,0,0"):match("(%d+),(%d+),(%d+),(%d+)")
        s = { tonumber(a) or 0, tonumber(b) or 0, tonumber(c) or 0, tonumber(d) or 0 }
        S.stats[key] = s
    end
    return s
end

function S.saveOptions()
    smudge.save("opts", table.concat({ S.n, S.k, S.rep and 1 or 0, S.max, S.auto and 1 or 0 }, ","))
end

function S.saveGame()
    local g = S.game
    smudge.save("game", (g and not g.result) and L.encode(g) or "")
end

function S.clearRow()
    for i = 1, 5 do S.row[i] = 0 end
    S.cur = 1
end

-- What the board's bottom part is for now.
function S.phase()
    local g = S.game
    if g.result then return "over" end
    if S.thinking then return "thinking" end
    if g.mode == "you" then return "edit" end
    if g.auto and g.secret == "" then return "secret" end
    if g.pending then return g.auto and "auto" or "score" end
    return "thinking"
end

-- Whether the device has to guess now.
local function startThinking()
    local g = S.game
    S.thinking = g and g.mode == "dev" and not g.result and not g.pending and
                     not (g.auto and g.secret == "") or false
    thinkingDrawn = false
end

function S.openMenu()
    S.thinking = false
    S.saveGame()
    S.want = "cmenu"
    smudge.request_update()
end

function S.showBoard()
    S.want = "board"
    startThinking()
    smudge.request_update()
end

function S.newGame(mode, preset)
    local n, k, rep = S.size(preset)
    local g = L.new(mode, preset, n, k, rep, S.max, S.auto)
    if mode == "you" then g.secret = L.randomCode(n, k, rep, rand) end
    S.game, S.note, S.left = g, nil, nil
    S.sb, S.sw, S.field = 0, 0, 1
    S.clearRow()
    S.saveGame()
    S.showBoard()
end

-- The device's guess. The screen is dropped to give it room.
local function deviceTurn()
    screen, screenName = nil, nil
    collectgarbage("collect")
    local A = smudge.dofile("ai.lua")(L)
    local g = S.game
    local t0 = smudge.millis()
    local code, left = A.choose(g, rand, smudge.millis, THINK_MS)
    smudge.log(string.format("codebreak: guess %d in %d ms, %d scores, %s codes left", L.count(g) + 1,
                             smudge.millis() - t0, A.work, tostring(left)))
    A = nil
    collectgarbage("collect")
    if code then
        g.pending, S.left = code, left
    else
        -- No code fits the scores: the last one goes back to be scored again.
        g.pending, S.left = L.drop(g), nil
        S.note = "No code fits these scores! Check this one (or undo more in the menu)"
    end
    S.sb, S.sw, S.field = 0, 0, 1
    S.thinking = false
    S.saveGame()
    smudge.request_update()
end

-- Callbacks --------------------------------------------------------------------

function on_init()
    S.w, S.h = smudge.get_bounds()
    S.m = smudge.get_metrics()
    S.touch = smudge.has_touch()
    math.randomseed(smudge.millis() + smudge.time())
    local n, k, r, m, a = smudge.load("opts", ""):match("^(%d),(%d),(%d),(%d+),(%d)$")
    if n then
        S.n, S.k, S.rep, S.max, S.auto = tonumber(n), tonumber(k), r == "1", tonumber(m), a == "1"
        if S.n ~= 5 then S.n = 4 end
        if S.k ~= 8 then S.k = 6 end
        if S.max ~= 8 and S.max ~= 12 then S.max = 10 end
    end
    S.game = L.decode(smudge.load("game", ""))
    if S.game and S.game.result then S.game = nil end
    S.clearRow()
    S.openMenu()
    on_init = nil -- called once: its code can go
end

local function load(name)
    if screenName ~= name then
        screen, screenName = nil, nil
        collectgarbage("collect")
        screen = smudge.dofile(name .. ".lua")(L, S)
        screenName = name
    end
    return screen
end

local function board(name) return S.want == "board" and name or S.want end

function on_draw()
    smudge.clear()
    load(board("draw")).draw()
    if S.thinking then thinkingDrawn = true end
end

function on_update()
    if S.want == "board" and S.thinking and thinkingDrawn then deviceTurn() end
end

function on_exit()
    S.saveGame()
end

function on_button(btn, pressed)
    if not pressed or S.thinking then return end
    load(board("input")).button(btn)
end

function on_tap(x, y)
    if S.thinking then return end
    load(board("input")).tap(x, y)
end
