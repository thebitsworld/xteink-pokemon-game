-- Logic Puzzles for Xteink Pokemon (Lua app): Hashi, Lights Out,
-- Slitherlink, Kakuro and Light Up, made fresh on the reader, each with a
-- single solution.
--
-- Each puzzle has its rules in a file of its own (hashi.lua, lights.lua,
-- slither.lua, kakuro.lua, akari.lua), its screen in another (hashiui.lua,
-- lightsui.lua, slitherui.lua, kakuroui.lua, akariui.lua, with board.lua
-- for what they share) and, except for Lights Out, a generator (hashigen.lua,
-- akarigen.lua; slitherloop.lua then slithergen.lua; kakurolay.lua then
-- kakurogen.lua) loaded only while a puzzle is made. make.lua makes a puzzle, start.lua reads the
-- saves and opens a saved puzzle; the menus are lgmenu.lua on top of the
-- shared menu.lua. Only one puzzle's rules, one screen and never a generator
-- next to a screen are in memory at a time: the X3 gives a Lua app about
-- 75 KB. This file keeps the state and the flow.
--
-- Each kind keeps one puzzle in progress, saved when you leave it.

local S = {
    -- Each kind's levels: "name size arg;..." (S.levelOf reads them; a string
    -- costs the X3 far less than a table per level). arg goes to the
    -- generator: 1 for "solved by reasoning alone" (none: guessing allowed),
    -- for Slitherlink the clues that stay at least.
    KINDS = {
        { id = "hashi", name = "Hashi", logic = "hashi.lua", ui = "hashiui.lua", gen = "hashigen.lua",
          blurb = "Join every island with bridges", levels = "Easy 7 1;Normal 9 1;Hard 11" },
        { id = "lights", name = "Lights Out", logic = "lights.lua", ui = "lightsui.lua",
          blurb = "Switch every light off", levels = "Small 5;Medium 6;Large 7" },
        -- 8x8 would not fit the X3's memory.
        { id = "slither", name = "Slitherlink", logic = "slither.lua", ui = "slitherui.lua", gen = "slithergen.lua",
          loop = "slitherloop.lua", blurb = "Draw one loop around the numbers", levels = "Easy 5 13;Normal 7 24;Hard 7 0" },
        { id = "kakuro", name = "Kakuro", logic = "kakuro.lua", ui = "kakuroui.lua", gen = "kakurogen.lua",
          loop = "kakurolay.lua", blurb = "Fill in digits that add up to the sums", levels = "Easy 6;Normal 8;Hard 10" },
        -- 12x12 would not fit the X3's memory.
        { id = "akari", name = "Light Up", logic = "akari.lua", ui = "akariui.lua", gen = "akarigen.lua",
          blurb = "Light every cell with bulbs", levels = "Easy 7 1;Normal 10 1;Hard 11" },
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
    stats = {},     -- solved, bestMs of each kind and level, flat: see S.stat
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

-- Where kind k's level lv keeps its stats: stats[i] solved, stats[i + 1]
-- the best time in ms (0: none).
function S.stat(k, lv) return (k - 1) * 6 + lv * 2 - 1 end

-- Level lv (1..3) of kind k: name, size, arg (or nil).
function S.levelOf(k, lv)
    local name, size, arg = S.KINDS[k].levels:match(("[^;]*;"):rep(lv - 1) .. "(%a+) (%d+) ?(%d*)")
    return name, tonumber(size), tonumber(arg)
end

function S.levelText(k, lv)
    local name, size = S.levelOf(k, lv)
    return string.format("%s %dx%d", name, size, size)
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
