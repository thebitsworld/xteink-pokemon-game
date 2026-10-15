-- Code Breaker start menu: the shared menu.lua with this game's items.
-- Usage:
--   local screen = smudge.dofile("cmenu.lua")(L, S)
-- with L the rules (logic.lua) and S the app's state (main.lua).

return function(L, S)
local M = {}
local menu = smudge.dofile("menu.lua")("Code Breaker", { "Find the hidden code from black and white pegs." })

local function size(n, k, rep) return string.format("%d x %d, %s", n, k, rep and "repeats" or "no repeats") end

local function record(mode, n, k, rep)
    local s = S.stat(S.statKey(mode, n, k, rep))
    if s[1] == 0 then return "No games yet" end
    local word = mode == "you" and "Won" or "Solved"
    if s[2] == 0 then return string.format("%s 0 of %d", word, s[1]) end
    return string.format("%s %d of %d, average %.1f, best %d", word, s[2], s[1], s[3] / s[2], s[4])
end

-- Takes back your last score of the device's guesses (by-hand games).
local function undoScore()
    local g = S.game
    g.pending = L.drop(g)
    S.sb, S.sw = 0, 0
    S.note = "Score it again"
    S.saveGame()
end

local function undoable()
    local g = S.game
    return g and not g.result and g.mode == "dev" and not g.auto and L.count(g) > 0
end

local function items()
    local out = {}
    if S.game and not S.game.result then out[1] = { label = "Resume", mode = 0 } end
    for i, P in ipairs(L.PRESETS) do
        out[#out + 1] = { label = P.name .. ": " .. size(P.n, P.k, P.rep), mode = i,
                          note = record("you", P.n, P.k, P.rep) }
    end
    out[#out + 1] = { label = "Custom: " .. size(S.n, S.k, S.rep), mode = 4, note = record("you", S.n, S.k, S.rep) }
    out[#out + 1] = { label = "Device breaks your code", mode = 5,
                      note = size(S.n, S.k, S.rep) .. ". " .. record("dev", S.n, S.k, S.rep) }
    out[#out + 1] = { label = "Options", mode = -2,
                      note = string.format("%d guesses; %s", S.max, S.auto and "app scores the device" or "you score the device") }
    if undoable() then out[#out + 1] = { label = "Undo last score", mode = -3 } end
    out[#out + 1] = { label = "Exit", mode = -1 }
    return out
end

local function choose(item)
    if not item then return end
    if item == "exit" or item.mode == -1 then
        smudge.exit()
    elseif item.mode == -2 then
        S.want = "opts"
        smudge.request_update()
    elseif item.mode == -3 then
        undoScore()
        S.showBoard()
    elseif item.mode == 0 then
        S.showBoard()
    elseif item.mode == 5 then
        S.newGame("dev", 4)
    else
        S.newGame("you", item.mode)
    end
end

function M.draw() menu.draw(items()) end
function M.button(btn) choose(menu.button(btn, items())) end
function M.tap(x, y) choose(menu.tap(x, y, items())) end

return M
end
