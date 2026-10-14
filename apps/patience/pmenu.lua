-- Patience start menu: the shared menu.lua with this app's items, and
-- starting games (kept here, out of main.lua, since only the menu starts
-- one). Usage:
--   local screen = smudge.dofile("pmenu.lua")(P, S)
-- with P the rules (logic.lua) and S the app's state (main.lua).

return function(P, S)
local M = {}
local menu = smudge.dofile("menu.lua")("Patience", { "FreeCell and Spider solitaire." })

local function record(i)
    local s = S.stats[i]
    return string.format("Won %d of %d, best streak %d", s[2], s[1], s[4])
end

local function items()
    local g = S.game
    local out = {}
    local playing = g and not P.won(g)
    if playing then
        local what = g.kind == P.FREECELL and ("FreeCell #" .. g.deal) or
                     ("Spider, " .. g.suits .. (g.suits == 1 and " suit" or " suits"))
        out[1] = { label = "Resume", act = "resume", note = what .. ", " .. g.moves .. " moves" }
        if P.canUndo(g) then out[#out + 1] = { label = "Undo last move", act = "undo" } end
    end
    out[#out + 1] = { label = "FreeCell: new deal", act = "fc", note = "#" .. S.nextDeal .. "  " .. record(1) }
    if playing and g.kind == P.FREECELL and g.moves > 0 then
        out[#out + 1] = { label = "FreeCell: restart deal #" .. g.deal, act = "restart" }
    end
    out[#out + 1] = { label = "Spider: 1 suit", act = "sp", suits = 1, note = record(2) }
    out[#out + 1] = { label = "Spider: 2 suits", act = "sp", suits = 2, note = record(3) }
    out[#out + 1] = { label = "Spider: 4 suits", act = "sp", suits = 4, note = record(4) }
    out[#out + 1] = { label = "Exit", act = "exit" }
    return out
end

-- Deals a new game (the old one, and this menu, leave memory first). A game
-- left unfinished counts as lost for the winning streak; a restarted deal is
-- the same game.
local function deal(act, suits)
    local old = S.game
    local i = old and S.kindIndex(old)
    if old and act ~= "restart" and not P.won(old) then
        S.stats[i][3] = 0
        S.saveStats(i)
    end
    local number = old and old.deal
    if act == "fc" then
        number = S.nextDeal
        S.nextDeal = math.random(1, 32000)
        smudge.save("next", tostring(S.nextDeal))
    end
    S.game, S.picked, S.count, S.cur = nil, nil, nil, 1
    old = nil
    S.dropScreen()
    local D = smudge.dofile("deal.lua")(P)
    if act == "sp" then
        S.game = D.spider(suits, function(n) return math.random(1, n) end)
    else
        S.game = D.freeCell(number)
    end
    D = nil
    i = S.kindIndex(S.game)
    if act == "restart" then
        S.note = "Deal " .. number .. " dealt again"
    else
        S.note = nil
        S.stats[i][1] = S.stats[i][1] + 1
        S.saveStats(i)
    end
    S.save()
    S.show("board")
end

local function choose(item)
    if not item then return end
    if item == "exit" or item.act == "exit" then
        smudge.exit() -- on_exit saves the game
    elseif item.act == "resume" then
        S.note = nil
        S.show("board")
    elseif item.act == "undo" then
        S.undo()
        S.show("board")
    else
        deal(item.act, item.suits)
    end
end

function M.draw() menu.draw(items()) end
function M.button(btn) choose(menu.button(btn, items())) end
function M.tap(x, y) choose(menu.tap(x, y, items())) end

return M
end
