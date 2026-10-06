-- Chess rules. No drawing, so it can be tested on a computer
-- (test/lua_apps/chess_test.lua). The computer opponent is in ai.lua.
--
-- All of the rules: castling, en passant, promotion, check, checkmate and
-- stalemate, and draws by the fifty-move rule, threefold repetition and
-- insufficient material.
--
-- The board is a 10x12 "mailbox": squares 21..98, a8 = 21, h8 = 28,
-- a1 = 91, h1 = 98, with a border of OFF squares so moves never need bounds
-- checks. Pieces: 1..6 white pawn, knight, bishop, rook, queen, king; 9..14
-- the black ones (+8). A move is one integer: from + to * 128 + promotion
-- piece type * 16384 + flag * 131072 (1 double pawn step, 2 en passant,
-- 3 castling).
--
-- The game's moves are kept as a string (four characters a move), not a
-- list of positions: going back or restoring a saved game replays it (see
-- history.lua, loaded only for that). Move notation is in view.lua.

local C = {}

C.EMPTY, C.OFF = 0, 7
C.PAWN, C.KNIGHT, C.BISHOP, C.ROOK, C.QUEEN, C.KING = 1, 2, 3, 4, 5, 6
C.WHITE, C.BLACK = 0, 1
C.DOUBLE, C.EN_PASSANT, C.CASTLE = 1, 2, 3

function C.square(file, rank) return 21 + (8 - rank) * 10 + (file - 1) end -- file 1..8 (a..h), rank 1..8
function C.fileOf(sq) return (sq - 21) % 10 + 1 end
function C.rankOf(sq) return 8 - (sq - 21) // 10 end
function C.colourOf(p) return p >= 9 and C.BLACK or C.WHITE end
function C.typeOf(p) return p >= 9 and p - 8 or p end

