-- Bazaar start menu: the shared menu.lua with this game's items. Usage:
--   local screen = smudge.dofile("bazaarmenu.lua")(B, S)
-- with B the rules (logic.lua) and S the app's state (main.lua).

return function(B, S)
local M = {}
local menu = smudge.dofile("menu.lua")("Bazaar", { "Trade goods, sell in sets, keep more camels.",
                                                   "The richer trader wins the round; two rounds win." })

local function items()
    local out = {}
    if S.game and S.game.phase ~= "over" then out[1] = { label = "Resume", level = 0 } end
    for i, name in ipairs(S.LEVELS) do
        out[#out + 1] = { label = "New match: " .. name, level = i,
                          note = string.format("Won %d of %d", S.stats[i][1], S.stats[i][2]) }
    end
    out[#out + 1] = { label = "Exit", level = -1 }
    return out
end

local function choose(item)
    if not item then return end
    if item == "exit" or item.level < 0 then
        smudge.exit()
        return
    end
    if item.level > 0 then S.newGame(item.level) end
    S.show("table")
end

function M.draw() menu.draw(items()) end
function M.button(btn) choose(menu.button(btn, items())) end
function M.tap(x, y) choose(menu.tap(x, y, items())) end

return M
end
