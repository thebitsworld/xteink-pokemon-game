-- Xiangqi games: setting one up, playing a real move, the result, and the
-- move history (restoring a saved game, taking moves back). Usage:
--   local G = smudge.dofile("game.lua")(C)
-- with C the rules (logic.lua). Loaded only for a real move or a new game and
-- dropped after, so the computer's search never has it in memory.
--
-- Results: a side with no legal move loses (checkmate and stalemate alike);
-- draws by threefold repetition, 120 plies (60 moves each) without a
-- capture, or when neither side has a piece that can cross the river.
--
-- The game's moves are kept as a string, three characters a move.

return function(C)
local G = {}

local EMPTY, OFF = C.EMPTY, C.OFF
local function square(f, r) return (11 - r) * 10 + f end
G.NO_CAPTURE_LIMIT = 120
local buf = {} -- legal moves, reused so a real move leaves little garbage

-- A hash of the position for repetition (board and side to move).
function G.hash(g)
    local h = g.side * 7
    local b = g.board
    for sq = 11, 109 do
        local v = b[sq]
        if v ~= OFF then h = (h * 31 + v) & 0x3fffffff end
    end
    return h
end

-- An empty board (`side` to move); place pieces with G.put.
function G.empty(side)
    local g = { board = {}, side = side or C.RED, half = 0, kings = { 0, 0 }, history = "", reps = {},
                result = nil, ply = 0, ucap = {}, uhalf = {} }
    local b = g.board
    for i = 0, 119 do b[i] = OFF end
    for r = 1, 10 do
        for f = 1, 9 do b[square(f, r)] = EMPTY end
    end
    return g
end

function G.put(g, sq, p)
    g.board[sq] = p
    if p == C.GENERAL then g.kings[1] = sq elseif p == C.GENERAL + 8 then g.kings[2] = sq end
end

local BACK = "RHEAKAEHR"
local TYPES = "KAEHRCP"

function G.new()
    local g = G.empty(C.RED)
    for f = 1, 9 do
        local t = TYPES:find(BACK:sub(f, f), 1, true)
        G.put(g, square(f, 1), t)
        G.put(g, square(f, 10), t + 8)
    end
    for f = 2, 8, 6 do
        G.put(g, square(f, 3), C.CANNON)
        G.put(g, square(f, 8), C.CANNON + 8)
    end
    for f = 1, 9, 2 do
        G.put(g, square(f, 4), C.SOLDIER)
        G.put(g, square(f, 7), C.SOLDIER + 8)
    end
    g.reps[1] = G.hash(g)
    return g
end

-- Whether either side still has a horse, chariot, cannon or soldier.
local function attackers(g)
    local b = g.board
    for sq = 11, 109 do
        local v = b[sq]
        if v ~= OFF and v ~= EMPTY and (v >= 9 and v - 8 or v) >= C.HORSE then return true end
    end
    return false
end

-- After a real move: is the game over? Sets g.result to "red", "black" or
-- "draw" and g.reason.
function G.updateResult(g)
    if not C.legalMoves(g, buf)[1] then
        g.result = g.side == C.RED and "black" or "red"
        g.reason = C.inCheck(g, g.side) and "checkmate" or "stalemate"
    elseif g.half >= G.NO_CAPTURE_LIMIT then
        g.result, g.reason = "draw", "60 moves each without a capture"
    elseif not attackers(g) then
        g.result, g.reason = "draw", "no attacking pieces left"
    else
        local h, count = G.hash(g), 0
        for _, x in ipairs(g.reps) do
            if x == h then count = count + 1 end
        end
        if count >= 3 then g.result, g.reason = "draw", "threefold repetition" end
    end
end

local DIGITS = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-_"

-- Plays a legal move in the real game. Returns false if it is not legal.
-- `replaying` leaves the history string to the caller.
function G.play(g, mv, replaying)
    if g.result then return false end
    local legal = false
    for _, m in ipairs(C.legalMoves(g, buf)) do
        if m == mv then legal = true break end
    end
    if not legal then return false end
    C.make(g, mv)
    g.lastCaptured = g.ucap[g.ply] ~= EMPTY
    g.ply = 0 -- the undo stack is only for search; a real move is final
    if not replaying then
        local a, b, c = mv % 64, (mv // 64) % 64, mv // 4096
        g.history = g.history .. DIGITS:sub(a + 1, a + 1) .. DIGITS:sub(b + 1, b + 1) .. DIGITS:sub(c + 1, c + 1)
    end
    if g.half == 0 then g.reps = {} end
    g.reps[#g.reps + 1] = G.hash(g)
    g.last = mv
    G.updateResult(g)
    return true
end

-- A new game with the first `count` moves of `history` replayed (nil if a
-- move in it is not legal).
function G.replay(history, count)
    local g = G.new()
    count = math.min(count or (#history // 3), #history // 3)
    for k = 0, count - 1 do
        local mv, mul = 0, 1
        for j = 1, 3 do
            local d = DIGITS:find(history:sub(k * 3 + j, k * 3 + j), 1, true)
            if not d then return nil end
            mv = mv + (d - 1) * mul
            mul = mul * 64
        end
        if not G.play(g, mv, true) then return nil end
    end
    g.history = history:sub(1, count * 3)
    return g
end

-- Takes back the last `n` moves.
function G.takeBack(g, n) return G.replay(g.history, C.moveCount(g) - n) end

return G
end
