-- Caro start menu: the shared menu.lua with this game's items. Usage:
--   local screen = smudge.dofile("cmenu.lua")(C, S)
-- with C the rules (logic.lua) and S the app's state (main.lua).

return function(C, S)
local M = {}
local menu = smudge.dofile("menu.lua")("Caro", { "Gomoku: five or more in a row wins." })

local function items()
    local game = S.game
    local out = {}
    local playing = game and game.winner == 0
    if playing then out[1] = { label = "Resume", mode = 0 } end
    for i, name in ipairs(S.LEVELS) do
        local r = S.records[i]
        out[#out + 1] = { label = "vs Device: " .. name, mode = i,
                          note = string.format("Won %d  Lost %d  Drawn %d", r[1], r[2], r[3]) }
    end
    out[#out + 1] = { label = "Two players", mode = #S.LEVELS + 1, note = "Take turns on this reader" }
    out[#out + 1] = { label = S.youFirst and "First move: You (X)" or "First move: Device (X)", mode = -2,
                      note = "Next game against the device" }
    if playing and game.n > 0 then out[#out + 1] = { label = "Take back a move", mode = -3 } end
    out[#out + 1] = { label = "Exit", mode = -1 }
    return out
end

local function choose(item)
    if not item then return end
    if item == "exit" or item.mode == -1 then
        smudge.exit()
    elseif item.mode == -2 then
        S.youFirst = not S.youFirst
        smudge.save("first", S.youFirst and "1" or "0")
        smudge.request_update()
    elseif item.mode == -3 then
        -- Back to your own move: one mark with two players, otherwise your
        -- last mark and the device's reply.
        C.undo(S.game, (S.vsDevice() and S.game.turn == S.human) and 2 or 1)
        S.cursorToLast()
        S.show("board")
    elseif item.mode > 0 then
        S.newGame(item.mode)
    else
        S.show("board")
    end
end

function M.draw() menu.draw(items()) end
function M.button(btn) choose(menu.button(btn, items())) end
function M.tap(x, y) choose(menu.tap(x, y, items())) end

return M
end
