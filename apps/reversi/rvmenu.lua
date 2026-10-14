-- Reversi start menu: the shared menu.lua with this game's items. Usage:
--   local screen = smudge.dofile("rvmenu.lua")(L, S)
-- with L the rules (logic.lua) and S the app's state (main.lua).
--
-- Two pages: the main one (Resume, the levels, Two players, Options, Take
-- back, Exit) and Options (your colour, Show moves). Both pages on one would
-- not fit the screen.

return function(L, S)
local M = {}
local menu = smudge.dofile("menu.lua")("Reversi", { "Outflank and flip: most discs wins." })
local page = "main"

local function colour(side) return side == L.BLACK and "Black" or "White" end

local function items()
    local out = {}
    if page == "options" then
        out[1] = { label = "You play " .. colour(S.human), act = "side",
                   note = (S.human == L.BLACK and "You move first" or "The device moves first") .. "; tap to swap" }
        out[2] = { label = "Show moves: " .. (S.hints and "On" or "Off"), act = "hints",
                   note = S.hints and "Dots on the squares you can play" or "Find the legal squares yourself" }
        out[3] = { label = "Done", act = "done" }
        return out
    end
    local playing = S.game and not S.game.result
    if playing then out[1] = { label = "Resume", act = "resume" } end
    for i, name in ipairs(S.LEVELS) do
        local r = S.records[i]
        out[#out + 1] = { label = "vs Device: " .. name, mode = i,
                          note = string.format("Won %d  Lost %d  Drawn %d", r[1], r[2], r[3]) }
    end
    out[#out + 1] = { label = "Two players", mode = #S.LEVELS + 1, note = "Take turns on this reader" }
    out[#out + 1] = { label = "Options", act = "options",
                      note = "You play " .. colour(S.human) .. ", moves " .. (S.hints and "shown" or "hidden") }
    if S.backCount() > 0 then out[#out + 1] = { label = "Take back a move", act = "back" } end
    out[#out + 1] = { label = "Exit", act = "exit" }
    return out
end

local function choose(item)
    if not item then return end
    if item == "exit" then
        -- Back: from Options to the main page, from the main page out.
        if page == "options" then
            page, menu.index = "main", 1
            smudge.request_update()
        else
            smudge.exit()
        end
        return
    end
    local act = item.act
    if act == "exit" then
        smudge.exit()
    elseif act == "options" or act == "done" then
        page, menu.index = act == "done" and "main" or "options", 1
        smudge.request_update()
    elseif act == "side" then
        S.human = 3 - S.human
        smudge.save("side", tostring(S.human))
        smudge.request_update()
    elseif act == "hints" then
        S.hints = not S.hints
        smudge.save("hints", S.hints and "1" or "0")
        smudge.request_update()
    elseif act == "back" then
        S.takeBack()
        S.show("board")
    elseif act == "resume" then
        S.refresh()
        S.show("board")
    elseif item.mode then
        S.newGame(item.mode)
        S.show("board")
    end
end

function M.draw() menu.draw(items()) end
function M.button(btn) choose(menu.button(btn, items())) end
function M.tap(x, y) choose(menu.tap(x, y, items())) end

return M
end
