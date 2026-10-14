-- Co Up games: dealing the face-down pieces, playing a real move (turning a
-- piece face up), the result, and the move history (restoring a saved game,
-- taking moves back). Usage:
--   local G = smudge.dofile("game.lua")(C)
-- with C the rules (logic.lua). Loaded only for a real move or a new game and
-- dropped after, so the computer's search never has it in memory.
--
-- What each face-down piece really is lives in g.secret (point -> type),
-- which only this file reads: the board shows the piece face down, with the
-- role of its point as its type. A real move by a face-down piece turns it
-- up; capturing one shows what it was (g.lost keeps every captured piece of
-- each side, face up, which is all the computer may know of the hidden ones).
--
-- Results: a side with no legal move loses (checkmate and stalemate alike);
-- draws by threefold repetition, 120 plies (60 moves each) without a
-- capture or a piece turned up, or when neither side has a piece that can
-- cross the river (only the generals left).
--
-- A game is its deal (g.deal: 30 letters, what lies on each starting point,
-- Red's 15 then Black's) and its moves (a string, three characters a move).

return function(C)
local G = {}

local EMPTY, OFF, HIDDEN = C.EMPTY, C.OFF, C.HIDDEN
local function square(f, r) return (11 - r) * 10 + f end
G.NO_CAPTURE_LIMIT = 120
local buf = {} -- legal moves, reused so a real move leaves little garbage

local TYPES = "KAEHRCP"
G.TYPES = TYPES
-- Each side's 15 pieces besides the general.
G.SET = "AAEEHHRRCCPPPPP"

-- The starting points other than the general's, from Red's side (file,
-- rank) and the role each gives: the deal's order.
local POINTS = { 1, 1, 2, 1, 3, 1, 4, 1, 6, 1, 7, 1, 8, 1, 9, 1, 2, 3, 8, 3, 1, 4, 3, 4, 5, 4, 7, 4, 9, 4 }
local ROLES = "RHEAAEHRCCPPPPP"
G.ROLES = ROLES

-- The point of the deal's i-th piece (1..15) for `side`.
function G.point(i, side)
    local f, r = POINTS[2 * i - 1], POINTS[2 * i]
    if side == C.BLACK then r = 11 - r end
    return square(f, r)
end

-- A hash of the position for repetition (board and side to move).
function G.hash(g)
    local h = g.side * 7
    local b = g.board
    for sq = 11, 109 do
        local v = b[sq]
        if v ~= OFF then h = (h * 37 + v) & 0x3fffffff end
    end
    return h
end

-- An empty board (`side` to move); place pieces with G.put.
function G.empty(side)
    local g = { board = {}, side = side or C.RED, half = 0, kings = { 0, 0 }, history = "", reps = {},
                result = nil, ply = 0, ucap = {}, uhalf = {}, secret = {}, lost = { "", "" }, deal = "" }
    local b = g.board
    for i = 0, 119 do b[i] = OFF end
    for r = 1, 10 do
        for f = 1, 9 do b[square(f, r)] = EMPTY end
    end
    return g
end

-- Puts piece `p` on `sq`; a face-down piece also needs what it really is
-- (`real`, a type 1..7).
function G.put(g, sq, p, real)
    g.board[sq] = p
    g.secret[sq] = real
    if p == C.GENERAL then g.kings[1] = sq elseif p == C.GENERAL + 8 then g.kings[2] = sq end
end

-- A random deal: each side's 15 pieces shuffled (`rand(n)` gives 1..n).
function G.shuffle(rand)
    local out = {}
    for side = 0, 1 do
        local t = {}
        for i = 1, 15 do t[i] = G.SET:sub(i, i) end
        for i = 15, 2, -1 do
            local j = rand(i)
            t[i], t[j] = t[j], t[i]
        end
        out[side + 1] = table.concat(t)
    end
    return out[1] .. out[2]
end

-- A new game from a deal (G.shuffle): the generals face up, the rest face
-- down on the other starting points.
function G.new(deal)
    local g = G.empty(C.RED)
    g.deal = deal
    G.put(g, square(5, 1), C.GENERAL)
    G.put(g, square(5, 10), C.GENERAL + 8)
    for side = 0, 1 do
        for i = 1, 15 do
            local role = TYPES:find(ROLES:sub(i, i), 1, true)
            local k = side * 15 + i
            local real = TYPES:find(deal:sub(k, k), 1, true)
            G.put(g, G.point(i, side), HIDDEN + side * 8 + role, real)
        end
    end
    g.reps[1] = G.hash(g)
    G.averages(g)
    return g
end