function C.encode(from, to, promo, flag) return from + to * 128 + (promo or 0) * 16384 + (flag or 0) * 131072 end
function C.from(mv) return mv % 128 end
function C.to(mv) return (mv // 128) % 128 end
function C.promo(mv) return (mv // 16384) % 8 end
function C.flag(mv) return mv // 131072 end

local KNIGHT_STEPS = { -21, -19, -12, -8, 8, 12, 19, 21 }
local KING_STEPS = { -11, -10, -9, -1, 1, 9, 10, 11 }
local DIAGONAL = { -11, -9, 9, 11 }
local STRAIGHT = { -10, -1, 1, 10 }

-- Castling rights: bits 1 white short, 2 white long, 4 black short, 8 black long.
-- A move from or to one of these squares clears the matching rights.
local RIGHTS_LOST = { [91] = 2, [95] = 3, [98] = 1, [21] = 8, [25] = 12, [28] = 4 }

function C.new()
    local g = { board = {}, side = C.WHITE, castle = 15, ep = 0, half = 0, full = 1, kings = { 95, 25 },
                history = "", reps = {}, result = nil, ply = 0, undo = {} }
    for i = 0, 120 do g.board[i] = C.OFF end -- a knight on a8 looks at square 0
    local back = { C.ROOK, C.KNIGHT, C.BISHOP, C.QUEEN, C.KING, C.BISHOP, C.KNIGHT, C.ROOK }
    for f = 1, 8 do
        g.board[C.square(f, 1)] = back[f]
        g.board[C.square(f, 2)] = C.PAWN
        for r = 3, 6 do g.board[C.square(f, r)] = C.EMPTY end
        g.board[C.square(f, 7)] = C.PAWN + 8
        g.board[C.square(f, 8)] = back[f] + 8
    end
    g.reps[1] = C.hash(g)
    return g
end

-- Whether `sq` is attacked by side `by`.
function C.attacked(g, sq, by)
    local b = g.board
    local add = by * 8
    -- Pawns: a white pawn on x attacks x-9 and x-11; a black one x+9, x+11.
    if by == C.WHITE then
        if b[sq + 9] == C.PAWN or b[sq + 11] == C.PAWN then return true end
    else
        if b[sq - 9] == C.PAWN + 8 or b[sq - 11] == C.PAWN + 8 then return true end
    end
    for _, d in ipairs(KNIGHT_STEPS) do
        if b[sq + d] == C.KNIGHT + add then return true end
    end
    for _, d in ipairs(KING_STEPS) do
        if b[sq + d] == C.KING + add then return true end
    end
    for _, d in ipairs(DIAGONAL) do
        local t = sq + d
        while b[t] == C.EMPTY do t = t + d end
        local p = b[t]
        if p == C.BISHOP + add or p == C.QUEEN + add then return true end
    end
    for _, d in ipairs(STRAIGHT) do
        local t = sq + d
        while b[t] == C.EMPTY do t = t + d end
        local p = b[t]
        if p == C.ROOK + add or p == C.QUEEN + add then return true end
    end
    return false
end

function C.inCheck(g, side) return C.attacked(g, g.kings[side + 1], 1 - side) end

-- Every pseudo-legal move for the side to move, written to out[base+1..]
-- (only captures and promotions when `noisy`). Returns the count.
function C.pseudoMoves(g, out, noisy, base)
    local b, side = g.board, g.side
    local add = side * 8
    base = base or 0
    local n = base
    local forward = side == C.WHITE and -10 or 10
    local startRank = side == C.WHITE and 2 or 7
    local lastRank = side == C.WHITE and 8 or 1
    for sq = 21, 98 do
        local p = b[sq]
        if p ~= C.EMPTY and p ~= C.OFF and C.colourOf(p) == side then
            local t = p - add
            if t == C.PAWN then
                local one = sq + forward
                local promoting = C.rankOf(one) == lastRank
                if b[one] == C.EMPTY then
                    if promoting then
                        for q = C.QUEEN, C.KNIGHT, -1 do n = n + 1 out[n] = C.encode(sq, one, q) end
                    elseif not noisy then
                        n = n + 1 out[n] = C.encode(sq, one)
                        local two = one + forward
                        if C.rankOf(sq) == startRank and b[two] == C.EMPTY then n = n + 1 out[n] = C.encode(sq, two, 0, C.DOUBLE) end
                    end
                end
                for k = 1, 2 do
                    local x = sq + forward + (k == 1 and -1 or 1)
                    local v = b[x]
                    if v ~= C.EMPTY and v ~= C.OFF and C.colourOf(v) ~= side then
                        if promoting then
                            for q = C.QUEEN, C.KNIGHT, -1 do n = n + 1 out[n] = C.encode(sq, x, q) end
                        else
                            n = n + 1 out[n] = C.encode(sq, x)
                        end
                    elseif x == g.ep and g.ep ~= 0 then
                        n = n + 1 out[n] = C.encode(sq, x, 0, C.EN_PASSANT)
                    end
                end
            elseif t == C.KNIGHT or t == C.KING then
                for _, d in ipairs(t == C.KNIGHT and KNIGHT_STEPS or KING_STEPS) do
                    local x = sq + d
                    local v = b[x]
                    if v == C.EMPTY then
                        if not noisy then n = n + 1 out[n] = C.encode(sq, x) end
                    elseif v ~= C.OFF and C.colourOf(v) ~= side then
                        n = n + 1 out[n] = C.encode(sq, x)
                    end
                end
            else
                local dirs = t == C.BISHOP and DIAGONAL or (t == C.ROOK and STRAIGHT or KING_STEPS)
                for _, d in ipairs(dirs) do
                    local x = sq + d
                    while b[x] == C.EMPTY do
                        if not noisy then n = n + 1 out[n] = C.encode(sq, x) end
                        x = x + d
                    end
                    local v = b[x]
                    if v ~= C.OFF and C.colourOf(v) ~= side then n = n + 1 out[n] = C.encode(sq, x) end
                end
            end
        end
    end
    -- Castling: rights, empty squares between, and no attacked square on
    -- the king's way (including where it starts).
    if not noisy then
        local home = side == C.WHITE and 95 or 25
        local opp = 1 - side
        local short, long = side == C.WHITE and 1 or 4, side == C.WHITE and 2 or 8
        if g.castle & short ~= 0 and b[home + 1] == C.EMPTY and b[home + 2] == C.EMPTY and
            not C.attacked(g, home, opp) and not C.attacked(g, home + 1, opp) and not C.attacked(g, home + 2, opp) then
            n = n + 1 out[n] = C.encode(home, home + 2, 0, C.CASTLE)
        end
        if g.castle & long ~= 0 and b[home - 1] == C.EMPTY and b[home - 2] == C.EMPTY and b[home - 3] == C.EMPTY and
            not C.attacked(g, home, opp) and not C.attacked(g, home - 1, opp) and not C.attacked(g, home - 2, opp) then
            n = n + 1 out[n] = C.encode(home, home - 2, 0, C.CASTLE)
        end
    end
    return n - base
end

-- Makes a move; the undo record goes on g.undo at depth g.ply.
function C.make(g, mv)
    local b = g.board
    local from, to, promo, flag = C.from(mv), C.to(mv), C.promo(mv), C.flag(mv)
    local p = b[from]
    local side = g.side
    local captured = b[to]
    local capSq = to
    if flag == C.EN_PASSANT then
        capSq = to + (side == C.WHITE and 10 or -10)
        captured = b[capSq]
        b[capSq] = C.EMPTY
    end
    g.ply = g.ply + 1
    local u = g.undo[g.ply]
    if not u then
        u = {}
        g.undo[g.ply] = u
    end
    u[1], u[2], u[3], u[4], u[5] = captured, g.castle, g.ep, g.half, capSq
    b[to] = (promo > 0) and (promo + side * 8) or p
    b[from] = C.EMPTY
    if flag == C.CASTLE then
        if to > from then
            b[to - 1], b[to + 1] = b[to + 1], C.EMPTY
        else
            b[to + 1], b[to - 2] = b[to - 2], C.EMPTY
        end
    end
    if C.typeOf(p) == C.KING then g.kings[side + 1] = to end
    g.castle = g.castle & ~((RIGHTS_LOST[from] or 0) | (RIGHTS_LOST[to] or 0))
    g.ep = (flag == C.DOUBLE) and (from + to) // 2 or 0
    g.half = (C.typeOf(p) == C.PAWN or captured ~= C.EMPTY) and 0 or g.half + 1
    g.side = 1 - side
end

function C.unmake(g, mv)
    local b = g.board
    local from, to, promo, flag = C.from(mv), C.to(mv), C.promo(mv), C.flag(mv)
    local u = g.undo[g.ply]
    g.ply = g.ply - 1
    local side = 1 - g.side
    g.side = side
    local p = b[to]
    if promo > 0 then p = C.PAWN + side * 8 end
    b[from] = p
    b[to] = C.EMPTY
    b[u[5]] = u[1]
    if flag == C.CASTLE then
        if to > from then
            b[to + 1], b[to - 1] = b[to - 1], C.EMPTY
        else
            b[to - 2], b[to + 1] = b[to + 1], C.EMPTY
        end
    end
    if C.typeOf(p) == C.KING then g.kings[side + 1] = from end
    g.castle, g.ep, g.half = u[2], u[3], u[4]
end

-- The legal moves for the side to move (into `out` when given).
function C.legalMoves(g, out)
    out = out or {}
    local buf = {}
    local n = C.pseudoMoves(g, buf)
    local k = 0
    local side = g.side
    for i = 1, n do
        local mv = buf[i]
        C.make(g, mv)
        if not C.inCheck(g, side) then
            k = k + 1
            out[k] = mv
        end
        C.unmake(g, mv)
    end
    for i = k + 1, #out do out[i] = nil end
    return out
end

-- A hash of the position for repetition (board, side, castling, en passant).
function C.hash(g)
    local h = g.side * 7 + g.castle * 131 + g.ep * 977
    local b = g.board
    for sq = 21, 98 do
        local v = b[sq]
        if v ~= C.OFF then h = (h * 31 + v) & 0x3fffffff end
    end
    return h
end

local function insufficient(g)
    local minors, other = 0, 0
    for sq = 21, 98 do
        local v = g.board[sq]
        if v ~= C.EMPTY and v ~= C.OFF then
            local t = C.typeOf(v)
            if t == C.KNIGHT or t == C.BISHOP then
                minors = minors + 1
            elseif t ~= C.KING then
                other = other + 1
            end
        end
    end
    return other == 0 and minors <= 1
end

-- After a real move: is the game over? Sets g.result to "white", "black" or
-- "draw" and g.reason.
function C.updateResult(g)
    local moves = C.legalMoves(g)
    if #moves == 0 then
        if C.inCheck(g, g.side) then
            g.result, g.reason = (g.side == C.WHITE) and "black" or "white", "checkmate"
        else
            g.result, g.reason = "draw", "stalemate"
        end
    elseif g.half >= 100 then
        g.result, g.reason = "draw", "fifty moves without a capture or pawn move"
    elseif insufficient(g) then
        g.result, g.reason = "draw", "not enough material to mate"
    else
        local h, count = C.hash(g), 0
        for _, x in ipairs(g.reps) do
            if x == h then count = count + 1 end
        end
        if count >= 3 then g.result, g.reason = "draw", "threefold repetition" end
    end
    return moves
end

-- Four printable characters per move in the history string.
local DIGITS = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-_"

local function packMove(mv)
    local s = ""
    for _ = 1, 4 do
        local d = mv % 64
        s = s .. DIGITS:sub(d + 1, d + 1)
        mv = mv // 64
    end
    return s
end

-- Plays a legal move in the real game. Returns false if it is not legal.
function C.play(g, mv)
    if g.result then return false end
    local legal = false
    for _, m in ipairs(C.legalMoves(g)) do
        if m == mv then legal = true break end
    end
    if not legal then return false end
    local moverIsBlack = g.side == C.BLACK
    C.make(g, mv)
    g.lastCaptured = g.undo[g.ply][1] ~= C.EMPTY
    g.ply = 0 -- the undo stack is only for search; a real move is final
    if moverIsBlack then g.full = g.full + 1 end
    g.history = g.history .. packMove(mv)
    if g.half == 0 then g.reps = {} end
    g.reps[#g.reps + 1] = C.hash(g)
    g.last = mv
    C.updateResult(g)
    return true
end

function C.moveCount(g) return #g.history // 4 end

return C
