-- Hexland start menu: the shared menu.lua with this game's items. Usage:
--   local screen = smudge.dofile("hxmenu.lua")(L, S)
-- with L the rules (logic.lua) and S the app's state (main.lua).

return function(L, S)
local M = {}
local menu = smudge.dofile("menu.lua")("Hexland", { "Settle a hex island, trade and build:",
                                                    "you against three device players, 10 points wins." })

local function items()
    local out = {}
    if S.game and S.game.phase ~= "over" then
        out[1] = { label = "Resume", level = 0, note = S.LEVELS[S.game.level] .. ", " .. L.vp(S.game, 1, true) .. " points" }
    end
    for i, name in ipairs(S.LEVELS) do
        local r = S.records[i]
        local note = "Won " .. r[1] .. " of " .. r[2]
        if r[2] > 0 then note = note .. string.format(", average place %.1f", r[3] / r[2]) end
        out[#out + 1] = { label = "New game: " .. name, level = i, note = note }
    end
    out[#out + 1] = { label = S.starter and "Board: Starter island" or "Board: Random island", level = -2,
                      note = "For new games; select to switch" }
    out[#out + 1] = { label = "Exit", level = -1 }
    return out
end

local function choose(item)
    if not item then return end
    if item == "exit" or item.level == -1 then
        smudge.exit()
    elseif item.level == -2 then
        S.starter = not S.starter
        smudge.save("board", S.starter and "1" or "0")
        smudge.request_update()
    elseif item.level > 0 then
        S.newGame(item.level)
    else
        S.show("board")
    end
end

function M.draw() menu.draw(items()) end
function M.button(btn) choose(menu.button(btn, items())) end
function M.tap(x, y) choose(menu.tap(x, y, items())) end

return M
end
