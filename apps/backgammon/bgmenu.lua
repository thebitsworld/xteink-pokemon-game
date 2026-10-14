-- Backgammon start menu: the shared menu.lua with this game's items. Usage:
--   local screen = smudge.dofile("bgmenu.lua")(S)
-- with S the app's state (main.lua).

return function(S)
local M = {}
local menu = smudge.dofile("menu.lua")("Backgammon", { "Race your 15 checkers home and bear them off.",
                                                      "A win scores 1, a gammon 2, a backgammon 3." })

local function items()
    local out = {}
    local g = S.game
    if g and not g.result then out[1] = { label = "Resume", mode = 0 } end
    for i, name in ipairs(S.LEVELS) do
        local r = S.records[i]
        out[#out + 1] = { label = "vs Device: " .. name, mode = i,
                          note = string.format("Won %d  Lost %d   Points %d-%d", r[1], r[2], r[3], r[4]) }
    end
    local r = S.records[#S.LEVELS + 1]
    out[#out + 1] = { label = "Two players", mode = #S.LEVELS + 1,
                      note = string.format("Points: White %d, Black %d", r[3], r[4]) }
    for i = 1, #S.records do
        if S.records[i][3] + S.records[i][4] > 0 then
            out[#out + 1] = { label = "New match", mode = -2, note = "Match points back to zero" }
            break
        end
    end
    out[#out + 1] = { label = "Exit", mode = -1 }
    return out
end

local function choose(item)
    if not item then return end
    if item == "exit" or item.mode == -1 then
        smudge.exit()
    elseif item.mode == -2 then
        for i, r in ipairs(S.records) do
            r[3], r[4] = 0, 0
            smudge.save("rec" .. i, table.concat(r, ","))
        end
        smudge.request_update()
    else
        if item.mode > 0 then S.newGame(item.mode) else S.G().refresh() end
        S.show("board")
    end
end

function M.draw() menu.draw(items()) end
function M.button(btn) choose(menu.button(btn, items())) end
function M.tap(x, y) choose(menu.tap(x, y, items())) end

return M
end
