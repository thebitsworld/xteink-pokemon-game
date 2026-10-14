-- Logic Puzzles for Xteink Pokemon (Lua app): Hashi, Lights Out and
-- Slitherlink, made fresh on the reader, each with a single solution.
--
-- Each puzzle has its rules in a file of its own (hashi.lua, lights.lua,
-- slither.lua), its screen in another (hashiui.lua, lightsui.lua,
-- slitherui.lua, with board.lua for what they share) and, for Hashi and
-- Slitherlink, a generator (hashigen.lua; slitherloop.lua then
-- slithergen.lua) loaded only while a puzzle is made. make.lua makes a
-- puzzle, start.lua reads the saves and opens a saved puzzle; the menus are
-- lgmenu.lua on top of the shared menu.lua. Only one puzzle's rules, one
-- screen and never a generator next to a screen are in memory at a time:
-- the X3 gives a Lua app about 75 KB. This file keeps the state and the flow.
--
-- Each kind keeps one puzzle in progress, saved when you leave it.

local S = {
    KINDS = {
        { id = "hashi", name = "Hashi", logic = "hashi.lua", ui = "hashiui.lua", gen = "hashigen.lua",
          blurb = "Join every island with bridges",
          levels = { { name = "Easy", size = 7, arg = true }, { name = "Normal", size = 9, arg = true },
                     { name = "Hard", size = 11, arg = false } } },
        { id = "lights", name = "Lights Out", logic = "lights.lua", ui = "lightsui.lua",
          blurb = "Switch every light off",
          levels = { { name = "Small", size = 5 }, { name = "Medium", size = 6 }, { name = "Large", size = 7 } } },
        { id = "slither", name = "Slitherlink", logic = "slither.lua", ui = "slitherui.lua", gen = "slithergen.lua",
          loop = "slitherloop.lua",
          blurb = "Draw one loop around the numbers",
          -- arg: clues that stay at least. 8x8 would not fit the X3's memory.
          levels = { { name = "Easy", size = 5, arg = 13 }, { name = "Normal", size = 7, arg = 24 },
                     { name = "Hard", size = 7, arg = 0 } } },
    },
    w = 480, h = 800, m = {}, touch = false,
    kind = nil,     -- index in KINDS of the puzzle in memory
    level = nil,
    K = nil,        -- that kind's rules module
    P = nil,        -- the puzzle
    optKind = 1,    -- the kind whose options are shown
    want = "main",  -- "main", "options", "pause", "play" or "gen"
    gen = nil,      -- { kind, level } of the puzzle to make
    note = nil,     -- a message under the board
    failed = nil,   -- a message on the options menu
    stats = {},     -- stats[kind][level] = { solved, bestMs }
    saved = {},     -- saved[kind] = { level, ms } of a saved puzzle in progress
    since = 0,      -- millis when the current stretch of play began
    -- Cursors, kept here so they outlive the screen modules.
    cr = 0, cc = 0, sel = nil, pen = 1,
}

local screen, screenName = nil, nil
local genDrawn = false

local function rand(n) return math.random(1, n) end

function S.resetCursor()
    S.cr, S.cc, S.sel, S.pen, S.note = 0, 0, nil, 1, nil
end

function S.formatTime(ms)
    local s = ms // 1000
    return string.format("%d:%02d", s // 60, s % 60)
end

function S.levelText(k, lv)
    local L = S.KINDS[k].levels[lv]
    return string.format("%s %dx%d", L.name, L.size, L.size)
end

-- Play time: elapsedMs holds finished stretches, S.since the running one.
function S.elapsed()
    local P = S.P
    if not P then return 0 end
    if P.solved or S.want ~= "play" then return P.elapsedMs end
    return P.elapsedMs + (smudge.millis() - S.since)
end

function S.show(name)
    if S.want == "play" and name ~= "play" and S.P and not S.P.solved then S.P.elapsedMs = S.elapsed() end
    if name == "play" and S.want ~= "play" then S.since = smudge.millis() end
    S.want = name
    smudge.request_update()
end

-- The puzzle in memory goes to its kind's save (or the save is cleared once solved).
function S.savePuzzle()
    local P = S.P
    if not P then return end
    if S.want == "play" and not P.solved then
        P.elapsedMs = S.elapsed()
        S.since = smudge.millis()
    end
    local key = "pz_" .. S.KINDS[S.kind].id
    if P.solved then
        smudge.save(key, "")
        S.saved[S.kind] = nil
    else
        smudge.save(key, S.level .. ";" .. S.K.serialize(P))
        S.saved[S.kind] = { S.level, P.elapsedMs }
    end
end

-- Drops the screen module (and with `all` the puzzle and its rules).
function S.drop(all)
    screen, screenName = nil, nil
    if all then S.P, S.K, S.kind = nil, nil, nil end
    collectgarbage("collect")
end

-- Opens kind k's saved puzzle (or the one in memory), in start.lua.
function S.continue(k) smudge.dofile("start.lua")(S).continue(k) end

-- A new puzzle of kind k at level lv: made by start.lua once "Making a
-- puzzle" is drawn.
function S.newPuzzle(k, lv)
    S.gen = { k, lv }
    S.failed = nil
    genDrawn = false
    S.show("gen")
end

-- Screens ---------------------------------------------------------------------

local function loadScreen()
    local name = S.want == "play" and "play" or S.want
    if screenName == name then return end
    S.drop(false)
    if name == "play" then
        screen = smudge.dofile(S.KINDS[S.kind].ui)(S, S.K)
    else
        screen = smudge.dofile("lgmenu.lua")(S)
    end
    screenName = name
end

function on_init()
    S.w, S.h = smudge.get_bounds()
    S.m = smudge.get_metrics()
    S.touch = smudge.has_touch()
    math.randomseed(smudge.millis() + smudge.time())
    smudge.dofile("start.lua")(S).init()
    S.show("main")
end

function on_draw()
    smudge.clear()
    if S.want == "gen" then
        S.drop(false)
        local info = S.KINDS[S.gen[1]]
        smudge.header(info.name, S.levelText(S.gen[1], S.gen[2]))
        smudge.centered_text(S.h // 2 - 20, "Making a puzzle...", "ui12", "bold", true)
        smudge.centered_text(S.h // 2 + 14, "It has a single solution.", "ui10", "regular", true)
        genDrawn = true
        return
    end
    loadScreen()
    screen.draw()
end

function on_update()
    if S.want == "gen" and genDrawn then smudge.dofile("make.lua")(S, rand).generate() end
end

function on_exit()
    S.savePuzzle()
end

function on_button(btn, pressed)
    if not pressed or S.want == "gen" then return end
    loadScreen()
    screen.button(btn)
end

function on_tap(x, y)
    if S.want == "gen" then return end
    loadScreen()
    screen.tap(x, y)
end
