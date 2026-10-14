-- Tien Len start menu: the shared menu.lua with this game's items and the
-- points table under the title. Usage:
--   local screen = smudge.dofile("tlmenu.lua")(T, S)
-- with T the rules (logic.lua) and S the app's state (main.lua).

return function(T, S)
local M = {}
local menu

local function build()
    local p, n = S.points, S.handsPlayed
    menu = nil
    menu = smudge.dofile("menu.lua")("Tien Len", {
        "Shed all your cards first: beat the table or pass.",
        string.format("Points after %d hand%s (3/2/1/0 a hand):", n, n == 1 and "" or "s"),
        string.format("You %d   East %d   North %d   West %d", p[1], p[2], p[3], p[4]) })
end
build()

local function items()
    local out = {}
    if S.game and not S.game.over then out[1] = { label = "Resume", act = 0 } end
    for i, name in ipairs(S.LEVELS) do
        local st = S.stats[i]
        out[#out + 1] = { label = "vs Device: " .. name, act = i,
                          note = string.format("Won %d of %d hands", st[1], st[2]) }
    end
    out[#out + 1] = { label = "Reset points", act = -2, note = "Start the points table from zero" }
    out[#out + 1] = { label = "Exit", act = -1 }
    return out
end

local function choose(item)
    if not item then return end
    if item == "exit" or item.act == -1 then
        smudge.exit()
    elseif item.act == -2 then
        S.points, S.handsPlayed, S.leader = { 0, 0, 0, 0 }, 0, 0
        S.saveTable()
        build()
        smudge.request_update()
    else
        -- A level starts a new hand (main.lua deals it once this menu is
        -- out of memory); Resume goes back to the hand.
        if item.act > 0 then S.deal = item.act else S.wake() end
        S.show("board")
    end
end

function M.draw() menu.draw(items()) end
function M.button(btn) choose(menu.button(btn, items())) end
function M.tap(x, y) choose(menu.tap(x, y, items())) end

return M
end
