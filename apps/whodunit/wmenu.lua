-- Whodunit start menu: the shared menu.lua with this game's items. Usage:
--   local screen = smudge.dofile("wmenu.lua")(W, S)
-- with W the rules (logic.lua) and S the app's state (main.lua).

return function(W, S)
local M = {}
local menu = smudge.dofile("menu.lua")("Whodunit", { "Work out who had what and was where,",
                                                     "then name the murderer." })

local function items()
    local out = {}
    if S.case and not S.result then out[1] = { label = "Resume", level = 0 } end
    for i, L in ipairs(W.LEVELS) do
        local st = S.stats[i]
        local shape = string.format("%d suspects%s", L.items, L.categories == 4 and ", motives too" or "")
        out[#out + 1] = { label = "New case: " .. L.name, level = i,
                          note = string.format("%s - solved %d of %d", shape, st[1], st[2]) }
    end
    out[#out + 1] = { label = "Exit", level = -1 }
    return out
end

local function choose(item)
    if not item then return end
    if item == "exit" or item.level < 0 then
        smudge.exit()
    elseif item.level == 0 then
        S.show("case")
    else
        S.newCase(item.level)
    end
end

function M.draw() menu.draw(items()) end
function M.button(btn) choose(menu.button(btn, items())) end
function M.tap(x, y) choose(menu.tap(x, y, items())) end

return M
end
