-- Xiangqi (Chinese chess) rules: what the computer's search needs - move
-- generation, check, make/unmake. Setting up a game, playing a real move and
-- the results are in game.lua, loaded only for that. No drawing, so both can
-- be tested on a computer (test/lua_apps/xiangqi_test.lua).
--
-- The board is a 10x12 "mailbox": index = row * 10 + col, the 9x10 board
-- at cols 1..9 and rows 1..10 (row 1 Black's back rank, row 10 Red's), with
-- a border of OFF points so no move needs a bounds check. Column 0 borders
-- both sides; a horse or elephant stepping two columns over it is stopped
-- by its leg or eye on the border first. Pieces: 1..7 red general, advisor,
-- elephant, horse, chariot, cannon, soldier; 9..15 the black ones (+8). A
-- move is one integer: from + to * 256.

local C = {}

C.EMPTY, C.OFF = 0, 16
C.GENERAL, C.HORSE, C.CANNON, C.SOLDIER = 1, 4, 6, 7 -- also 2 advisor, 3 elephant, 5 chariot
C.RED, C.BLACK = 0, 1
-- A point is row * 10 + col: (11 - rank) * 10 + file, file 1..9 (a..i) and
-- rank 1..10 from Red's side. A move is from + to * 256.

local EMPTY, OFF = 0, 16
local GENERAL, ELEPHANT, HORSE, CHARIOT, CANNON, SOLDIER = 1, 3, 4, 5, 6, 7

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
-- generals facing each other on an open file). Only for generals' squares:
-- it assumes the square is in the defender's palace.
function C.attacked(g, sq, by)
    local b = g.board
    local add = by * 8
    local rook, cannon, gen, horse = CHARIOT + add, CANNON + add, GENERAL + add, HORSE + add
    for k = 1, 4 do
        local d = ORTH[k]
        local x = sq + d
        while b[x] == EMPTY do x = x + d end
        local v = b[x]
        if v == rook or v == gen then return true end
        if v ~= OFF then
            x = x + d
            while b[x] == EMPTY do x = x + d end
            if b[x] == cannon then return true end
        end
    end
    for k = 1, 8 do
        if b[sq + HORSE_TO[k]] == horse and b[sq + HORSE_AT_LEG[k]] == EMPTY then return true end
    end
    local pawn = SOLDIER + add
    if b[sq - 1] == pawn or b[sq + 1] == pawn then return true end
    if by == 0 then return b[sq + 10] == pawn end
    return b[sq - 10] == pawn
end

function C.inCheck(g, side) return C.attacked(g, g.kings[side + 1], 1 - side) end

-- Every pseudo-legal move for the side to move, written to out[base+1..]
-- (only captures when `noisy`). Returns the count.
function C.pseudoMoves(g, out, noisy, base)
    local b, side = g.board, g.side
    local add = side * 8
    local black = side == 1
    local n = base or 0
    local start = n
    for sq = 11, 109 do
        local p = b[sq]
        if p ~= EMPTY and p ~= OFF and (p >= 9) == black then
            local t = p - add
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
                    if v ~= OFF and (v >= 9) ~= black then n = n + 1 out[n] = sq + x * 256 end
                end
            elseif t == HORSE then
                for k = 1, 8 do
                    if b[sq + HORSE_LEG[k]] == EMPTY then
                        local x = sq + HORSE_TO[k]
                        local v = b[x] or OFF -- two rows past the bottom corner
                        if v == EMPTY then
                            if not noisy then n = n + 1 out[n] = sq + x * 256 end
                        elseif v ~= OFF and (v >= 9) ~= black then
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
                    elseif v ~= OFF and (v >= 9) ~= black then
                        n = n + 1 out[n] = sq + x * 256
                    end
                end
            else
                -- General, advisor, elephant: steps that stay on their side.
                local steps = t == GENERAL and ORTH or DIAG
                for k = 1, 4 do
                    local d = steps[k]
                    local x = sq + d
                    local ok
                    if t == ELEPHANT then
                        ok = b[x] == EMPTY
                        x = x + d
                        local row = x // 10
                        ok = ok and ((black and row <= 5) or (not black and row >= 6))
                    else
                        ok = inPalace(x, side)
                    end
                    if ok then
                        local v = b[x] or OFF
                        if v == EMPTY then
                            if not noisy then n = n + 1 out[n] = sq + x * 256 end
                        elseif v ~= OFF and (v >= 9) ~= black then
                            n = n + 1 out[n] = sq + x * 256
                        end
                    end
                end
            end
        end
    end
    return n - start
end

-- Makes a move; what it undoes is kept at depth g.ply.
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
    g.half = captured ~= EMPTY and 0 or g.half + 1
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
