-- Co Ganh start menu: the shared menu.lua with this game's items. Usage:
--   local screen = smudge.dofile("gmenu.lua")(C, S)
-- with C the rules (logic.lua) and S the app's state (main.lua).

return function(C, S)
local M = {}
local menu = smudge.dofile("menu.lua")("Co Ganh", { "Flank and surround pieces to win them over." })

local function moveCount() return #S.game.history // 2 end

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
    -- Your side and the "mo" rule share a button: each select steps through
    -- the four choices.
    out[#out + 1] = { label = "You play " .. (S.side == C.WHITE and "first" or "second") ..
                          (S.forced and ", forced ganh on" or ", forced ganh off"), mode = -2,
                      note = "Next game; select to change" }
    if playing and moveCount() > 0 then out[#out + 1] = { label = "Take back a move", mode = -3 } end
    out[#out + 1] = { label = "Exit", mode = -1 }
    return out
end

local function choose(item)
    if not item then return end
    if item == "exit" or item.mode == -1 then
        smudge.exit()
    elseif item.mode == -2 then
        -- Only new games take them: a game in progress keeps its sides and rules.
        -- First/off, second/off, first/on, second/on, and round again.
        if S.side == C.BLACK then S.forced = not S.forced end
        S.side = 3 - S.side
        smudge.save("side", tostring(S.side))
        smudge.save("forced", S.forced and "1" or "0")
        smudge.request_update()
    elseif item.mode == -3 then
        -- Back to your own move: one move with two players, otherwise to the
        -- last position where it was your turn. main.lua replays the game
        -- once this menu is out of memory, and shows the board.
        local n = (S.vsDevice() and S.game.side == S.human) and 2 or 1
        S.takeBack = math.min(n, moveCount())
        S.show("board")
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
