-- Co Ganh games: setting one up, playing a real move, the result, and the
-- move history (restoring a saved game, taking moves back). Usage:
--   local G = smudge.dofile("game.lua")(C)
-- with C the rules (logic.lua). Loaded only for a real move or a new game and
-- dropped after, so the computer's search never has it in memory.
--
-- Results: a player whose last piece is converted loses (the winner then has
-- all 16). Draws: the same position (same side to move) a third time, 50
-- moves each (100 in all) in which no piece changes colour - a conversion
-- cannot be undone, so it starts both counts afresh - or 150 moves each
-- (300 in all) in the whole game, as pieces can change sides back and forth
-- for ever.
--
-- The game's moves are kept as a string, two letters a move (from, to: A..Y
-- for points 1..25).

return function(C)
local G = {}

G.QUIET_LIMIT = 100
G.MOVE_LIMIT = 300
local buf = {} -- legal moves, reused so a real move leaves little garbage

-- The position for repetition, as text: White's points as bits (times 2,
-- plus the side to move) and Black's points as bits, in hex.
local function key(g)
    local w, k, b = 0, 0, g.b
    for p = 25, 1, -1 do
        w = w * 2 + (b[p] == 1 and 1 or 0)
        k = k * 2 + (b[p] == 2 and 1 or 0)
    end
    return string.format("%08x%07x", w * 2 + g.side - 1, k)
end

function G.new(forced)
    local g = C.new(forced)
    g.history, g.half, g.reps, g.moves = "", 0, key(g), 0
    g.result, g.reason, g.last, g.conv = nil, nil, nil, nil
    g.ganh, g.vay = 0, 0
    return g
end

function G.moveCount(g) return #g.history // 2 end

-- After a real move: is the game over? Sets g.result to "white", "black" or
-- "draw" and g.reason.
function G.updateResult(g)
    local loser = g.side == C.WHITE and "black" or "white"
    if g.on[g.side] == 0 then
        g.result, g.reason = loser, "pieces"
    elseif C.gen(g, buf, 0) == 0 then
        g.result, g.reason = loser, "blocked"
    elseif g.half >= G.QUIET_LIMIT then
        g.result, g.reason = "draw", "50 moves each without a capture"
    elseif g.moves >= G.MOVE_LIMIT then
        g.result, g.reason = "draw", "150 moves each"
    else
        local k, count = key(g), 0
        for i = 1, #g.reps, 15 do
            if g.reps:sub(i, i + 14) == k then count = count + 1 end
        end
        if count >= 3 then g.result, g.reason = "draw", "threefold repetition" end
    end
end

-- Plays a legal move in the real game. Returns false if it is not legal.
-- `replaying` leaves the history string to the caller. Sets g.last, g.conv
-- (the converted points), g.ganh and g.vay (how many each way).
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
    local from, to = mv % 32, mv // 32
    local ganh = C.ganhCount(g.b, to, g.side)
    local k = C.make(g, mv)
    -- The search's undo lists are not needed for a real move: only the
    -- point just left (for the "mo" rule) is kept.
    g.conv = C.lastConverted(g)
    g.ct, g.ply, g.lf[1] = 0, 1, from
    g.ganh, g.vay = ganh, k - ganh
    if not replaying then
        g.history = g.history .. string.char(64 + from, 64 + to)
    end
    if k > 0 then
        g.half, g.reps = 0, ""
    else
        g.half = g.half + 1
    end
    g.reps = g.reps .. key(g)
    g.last, g.moves = mv, g.moves + 1
    G.updateResult(g)
    return true
end

-- A new game with the first `count` moves of `history` replayed (nil if a
-- move in it is not legal).
function G.replay(history, forced, count)
    local g = G.new(forced)
    count = math.min(count or (#history // 2), #history // 2)
    for i = 0, count - 1 do
        local a, b = history:byte(i * 2 + 1, i * 2 + 2)
        a, b = a - 64, b - 64
        if a < 1 or a > 25 or b < 1 or b > 25 then return nil end
        if not G.play(g, a + b * 32, true) then return nil end
    end
    g.history = history:sub(1, count * 2)
    return g
end

-- Takes back the last `n` moves.
function G.takeBack(g, n) return G.replay(g.history, g.forced, G.moveCount(g) - n) end

return G
end
