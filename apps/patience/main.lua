-- Patience for Xteink Pokemon (Lua app): FreeCell and Spider solitaire.
-- The rules live in logic.lua; dealing (deal.lua), saving (save.lua), undo
-- and "send it" (tools.lua) are loaded only for that. The table screen is two modules, draw.lua and
-- input.lua (both with layout.lua), and the start menu is pmenu.lua with the
-- shared menu.lua: one screen module in memory at a time. This file holds the
-- game, the statistics and the flow between screens.
--
-- Buttons (X3/X4): Left/Right move the cursor over the columns, free cells,
-- foundations and the Spider stock; Up/Down take more or fewer cards of the
-- run; Confirm picks them up, then puts them on the column under the cursor
-- (Confirm on the same place sends them wherever they fit). Back puts them
-- down again, or opens the menu. Touch (X4 Pro): tap a card (it takes the
-- cards on top of it), then tap where it goes; tap it twice to send it.

local P = smudge.dofile("logic.lua")

local S = {
    w = 480, h = 800, m = {}, touch = false,
    game = nil,
    stats = {},        -- per kind (1 FreeCell, 2..4 Spider 1/2/4 suits): { played, won, streak, best }
    nextDeal = 1,      -- the FreeCell deal "New deal" starts
    cur = 1,           -- cursor: a place (logic.lua's locations; 13 = the foundations)
    count = nil,       -- cards of the run under the cursor to pick (nil: all that can move)
    picked = nil,      -- the place of the picked cards, and how many
    pickN = 0,
    note = nil,        -- a line for the status bar: why a move was refused
    want = "menu",
    dirty = false,     -- the game is saved once the current screen is dropped
}
local SPIDER_KIND = { [1] = 2, [2] = 3, [4] = 4 }

local screen, screenName = nil, nil

-- Statistics row of a game: 1 FreeCell, 2..4 Spider with 1, 2, 4 suits.
function S.kindIndex(g) return g.kind == P.FREECELL and 1 or SPIDER_KIND[g.suits] end

function S.saveStats(i)
    local s = S.stats[i]
    smudge.save("st" .. i, s[1] .. "," .. s[2] .. "," .. s[3] .. "," .. s[4])
end

function S.save() S.dirty = true end

function S.dropScreen()
    screen, screenName = nil, nil
    collectgarbage("collect")
end

local function saveNow()
    S.dirty = false
    local text = ""
    if S.game and not P.won(S.game) then text = smudge.dofile("save.lua")(P).serialize(S.game) end
    smudge.save("game", text)
    text = nil
    collectgarbage("collect")
end

function S.show(name)
    S.want = name
    if name == "menu" then
        S.picked = nil
        S.save()
    end
    smudge.request_update()
end

-- Undo (from the menu or the table's Undo button).
function S.undo()
    S.picked, S.count = nil, nil
    S.note = smudge.dofile("tools.lua")(P).undo(S.game) and nil or "Nothing to undo"
    smudge.request_update()
end

-- Loads "menu", "draw" or "input", dropping the other first.
local function loadScreen(name)
    if screenName == name and not S.dirty then return end
    S.dropScreen()
    if S.dirty then saveNow() end
    screen = smudge.dofile(name == "menu" and "pmenu.lua" or (name .. ".lua"))(P, S)
    screenName = name
end

-- Callbacks ------------------------------------------------------------------

function on_init()
    S.w, S.h = smudge.get_bounds()
    S.m = smudge.get_metrics()
    S.touch = smudge.has_touch()
    math.randomseed(smudge.millis() + smudge.time())
    for i = 1, 4 do
        local a, b, c, d = smudge.load("st" .. i, "0,0,0,0"):match("(%d+),(%d+),(%d+),(%d+)")
        S.stats[i] = { tonumber(a) or 0, tonumber(b) or 0, tonumber(c) or 0, tonumber(d) or 0 }
    end
    S.nextDeal = tonumber(smudge.load("next", "")) or math.random(1, 32000)
    local saved = smudge.load("game", "")
    if saved ~= "" then
        S.game = smudge.dofile("save.lua")(P).deserialize(saved)
        saved = nil
        collectgarbage("collect")
    end
    S.show("menu")
end

function on_draw()
    smudge.clear()
    loadScreen(S.want == "menu" and "menu" or "draw")
    screen.draw()
end

function on_exit()
    S.dropScreen()
    saveNow()
end

function on_button(btn, pressed)
    if not pressed then return end
    loadScreen(S.want == "menu" and "menu" or "input")
    screen.button(btn)
end

function on_tap(x, y)
    loadScreen(S.want == "menu" and "menu" or "input")
    screen.tap(x, y)
end
