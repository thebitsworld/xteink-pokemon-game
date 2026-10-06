-- Chess start menu: the shared menu.lua with this game's items. Usage:
--   local screen = smudge.dofile("chessmenu.lua")(C, S)
-- with C the rules (logic.lua) and S the app's state (main.lua).

return function(C, S)
local M = {}
local menu = smudge.dofile("menu.lua")("Chess", { "Checkmate the opposing king." })

local function items()
    local out = {}
    local playing = S.game and not S.game.result
    if playing then out[1] = { label = "Resume", mode = 0 } end
    for i, L in ipairs(S.LEVELS) do
        local r = S.records[i]
        out[#out + 1] = { label = "vs Device: " .. L.name, mode = i,
                          note = string.format("Won %d  Lost %d  Drawn %d", r[1], r[2], r[3]) }
    end
    out[#out + 1] = { label = "Two players", mode = #S.LEVELS + 1, note = "Take turns on this reader" }
    out[#out + 1] = { label = "You play " .. (S.human == C.WHITE and "White" or "Black"), mode = -2,
                      note = "Against the device; tap to swap" }
    if playing and C.moveCount(S.game) > 0 then out[#out + 1] = { label = "Take back a move", mode = -3 } end
    out[#out + 1] = { label = "Exit", mode = -1 }
    return out
end

local function choose(item)
    if not item then return end
    if item == "exit" or item.mode == -1 then
        smudge.exit()
    elseif item.mode == -2 then
        S.human = 1 - S.human
        smudge.save("side", tostring(S.human))
        smudge.request_update()
    elseif item.mode == -3 then
        -- Back to your own move: one ply with two players, otherwise to the
        -- last position where it was your turn.
        local n = (S.vsDevice() and S.game.side == S.human) and 2 or 1
        local back = smudge.dofile("history.lua")(C).takeBack(S.game, math.min(n, C.moveCount(S.game)))
        if back then S.game = back end
        S.note, S.deviceMoved = "Move taken back", false
        S.refresh()
        menu.index = 1
        smudge.request_update()
    else
        if item.mode > 0 then S.newGame(item.mode) else S.refresh() end
        S.show("board")
    end
end

function M.draw() menu.draw(items()) end
function M.button(btn) choose(menu.button(btn, items())) end
function M.tap(x, y) choose(menu.tap(x, y, items())) end

return M
end
