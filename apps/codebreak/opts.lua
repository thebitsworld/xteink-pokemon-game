-- Code Breaker options, on the shared menu.lua: each select steps one
-- option. They apply to the next game. Usage:
--   local screen = smudge.dofile("opts.lua")(L, S)

return function(L, S)
local M = {}
local menu = smudge.dofile("menu.lua")("Options", { "For the next game. Select an option to change it." })

local function items()
    return {
        { label = "Positions: " .. S.n, id = 1, note = "Custom game and Device breaks your code" },
        { label = "Symbols: " .. S.k, id = 2, note = "Custom game and Device breaks your code" },
        { label = "Repeated symbols: " .. (S.rep and "allowed" or "not allowed"), id = 3,
          note = "Custom game and Device breaks your code" },
        { label = "Guesses: " .. S.max, id = 4, note = "Every game" },
        { label = "Scoring: " .. (S.auto and "by the app" or "by you"), id = 5,
          note = S.auto and "Enter your code; the app scores the device" or "Keep your code in mind; score each guess" },
        { label = "Done", id = 0 },
    }
end

local function choose(item)
    if not item then return end
    if item == "exit" or item.id == 0 then
        S.saveOptions()
        S.want = "cmenu"
    elseif item.id == 1 then
        S.n = S.n == 4 and 5 or 4
    elseif item.id == 2 then
        S.k = S.k == 6 and 8 or 6
    elseif item.id == 3 then
        S.rep = not S.rep
    elseif item.id == 4 then
        S.max = S.max == 8 and 10 or (S.max == 10 and 12 or 8)
    elseif item.id == 5 then
        S.auto = not S.auto
    end
    smudge.request_update()
end

function M.draw() menu.draw(items()) end
function M.button(btn) choose(menu.button(btn, items())) end
function M.tap(x, y) choose(menu.tap(x, y, items())) end

return M
end
