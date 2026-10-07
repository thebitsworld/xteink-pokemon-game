-- Whodunit for Xteink Pokemon (Lua app): logic-grid murder mysteries, a new
-- one every time. The rules live in logic.lua and the case generator in
-- gen.lua and its wording in words.lua (each loaded only to build a case, one
-- after the other); the screens are modules of their
-- own - view.lua (clues, grid, accusation) and wmenu.lua (the start menu) -
-- and only one is in memory at a time. This file holds the state.
--
-- Read the clues and the case file, mark the grid (each press cycles a cell:
-- blank, cross, circle; a circle crosses out the rest of its row and column
-- in that block), then accuse the murderer with their weapon and place.
-- Buttons (X3/X4): Left/Right switch tabs on the tab row, Down enters a
-- tab, the arrows move inside it and Up at its top goes back to the tabs.
-- Touch (X4 Pro): tap tabs, cells and the arrows.

local W = smudge.dofile("logic.lua")

-- State shared with the screen modules.
local S = {
    w = 480, h = 800, m = {}, touch = false,
    case = nil,
    grid = {},
    tab = 1,              -- 1 clues, 2 grid, 3 accuse
    focus = "tabs",       -- "tabs" or "content"
    scroll = 0,           -- first line of the clues page
    gr = 1, gc = 1,       -- grid cursor (row, column)
    arow = 1,             -- accusation row
    accused = { 1, 1, 1, 1 },
    result = nil,         -- "right" or "wrong" once accused
    stats = {},           -- per level: { solved, played }
    want = "menu",
}

local screen, screenName = nil, nil
local elapsedMs, sessionStart = 0, 0

function S.formatTime(ms)
    local s = ms // 1000
    return string.format("%d:%02d", s // 60, s % 60)
end

function S.elapsed()
    if S.result or S.want ~= "case" then return elapsedMs end
    return elapsedMs + (smudge.millis() - sessionStart)
end

local function pauseClock()
    if S.case and not S.result and S.want == "case" then elapsedMs = S.elapsed() end
end

function S.show(name)
    pauseClock()
    S.want = name
    if name == "case" then sessionStart = smudge.millis() end
    smudge.request_update()
end

local function loadScreen()
    if screenName == S.want then return end
    screen, screenName = nil, nil
    collectgarbage("collect")
    screen = smudge.dofile(S.want == "menu" and "wmenu.lua" or "view.lua")(W, S)
    screenName = S.want
end

-- Builds the case for `seed`: the generator, then (once it is dropped) the
-- wording - both at once do not fit the X3.
local function build(level, seed)
    screen, screenName = nil, nil
    collectgarbage("collect")
    local case = smudge.dofile("gen.lua")(W).generate(level, seed)
    collectgarbage("collect")
    if case then smudge.dofile("words.lua")(W).finish(case) end
    collectgarbage("collect")
    return case
end

-- A new case is built in on_update, after a frame saying so has been drawn
-- and the screen that asked for it has let go: the generator needs the room.
local pending, pendingDrawn = nil, false

function S.newCase(level)
    pending, pendingDrawn = level, false
    screen, screenName = nil, nil
    smudge.request_update()
end

local function startCase(level)
    local seed = (smudge.time() * 1000 + smudge.millis()) & 0x7fffffff
    local case = build(level, seed)
    if not case then
        S.show("menu")
        return
    end
    S.case, S.grid, S.result = case, {}, nil
    S.tab, S.focus, S.scroll, S.gr, S.gc, S.arow = 1, "tabs", 0, 1, 1, 1
    S.accused = { 1, 1, 1, 1 }
    elapsedMs = 0
    S.show("case")
end

function S.accuse()
    local right = W.check(S.case, S.accused)
    elapsedMs = S.elapsed()
    S.result = right and "right" or "wrong"
    local st = S.stats[S.case.level]
    st[2] = st[2] + 1
    if right then st[1] = st[1] + 1 end
    smudge.save("st" .. S.case.level, st[1] .. "," .. st[2])
    smudge.save("game", "")
    smudge.request_update()
end

-- Callbacks ------------------------------------------------------------------

function on_init()
    S.w, S.h = smudge.get_bounds()
    S.m = smudge.get_metrics()
    S.touch = smudge.has_touch()
    for i = 1, #W.LEVELS do
        local a, b = (smudge.load("st" .. i, "0,0")):match("(%d+),(%d+)")
        S.stats[i] = { tonumber(a) or 0, tonumber(b) or 0 }
    end
    -- A case in progress: level, seed, time and marks; the case is rebuilt
    -- from its seed.
    local level, seed, ms, marks = smudge.load("game", ""):match("^(%d)|(%d+)|(%d+)|(%d*)$")
    if level then
        local case = build(tonumber(level), tonumber(seed))
        if case then
            S.case, elapsedMs = case, tonumber(ms)
            S.grid = W.unpackMarks(case, marks)
        end
    end
    S.show("menu")
end

function on_draw()
    smudge.clear()
    if pending then
        smudge.header("Whodunit", W.LEVELS[pending].name)
        smudge.centered_text(S.h // 2 - 20, "Building a new case...", "ui12", "bold", true)
        pendingDrawn = true
        return
    end
    loadScreen()
    screen.draw()
end

function on_update()
    if pending and pendingDrawn then
        local level = pending
        pending = nil
        startCase(level)
    end
end

function on_exit()
    pauseClock()
    local keep = S.case and not S.result
    smudge.save("game", keep and string.format("%d|%d|%d|%s", S.case.level, S.case.seed, elapsedMs,
                                               W.packMarks(S.case, S.grid)) or "")
end

function on_button(btn, pressed)
    if not pressed or pending then return end
    loadScreen()
    screen.button(btn)
end

function on_tap(x, y)
    if pending then return end
    loadScreen()
    screen.tap(x, y)
end
