-- Nine Men's Morris games: setting one up, playing a real move, the result,
-- and the move history (restoring a saved game, taking moves back). Usage:
--   local G = smudge.dofile("game.lua")(C)
-- with C the rules (logic.lua). Loaded only for a real move or a new game and
-- dropped after, so the computer's search never has it in memory.
--
-- Results: a player left with two pieces, or with no legal move, loses.
-- Draws: the same position (same side to move) a third time, or 50 moves
-- each (100 in all) once both players have placed all their pieces, without
-- a mill. Placing a piece and closing a mill cannot be undone, so they start
-- both counts afresh.
--
-- The game's moves are kept as a string, three characters a move (from, to,
-- removed: 0..24 each).

return function(C)
local G = {}

G.NO_MILL_LIMIT = 100
local buf = {} -- legal moves, reused so a real move leaves little garbage

-- The position for repetition, as text: White's points as bits (times 4,
-- plus the side to move) and Black's points as bits, in hex. g.reps holds
-- the positions since the last placement or mill, one after the other.
local function key(g)
    local w, k, b = 0, 0, g.b
    for p = 24, 1, -1 do
        w = w * 2 + (b[p] == 1 and 1 or 0)
        k = k * 2 + (b[p] == 2 and 1 or 0)
    end
    return string.format("%07x%06x", w * 4 + g.side, k)
end

function G.new(flying)
    local g = C.new(flying)
    g.history, g.half, g.reps = "", 0, ""
    g.result, g.reason, g.last = nil, nil, nil
    return g
end

function G.moveCount(g) return #g.history // 3 end

-- After a real move: is the game over? Sets g.result to "white", "black" or
-- "draw" and g.reason.
function G.updateResult(g)
    local loser = g.side == C.WHITE and "black" or "white"
    if C.lost(g) then
        g.result, g.reason = loser, "pieces"
    elseif C.gen(g, buf, 0) == 0 then
        g.result, g.reason = loser, "blocked"
    elseif g.half >= G.NO_MILL_LIMIT then
        g.result, g.reason = "draw", "50 moves each without a mill"
    else
        local k, count = key(g), 0
        for i = 1, #g.reps, 13 do
            if g.reps:sub(i, i + 12) == k then count = count + 1 end
        end
        if count >= 3 then g.result, g.reason = "draw", "threefold repetition" end
    end
end

local DIGITS = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"

-- Plays a legal move in the real game. Returns false if it is not legal.
-- `replaying` leaves the history string to the caller.
function G.play(g, mv, replaying)
    if g.result then return false end
    local n, legal = C.gen(g, buf, 0), false
    for i = 1, n do
        if buf[i] == mv then
            legal = true
            break
        end
    end
    if not legal then return false end
    local from, to, rem = mv % 32, (mv // 32) % 32, mv // 1024
    C.make(g, mv)
    if not replaying then
        g.history = g.history .. DIGITS:sub(from + 1, from + 1) .. DIGITS:sub(to + 1, to + 1) ..
                        DIGITS:sub(rem + 1, rem + 1)
    end
    if from == 0 or rem > 0 or g.hand[1] + g.hand[2] > 0 then
        -- Cannot be undone: the counts start afresh.
        g.half, g.reps = 0, ""
    else
        g.half = g.half + 1
    end
    g.reps = g.reps .. key(g)
    g.last = mv
    G.updateResult(g)
    return true
end

-- A new game with the first `count` moves of `history` replayed (nil if a
-- move in it is not legal).
function G.replay(history, flying, count)
    local g = G.new(flying)
    count = math.min(count or (#history // 3), #history // 3)
    for k = 0, count - 1 do
        local mv, mul = 0, 1
        for j = 1, 3 do
            local d = DIGITS:find(history:sub(k * 3 + j, k * 3 + j), 1, true)
            if not d or d > 25 then return nil end
            mv = mv + (d - 1) * mul
            mul = mul * 32
        end
        if not G.play(g, mv, true) then return nil end
    end
    g.history = history:sub(1, count * 3)
    return g
end

-- Takes back the last `n` moves.
function G.takeBack(g, n) return G.replay(g.history, g.flying, G.moveCount(g) - n) end

return G
end
