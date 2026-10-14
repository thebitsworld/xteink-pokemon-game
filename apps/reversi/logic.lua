-- Reversi rules. Usage: local L = smudge.dofile("logic.lua") (no smudge
-- calls, so test/lua_apps/reversi_test.lua runs it on a computer).
--
-- The board is a flat 100-cell table with a border: the square in column c
-- (1..8, a..h) and row r (1..8, top to bottom) is r * 10 + c, and the border
-- cells hold OFF, so a walk in any of the eight directions stops there
-- without bounds checks.
--
-- A game: { b = board, side = to move, history = moves played (one
-- character each), sides = who played each move, last = last square,
-- passed = the side that had to pass right before this turn, result = nil
-- or "black" / "white" / "draw" }. Passes are not stored: replaying the
-- moves finds them again.

local L = {}

local EMPTY, BLACK, WHITE, OFF = 0, 1, 2, 3
L.EMPTY, L.BLACK, L.WHITE, L.OFF = EMPTY, BLACK, WHITE, OFF
local DIRS = { -11, -10, -9, -1, 1, 9, 10, 11 }
L.DIRS = DIRS

function L.square(c, r) return r * 10 + c end
function L.name(sq) return string.char(96 + sq % 10) .. (sq // 10) end

function L.newBoard()
    local b = {}
    for i = 0, 99 do b[i] = OFF end
    for r = 1, 8 do
        for c = 1, 8 do b[r * 10 + c] = EMPTY end
    end
    b[44], b[55] = WHITE, WHITE -- d4, e5
    b[45], b[54] = BLACK, BLACK -- e4, d5
    return b
end

-- Whether `side` may play on `sq`: it must flip at least one disc.
local function canPlay(b, sq, side)
    if b[sq] ~= EMPTY then return false end
    local opp = 3 - side
    for i = 1, 8 do
        local d = DIRS[i]
        local p = sq + d
        if b[p] == opp then
            p = p + d
            while b[p] == opp do p = p + d end
            if b[p] == side then return true end
        end
    end
    return false
end
L.canPlay = canPlay

-- The legal squares of `side` into out[base+1..], in reading order. Returns
-- how many.
function L.moves(b, side, out, base)
    base = base or 0
    local n = 0
    for sq = 11, 88 do
        if b[sq] == EMPTY and canPlay(b, sq, side) then
            n = n + 1
            out[base + n] = sq
        end
    end
    return n
end

function L.hasMove(b, side)
    for sq = 11, 88 do
        if b[sq] == EMPTY and canPlay(b, sq, side) then return true end
    end
    return false
end

-- How many discs a move on `sq` would flip (0: not legal).
function L.flipCount(b, sq, side)
    if b[sq] ~= EMPTY then return 0 end
    local opp, total = 3 - side, 0
    for i = 1, 8 do
        local d = DIRS[i]
        local p, k = sq + d, 0
        while b[p] == opp do p, k = p + d, k + 1 end
        if k > 0 and b[p] == side then total = total + k end
    end
    return total
end

-- Plays `sq` for `side` (assumed legal), pushing the flipped squares on
-- fs[top+1..]. Returns the new top; L.unmake takes it back.
function L.make(b, sq, side, fs, top)
    local opp = 3 - side
    b[sq] = side
    for i = 1, 8 do
        local d = DIRS[i]
        local p = sq + d
        if b[p] == opp then
            p = p + d
            while b[p] == opp do p = p + d end
            if b[p] == side then
                p = sq + d
                while b[p] == opp do
                    b[p] = side
                    top = top + 1
                    fs[top] = p
                    p = p + d
                end
            end
        end
    end
    return top
end

function L.unmake(b, sq, side, fs, oldTop, top)
    local opp = 3 - side
    for i = oldTop + 1, top do b[fs[i]] = opp end
    b[sq] = EMPTY
end

-- Discs of each side and empty squares.
function L.counts(b)
    local nb, nw, ne = 0, 0, 0
    for sq = 11, 88 do
        local v = b[sq]
        if v == BLACK then nb = nb + 1 elseif v == WHITE then nw = nw + 1 elseif v == EMPTY then ne = ne + 1 end
    end
    return nb, nw, ne
end

-- Real games -----------------------------------------------------------------

function L.new()
    return { b = L.newBoard(), side = BLACK, history = "", sides = "", last = nil, passed = nil, result = nil }
end

local flips = {}

-- Called after a move or on setting up a position: passes for a side with
-- no move, ends the game when neither side can move.
function L.settle(g)
    g.passed = nil
    if L.hasMove(g.b, g.side) then return end
    local other = 3 - g.side
    if L.hasMove(g.b, other) then
        g.passed, g.side = g.side, other
        return
    end
    local nb, nw = L.counts(g.b)
    g.result = nb > nw and "black" or (nw > nb and "white" or "draw")
end

-- Plays a legal move in the real game. Returns false if it is not legal.
function L.play(g, sq)
    if g.result or not sq or not canPlay(g.b, sq, g.side) then return false end
    L.make(g.b, sq, g.side, flips, 0)
    g.history = g.history .. string.char(48 + (sq // 10 - 1) * 8 + sq % 10 - 1)
    g.sides = g.sides .. g.side
    g.last = sq
    g.side = 3 - g.side
    L.settle(g)
    return true
end

function L.moveCount(g) return #g.history end

-- A new game with the first `count` moves of `history` replayed (nil if a
-- move in it is not legal).
function L.replay(history, count)
    local g = L.new()
    count = math.min(count or #history, #history)
    for k = 1, count do
        local i = history:byte(k) - 48
        if i < 0 or i > 63 or not L.play(g, (i // 8 + 1) * 10 + i % 8 + 1) then return nil end
    end
    return g
end

-- How many moves to take back to reach the last earlier position where
-- `side` was to move (0 when there is none). With `side` nil, one move.
function L.backTo(g, side)
    local n = #g.sides
    if not side then return n > 0 and 1 or 0 end
    for k = n, 1, -1 do
        if g.sides:byte(k) - 48 == side then return n - k + 1 end
    end
    return 0
end

function L.takeBack(g, n) return L.replay(g.history, #g.history - n) end

return L
