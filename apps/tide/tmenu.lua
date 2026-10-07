-- The Tide & Paper start menu: the shared menu.lua with this game's items.
--   local screen = smudge.dofile("tmenu.lua")(S)

return function(S)
local M = {}
local menu = smudge.dofile("menu.lua")("Tide & Paper", { "Collect pairs and sets of sea cards;",
                                                          "first to 40 points wins." })

local function items()
    local out = {}
    local g = S.game
    if g and g.phase ~= "over" then
        out[#out + 1] = { label = "Resume", level = 0,
                          note = string.format("%s - you %d, device %d", S.LEVELS[S.level], g.scores[1], g.scores[2]) }
    end
    for i, name in ipairs(S.LEVELS) do
        local s = S.stats[i]
        out[#out + 1] = { label = "New game: " .. name, level = i, note = string.format("Won %d of %d", s[1], s[2]) }
    end
    out[#out + 1] = { label = "Exit", level = -1 }
    return out
end

local function choose(item)
    if not item then return end
    if item == "exit" or item.level < 0 then
        smudge.exit()
    elseif item.level == 0 then
        S.show("table")
    else
        S.newGame(item.level)
    end
end

function M.draw() menu.draw(items()) end
function M.button(btn) choose(menu.button(btn, items())) end
function M.tap(x, y) choose(menu.tap(x, y, items())) end

return M
end
