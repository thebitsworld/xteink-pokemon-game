-- Co Up (cờ úp, hidden xiangqi) rules: what the computer's search needs -
-- move generation, check, make/unmake. Setting up a game, playing a real move
-- (turning pieces face up) and the results are in game.lua, loaded only for
-- that. No drawing, so both can be tested on a computer
-- (test/lua_apps/coup_test.lua).
--
-- The board is a 10x12 "mailbox" as in the xiangqi app: index = row * 10 +
-- col, the 9x10 board at cols 1..9 and rows 1..10 (row 1 Black's back rank,
-- row 10 Red's), with a border of OFF points so no move needs a bounds check.
--
-- A piece is type + 8 for Black + 16 when face down: types 1..7 general,
-- advisor, elephant, horse, chariot, cannon, soldier. A face-down piece's
-- type is the role of the point it started on - how it moves - not what it
-- is: that is kept apart in game.lua (g.secret) and never read here, so the
-- computer's search cannot see it. OFF (16) is no real piece: there is no
-- face-down general. A move is one integer: from + to * 256.
--
-- Face-up advisors and elephants are free (the usual cờ úp rule): an advisor
-- steps one point diagonally anywhere, an elephant two (still blocked on its
-- eye) and it may cross the river. Face-down ones move as from their home
-- points: the advisor into its palace, the elephant on its own side.

local C = {}

C.EMPTY, C.OFF = 0, 16
C.GENERAL, C.HORSE, C.CANNON, C.SOLDIER = 1, 4, 6, 7 -- also 2 advisor, 3 elephant, 5 chariot
C.HIDDEN = 16
C.RED, C.BLACK = 0, 1
-- What the computer thinks each type is worth. Face-up advisors and
-- elephants roam the board here, so they are worth a little more than in
-- xiangqi.
C.VALUE = { 0, 220, 220, 400, 900, 450, 100 }

local EMPTY, OFF = 0, 16
local GENERAL, ADVISOR, ELEPHANT, HORSE, CHARIOT, CANNON, SOLDIER = 1, 2, 3, 4, 5, 6, 7

local ORTH = { -10, -1, 1, 10 }
local DIAG = { -11, -9, 9, 11 }
-- Horse moves and the leg each one steps over.
local HORSE_TO = { -21, -19, -12, -8, 8, 12, 19, 21 }
local HORSE_LEG = { -10, -10, -1, 1, -1, 1, 10, 10 }
-- A horse at general + HORSE_TO[k] reaches the general unless the square
-- next to the general on that diagonal is taken.
local HORSE_AT_LEG = { -11, -9, -11, -9, 9, 11, 9, 11 }

local function inPalace(sq, side)
    local f = sq % 10
    if f < 4 or f > 6 then return false end
    local row = sq // 10
    if side == 0 then return row >= 8 end
    return row <= 3
end

-- Whether the general on `sq` is attacked by side `by` (including the two
-- generals facing each other on an open file). Face-down pieces attack as
-- their role. Only for generals' squares: it assumes the square is in the
-- defender's palace.
function C.attacked(g, sq, by)
    local b = g.board
    local add = by * 8
    local rook, cannon, gen, horse = CHARIOT + add, CANNON + add, GENERAL + add, HORSE + add
    for k = 1, 4 do
        local d = ORTH[k]
        local x = sq + d
        while b[x] == EMPTY do x = x + d end
        local v = b[x] & 15
        if v == rook or v == gen then return true end
        if b[x] ~= OFF then
            x = x + d
            while b[x] == EMPTY do x = x + d end
            if b[x] & 15 == cannon then return true end
        end
    end
    for k = 1, 8 do
        local v = b[sq + HORSE_TO[k]]
        if v and v & 15 == horse and v ~= OFF and b[sq + HORSE_AT_LEG[k]] == EMPTY then return true end
    end
    -- Face-up advisors and elephants roam, so they can reach a general too.
    local adv, ele = ADVISOR + add, ELEPHANT + add
    for k = 1, 4 do
        local d = DIAG[k]
        if b[sq + d] == adv or (b[sq + 2 * d] == ele and b[sq + d] == EMPTY) then return true end
    end
    local pawn = SOLDIER + add
    if b[sq - 1] & 15 == pawn or b[sq + 1] & 15 == pawn then return true end
    if by == 0 then return b[sq + 10] & 15 == pawn end
    return b[sq - 10] & 15 == pawn
end

function C.inCheck(g, side) return C.attacked(g, g.kings[side + 1], 1 - side) end

-- Every pseudo-legal move for the side to move, written to out[base+1..]
-- (only captures when `noisy`). Returns the count.
function C.pseudoMoves(g, out, noisy, base)
    local b, side = g.board, g.side
    local black = side == 1
    local n = base or 0
    local start = n
    for sq = 11, 109 do
        local p = b[sq]
        if p ~= EMPTY and p ~= OFF and ((p & 8) ~= 0) == black then
            local t = p & 7
            if t == CHARIOT or t == CANNON then
                for k = 1, 4 do
                    local d = ORTH[k]
                    local x = sq + d
                    while b[x] == EMPTY do
                        if not noisy then n = n + 1 out[n] = sq + x * 256 end
                        x = x + d
                    end
                    if t == CANNON and b[x] ~= OFF then
                        x = x + d
                        while b[x] == EMPTY do x = x + d end
                    end
                    local v = b[x]
                    if v ~= OFF and ((v & 8) ~= 0) ~= black then n = n + 1 out[n] = sq + x * 256 end
                end
            elseif t == HORSE then
                for k = 1, 8 do
                    if b[sq + HORSE_LEG[k]] == EMPTY then
                        local x = sq + HORSE_TO[k]
                        local v = b[x] or OFF -- two rows past the bottom corner
                        if v == EMPTY then
                            if not noisy then n = n + 1 out[n] = sq + x * 256 end
                        elseif v ~= OFF and ((v & 8) ~= 0) ~= black then
                            n = n + 1 out[n] = sq + x * 256
                        end
                    end
                end
            elseif t == SOLDIER then
                local row = sq // 10
                local fwd = black and 10 or -10
                local crossed = (black and row >= 6) or (not black and row <= 5)
                for k = 1, crossed and 3 or 1 do
                    local x = sq + (k == 1 and fwd or (k == 2 and -1 or 1))
                    local v = b[x]
                    if v == EMPTY then
                        if not noisy then n = n + 1 out[n] = sq + x * 256 end
                    elseif v ~= OFF and ((v & 8) ~= 0) ~= black then
                        n = n + 1 out[n] = sq + x * 256
                    end
                end
            else
                -- General, advisor, elephant. The general and face-down
                -- guards keep to their palace and side; face-up guards are free.
                local steps = t == GENERAL and ORTH or DIAG
                local bound = t == GENERAL or p >= 16
                for k = 1, 4 do
                    local d = steps[k]
                    local x = sq + d
                    local ok = true
                    if t == ELEPHANT then
                        ok = b[x] == EMPTY
                        x = x + d
                        if bound then
                            local row = x // 10
                            ok = ok and ((black and row <= 5) or (not black and row >= 6))
                        end
                    elseif bound then
                        ok = inPalace(x, side)
                    end
                    if ok then
                        local v = b[x] or OFF
                        if v == EMPTY then
                            if not noisy then n = n + 1 out[n] = sq + x * 256 end
                        elseif v ~= OFF and ((v & 8) ~= 0) ~= black then
                            n = n + 1 out[n] = sq + x * 256
                        end
                    end
                end
            end
        end
    end
    return n - start
end

-- Makes a move; what it undoes is kept at depth g.ply. A face-down piece
-- stays face down here (the search does not know what it is); game.lua turns
-- it up after a real move.
function C.make(g, mv)
    local b = g.board
    local from, to = mv % 256, mv // 256
    local p = b[from]
    local captured = b[to]
    local ply = g.ply + 1
    g.ply = ply
    g.ucap[ply], g.uhalf[ply] = captured, g.half
    b[to], b[from] = p, EMPTY
    if p == GENERAL then g.kings[1] = to elseif p == GENERAL + 8 then g.kings[2] = to end
    g.half = (captured ~= EMPTY or p >= 16) and 0 or g.half + 1
    g.side = 1 - g.side
end

function C.unmake(g, mv)
    local b = g.board
    local from, to = mv % 256, mv // 256
    local ply = g.ply
    local p = b[to]
    b[from], b[to] = p, g.ucap[ply]
    g.half = g.uhalf[ply]
    g.ply = ply - 1
    if p == GENERAL then g.kings[1] = from elseif p == GENERAL + 8 then g.kings[2] = from end
    g.side = 1 - g.side
end

-- The legal moves for the side to move (into `out` when given: the
-- pseudo-legal moves are written there and the legal ones kept in place).
function C.legalMoves(g, out)
    out = out or {}
    local old = #out
    local n = C.pseudoMoves(g, out)
    local k = 0
    local side = g.side
    for i = 1, n do
        local mv = out[i]
        C.make(g, mv)
        if not C.inCheck(g, side) then
            k = k + 1
            out[k] = mv
        end
        C.unmake(g, mv)
    end
    for i = k + 1, math.max(n, old) do out[i] = nil end
    return out
end

function C.moveCount(g) return #g.history // 3 end

return C
