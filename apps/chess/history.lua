-- Replaying a chess game's move history: restoring a saved game and taking
-- moves back. Usage: local History = smudge.dofile("history.lua")(C), with C
-- the rules (logic.lua). Loaded only when needed.

return function(C)
local H = {}

local DIGITS = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-_"

local function unpackMove(s, k)
    local mv, mul = 0, 1
    for j = 0, 3 do
        local d = DIGITS:find(s:sub(k + j, k + j), 1, true)
        if not d then return nil end
        mv = mv + (d - 1) * mul
        mul = mul * 64
    end
    return mv
end

-- A new game with the first `count` moves of `history` replayed (nil if a
-- move in it is not legal).
function H.replay(history, count)
    local g = C.new()
    count = math.min(count or (#history // 4), #history // 4)
    for k = 1, count do
        local mv = unpackMove(history, (k - 1) * 4 + 1)
        if not mv or not C.play(g, mv) then return nil end
    end
    return g
end

-- Takes back the last `n` moves.
function H.takeBack(g, n) return H.replay(g.history, C.moveCount(g) - n) end

return H
end