-- Each side's face-down pool, from what is public: its set less its face-up
-- pieces and the ones it lost (never g.secret). Returns counts[side+1][type].
local SET_COUNT = { 0, 2, 2, 2, 2, 2, 5 }
function G.pool(g)
    local count = { {}, {} }
    for s = 1, 2 do
        for t = 1, 7 do count[s][t] = SET_COUNT[t] end
    end
    local b = g.board
    for sq = 11, 109 do
        local p = b[sq]
        if p ~= EMPTY and p ~= OFF and p < HIDDEN and p & 7 ~= C.GENERAL then
            local c = count[(p & 8) // 8 + 1]
            c[p & 7] = c[p & 7] - 1
        end
    end
    for s = 1, 2 do
        local lost = g.lost[s]
        for i = 1, #lost do
            local t = TYPES:find(lost:sub(i, i), 1, true)
            count[s][t] = count[s][t] - 1
        end
    end
    return count
end

-- Sets g.avg: the average value (C.VALUE) of each side's face-down pool,
-- for the computer.
function G.averages(g)
    local count = G.pool(g)
    g.avg = g.avg or {}
    for s = 1, 2 do
        local n, sum = 0, 0
        for t = 2, 7 do
            n = n + count[s][t]
            sum = sum + count[s][t] * C.VALUE[t]
        end
        g.avg[s] = n > 0 and sum // n or 300
    end
end

-- Whether a deal string is well formed: each side has exactly its set.
function G.validDeal(deal)
    if type(deal) ~= "string" or #deal ~= 30 then return false end
    for side = 0, 1 do
        local t = {}
        for i = 1, 15 do t[i] = deal:sub(side * 15 + i, side * 15 + i) end
        table.sort(t)
        if table.concat(t) ~= "AACCEEHHPPPPPRR" then return false end
    end
    return true
end

-- Whether either side still has a piece besides its general (every piece
-- here may cross the river once face up, advisors and elephants included).
local function attackers(g)
    local b = g.board
    for sq = 11, 109 do
        local v = b[sq]
        if v ~= OFF and v ~= EMPTY and v & 7 ~= C.GENERAL then return true end
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

-- Plays a legal move in the real game, turning up a face-down piece that
-- moves and showing one that is captured (g.flipped and g.taken: the types,
-- or nil). Returns false if it is not legal. `replaying` leaves the history
-- string to the caller.
function G.play(g, mv, replaying)
    if g.result then return false end
    local legal = false
    for _, m in ipairs(C.legalMoves(g, buf)) do
        if m == mv then legal = true break end
    end
    if not legal then return false end
    local b = g.board
    local from, to = mv % 256, mv // 256
    local victim = b[to]
    local taken = nil
    if victim ~= EMPTY then
        local vs = (victim & 8) // 8
        taken = victim >= HIDDEN and g.secret[to] or victim & 7
        g.lost[vs + 1] = g.lost[vs + 1] .. TYPES:sub(taken, taken)
    end
    g.taken = taken
    g.takenHidden = victim >= HIDDEN
    g.secret[to] = nil
    local flipped = nil
    if b[from] >= HIDDEN then
        flipped = g.secret[from]
        g.secret[from] = nil
    end
    C.make(g, mv)
    if flipped then b[to] = flipped + (1 - g.side) * 8 end
    g.flipped = flipped
    g.lastCaptured = victim ~= EMPTY
    g.ply = 0 -- the undo stack is only for search; a real move is final
    if not replaying then
        local a, b2, c = mv % 64, (mv // 64) % 64, mv // 4096
        g.history = g.history .. DIGITS:sub(a + 1, a + 1) .. DIGITS:sub(b2 + 1, b2 + 1) .. DIGITS:sub(c + 1, c + 1)
    end
    if g.half == 0 then g.reps = {} end
    g.reps[#g.reps + 1] = G.hash(g)
    g.last = mv
    if not replaying and (flipped or victim ~= EMPTY) then G.averages(g) end
    G.updateResult(g)
    return true
end

-- A new game from `deal` with the first `count` moves of `history` replayed
-- (nil if the deal or a move in it is not valid).
function G.replay(deal, history, count)
    if not G.validDeal(deal) then return nil end
    local g = G.new(deal)
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
        if k % 8 == 7 then collectgarbage("collect") end -- the X3's heap: keep the garbage low
    end
    g.history = history:sub(1, count * 3)
    G.averages(g)
    return g
end

-- Takes back the last `n` moves (the face-down pieces go back unchanged).
function G.takeBack(g, n) return G.replay(g.deal, g.history, C.moveCount(g) - n) end

return G
end
