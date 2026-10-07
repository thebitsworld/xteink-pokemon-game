-- Dungeon Map for Xteink Pokemon (Lua app): wall in a dungeon from the
-- numbers along its edges, the monsters in its dead ends and the treasure in
-- its rooms. Rules in logic.lua; the play screen in view.lua, the start menu
-- in menu.lua and the map maker in gen.lua are each loaded only while needed,
-- so they are never in memory together (the X3 gives a Lua app about 75 KB).
-- This file holds the state and handles input.
--
-- Buttons (X3/X4): Left/Right and Up/Down move the cursor, Confirm cycles a
-- cell (wall, open, unknown), Back opens the menu. Touch (X4 Pro): tap a cell
-- to put a wall there - or to mark it open, with the Wall/Open switch - and
-- tap it again to clear it.

local D = smudge.dofile("logic.lua")

-- State shared with view.lua.
local S = {
    w = 480, h = 800, m = {}, touch = false,
    level = 1,
    puzzle = nil,
    marks = nil,      -- { wall = row masks, open = row masks }
    solved = false,
    curC = 1, curR = 1,
    openMode = false,
}
local menu = nil      -- the menu module while the menu is on screen
local view = nil      -- the play screen module while a map is on screen
local rules = nil     -- the rules page while it is on screen
local sessionStart = 0
local stats = {}      -- per level: { solved, bestMs }
local generateFailed = false

local function rand(n) return math.random(1, n) end

