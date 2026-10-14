-- Dots and Boxes start menu: the shared menu.lua with this game's items.
-- Usage:
--   local screen = smudge.dofile("dmenu.lua")(D, S)
--   screen.draw(); screen.button(btn); screen.tap(x, y)
-- with D the rules (logic.lua) and S the app's state (main.lua). The first
-- time it also loads the records, the settings and a saved game.

return function(D, S)
local M = {}
local G = smudge.dofile("game.lua")(D)
local menu = smudge.dofile("menu.lua")("Dots and Boxes", { "Close a box to score it and draw again.",
                                                           "Most boxes wins." })

if not S.records then
    S.records = {}
    for i = 1, #S.LEVELS do
        local a, b, c = (smudge.load("rec" .. i, "0,0,0")):match("(%d+),(%d+),(%d+)")
        S.records[i] = { tonumber(a) or 0, tonumber(b) or 0, tonumber(c) or 0 }
    end
    S.size = tonumber(smudge.load("size", "2")) or 2
    if S.size < 1 or S.size > #S.SIZES then S.size = 2 end
    S.nextFirst = tonumber(smudge.load("first", "1")) == 2 and 2 or 1
    local mode, data = smudge.load("game", ""):match("^(%d)#(.+)$")
    local g = data and G.decode(data)
    if g and not D.over(g) then
        S.mode, S.game = tonumber(mode), g
        if S.mode < 1 or S.mode > #S.LEVELS + 1 then S.mode = #S.LEVELS + 1 end
        S.cursor = g.n // 2 + 1
    end
end

local function canTakeBack()
    local g = S.game
    if not g or D.over(g) then return false end
    if not S.vsDevice() then return #g.hist > 0 end
    for _, l in ipairs(g.hist) do
        if g.lines[l] == 1 then return true end
    end
    return false
end

-- Takes back your last line (and the device's lines after it).
local function takeBack()
    local g = S.game
    if S.vsDevice() then
        while G.lastPlayer(g) == 2 do G.undo(g) end
    end
    local l = G.undo(g)
    if l then S.cursor = l end
    S.note = "Line taken back"
end

local function items()
    local out = {}
    if S.game and not D.over(S.game) then out[1] = { label = "Resume", mode = 0 } end
    for i, name in ipairs(S.LEVELS) do
        local r = S.records[i]
        out[#out + 1] = { label = "vs Device: " .. name, mode = i,
                          note = string.format("Won %d  Lost %d  Drawn %d", r[1], r[2], r[3]) }
    end
    out[#out + 1] = { label = "Two players", mode = #S.LEVELS + 1, note = "Take turns on this reader" }
    local n = S.SIZES[S.size]
    out[#out + 1] = { label = string.format("Board: %dx%d boxes", n, n), mode = -2,
                      note = S.touch and "Next game; tap to change" or "Next game; Select to change" }
    if canTakeBack() then out[#out + 1] = { label = "Take back a move", mode = -3 } end
    out[#out + 1] = { label = "Exit", mode = -1 }
    return out
end

local function choose(item)
    if not item then return end
    if item == "exit" or item.mode == -1 then
        smudge.exit()
    elseif item.mode == -2 then
        -- Only new games take it: a game in progress keeps its board.
        S.size = S.size % #S.SIZES + 1
        smudge.save("size", tostring(S.size))
        smudge.request_update()
    else
        if item.mode == -3 then
            takeBack()
        elseif item.mode > 0 then
            G.start(S, item.mode)
        end
        S.showBoard()
    end
end

function M.draw() menu.draw(items()) end
function M.button(btn) choose(menu.button(btn, items())) end
function M.tap(x, y) choose(menu.tap(x, y, items())) end

return M
end
