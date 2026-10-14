-- Dots and Boxes for Xteink Pokemon (Lua app).
-- The rules a move needs live in logic.lua, the rest of them (making a game,
-- taking back, geometry) in game.lua and the device's player in ai.lua. The
-- board screen (drawing and input) is board.lua and the start menu dmenu.lua
-- (on the shared menu.lua). Only one screen - or the device while it thinks
-- - is in memory at a time: this file keeps just the state and the flow.
--
-- Buttons (X3/X4): the arrows move the cursor between free lines (each
-- press goes to the nearest one that way), Confirm draws it, Back opens the
-- menu. Touch (X4 Pro): tap a line between two dots; Menu at the top right.
-- Against the device you are player 1 (solid boxes); who starts alternates.

local D = smudge.dofile("logic.lua")
local THINK_MS = 1500

-- State shared with the screen modules.
local S = {
    LEVELS = { "Easy", "Normal", "Hard" },
    w = 480, h = 800, m = {}, touch = false,
    mode = 2,       -- 1..3 against the device at that level, 4 two players
    game = nil,
    cursor = 1,     -- the line under the cursor
    drift = 1,      -- which way the cursor last stepped sideways
    thinking = false,
    note = nil,
    SIZES = { 3, 4, 5, 6 },
    size = 2,       -- index in SIZES: the next game's board
    nextFirst = 1,  -- who draws first in the next game
    records = nil,  -- per level: won, lost, drawn (loaded by dmenu.lua)
    want = "dmenu", -- the screen to show, loaded once the callback that
                    -- asked for it has returned (its module is then free)
}

local screen, screenName = nil, nil
local thinkingDrawn = false

local function rand(n) return math.random(1, n) end
function S.vsDevice() return S.mode <= #S.LEVELS end

function S.saveGame()
    local g = S.game
    smudge.save("game", (g and not D.over(g)) and (S.mode .. "#" .. D.encode(g)) or "")
end

-- Loads the screen module `name` ("dmenu" or "board"), dropping the other.
local function load(name)
    if screenName ~= name then
        screen, screenName = nil, nil
        collectgarbage("collect")
        screen = smudge.dofile(name .. ".lua")(D, S)
        screenName = name
    end
    return screen
end

local function startThinking()
    S.thinking = S.vsDevice() and not D.over(S.game) and S.game.turn == 2
    thinkingDrawn = false
end

function S.openMenu()
    S.thinking = false
    S.saveGame()
    S.want = "dmenu"
    smudge.request_update()
end

function S.showBoard()
    S.want = "board"
    startThinking()
    smudge.request_update()
end

-- After a line: records and the saved game once it is over, then the
-- device's turn if it is its move.
function S.moved()
    local g = S.game
    if D.over(g) then
        if S.vsDevice() then
            local r, win = S.records[S.mode], D.winner(g)
            local i = win == 1 and 1 or (win == 2 and 2 or 3)
            r[i] = r[i] + 1
            smudge.save("rec" .. S.mode, r[1] .. "," .. r[2] .. "," .. r[3])
        end
        S.saveGame()
    end
    startThinking()
    smudge.request_update()
end

-- The device's whole turn: lines until it closes no box. The board screen is
-- dropped to give the search its room.
local function deviceTurn()
    screen, screenName = nil, nil
    collectgarbage("collect")
    -- The search makes short-lived values: collect them sooner while it runs.
    collectgarbage("incremental", 100, 400)
    local AI = smudge.dofile("ai.lua")(D)
    collectgarbage("collect")
    local g = S.game
    local taken = 0
    while not D.over(g) and g.turn == 2 do
        local l = AI.choose(g, S.mode, rand, smudge.millis, THINK_MS)
        if not l then break end
        taken = taken + D.play(g, l)
    end
    AI = nil
    collectgarbage("collect")
    collectgarbage("incremental", 200, 100)
    if taken > 0 and not D.over(g) then
        S.note = taken == 1 and "The device took a box" or ("The device took " .. taken .. " boxes")
    else
        S.note = nil
    end
    S.moved()
    if not D.over(g) then S.saveGame() end
end

-- Callbacks -----------------------------------------------------------------------

function on_init()
    S.w, S.h = smudge.get_bounds()
    S.m = smudge.get_metrics()
    S.touch = smudge.has_touch()
    math.randomseed(smudge.millis() + smudge.time())
    S.openMenu()
end

function on_draw()
    smudge.clear()
    load(S.want).draw()
    if S.thinking then thinkingDrawn = true end
end

function on_update()
    if S.want == "board" and S.thinking and thinkingDrawn then deviceTurn() end
end

function on_exit()
    S.saveGame()
end

function on_button(btn, pressed)
    if not pressed or (S.thinking and btn ~= "back") then return end
    load(S.want).button(btn)
end

function on_tap(x, y)
    if S.thinking then return end
    load(S.want).tap(x, y)
end