function S.formatTime(ms)
    local s = ms // 1000
    return string.format("%d:%02d", s // 60, s % 60)
end

function S.elapsed()
    local p = S.puzzle
    if not p then return 0 end
    if S.solved or menu then return p.elapsedMs end
    return p.elapsedMs + (smudge.millis() - sessionStart)
end

local function pauseClock()
    if S.puzzle and not S.solved and not menu then S.puzzle.elapsedMs = S.elapsed() end
end

local function openMenu()
    pauseClock()
    view = nil
    collectgarbage("collect")
    local lines = { "Wall in the map: the numbers count each", "row's and column's walls." }
    if generateFailed then lines[3] = "Could not make a map - try again." end
    menu = smudge.dofile("menu.lua")("Dungeon Map", lines)
    smudge.request_update()
end

local function playScreen()
    if not view then view = smudge.dofile("view.lua")(D, S) end
    return view
end

-- Makes a new map; the menu, the play screen and then the map maker are each
-- dropped around it.
local function newPuzzle(lv)
    S.level, S.puzzle, S.marks = lv, nil, nil
    menu, view = nil, nil
    collectgarbage("collect")
    local p = smudge.dofile("gen.lua")(D).generate(lv, rand, 60)
    collectgarbage("collect")
    generateFailed = p == nil
    if not p then
        openMenu()
        return
    end
    local mk = { wall = {}, open = {} }
    for r = 1, p.size do mk.wall[r], mk.open[r] = 0, 0 end
    S.puzzle, S.marks, S.solved = p, mk, false
    S.curC, S.curR, S.openMode = 1, 1, false
    sessionStart = smudge.millis()
    smudge.request_update()
end

local function menuItems()
    local items = {}
    if S.puzzle and not S.solved then
        items[#items + 1] = { label = "Resume", level = 0,
                              note = string.format("%s %dx%d", D.LEVELS[S.level].name, S.puzzle.size, S.puzzle.size) }
    end
    for i, L in ipairs(D.LEVELS) do
        local s = stats[i]
        local note = s[1] > 0 and string.format("Solved %d  Best %s", s[1], S.formatTime(s[2])) or "Not solved yet"
        local label = string.format("%s  %dx%d%s", L.name, L.size, L.size, L.rooms > 0 and " + treasure" or "")
        items[#items + 1] = { label = label, level = i, note = note }
    end
    items[#items + 1] = { label = "How to play", level = -2 }
    items[#items + 1] = { label = "Exit", level = -1 }
    return items
end

-- Actions --------------------------------------------------------------------

local function checkSolved()
    local p = S.puzzle
    if not D.solved(p, S.marks.wall) then return end
    S.solved = true
    p.elapsedMs = p.elapsedMs + (smudge.millis() - sessionStart)
    local s = stats[S.level]
    s[1] = s[1] + 1
    if s[2] == 0 or p.elapsedMs < s[2] then s[2] = p.elapsedMs end
    smudge.save("st" .. S.level, string.format("%d,%d", s[1], s[2]))
    smudge.save("game", "")
end

local function cellState(c, r)
    local b = D.bit(c)
    if S.marks.wall[r] & b ~= 0 then return "wall" end
    if S.marks.open[r] & b ~= 0 then return "open" end
    return nil
end

-- Sets cell (c, r) to "wall", "open" or nil (unknown). Monsters and chests stay.
local function setCell(c, r, state)
    local p, mk, b = S.puzzle, S.marks, D.bit(c)
    if (p.monsters[r] | p.chests[r]) & b ~= 0 then return end
    mk.wall[r] = mk.wall[r] & ~b
    mk.open[r] = mk.open[r] & ~b
    if state == "wall" then mk.wall[r] = mk.wall[r] | b end
    if state == "open" then mk.open[r] = mk.open[r] | b end
    checkSolved()
    smudge.request_update()
end

local function choose(item)
    if not item then return end
    if item ~= "exit" and item.level == -2 then
        rules = smudge.dofile("rules.lua")()
        smudge.request_update()
    elseif item == "exit" or item.level < 0 then
        smudge.exit()
    elseif item.level == 0 then
        menu = nil
        collectgarbage("collect")
        sessionStart = smudge.millis()
        smudge.request_update()
    else
        newPuzzle(item.level)
    end
end

-- Callbacks ------------------------------------------------------------------

function on_init()
    S.w, S.h = smudge.get_bounds()
    S.m = smudge.get_metrics()
    S.touch = smudge.has_touch()
    math.randomseed(smudge.millis() + smudge.time())
    for i = 1, #D.LEVELS do
        local a, b = (smudge.load("st" .. i, "0,0")):match("(%d+),(%d+)")
        stats[i] = { tonumber(a) or 0, tonumber(b) or 0 }
    end
    local p, mk = D.deserialize(smudge.load("game", ""))
    if p then S.puzzle, S.marks, S.level = p, mk, p.level end
    -- openMenu() pauses the clock; a saved game has not been running since boot.
    sessionStart = smudge.millis()
    openMenu()
end

function on_draw()
    smudge.clear()
    if rules then
        rules.draw()
    elseif menu then
        menu.draw(menuItems())
    else
        playScreen().draw()
    end
end

function on_exit()
    pauseClock()
    smudge.save("game", (S.puzzle and not S.solved) and D.serialize(S.puzzle, S.marks) or "")
end

function on_button(btn, pressed)
    if not pressed then return end
    if rules then
        rules = nil
        collectgarbage("collect")
        smudge.request_update()
        return
    end
    if menu then
        choose(menu.button(btn, menuItems()))
        return
    end
    if btn == "back" then
        openMenu()
        return
    end
    if S.solved then
        if btn == "confirm" then newPuzzle(S.level) end
        return
    end
    local n = S.puzzle.size
    if btn == "left" then
        S.curC = (S.curC - 2) % n + 1
    elseif btn == "right" then
        S.curC = S.curC % n + 1
    elseif btn == "up" or btn == "page_back" then
        S.curR = (S.curR - 2) % n + 1
    elseif btn == "down" or btn == "page_forward" then
        S.curR = S.curR % n + 1
    elseif btn == "confirm" then
        local st = cellState(S.curC, S.curR)
        setCell(S.curC, S.curR, st == nil and "wall" or (st == "wall" and "open" or nil))
        return
    end
    smudge.request_update()
end

function on_tap(x, y)
    if rules then
        rules = nil
        collectgarbage("collect")
        smudge.request_update()
        return
    end
    if menu then
        choose(menu.tap(x, y, menuItems()))
        return
    end
    local v = playScreen()
    local tb = v.toolbar()
    if smudge.in_rect(x, y, tb.menu.x, tb.menu.y, tb.menu.w, tb.menu.h) then
        openMenu()
        return
    end
    if smudge.in_rect(x, y, tb.mode.x, tb.mode.y, tb.mode.w, tb.mode.h) then
        S.openMode = not S.openMode
        smudge.request_update()
        return
    end
    if S.solved then
        newPuzzle(S.level)
        return
    end
    local c, r = v.cellAt(x, y)
    if not c then return end
    S.curC, S.curR = c, r
    local want = S.openMode and "open" or "wall"
    setCell(c, r, cellState(c, r) == want and nil or want)
end
