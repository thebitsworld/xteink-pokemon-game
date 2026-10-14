-- O An Quan start menu: the shared menu.lua with this game's items. Usage:
--   local screen = smudge.dofile("oqmenu.lua")(C, S)
-- with C the rules (logic.lua) and S the app's state (main.lua).

return function(C, S)
local M = {}
local menu = smudge.dofile("menu.lua")("O An Quan", { "Sow the stones, capture the squares,",
                                                      "take the mandarins." })

local function items()
    local out = {}
    if S.s and S.s[C.OVER] == 0 then out[1] = { label = "Resume", mode = 0 } end
    for i, L in ipairs(S.LEVELS) do
        local r = S.records[i]
        out[#out + 1] = { label = "vs Device: " .. L.name, mode = i,
                          note = string.format("Won %d  Lost %d  Drawn %d", r[1], r[2], r[3]) }
    end
    out[#out + 1] = { label = "Two players", mode = #S.LEVELS + 1, note = "Take turns on this reader" }
    out[#out + 1] = { label = "Exit", mode = -1 }
    return out
end

local function choose(item)
    if not item then return end
    if item == "exit" or item.mode == -1 then
        smudge.exit()
        return
    end
    if item.mode > 0 then S.newGame(item.mode) else S.refresh() end
    S.show("board")
end

function M.draw() menu.draw(items()) end
function M.button(btn) choose(menu.button(btn, items())) end
function M.tap(x, y) choose(menu.tap(x, y, items())) end

return M
end
